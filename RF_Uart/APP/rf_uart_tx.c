/********************************** (C) COPYRIGHT *******************************
 * File Name          : rf_basic.c
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2024/08/15
 * Description        : 无线串口-发送端，注：RF发送失败 RESEND_COUNT 次数会丢弃数据包
 *
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *******************************************************************************/

/******************************************************************************/
/* 头文件包含 */
#include "rf.h"
#include "rf_uart_tx.h"
#include "uart.h"

/*********************************************************************
 * GLOBAL TYPEDEFS
 */

rfTxBuf_t gTxBuf;
uint32_t gRfRxCount;
uint16_t gInterval;
uint16_t gTimeoutMax;
uint16_t gTimeout;
uint16_t gServerData;

uint8_t gTxDataSeq;
static uint16_t gBootCount = 0;         /* 连续快速重启计数：非 0 = 待清零（跑满窗口由主循环清） */
static uint8_t  gUnbindBoot = 0;        /* 本次上电刚完成解绑：本轮不配对，让"已解绑"状态可见 */
uint8_t gRfStatus;
uint8_t gBoundStatus;
uint8_t getDataProbe;
/* 【DTR 极性】见 rf_uart_tx.h / OPCODE_DTR_POL。上电由 RF_DtrPolLoad() 从 Flash 读回 */
volatile uint8_t gDtrPol;
/* 【环满不确认】从机的 UART 发送环满时，write_buf() 会**整包拒收**。老代码只"计数"、
 * 却仍然走公共尾部 gTxDataSeq++ —— 等于对主机说"这包我收到了"，主机于是不再重发，
 * **这一包下行数据就永久丢了**。2026-10-13 实测这是"下行整包丢 ~50%"的根因：
 *   · 丢的是整包、零字节损伤、零重复（write_buf 整包拒收的特征）；
 *   · 与两块板的距离无关（4cm 一样丢）→ 不是空口；
 *   · 与轮询间隔(1/3ms)、从机接收窗口(150/2000us)无关 → 不是收发时机；
 *   · 线速越低丢得越多（115200 丢 85% > 921600 丢 45%）→ 环排空更慢；
 *   · 限速喂料无效 → 主机按轮询节奏(250B/ms)突发下发，不跟喂料速率走。
 * 修法：环满时**不推进序号** → 下一次轮询仍报同一个 seq → 主机命中"重复包"分支，
 * 把缓存的那一包原样重发 → 等 UART 把环排空后再收。用现成的重传机制做背压，
 * 不需要改协议。实测（bulk_test.py，逐字节对齐）：修复前 921600 有效率 11~17%、
 * 修复后 **100%**（坏/丢/重复全 0，两条流逐字节一致），write 阻塞占比 30%→52%
 * （反压终于传到 PC 侧）。 */
static uint8_t gRfRxFullHold = 0;
uint32_t  gRfRxFlag;
rfPackage_t *pPkt_t;

static void rfProcessRx( rfPackage_t *pPkt );
static void rfProcessTx( void );
static int  __HIGH_CODE rfSaveBoundInfo( rfBoundInfo_t *info );
static void __attribute__((noinline)) rfBootCountStartup( void );
static void __attribute__((noinline)) rfBootCountClearTask( void );
static void rfProcessTimeout( void );
void LedTimerCalibBegin( void );
void LedTimerCalibEnd( void );
void LedDataPulse( void );          /* 定义在文件后部，此处前置声明避免隐式声明告警 */
static void __attribute__((noinline)) rfDelayMs( uint32_t ms );   /* SysTick 真实毫秒延时，见定义处 */

// tf status callbacks
rfStatusCBs_t rfCBs =
{
    rfProcessRx,
    rfProcessTx,
    rfProcessTimeout,
    rfProcessTimeout,
};

#define  RF_BUF_LEN    512
static uint8_t rf_buf[RF_BUF_LEN];
static struct simple_buf rf_buffer;
struct simple_buf *pRfBuf = NULL;

uint8_t volatile RF_bound_Flag;
uint32_t ledcount = 0;

/*********************************************************************
 * @fn      uart_buffer_create
 *
 * @brief   Create a file called uart_buffer simple buffer of the buffer
 *          and assign its address to the variable pointed to by the buffer pointer.
 *
 * @param   buf    -   a parameter buf pointing to a pointer.
 *
 * @return  none
 */
static void rf_buffer_create(struct simple_buf **buf)
{
    *buf = simple_buf_create(&rf_buffer, rf_buf, sizeof(rf_buf) );
}

/* ===================== 【DTR 极性】 =====================
 * 语义：0 = 默认（未断言/默认电平 = 高，断言 = 低）、1 = 反相（反过来）。
 * 掉电保存：写在绑定信息那个 4KB 扇区里（整个扇区重写，由 rfSaveDtrPol() 负责把绑定信息
 *           原样带回去）。下面两个是给 uart.c 上电初始化用的：UART_Init() 必须先
 *           RF_DtrPolLoad()，再按 RF_DtrPolGet() 摆 PA3 的上电默认电平。 */
void RF_DtrPolLoad( void )
{
    rfBoundInfo_t *pInfo = (rfBoundInfo_t *)(BOUND_INFO_FLASH_ADDR);
    uint8_t        pol   = pInfo->dtrPol;

    /* 只有"值 + 按位取反"互补才算有效：老固件写的记录里这两字节是擦除态 0xFF，
     * 校验不过 -> 默认极性（升级固件不会把老设备的 DTR 变成反相）。 */
    gDtrPol = ( pInfo->dtrPolChk == (uint8_t)( ~pol ) ) ? ( pol & 1 ) : 0;
}

uint8_t RF_DtrPolGet( void )
{
    return gDtrPol;
}

#if((defined(DTR_RTS_FUNC)) && (DTR_RTS_FUNC == TRUE))
/* ===================== 【DTR/RTS 生效规则】 =====================
 * DTR（PA3 -> 目标 BOOT0）与 RTS（PA2 -> 目标 RESET）**故意不同**：
 *   · DTR：纯跟随主机 —— 断言拉低、释放拉高，不自动恢复（进 Bootloader 要能把 BOOT0 按住）；
 *   · RTS：**断言沿触发一次固定形状的复位脉冲** —— 先等 RTS_PULSE_DELAY_MS（默认 500ms，
 *          此间 PA2 保持高、不复位），再压低 RTS_PULSE_LOW_MS（默认 200ms），然后回高。
 *          与上位机把 RTS 按住多久**无关**（提前释放也不影响，仍在断言沿起算）——
 *          详见 uart.h 里 RTS_PULSE_* 的说明（含为什么这么设计）。
 * 两脚的默认电平都是"未断言 = 输出高"；未配对 / 断链时立即回到默认电平（不把目标按在复位上）。
 *
 * 为什么不在 ISR 里直接搬引脚（老行为：在 RF 回调里直接写 PA2/PA3）：
 *   1) 这一包同时还可能带线码变更，老实现把 SetSysClock + mDelaymS(10) + LED 重标定 都跑在
 *      RF 中断里（24MHz 档 mDelaymS(10) 真实 41.7ms）—— 引脚边沿被排在这段之后，实测要
 *      十几~几十毫秒才动，而且主循环（LED 全靠它刷）被整段冻结；
 *   2) "整包延后固定毫秒数"和"释放后保持 N 毫秒"在中断里都没法非阻塞地等。
 * 做法：ISR 只记**最终值 + 时刻**（与老的"只保留最终值"语义一致），主循环
 *       RF_StatusQuery() 每圈调 rfFlushDtrRts() 到期搬 GPIO。零阻塞、不占中断时间。
 * 竞态说明：共享变量都按 volatile 访问。即使"主循环清标志"与"ISR 写新值"交叠，
 *      最坏也只是把同一个电平多写一次 —— GPIOA 的 SET/CLR 是原子的，写同一电平无副作用。 */
static volatile uint8_t  gIoPending;        /* 有未生效的 IO 状态（ISR 置 1，主循环清 0） */
static volatile uint8_t  gIoStausPending;   /* 最后一次下发的 ioStaus（只保留最终值） */
static volatile uint32_t gIoTickPending;    /* 最后一次下发的时刻（SysTick 计数） */
/* 下面三个同样会被 ISR（断链时的 rfDtrRtsToDefault）读改，所以一并 volatile */
static volatile uint8_t  gHostRts;          /* 主机 RTS 的逻辑电平：1 = 未断言（默认高） */
static volatile uint8_t  gRtsPulseStage;    /* 复位脉冲阶段：0=空闲 1=延迟中(PA2 高) 2=脉冲中(PA2 低) */
static volatile uint32_t gRtsPulseTick;     /* 当前阶段的起点（SysTick 计数） */

/* 两脚回到默认（未断言 = 输出高），并丢掉等待中的 IO 状态。
 * 上电与断链时调用：断链不把目标一直按在复位上。 */
static void __attribute__((noinline)) rfDtrRtsToDefault( void )
{
    /* DTR（PA3）的默认（未断言）电平随极性：默认极性 = 高、反相 = 低。
     * RTS（PA2）不参与翻转，恒为高 —— 它的语义是"复位脉冲"，翻过来会把烧录时序搞乱。 */
    if( gDtrPol )   GPIOA_ResetBits( DTR_PIN );
    else            GPIOA_SetBits( DTR_PIN );
    GPIOA_SetBits( RTS_PIN );
    gHostRts       = 1;
    gRtsPulseStage = 0;                     /* 丢掉在飞的复位脉冲 */
    gIoPending     = 0;
}

static void rfSaveDtrPol( void );       /* 定义在文件后部，挨着绑定信息 Flash 读写 */

/* 【DTR 极性】变更请求：ISR（rfProcessRx）只记值 + 置标志，主循环 rfFlushDtrRts() 到期
 * 改电平并写 Flash —— 擦一个扇区要几毫秒，不能在射频中断里做。 */
static volatile uint8_t gDtrPolReq;         /* 待生效的极性 */
static volatile uint8_t gDtrPolReqFlag;     /* ISR 置 1，主循环清 0 */

static void __attribute__((noinline)) rfQueueDtrPol( uint8_t pol )
{
    gDtrPolReq     = pol & 1;
    gDtrPolReqFlag = 1;
}

/* 生效一次极性变更：改 PA3 电平 + 落 Flash。只有真的变了才擦 Flash（省寿命）。 */
static void __attribute__((noinline)) rfApplyDtrPol( uint8_t pol )
{
    if( pol == gDtrPol )
    {
        return;                         /* 没变：不擦 Flash */
    }
    gDtrPol = pol;
    /* 立刻把 PA3 摆到新极性下的"未断言"电平（也就是新的默认电平）。
     * 若上位机此刻正断言 DTR：主机切极性时会同时置 UART_Status，紧接着那一包 BSP
     * 会在 10ms 去抖窗口后把 PA3 摆到"断言"电平（见 rfQueueDtrRts）。 */
    if( gDtrPol )   GPIOA_ResetBits( DTR_PIN );
    else            GPIOA_SetBits( DTR_PIN );
    rfSaveDtrPol( );                    /* 掉电保存（与绑定信息同扇区） */

    /* 【保存完自行重启】极性已落 Flash，重启后由 RF_DtrPolLoad() 从一开始就按新极性摆
     * 上电默认电平，运行状态也一并清干净。现场反馈：切完极性后从机 LED 会异常亮一段、
     * UART 发送环卡住，只能手动重启 —— 既然手动重启能恢复，就让它自己重启。
     * 代价：链路断一次，主机超时后重新绑定（主机会把 gDataSeq 归零重新同步）。
     * 注意 rfSaveDtrPol() 已把 bootCount 清零，这次主动重启不计入「连重启 5 次解绑」。
     * 幂等：极性没变时本函数在上面就 return 了，所以主机重发同一包 DTR_POL 不会重启第二遍。 */
    rfDelayMs( 2 );                     /* 只留极小余量让 Flash 写完落定；用户要"收到就直接重启" */
    SYS_ResetExecute( );
    while( 1 )
    {
    }                                   /* 不应返回 */
}

static void __attribute__((noinline)) rfQueueDtrRts( uint8_t ioStaus )
{
    gIoStausPending = ioStaus;
    gIoTickPending  = SysTick->CNT;         /* 从"最后一次变化"起算 → 天然滤掉短毛刺 */
    gIoPending      = 1;
}

/* 主循环调用：① 先推进复位脉冲状态机；② 再把到期的 IO 状态搬下去。
 * 刻意不加 __HIGH_CODE（省从机 RAM，主循环调它不敏感）。 */
static void __attribute__((noinline)) rfFlushDtrRts( void )
{
    uint8_t io;

    /* ① 【DTR 极性】待生效的极性变更（来自 OPCODE_DTR_POL）：改电平 + 写 Flash 都在这里做，
     *    不在射频中断里擦 Flash。放在最前面，是为了让"切完极性、紧接着那包 BSP 的 IO 状态"
     *    按新极性生效。 */
    if( gDtrPolReqFlag )
    {
        gDtrPolReqFlag = 0;
        rfApplyDtrPol( gDtrPolReq );
    }

    /* ② 复位脉冲状态机（只动 PA2，不碰 DTR）：
     *    1 = 延迟中（PA2 保持高）-> 到点拉低；
     *    2 = 已拉低 -> **上位机释放了**且低电平已满最短脉宽才回高；
     *        上位机一直按着就一直是低（兼容"按住 RESET"的上位机流程） */
    if( gRtsPulseStage == 1 )
    {
        if( (uint32_t)( SysTick->CNT - gRtsPulseTick ) >= LedMsToTicks( RTS_PULSE_DELAY_MS ) )
        {
            gRtsPulseStage = 2;
            gRtsPulseTick  = SysTick->CNT;
            GPIOA_ResetBits( RTS_PIN );     /* 脉冲开始 */
        }
    }
    else if( gRtsPulseStage == 2 )
    {
        if( gHostRts                                  /* 上位机已释放 RTS */
         && (uint32_t)( SysTick->CNT - gRtsPulseTick ) >= LedMsToTicks( RTS_PULSE_LOW_MS ) )
        {
            gRtsPulseStage = 0;
            GPIOA_SetBits( RTS_PIN );       /* 复位结束：回高 */
        }
    }

    /* ③ 到期的 IO 状态 */
    if( !gIoPending )
    {
        return;
    }
#if (DTR_RTS_RESP_MS != 0)
    if( (uint32_t)( SysTick->CNT - gIoTickPending ) < LedMsToTicks( DTR_RTS_RESP_MS ) )
    {
        return;
    }
#endif
    io = (uint8_t)gIoStausPending;
    gIoPending = 0;                         /* 先清标志：此后新事件重新排队（见上面竞态说明） */

    /* 【等待期间掉链了就不再搬】否则可能把断链时刚拉高的 PA2 又按回低电平。
     * 未配对时主机本就下不发 ioStaus，所以这个判断不会挡掉正常的首次同步。 */
    if( !RF_bound_Flag )
    {
        return;
    }

    /* DTR（PA3）：纯跟随主机，不自动恢复；输出电平按极性取反。
     * ioStaus 的 bit5 = 1 表示"未断言" -> 输出默认电平（默认极性 = 高、反相 = 低） */
    if( ( ( io & 0x20 ) ? 1 : 0 ) ^ gDtrPol )   GPIOA_SetBits( DTR_PIN );
    else                                       GPIOA_ResetBits( DTR_PIN );

    /* RTS（PA2）：见 uart.h 的 RTS_PULSE_* —— 断言沿触发一次"先延迟、再固定脉宽"的复位脉冲 */
#if (RTS_PULSE_DELAY_MS == 0) && (RTS_PULSE_LOW_MS == 0)
    /* 两个值都为 0：不做脉冲发生器，PA2 纯跟随主机 RTS（老行为） */
    if( io & 0x40 )     GPIOA_SetBits( RTS_PIN );
    else                GPIOA_ResetBits( RTS_PIN );
    gHostRts = ( io & 0x40 ) ? 1 : 0;
#else
    if( !( io & 0x40 ) )                    /* 主机断言 RTS（低） */
    {
        if( gHostRts && !gRtsPulseStage )   /* 断言沿 + 当前空闲 -> 启动一次脉冲序列 */
        {
            gRtsPulseStage = 1;             /* 1 = 延迟中（此间 PA2 保持高、不复位） */
            gRtsPulseTick  = SysTick->CNT;
            GPIOA_SetBits( RTS_PIN );
        }
        gHostRts = 0;
    }
    else                                    /* 主机释放 RTS：交给状态机收尾（低满最短脉宽后回高） */
    {
        gHostRts = 1;
    }
#endif
}
#endif

/* 【2 Mbps 用哪档主频】请求 2 Mbps 要求 Fsys 是 16 MHz 的整数倍，只有两条路：
 *   32 MHz + DL=2 -> 2.000000 Mbps（0%，**32 MHz 晶振直出**，不做 PLL 分频）
 *   16 MHz + DL=1 -> 2.000000 Mbps（0%，32 MHz 晶振 ÷2，用到芯片允许的最小 DL）
 * 默认取 32 MHz：晶振直出（2.4G 库最可能按 32 MHz 设计）+ CPU 余量是 16 MHz 的两倍。
 * 若台架上发现 32 MHz 下射频/时序不对，把 BAUD2M_SYS_CLK 改成 CLK_SOURCE_HSE_16MHz 即可
 * （同样 0% 误差，只是 DL 从 2 变 1）。
 * 注意：这两档都走 SDK 的"32M 分频"分支，而本项目此前只跑过 PLL 的 24/100 MHz ——
 * 所以 2 Mbps 必须上台架验证（RF 保活/吞吐 + 用示波器量位宽 0.5us）。 */
#ifndef CLK_SOURCE_HSE_32MHz
#define CLK_SOURCE_HSE_32MHz   ((SYS_CLKTypeDef)0x01)   /* 分频系数=1；SDK 的 enum 没列这一档 */
#endif
#ifndef BAUD2M_SYS_CLK
#define BAUD2M_SYS_CLK         CLK_SOURCE_HSE_32MHz
#endif

/* 【2 Mbps 总开关：默认关】2 Mbps 的线速率是精确的（见上），但它要求从机跑 32 MHz ——
 * 而本项目此前只验证过 PLL 的 24/100 MHz，32 MHz（走 SDK 的"32M 分频"分支）**没上过台架**：
 * RF 库在 32 MHz 下能否正常保活/吞吐、2 Mbps 连续收数据时 CPU 够不够，都还没实测。
 * 所以默认把上限留回 **1.5 Mbps**（= 现有文档与实测的行为），代码在这儿备着：
 * 台架验证 32 MHz + 2 Mbps（示波器量 0.5us/位 + 内容校验 + 保活）通过后，把这里改成 1，
 * 同时更新 README / SKILL / chip-spec 里"上限 1.5 Mbps、2 Mbps 不可用"的那几处。 */
#ifndef BAUD2M_ENABLE
#define BAUD2M_ENABLE          0
#endif

/* 【按波特率选主频：把 UART 整数分频的误差压到最小】
 * 从机的串口波特率 = Fsys/(8*DL)（DL 为整数，见 uart.c 的 UART_SetBuad），也就是一组
 * **离散格点** —— 同一个请求值在不同 Fsys 下离格点的距离不一样，所以"用哪档主频"直接
 * 决定实际波特率的误差。下表是按"在芯片可选主频里挑误差最小的那一档"离线算出来的，
 * 取舍还多了两条：优先用已经在跑的 24MHz/100MHz（少引入没验证过的主频），误差相同时取
 * 主频高的那档（CPU 余量更大）。
 *
 *   请求 921600：100MHz(DL=14) -> 892857（-3.12%）   60MHz(DL=8)  -> 937500（+1.72%）★本片最优
 *   请求 576000：100MHz(DL=22) -> 568182（-1.36%）   60MHz(DL=13) -> 576923（+0.16%）
 *   请求 256000： 24MHz(DL=12) -> 250000（-2.34%）  100MHz(DL=49) -> 255102（-0.35%）
 *
 * 921600 在本片**没有精确解**（需要 Fsys = 7.3728MHz 的整数倍，候选主频里没有）；
 * 705882 / 1090909 这类只有 Linux 驱动才会用到的怪值同理，收益太小就不单独占一档。
 * 不在表里的波特率走下面的默认规则（400k~1M 用 100MHz，其余用 24MHz），与改动前一致。
 * 返回的枚举直接喂给 SetSysClock()。 */
typedef struct
{
    uint32_t        baud;
    SYS_CLKTypeDef  clk;
} rfBaudClk_t;

static const rfBaudClk_t rfBaudClkTab[] =
{
    {  256000u, CLK_SOURCE_HSE_PLL_100MHz },
    {  576000u, CLK_SOURCE_HSE_PLL_60MHz  },
    {  921600u, CLK_SOURCE_HSE_PLL_60MHz  },
#if( BAUD2M_ENABLE )
    { 2000000u, BAUD2M_SYS_CLK            },   /* 32MHz -> DL=2 -> 精确 2.000000 Mbps */
#endif
};

static SYS_CLKTypeDef rfPickSysClock( uint32_t baud )
{
    uint8_t i;

    for( i = 0; i < (uint8_t)( sizeof(rfBaudClkTab) / sizeof(rfBaudClkTab[0]) ); i++ )
    {
        if( rfBaudClkTab[i].baud == baud )
        {
            return rfBaudClkTab[i].clk;
        }
    }

    if( (baud > 400000u) && (baud < 1000000u) )
    {
        return CLK_SOURCE_HSE_PLL_100MHz;
    }

    return CLK_SOURCE_HSE_PLL_24MHz;
}

/*******************************************************************************
 * @fn      rfApplyLineCode
 *
 * @brief   把主机下发的串口线码（波特率/停止位/校验/数据位）落到从机 UART。
 *
 *          ★ 这里有两个"省"：
 *          1) **判等在前**：ioStaus(DTR/RTS) 与线码共用 OPCODE_BSP，老实现每改一次
 *             DTR/RTS 都要把这一整段重跑一遍（含 SetSysClock 与 mDelaymS(10)，而
 *             mDelaymS(10) 在 24MHz 档真实要 41.7ms）—— 主循环/LED 被整段冻结，
 *             DTR/RTS 边沿还被排在这段之后（实测十几~几十毫秒才动）。现在按寄存器
 *             现状判等，线码没变直接返回，DTR/RTS 走调用方的延时队列。
 *          2) **刻意不加 __HIGH_CODE**（放 Flash）：调用方 rfProcessRx() 在 .highcode
 *             (RAM)，靠 noinline 把这段留在 Flash —— 内联回去会让这一百多字节白占
 *             从机 RAM（从机只剩几百字节，96% 出头就跑不稳）。
 *
 * @param   pRsp  PKT_CMD_RSP_STATUS 里带的线码 + ioStaus（BaudRate 会被就地夹上限）
 * @return  None.
 */
static void __attribute__((noinline)) rfApplyLineCode( rfRsp_t *pRsp )
{
    uint8_t  lcr;
    uint32_t clkBefore;

    if( !pRsp )             /* 入参校验：调用方传的是 RxBuf 内的应答体，正常非空 */
    {
        return;
    }

    /* 【波特率上限 2000000】从机 UART 只有**整数**分频（DL = round(Fsys/8/baud)），
     * 而它的串口直接对目标设备（用户按标准值设置），所以"实际码率"必须尽量等于请求值。
     * 能精确生成的上限由主频决定，现在这几档都是 0%：
     *   1.5 Mbps @24M  → DL=2 → 1.500000M（0%）
     *   2   Mbps @32M  → DL=2 → 2.000000M（0%，32 MHz 晶振直出；备选 16M+DL=1）
     *   3   Mbps @24M  → DL=1 → 3.000000M（0%，但链路吞吐只有 27~50 KB/s，毫无收益）
     *   4   Mbps → 没有精确档（100M 只能 4.1667M，+4.2%），且电脑端端口根本打不开
     * 上限由 BAUD2M_ENABLE 决定：开着是 **2 Mbps**、关着是 **1.5 Mbps**（默认，见上面的开关说明）；
     * 超过就夹到上限，不让从机悄悄跑在一个偏差几十% 的码率上（那种情况下上位机只会看到乱码，很难查）。
     * 【注意】"线速率能生成"不等于"能持续传"：2 Mbps = 250 KB/s，而 2.4G 实测吞吐只有
     * 27~50 KB/s —— 放开 2 Mbps 是为了**跟上目标设备自己的线码**（短帧/低占空比场景），
     * 想靠它提吞吐是没用的，持续灌数据照样会在 512 B RF 环 / 3 KB UART 环上积压丢包。
     * 【每个常用档该配哪档主频见下面 rfPickSysClock() 的表】选主频就是选误差：本函数只负责
     * 夹上限，别在这里顺手改主频。 */
#if( BAUD2M_ENABLE )
    if( pRsp->buad_t.BaudRate > 2000000u )
    {
        pRsp->buad_t.BaudRate = 2000000u;       /* 2 Mbps 精确档：32 MHz + DL=2 */
    }
#else
    if( pRsp->buad_t.BaudRate > 1500000u )
    {
        pRsp->buad_t.BaudRate = 1500000u;       /* 默认：与现有文档/实测一致的上限 */
    }
#endif

    /* 【只在"线码真的变了"时才跑重配】判据与下面写寄存器的顺序一一对应：波特率看
     * gBaudRate（UART_SetBuad 维护），停止位/校验/数据位看 R8_UART_LCR 的现状 ——
     * 这样不用新增任何 RAM 变量来记"上次下发的是什么"。
     * 数据位按**写入效果**比较（都要 &3）：老实现是 `LCR |= (DataBits-5)` 而 LCR 里只有低
     * 两位有效，所以 DataBits=0（主机侧未初始化）等于 8 位，不能拿 0-5=251 去比。 */
    lcr = R8_UART_LCR;
    if( !( (pRsp->buad_t.BaudRate != gBaudRate)
        || ((uint8_t)(lcr & RB_LCR_WORD_SZ)        != (uint8_t)((pRsp->buad_t.DataBits-5)&3))
        || ((uint8_t)((lcr & RB_LCR_STOP_BIT)?1:0) != (uint8_t)(pRsp->buad_t.StopBits?1:0))
        || ((uint8_t)((lcr & RB_LCR_PAR_EN)?1:0)   != (uint8_t)(pRsp->buad_t.ParityType?1:0))
        || ((pRsp->buad_t.ParityType)
            && ((uint8_t)((lcr & RB_LCR_PAR_MOD)>>4) != (uint8_t)((pRsp->buad_t.ParityType-1)&3))) ) )
    {
        return;
    }

    clkBefore = GetSysClock( );
    SetSysClock( rfPickSysClock( pRsp->buad_t.BaudRate ) );   /* 见 rfPickSysClock() 上面那张表的取舍 */
    /* 【这一档必须是"按波特率"而不是"按区间"】UART 的 DL 是整数，同一个请求值在不同主频下
     * 离格点的距离不同：921600 在 100MHz 是 892857（-3.12%），换到 60MHz 就是 937500
     * （+1.72%）—— 选主频就是选误差。*/
    /* 【只在主频真的变了才重标 LED 时基】标定拿 mDelaymS(10) 的固定循环数当基准，
     * 24MHz 档要实打实跑 41.7ms；主频没变就没理由付这个代价。 */
    if( GetSysClock( ) != clkBefore )
    {
        LedTimerCalibBegin( );
        mDelaymS(10);
        LedTimerCalibEnd( );
    }

    UART_SetBuad( pRsp->buad_t.BaudRate );
    /* 【主频一变，轮询定时器的"计数"必须重算】gIntervalTimer 存的是**计数**，由
     * UART_SetTimer() 按当时主频算出：100MHz 下 ms=10 → 500000 计数（5ms/tick，
     * 两个 tick 一次轮询 = 10ms）；同一份计数放到 24MHz 上就变成 20.8ms/tick，
     * 轮询周期 41.7ms —— 下行延迟肉眼可见（实测上位机看到 51ms）。反过来
     * （24MHz 算出、跑在 100MHz）轮询会快到 2.4ms，白白占空口。所以每次切完
     * 主频都要按当前主频重算并重启这个定时器。 */
    if( gInterval )
    {
        UART_SetTimer( gInterval );
    }
    // 停止位
    if( pRsp->buad_t.StopBits )
    {
        R8_UART_LCR |= RB_LCR_STOP_BIT ; // 2个停止位
    }
    else
    {
        R8_UART_LCR &= ~RB_LCR_STOP_BIT;// 1个停止位
    }

    // 奇偶校验
    if( pRsp->buad_t.ParityType )
    {
        R8_UART_LCR &= ~RB_LCR_PAR_MOD;
        R8_UART_LCR |= ((pRsp->buad_t.ParityType-1)&3)<<4;
        R8_UART_LCR |= RB_LCR_PAR_EN;
    }
    else
    {
        R8_UART_LCR &= ~RB_LCR_PAR_EN;
    }

    // 数据位
    R8_UART_LCR &= ~RB_LCR_WORD_SZ;
    R8_UART_LCR |= (pRsp->buad_t.DataBits-5);
    if( pRsp->buad_t.DataBits )
    {

    }
}

/*******************************************************************************
 * @fn      rf_disconnect
 *
 * @brief   断开连接
 *
 * @param   None.
 *
 * @return  None.
 */
__HIGH_CODE
static void rf_disconnect( void )
{
    gBoundStatus = BOUND_STATUS_IDLE;
    UART_SetTimer( ADV_INTERVAL );
    rf_tx_set_sync_word( AA );
    rf_tx_set_frequency( DEF_FREQUENCY );

    rf_rx_set_sync_word( AA );
    rf_rx_set_frequency( DEF_FREQUENCY );

    RF_bound_Flag = 0;
#if((defined(DTR_RTS_FUNC)) && (DTR_RTS_FUNC == TRUE))
    rfDtrRtsToDefault( );               /* 断链：PA2/PA3 立即回默认高，别把目标按在复位上 */
#endif
    PRINT("disconnect.\n" );
}

/*******************************************************************************
 * @fn      rf_bound
 *
 * @brief   断开连接
 *
 * @param   None.
 *
 * @return  None.
 */
__HIGH_CODE
static void rf_bound( bound_rsp_t *rsp )
{
    rfBoundInfo_t info;

    gTimeout = 0;
    gBoundStatus = BOUND_STATUS_WAIT;
    gInterval = rsp->interval;
    gTimeoutMax = (rsp->timeout*10/rsp->interval);
    gServerData = rsp->severData;
    rf_tx_set_sync_word( rsp->accessaddr );
    rf_tx_set_frequency( rsp->channel );
    rf_tx_set_phy_type( rsp->phy );
    rf_rx_set_sync_word( rsp->accessaddr );
    rf_rx_set_frequency( rsp->channel );
    rf_rx_set_phy_type( rsp->phy );
    info.head = 0x55aa;
    info.serverData = gServerData;
    /* 【不要在这里清零启动计数！】绑定成功 != 正常使用：换主机时旧主机往往还插在
     * 电脑上，从机每次上电都先回连成功；一旦在这里清零，5 次重启永远攒不够，
     * 表现就是"怎么重启都解不了绑、只能刷固件"。
     * 计数只由"上电后连续运行满 BOOT_FAST_RESET_SEC 秒"清零（rfBootCountClearTask）。 */
    info.bootCount = gBootCount;
    info.resv = BOOT_CNT_MAGIC;
    rfSaveBoundInfo( &info );

    RF_bound_Flag = 1;
    PRINT("bound success.%x %x\n",rsp->accessaddr,rsp->channel );
}

/*******************************************************************************
 * @fn      RF_ProcessRx
 *
 * @brief
 *
 * @param   None.
 *
 * @return  None.
 */
__HIGH_CODE
static void rfProcessRx( rfPackage_t *pPkt )
{
    if( gTxDataSeq == pPkt->seq )
    {
        if( pPkt->type == PKT_DATA_RSP_ACK )
        {
            // 数据发送成功
            if( pPkt->length > PKT_DATA_OFFSET+1 )
            {
                typeBufSize len;
#if(defined(LED_FUNC)) && (LED_FUNC == TRUE)
                /* 【只有真带下行数据才闪】下行数据有两条捎带路径（本包 / PKT_CMD_RSP_STATUS），
                 * 这里补上"本包"这一条。但**不能无条件闪**：主机对上次上行数据的**纯 ACK**
                 * 也是这个类型（`length == PKT_DATA_OFFSET`），而且它在重传时会被原样重发 ——
                 * 无条件闪就会出现"没有任何数据在传、LED 却偶尔闪一下"的假象（实测反馈）。
                 * 上行数据那一侧由发包处（`s == 0` 分支）闪，两条真数据路径都还在，语义不变。 */
                LedDataPulse( );
#endif

                pPkt_t = pPkt;
                {
                    rfRsp_t *pRsp_t = (rfRsp_t *)(pPkt_t+1);
                    len = pPkt_t->length-PKT_DATA_OFFSET-1;
                    gRfRxFlag = write_buf( pRfBuf, pRsp_t->other.rspData, &len );
                    if( !len )
                    {
                        gRfRxFullHold = 1;      /* 环满：本包不确认（见 gRfRxFullHold 的说明） */
                        /* 缓冲满：默认丢弃（RF_RX_FULL_BLOCK=0），可选老行为 UART_Send 忙等，
                         * 见 uart.h 里 RF_RX_FULL_BLOCK 的说明。 */
#if(defined(RF_RX_FULL_BLOCK)) && (RF_RX_FULL_BLOCK == 1)
                        UART_Send(pRsp_t->other.rspData,pPkt_t->length-PKT_DATA_OFFSET-1);
#endif
                    }
                    if( !R8_UART_TFC )
                    {
                        PFIC_SetPendingIRQ( UART_IRQn );
                    }
                }
            }
            getDataProbe = 6;
        }
        else if( pPkt->type == PKT_CMD_BOUND_RSP )
        {
            /* 本轮刚解绑：先不接受任何配对 —— 让"已解绑"状态稳定可见，
             * 也避免被旁边还开着的旧主机立刻绑回去（否则看起来就像没解绑）。
             * 下次上电 gUnbindBoot 自动归零，即可正常配新主机。 */
            if( !gUnbindBoot )
            {
                rf_bound( (bound_rsp_t *)(pPkt+1) );
            }
        }
        else if( pPkt->type == PKT_CMD_RSP_STATUS )
        {
            rfRsp_t *pRsp_t = (rfRsp_t *)(pPkt+1);

            if( gBoundStatus == BOUND_STATUS_WAIT )
            {
                gBoundStatus = BOUND_STATUS_EST;
                UART_SetTimer( gInterval );
            }

            if(  pPkt->length == PKT_DATA_OFFSET )
            {

            }
            // 状态应答为对端设备波特率
            else if( pRsp_t->opcode == OPCODE_BSP )
            {
#if((defined(DTR_RTS_FUNC)) && (DTR_RTS_FUNC == TRUE))
                /* 【先排 DTR/RTS，再管线码】两个理由：① 延时从"这一包到达"起算，不会被下面的
                 * 线码重配（可能要切主频 + 几十毫秒级延时）顶后；② 引脚该有的电平先记下，
                 * 后面那段万一有什么意外也不影响它。 */
                rfQueueDtrRts( pRsp_t->buad_t.ioStaus );    /* PA3 <- DTR、PA2 <- RTS（延时见 uart.h） */
#endif
                rfApplyLineCode( pRsp_t );      /* 线码真变了才重配（判等在里面；放 Flash 省 RAM） */
            }
#if((defined(DTR_RTS_FUNC)) && (DTR_RTS_FUNC == TRUE))
            else if( pRsp_t->opcode == OPCODE_DTR_POL )
            {
                /* 【DTR 极性：收到就立刻做，不等主循环】
                 * 原来只记标志、由主循环的 rfFlushDtrRts() 去改电平和写 Flash。实测那条路
                 * 一发出极性指令就先陷入约 6 秒的序号失步重传风暴（主机 dup/rtx ~3100/s），
                 * 保存和重启都被排在风暴之后 —— 现场就是"发完指令，过好几秒从机才重启"。
                 * 改成在这里（射频 ISR 内）立刻做完：改电平 -> 写 Flash -> 软复位。
                 * 与 rf_bound() 保存绑定是同一个上下文（ISR 内写 Flash），那条路一直是好的。
                 * 下面仍置一次标志，作为"万一没能立刻重启"的兜底。 */
                rfQueueDtrPol( pRsp_t->other.rspData[0] );
                rfApplyDtrPol( pRsp_t->other.rspData[0] );
            }
#endif
            else if( pRsp_t->opcode == OPCODE_DATA )
            {
                typeBufSize len;

                pPkt_t = pPkt;
                getDataProbe = 6;


#if(defined(ST_ISP_FUNC)) && (ST_ISP_FUNC == TRUE)
                /* RESET/BOOT 引脚控制协议解析，仅在连接下载时触发。
                 * 【与 DTR/RTS 直控共存】这两个脚（PA2=RESET、PA3=BOOT）平时跟随 PC 的
                 * DTR/RTS；收到 0x7F 时由这里临时接管跑下载时序，结束时保持
                 * "目标停在 Bootloader"的那个电平。不需要就在 uart.h 把
                 * ST_ISP_FUNC 置 FALSE。 */
                if(pRsp_t->other.rspData[0]==0x7f && (pPkt_t->length-PKT_DATA_OFFSET-1)==1)
                {
                    /* 【这里必须用 SysTick 版延时】mDelaymS() 是按编译期 FREQ_SYS=100MHz
                     * 标定的软件循环，从机跑 24MHz 时同样的循环会长约 4.2 倍 —— 标称的
                     * 1/1/1/50 ms 实际会变成 4.2/4.2/4.2/208 ms，而且 400k~1M 档（切
                     * 100MHz）又变回准的：时序随主频档位漂。rfDelayMs() 用 LedTimerInit()
                     * 标定过的 SysTick，任何主频下都是真实毫秒。 */
                    //BOOT脚拉高、RESET低电平复位进BOOT
                    GPIOA_SetBits( BOOT_PIN );
                    rfDelayMs(1);
                    GPIOA_ResetBits( RESET_PIN );
                    rfDelayMs(1);
                    GPIOA_SetBits( RESET_PIN );
                    rfDelayMs(1);
                    GPIOA_ResetBits( BOOT_PIN );
                    rfDelayMs(50);
                }
#endif
                {
                    rfRsp_t *pRsp_t = (rfRsp_t *)(pPkt_t+1);
                    len = pPkt_t->length-PKT_DATA_OFFSET-1;
                    gRfRxFlag = write_buf( pRfBuf, pRsp_t->other.rspData, &len );
                    if( !len )
                    {
                        gRfRxFullHold = 1;      /* 环满：本包不确认（见 gRfRxFullHold 的说明） */
                        /* 同 ACK 分支：默认丢弃，RF_RX_FULL_BLOCK=1 时为老行为（忙等发完） */
#if(defined(RF_RX_FULL_BLOCK)) && (RF_RX_FULL_BLOCK == 1)
                        UART_Send(pRsp_t->other.rspData,pPkt_t->length-PKT_DATA_OFFSET-1);
#endif
                    }
                    if( !R8_UART_TFC )
                    {
                        PFIC_SetPendingIRQ( UART_IRQn );
                    }
                }
#if(defined(LED_FUNC)) && (LED_FUNC == TRUE)
            LedDataPulse( );        /* 有数据收发 → LED 亮一下 */
#endif

            }
        }
        else
        {

        }
        gRfStatus = RF_STATUS_IDLE;
        gTxBuf.status = STA_IDLE;
        if( gRfRxFullHold )
        {
            /* 环满丢包：**不推进序号** —— 让主机把这一包当"重复包"重发（见 gRfRxFullHold 说明） */
            gRfRxFullHold = 0;
        }
        else
        {
            gTxDataSeq ++;
        }
        gTimeout = 0;
    }
    else
    {
        /* 【seq 失步保护】seq 不匹配（主机重传/迟到包）：RFIP_SetRx 是一次性接收，
         * 收到包后必须再次调用才会继续收下一包；而本函数是唯一的 RX 路径入口，
         * 这里若不重开窗口，接收会永久停摆，主循环又卡在"等应答"不再发包 ——
         * 表现为链路静默、但主机侧没有任何 disconnect/connect timeout 日志。
         * 注意：只重开接收，不动 gTxDataSeq 等状态，让既有超时重传机制接管。 */
        rf_rx_start( 150 );
    }
}

/*******************************************************************************
 * @fn      RF_ProcessTx
 *
 * @brief
 *
 * @param   None.
 *
 * @return  None.
 */
__HIGH_CODE
static void rfProcessTx( void )
{
    if( gRfStatus == RF_STATUS_RETX )
    {
        gRfStatus = RF_STATUS_REWAIT;
    }
    else
    {
        gRfStatus = RF_STATUS_WAITRSP;
    }
    rf_rx_start( 150 );
}

/*******************************************************************************
 * @fn      RF_ProcessTimeout
 *
 * @brief
 *
 * @param   None.
 *
 * @return  None.
 */
__HIGH_CODE
static  void rfProcessTimeout( void )
{
    /* 【只在"真有包在等应答"时才重传】原实现无条件置 STA_RESEND：空闲时（STA_IDLE）
     * 一个杂散的超时/CRC 事件也会把**上一包**（旧 seq）再发一次 —— 白占空口，对端还会
     * 收到 seq 不匹配的重复包。只有 STA_BUSY（已发出、正等应答）才需要重传。 */
    if( gTxBuf.status == STA_BUSY )
    {
        gTxBuf.status = STA_RESEND;
    }
    if( gRfStatus == RF_STATUS_WAITRSP )
    {
        gTxBuf.resendCount = RESEND_COUNT;
    }
    else
    {
        if( gBoundStatus == BOUND_STATUS_WAIT )
        {
            if( ++ gTimeout > BOUND_EST_COUNT )
            {
                rf_disconnect( );
            }
            gTxBuf.status = STA_IDLE;
        }
        else if( gBoundStatus == BOUND_STATUS_EST )
        {
            if( ++ gTimeout > gTimeoutMax )
            {
                rf_disconnect( );
                gTxBuf.status = STA_IDLE;
            }
        }
    }
}


/* LED 时基：SysTick 自由计数 + 启动标定，使闪烁周期是真实时间 */
static uint32_t gLedHalfTicks  = 0;
static uint32_t gLedPulseTicks = 0;             /* 数据提示脉冲宽度（计数） */
static uint32_t gLedCalClock   = 0;
/* 【数据提示改用"序号闩锁"，不再"时刻比对"】ISR 只累加计数，主循环看到计数变了才用**自己采样的**
 * SysTick 起一段完整提示。原写法是"ISR 记时刻 + 主循环拿早先采样好的 now 去比"：只要 RF 接收
 * 中断正好落在"采样 now"与"读时刻"之间（约 8~10 个周期 ≈0.35us），now 就比时刻旧，相减下溢成
 * 巨大值 → 判定"已过期"并把脉冲清掉 → 表现为"数据收到了、灯偶尔不闪"。闩锁写法没有这个竞态，
 * 而且 ISR 里即使被 UART_Send() 阻塞上百毫秒，提示也照样完整显示 LED_DATA_PULSE_MS 毫秒。 */
static volatile uint8_t  gLedPulseCnt  = 0;     /* ISR：有新数据活动就 +1 */
static uint8_t           gLedPulseSeen = 0;     /* 主循环：已消费到哪个计数 */
static uint32_t          gLedLitStart  = 0;     /* 主循环：本次提示起点（SysTick 计数） */

uint32_t gLedTicksPerMs = 0;                    /* 每毫秒的 SysTick 计数（标定） */

/* 去抖后的连接状态：连上立即置 1；断开要持续 LINK_DEBOUNCE_MS 才清 0 */
volatile uint8_t  gLinkStable     = 0;
static   uint8_t  gLinkDownActive = 0;
static   uint32_t gLinkDownTick   = 0;

/*******************************************************************************
 * @fn      LedMsToTicks
 *
 * @brief   毫秒 -> SysTick 计数（用标定出的 gLedTicksPerMs）
 *
 * @return  对应的 SysTick 计数
 */
uint32_t LedMsToTicks( uint32_t ms )
{
    if( gLedTicksPerMs == 0 )
    {
        return ms;
    }
    return gLedTicksPerMs * ms;
}

static uint32_t gLedCalT0 = 0;

/*******************************************************************************
 * @fn      LedTimerCalibBegin
 *
 * @brief   记录标定起点（SysTick 计数），随后必须紧跟一段已知延时，
 *          再由 LedTimerCalibEnd() 完成标定。
 *
 * @return  None.
 */
void LedTimerCalibBegin( void )
{
    gLedCalT0 = SysTick->CNT;
}

/*******************************************************************************
 * @fn      LedTimerCalibEnd
 *
 * @brief   完成 LED 时基标定。Δt 是一段 mDelaymS(10) 期间 SysTick 走过的计数。
 *          【必须实测，不能按主频比例推算】：SysTick 的计数频率是否随
 *          SetSysClock() 变化并无保证，实测才可靠。
 *
 * @return  None.
 */
void LedTimerCalibEnd( void )
{
    uint32_t t1     = SysTick->CNT;
    uint32_t sysclk = GetSysClock( );

    if( sysclk == 0 )
    {
        sysclk = FREQ_SYS;
    }

    /* mDelaymS() 的软件循环次数是按编译期 FREQ_SYS 算的【固定值】，主频越低，
     * 同样的循环越慢：真实时长 = 10ms × FREQ_SYS / sysclk。
     * 于是每毫秒的 SysTick 计数 = ticks / 真实时长，
     * 故"LED_BLINK_MS 毫秒的计数" = ticks × sysclk × LED_BLINK_MS / (10 × FREQ_SYS)。 */
    {
        uint32_t ticks_per_ms = (uint32_t)( (uint64_t)( t1 - gLedCalT0 ) * sysclk
                                            / ( 10ULL * FREQ_SYS ) );
        if( ticks_per_ms == 0 )
        {
            ticks_per_ms = 1;
        }
        gLedTicksPerMs = ticks_per_ms;
        gLedHalfTicks  = ticks_per_ms * LED_BLINK_MS;
        gLedPulseTicks = ticks_per_ms * LED_DATA_PULSE_MS;
    }
    if( gLedHalfTicks == 0 )
    {
        gLedHalfTicks = 1;
    }
    gLedCalClock = sysclk;
}

/*******************************************************************************
 * @fn      LedDataPulse
 *
 * @brief   标记"刚有数据收发"，让 LED 亮 LED_DATA_PULSE_MS 毫秒（可见的闪一下）。
 *          在中断里调用也安全。
 *
 * @return  None.
 */
void LedDataPulse( void )
{
#if(defined(LED_FUNC)) && (LED_FUNC == TRUE) && (LED_DATA_BLINK == 1)
    gLedPulseCnt ++;        /* 只累加、不取时刻：见 gLedPulseCnt 定义处的说明 */
#endif
}

/*******************************************************************************
 * @fn      LedTimerInit
 *
 * @brief   初始化 LED 时基：SysTick 自由计数（不使能中断），并标定一次。
 *
 * @return  None.
 */
void LedTimerInit( void )
{
    SysTick->CNTL = 0;
    SysTick->CMP  = 0xFFFFFFFF;                 /* 最大重载值，不使能中断 */
    SysTick->SR   = 0;
    SysTick->CTLR = SysTick_CTLR_STRE | SysTick_CTLR_STCLK | SysTick_CTLR_STE;

    LedTimerCalibBegin( );
    mDelaymS( 10 );                             /* 软件延时 10ms 作参考 */
    LedTimerCalibEnd( );
}

#if(defined(LED_FUNC)) && (LED_FUNC == TRUE)
/*******************************************************************************
 * @fn      LedStatusQuery
 *
 * @brief   LED 指示：未连接 → 快闪（周期 = LED_BLINK_MS*2 ms）；连接成功 → 熄灭，
 *          但收发数据时亮 LED_DATA_PULSE_MS 毫秒（脉冲由 LedDataPulse() 以序号闩锁置位）。
 *          ledcount 当"上次翻转时的 SysTick 计数"用，与主循环速度无关。
 *
 *          刻意**不加 __HIGH_CODE**：人眼看灯不是时序敏感路径，主循环每圈调一次即可；
 *          放进 .highcode 会白占从机 RAM（从机只剩几百字节）。主机 LedStatusQuery() 同样处理。
 *
 * @return  None.
 */
static void __attribute__((noinline)) LedStatusQuery( void )
{
    uint32_t now = SysTick->CNT;
    uint8_t  lit = 0;

    if( gLinkStable )
    {
#if(LED_DATA_BLINK == 1)
        if( gLedPulseCnt != gLedPulseSeen )     /* 有新的数据活动 → 起一段完整提示 */
        {
            gLedPulseSeen = gLedPulseCnt;
            gLedLitStart  = now;                /* 从"主循环看到"这一刻起算 */
        }
        if( (uint32_t)( now - gLedLitStart ) < gLedPulseTicks )
        {
            lit = 1;
        }
#endif
        if( lit )   GPIOA_SetBits(LED_PIN);        /* 数据活动 → 亮一下 */
        else        GPIOA_ResetBits(LED_PIN);      /* 平时熄灭 */
        ledcount = 0;
    }
    else
    {
        if( (uint32_t)( now - ledcount ) >= gLedHalfTicks )
        {
            /* 用原子 SET/CLR 代替 GPIOA_InverseBits()（那是 R32_PA_OUT ^= pin 的**读改写**）：
             * 否则它与中断里 DTR/RTS 的 GPIOA_SetBits/ResetBits 撞上时，这里写回的旧值会把
             * 那一次 DTR/RTS 变化吞掉（表现为"某次断言没生效"）。 */
            if( R32_PA_OUT & LED_PIN )  GPIOA_ResetBits( LED_PIN );
            else                        GPIOA_SetBits( LED_PIN );
            ledcount = now;
        }
    }
}
#endif

/*******************************************************************************
 * @fn      RF_StatusQuery
 *
 * @brief   状态处理
 *
 * @param   None.
 *
 * @return  None.
 */
__HIGH_CODE
void RF_StatusQuery( void )
{
    uint8_t s;

    rfBootCountClearTask();     /* 【连续重启 N 次解绑】跑满窗口即清零启动计数 */

    /* ---- 连接状态去抖 ---- */
    {
        uint32_t now = SysTick->CNT;

        if( RF_bound_Flag )
        {
            gLinkStable     = 1;
            gLinkDownActive = 0;
        }
        else if( gLinkStable )
        {
            if( gLinkDownActive == 0 )
            {
                gLinkDownActive = 1;
                gLinkDownTick   = now;
            }
            else if( (uint32_t)( now - gLinkDownTick ) >= LedMsToTicks( LINK_DEBOUNCE_MS ) )
            {
                gLinkStable     = 0;
                gLinkDownActive = 0;
            }
        }
    }

#if(defined(LED_FUNC)) && (LED_FUNC == TRUE)
    LedStatusQuery( );          /* LED 指示（定义见文件后部，刻意不进 .highcode，省 RAM） */
#endif
#if((defined(DTR_RTS_FUNC)) && (DTR_RTS_FUNC == TRUE))
    rfFlushDtrRts( );           /* DTR/RTS 延时到点才搬引脚（见 uart.h 的 DTR_RTS_RESP_MS） */
#endif
    if( gTxBuf.status == STA_IDLE )
    {
        rfPackage_t *pPkt_t = (rfPackage_t *)gTxBuf.TxBuf;

        gTxBuf.len  = DATA_LEN_MAX_TX;
        s = UART_RxQuery( (void *)(pPkt_t+1), &gTxBuf.len );
        // 发送数据
        if( s == 0 )
        {
#if(defined(LED_FUNC)) && (LED_FUNC == TRUE)
            LedDataPulse( );        /* 有数据收发 → LED 亮一下 */
#endif

            gRfStatus = RF_STATUS_TX;
            pPkt_t->type = PKT_DATA_FLAG;
            pPkt_t->length = gTxBuf.len + PKT_DATA_OFFSET;
            gTxBuf.status = STA_BUSY;
            pPkt_t->seq = gTxDataSeq;
            pPkt_t->resv = 0;
            rf_tx_start( gTxBuf.TxBuf, 60 );
        }
        else if( s == 0x80 )
        {
            if( gBoundStatus )
            {
                // 获取状态
                gRfStatus = RF_STATUS_GETS;
                pPkt_t->type = PKT_CMD_GET_STATUS;
                pPkt_t->length = PKT_DATA_OFFSET;
            }
            else
            {
                // 请求绑定
                bound_req_t *pReq_t = (bound_req_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];

                gRfStatus = RF_STATUS_REQ;
                pPkt_t->type = PKT_CMD_BOUND_REQ;
                pPkt_t->length = PKT_DATA_OFFSET + sizeof(bound_req_t);
                pReq_t->interval = ADV_INTERVAL;
                pReq_t->severData = gServerData;
                gTxDataSeq = 0;
            }
            gTxBuf.status = STA_BUSY;
            pPkt_t->seq = gTxDataSeq;
            pPkt_t->resv = 0;
            rf_tx_start( gTxBuf.TxBuf, 60 );
        }
        else
        {
            if( getDataProbe )
            {
                getDataProbe--;
                gRfStatus = RF_STATUS_GETS;
                pPkt_t->type = PKT_CMD_GET_STATUS;
                pPkt_t->length = PKT_DATA_OFFSET;
                gTxBuf.status = STA_BUSY;
                pPkt_t->seq = gTxDataSeq;
                pPkt_t->resv = 0;
                rf_tx_start( gTxBuf.TxBuf, 60 );
            }
        }
    }
    else if( gTxBuf.status == STA_RESEND )
    {
        if( gTxBuf.resendCount )
        {
            if( gTxBuf.resendCount < RESEND_COUNT/2 )
            {
                PRINT("*%d %d\n",gTxBuf.resendCount,gTxDataSeq);
            }
            gRfStatus = RF_STATUS_RETX;
            gTxBuf.resendCount--;       /* 0xFF 那种"无限重传"没有任何地方设置，原 guard 是死代码 */
            gTxBuf.status = STA_BUSY;
            rf_tx_start( gTxBuf.TxBuf, 60 );
        }
        else
        {
            // 发送失败丢弃
            PRINT(" resend fail.%d\n",gTxBuf.TxBuf[1]);
            gTxBuf.status = STA_IDLE;
        }
    }
}


/*******************************************************************************
 * @fn      rfSaveBoundInfo
 *
 * @brief   擦除并写入绑定信息，写后用 FLASH_ROM_VERIFY 校验，失败重试。
 *
 *          ★ 必须 __HIGH_CODE：Flash 擦写期间 CPU 取指受影响，操作代码要在 RAM
 *          （.highcode 段）里执行 —— 原厂 rf_bound() 也是这么做的。此前为省 RAM
 *          把它排除在 .highcode 外，导致写入静默失败（绑定与 bootCount 都留不住）。
 *
 *          Flash 写入的硬约束：**源 Buffer 必须在 RAM 且 4 字节对齐**。
 *          rfBoundInfo_t 已加 aligned(4)；这里再加 VERIFY 兜底，避免出现
 *          "擦掉了却没写进去"，使 bootCount 永远从 0 开始、解绑阈值达不到。
 *
 * @param   info 待写入的绑定信息（RAM，4 字节对齐）
 * @return  None.
 */
/* Flash 擦写失败报警：刻意**不加** __HIGH_CODE。
 * 调用它时擦写已结束，可以安全执行 Flash 里的代码；放进 RAM 会白占内存
 * （从机 RAM 已到 95%+，96% 即跑不稳）。 */
static void rfFlashFailAlarm( void )
{
#if(defined(LED_FUNC)) && (LED_FUNC == TRUE)
    /* 急促闪 20 次（100ms/100ms，共 4 秒）：时长够长、节奏够急，
     * 与"启动慢闪 300/400ms"和"解绑常亮 2 秒"都不会混淆。 */
    uint8_t i;
    for( i = 0; i < 20; i++ )
    {
        GPIOA_SetBits( LED_PIN );
        rfDelayMs( 100 );   /* 必须真实毫秒：mDelaymS 在 24MHz 档会被拉长 4.2 倍（4 秒报警变 16.7 秒） */
        GPIOA_ResetBits( LED_PIN );
        rfDelayMs( 100 );
    }
#endif
}

static int __HIGH_CODE rfSaveBoundInfoTry( rfBoundInfo_t *info )
{
    /* 单次尝试。必须 __HIGH_CODE：擦写期间 CPU 取指受影响，碰 Flash 的代码要在 RAM 跑。
     * ISP572.h 明确 ERASE / WRITE / VERIFY 均 "return 0 if success"。
     * 原实现只查 VERIFY、丢弃了 ERASE/WRITE 的返回值 —— 若擦除未成功，Flash 写入
     * 按位与（1->0）仍可能通过校验，旧数据与新数据混在一起，绑定/解绑行为诡异且
     * 无从排查。这里三者都查，便于上层判断失败原因。 */
    /* 【必须在擦写期间关中断】Flash 擦写期间 CPU 取指受限：此时若来了中断，而 ISR 或其
     * 调用链上有任何**驻留 Flash**的代码，取指就会失败 -> 状态损坏/跑飞。
     * 绑定 / 上电计数 / 解绑这三个写点都跑在**射频 ISR 内**（rf_bound 等），射频 ISR 不能
     * 自我抢占，所以一直没暴露；而【DTR 极性】的写点在**主循环**
     * （rfFlushDtrRts -> rfApplyDtrPol -> rfSaveDtrPol），擦写期间射频 ISR 照常触发 ——
     * 现场表现正是"发完 A5 5A 01 <pol>，从机 LED 恒亮、UART 发送环卡住、只能重启从机"。
     * 这里统一关中断把四条写路径一起保护。用 SYS_ 版：它带中断使能状态的保存/恢复，
     * 可嵌套、不会误开本来关着的中断。擦写只有几毫秒，期间丢几个轮询包无妨
     * （主机会照常重发；绑定那条路径本来就在 ISR 里独占同样长的时间）。 */
    {
        uint32_t irqSta;
        int rc = 0;

        SYS_DisableAllIrq( &irqSta );

        if( FLASH_ROM_ERASE( BOUND_INFO_FLASH_ADDR, 4096 ) != 0 )
        {
            rc = -1;
        }
        else if( FLASH_ROM_WRITE( BOUND_INFO_FLASH_ADDR, info, sizeof(*info) ) != 0 )
        {
            rc = -2;
        }
        else if( FLASH_ROM_VERIFY( BOUND_INFO_FLASH_ADDR, info, sizeof(*info) ) != 0 )
        {
            rc = -3;
        }

        SYS_RecoverIrq( irqSta );
        return rc;
    }
}

/* 重试与报警：刻意**不加** __HIGH_CODE（省 RAM）。
 * 每次尝试返回时 Flash 操作已结束，此时执行 Flash 里的代码是安全的。 */
static int rfSaveBoundInfo( rfBoundInfo_t *info )
{
    int tries;
    int rc = 0;

    /* 【DTR 极性】统一在这里补这两个字段：记录是**整个扇区重写**的，任何一个写点漏填这个
     * 新字段都会把栈上垃圾写进 Flash（而且 VERIFY 会失败、报 flash op failed）。收口在这里，
     * 上电计数 / 解绑 / 绑定 / 极性保存四条路径都不用各自记得填。 */
    info->dtrPol    = gDtrPol;
    info->dtrPolChk = (uint8_t)( ~gDtrPol );

    for( tries = 0; tries < 3; tries++ )
    {
        rc = rfSaveBoundInfoTry( info );
        if( rc == 0 )
        {
            return 0;
        }
        PRINT("bound info flash op failed(%d), retry %d\n", rc, tries + 1);
    }

    /* 失败不再静默：此前用户只能感觉"解绑不好用"，无从判断原因 */
    PRINT("!!! bound info flash write FAILED (rc=%d)\n", rc);
    rfFlashFailAlarm( );
    return rc;
}

/*******************************************************************************
 * @fn      rfSaveDtrPol
 *
 * @brief   把当前 DTR 极性落 Flash（掉电保存）。
 *
 *          整个 4KB 扇区会被擦掉重写，所以必须先把记录读回来带着写 —— 否则这一擦会把
 *          绑定信息、连续重启计数一起抹掉。极性那两个字节由 rfSaveBoundInfo() 统一补。
 *          调用点在主循环（rfFlushDtrRts -> rfApplyDtrPol），不在中断里。
 *
 * @param   None.
 * @return  None.
 */
static void __attribute__((noinline)) rfSaveDtrPol( void )
{
    rfBoundInfo_t *pInfo = (rfBoundInfo_t *)(BOUND_INFO_FLASH_ADDR);
    rfBoundInfo_t  info;

    info.head       = pInfo->head;
    info.serverData = pInfo->serverData;
    /* 【主动重启前清零】切完极性从机会自己重启（见 rfApplyDtrPol）；若把计数留着，
     * 这次重启会让它 +1 —— 连改 5 次极性就触发「连续重启 BOOT_UNBIND_TIMES 次解绑」。
     * 归零 = 把这次主动重启从计数里摘出去。安全性：极性指令只能从**已建立的链路**
     * 下发，所以设备此刻不可能是"卡在启动循环"的状态，清零不会削弱该保护。
     * resv 一并写成魔数：确保下次上电 bootCount 被当成"有效且为 0"读回。 */
    info.bootCount  = 0;
    info.resv       = BOOT_CNT_MAGIC;
    rfSaveBoundInfo( &info );
    PRINT("dtr pol saved:%d\n", gDtrPol);
}

/*******************************************************************************
 * @fn      rfDelayMs
 *
 * @brief   通用的"真实毫秒"延时：启动 LED 提示、一键下载(RESET/BOOT)时序、Flash 失败报警都用它。
 *
 *          mDelaymS() 是按编译期 FREQ_SYS=100MHz 标定的软件循环，而从机实际跑 24MHz 时
 *          同样的循环会长约 4.2 倍（100MHz 档又正常）—— 凡是要"真时间"的地方都不能用它。
 *          这里用 LedTimerInit() 标定过的 SysTick，必须在 LedTimerInit() 之后调用。
 *
 *          刻意 noinline 且**不加** __HIGH_CODE（延时只读 SysTick，放进 RAM 白占从机内存）。
 *
 * @param   ms 毫秒数
 * @return  None.
 */
static void __attribute__((noinline)) rfDelayMs( uint32_t ms )
{
    uint32_t t0;

    if( gLedTicksPerMs == 0 )       /* 时基还没标定：退回软件延时，避免死循环 */
    {
        mDelaymS( (uint16_t)ms );
        return;
    }
    t0 = SysTick->CNT;
    while( (uint32_t)( SysTick->CNT - t0 ) < LedMsToTicks( ms ) )
    {
    }
}

/*******************************************************************************
 * @fn      rfBootCountStartup
 *
 * @brief   上电维护「连续快速重启计数」：达到 BOOT_UNBIND_TIMES 次即解绑。
 *
 *          清零只有一条途径 —— 主循环里"跑满 BOOT_FAST_RESET_SEC 秒"
 *          （rfBootCountClearTask）；**绝不因为配对成功而清零**，原因此处不再赘述，
 *          见 rf_bound() 里的注释（那正是"怎么重启都不解绑"的根因）。
 *
 *          刻意**不加** __HIGH_CODE：本函数只在启动阶段跑一次（含 Flash 擦写），
 *          放进 .highcode 会白占 RAM。
 *
 * @return  None.
 */
static void __attribute__((noinline)) rfBootCountStartup( void )
{
    rfBoundInfo_t *pInfo = (rfBoundInfo_t *)(BOUND_INFO_FLASH_ADDR);
    rfBoundInfo_t  info;
    uint16_t       bootCnt;

    if( pInfo->head == BOUND_INFO_HEAD )
    {
        gServerData = pInfo->serverData;
        /* 旧格式(resv 不是魔数)时 bootCount 无效，从 0 起算，避免升级后误触发 */
        bootCnt     = (pInfo->resv == BOOT_CNT_MAGIC) ? pInfo->bootCount : 0;
    }
    else
    {
        gServerData = 0;
        bootCnt     = 0;
    }

    /* 本来就没绑定时无"解绑"可言：不计数、不写 Flash（省擦写，也免得在未绑定时
     * 给出"解绑"提示把人搞糊涂）。 */
    if( gServerData == 0 )
    {
        gBootCount = 0;
        return;
    }

    if( (uint16_t)(bootCnt + 1) >= BOOT_UNBIND_TIMES )
    {
        /* 连续快速重启达到阈值 → 解绑：清掉绑定信息并把计数归零 */
        PRINT("reboot %d times -> unbind.\n", bootCnt + 1);
        gServerData = 0;
        gBootCount  = 0;
        gUnbindBoot = 1;            /* 本轮不再配对，见 rfProcessRx() */
        info.head       = 0;
        info.serverData = 0;
        info.bootCount  = 0;
        info.resv       = 0;
        rfSaveBoundInfo( &info );
#if(defined(LED_FUNC)) && (LED_FUNC == TRUE)
        /* 解绑成功提示：常亮 2 秒（与"启动短闪 N 次""失败急促闪"截然不同的模式） */
        GPIOA_SetBits( LED_PIN );
        rfDelayMs( 2000 );
        GPIOA_ResetBits( LED_PIN );
#endif
    }
    else
    {
        /* 记录本次启动(+1)。清零只走"跑满窗口"那条路（rfBootCountClearTask），
         * 不再有"配对成功即清零"，也不再有"上电后极短时间必须断电"的窄窗口。 */
        gBootCount = bootCnt + 1;
        info.head       = BOUND_INFO_HEAD;
        info.serverData = gServerData;
        info.bootCount  = gBootCount;
        info.resv       = BOOT_CNT_MAGIC;
        rfSaveBoundInfo( &info );
#if(defined(LED_FUNC)) && (LED_FUNC == TRUE)
        /* 启动提示：短闪 gBootCount 次 —— 使用者据此判断"计数有没有在涨"。
         * 此前完全没有反馈，操作时根本不知道自己到了第几次。 */
        {
            uint16_t i;
            for( i = 0; i < gBootCount; i++ )
            {
                GPIOA_SetBits( LED_PIN );
                rfDelayMs( 100 );
                GPIOA_ResetBits( LED_PIN );
                rfDelayMs( 250 );
            }
        }
#endif
    }
}

/*******************************************************************************
 * @fn      rfBootCountClearTask
 *
 * @brief   主循环任务：上电后连续运行满 BOOT_FAST_RESET_SEC 秒即把启动计数清零，
 *          这样只有"上电后很快又断电"（快速重启）才会累积到解绑阈值。
 *          同样刻意不加 __HIGH_CODE（时序不敏感），省 RAM。
 *
 * @return  None.
 */
static void __attribute__((noinline)) rfBootCountClearTask( void )
{
    static uint32_t bootRefTick = 0;
    rfBoundInfo_t  *pInfo = (rfBoundInfo_t *)(BOUND_INFO_FLASH_ADDR);
    rfBoundInfo_t  info;
    uint32_t       now;

    if( gBootCount == 0 )
    {
        bootRefTick = 0;
        return;
    }

    now = SysTick->CNT;
    if( bootRefTick == 0 )
    {
        bootRefTick = now;
    }
    if( (uint32_t)( now - bootRefTick ) < LedMsToTicks( BOOT_FAST_RESET_SEC * 1000 ) )
    {
        return;
    }

    /* 跑满窗口 = 正常使用（不是快速重启）→ 清零计数。
     * 绑定值以 Flash 当前内容为准：这十几秒里 rf_bound() 可能刚写入新值，
     * 不能拿上电时读到的旧 gServerData 覆盖回去。 */
    gBootCount  = 0;
    bootRefTick = 0;
    info.head       = BOUND_INFO_HEAD;
    info.serverData = ( pInfo->head == BOUND_INFO_HEAD ) ? pInfo->serverData : gServerData;
    info.bootCount  = 0;
    info.resv       = BOOT_CNT_MAGIC;
    rfSaveBoundInfo( &info );
    PRINT("boot counter cleared.\n");
}

/*******************************************************************************
 * @fn      RF_UartTxInit
 *
 * @brief   RF uart发送应用初始化
 *
 * @param   None.
 *
 * @return  None.
 */
__HIGH_CODE
void RF_UartTxInit( void )
{
    PRINT("----------------- rf uart tx mode -----------------\n");
    gTxDataSeq = 0;
    gRfRxFlag = 0;
    gBoundStatus = BOUND_STATUS_IDLE;
    gTxBuf.status = 0;
    rf_buffer_create(&pRfBuf);

#if((defined(DTR_RTS_FUNC)) && (DTR_RTS_FUNC == TRUE))
    rfDtrRtsToDefault( );               /* 上电默认：DTR/RTS 都未断言（输出高），并复位内部状态 */
#endif
    rfBootCountStartup();               /* 【重启 N 次解绑】上电计数/解绑 */

    PRINT("gServerData = %x \n",gServerData);
    RFRole_RegisterStatusCbs( &rfCBs );
}

/******************************** endfile @rf ******************************/
