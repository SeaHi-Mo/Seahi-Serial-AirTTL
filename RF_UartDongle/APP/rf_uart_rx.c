/********************************** (C) COPYRIGHT *******************************
 * File Name          : rf_basic.c
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2024/08/15
 * Description        : 无线串口-接收端
 *
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *******************************************************************************/

/******************************************************************************/
/* 头文件包含 */
#include "rf.h"
#include "rf_uart_rx.h"
#include "usb_uart.h"
#include "ISP572.h"          /* FLASH_ROM_ERASE / FLASH_ROM_WRITE：绑定信息持久化 */


uint8_t volatile RF_bound_Flag;
uint32_t ledcount = 0;

/*********************************************************************
 * GLOBAL TYPEDEFS
 */
static uint8_t rf_buf[RF_BUF_LEN];
static struct simple_buf *pRfBuf = NULL;
static struct simple_buf rf_buffer;
uint32_t gBaudRate;

rfTxBuf_t gTxBuf;
uint32_t gSysClock;
uint32_t gRfTxCount;
uint32_t gIntervalTimer;
uint16_t gServerData; // 掉电保存至 flash（rfLoad/saveServerData 实现）

/* ---- 绑定信息持久化（严格绑定所需）----------------------------------------
 * 主机必须记住"自己绑的是哪台从机"，否则：重启后归零、断开连接也被清零，
 * 任何从机/任何主机都能重新接上，"绑定"就失去互斥意义。
 * ★ Flash 擦写期间 CPU 取指受影响，操作代码必须放 RAM（.high_code）执行。 */
#define  BOUND_INFO_FLASH_ADDR   (1024*236)

__HIGH_CODE
static void rfSaveServerData( void )
{
    rfBoundInfo_t info;

    info.head       = 0x55aa;
    info.serverData = gServerData;
    info.bootCount  = 0;
    info.resv       = 0;
    FLASH_ROM_ERASE( BOUND_INFO_FLASH_ADDR, 4096 );
    FLASH_ROM_WRITE( BOUND_INFO_FLASH_ADDR, &info, sizeof(info) );
}

static void rfLoadServerData( void )
{
    rfBoundInfo_t *pInfo = (rfBoundInfo_t *)(BOUND_INFO_FLASH_ADDR);

    gServerData = ( pInfo->head == BOUND_INFO_HEAD ) ? pInfo->serverData : 0;
    PRINT("load serverData = %x\n", gServerData);
}
uint16_t gTimeoutMax;
uint16_t gTimeout;

uint8_t gDataSeq;
uint8_t gRfStatus;
uint8_t gBoundStatus;
uint8_t gRxDataStatus;

/* ---- 【下行丢包重发】--------------------------------------------------------
 * 下行数据是"主机应答从机轮询"捎带发出的（PKT_CMD_RSP_STATUS / PKT_DATA_RSP_ACK）。
 * 承载数据的那一包若在空中丢了，从机会用**同一个 seq** 重发轮询；原来的实现
 * 只回一个空 ACK —— 那 100 字节（主机已经从 USB 取走了）就永久消失。
 * 实测 1.5M 轻载（100 B/s）下约 1.08%（10/900）的帧就是这么丢的，而且主机
 * 的 CRC 回调不打印、链路也没断，两边都看不见。
 * 现在把最近一次"带下行数据"的应答原样缓存，遇到从机重发同一个 seq 就重发它，
 * 让重试机制真能把数据救回来。
 * 不会重复投递：从机只有在没收到有效应答时才会重发同一个 seq；而主机的
 * gDataSeq 与从机的 gTxDataSeq 严格同步推进，不存在"旧缓存隔 256 个 seq 误命中"。 */
static uint8_t gLastDataRsp[BUF_LEN_TX];
static uint8_t gLastDataRspLen = 0;     /* 0 = 当前没有可重发的下行包 */

/* ---- 诊断计数（**只在主循环里限速打印**）----------------------------------------
 * 【为什么绝不能在这些 ISR 里 PRINT】dbg_printf -> _uart_putc 是**忙等**：
 *   115200 下 1 字节 87us，"crc err\n" 8 字节 ≈ **0.7ms**。
 * 在 RF 接收中断里打印 = 每次 CRC 错都让主机停 0.7ms：
 *   错误率一高 → 主机回包被拖到从机 150us 接收窗口之外 → 从机重传 →
 *   与主机"迟到"的回包在空口相撞 → 更多 CRC 错 → 更多打印 —— **正反馈雪崩**。
 * 实测现象就是"持续发送久了大量 crc err"，跟着链路卡死 1s 被看门狗断开。
 * 现在 ISR 只累加计数，主循环每秒最多打印一行。 */
static volatile uint16_t gCrcErrCnt = 0;    /* RF 中断里的 CRC 错次数 */
static volatile uint16_t gRxErrCnt  = 0;    /* 收到"序号不认识"的包（失步征兆）次数 */
static volatile uint16_t gUlostCnt  = 0;    /* 上行数据因 RF 环满被丢的次数（PC 侧来不及取）*/
static volatile uint16_t gRejCnt    = 0;    /* 配对被拒次数（严格绑定不符 / RSSI 不够）*/
static volatile uint16_t gRejLocal  = 0;    /* 最近一次被拒时：本机记录的绑定值 */
static volatile uint16_t gRejRemote = 0;    /* 最近一次被拒时：从机带来的绑定值 */
static volatile int8_t   gLastRssi  = 0;    /* 最近一次收到的广播 RSSI（配对时看它）*/

/* ---- RSSI 观测（fix3：本轮【只记录、不参与任何判断】，行为与 fix2 完全一致）----------
 * 为什么需要：RSSI 目前只出现在 rej 行里，成功建链/回连时从来不打印 —— 于是没人知道
 * "临界距离上抖动"到底发生在多少 dBm，也就没法给重连门限定一个有理有据的值。
 * 这一版先把两个标定点量出来：
 *   · join rssi  —— 接受一次建链/回连时的 RSSI   → 用来定 R_JOIN（重连准入门限）
 *   · rx rssi/min/n —— 连接态逐包采样，给该秒内最差值与包数 → 用来定 R_KEEP（保持门限）
 * 【纪律】这些量一律只在 ISR 里【赋值】，打印全部放在主循环的 rfDiagReport() 里 ——
 *        在 ISR 里 PRINT 会把主机拖垮（见下面配对分支的注释）。 */
static volatile int8_t   gJoinRssi    = 0;   /* 最近一次"接受"BOUND_REQ 时的 RSSI */
static volatile uint8_t  gJoinRssiNew = 0;   /* 1 = 有新值待打印 */
static volatile int8_t   gRxRssiLast  = 0;   /* 连接态：最近一包的 RSSI */
static volatile int8_t   gRxRssiMin   = 0;   /* 连接态：本报告周期内最差 */
static volatile uint16_t gRxRssiCnt   = 0;   /* 连接态：本报告周期内采样包数 */
static volatile uint8_t  gJoinGoodCnt  = 0;   /* fix4：连续达标的广播次数（低于门限立刻清零）*/
static volatile uint8_t  gJoinCooling  = 0;   /* fix4：1 = 掉链冷却期内，暂不接受回连 */
static volatile uint32_t gJoinCoolTick = 0;   /* fix4：掉链时刻（SysTick 计数）*/
static uint8_t           gBadQualCnt   = 0;   /* fix5：连续"质量不达标"的秒数（仅主循环用）*/
extern volatile uint16_t gUsbOvfCnt;        /* 定义在 usb_uart.c：USB OUT 环满次数 */
/* 定义在 rf.c（两端共用底座，未在 rf.h 里导出）：rfRxStartChecked() 要自己设接收窗口 */
extern rfipRx_t          gRxParam;
static uint32_t          gDiagLastTick = 0;

/* ---- 保持侧质量门限（fix5）---------------------------------------------------
 * 准入门限（上面）保证"信号够好才连"，但**连上之后信号变差它并不会主动断**：
 * 实测 rssi -70 ~ -83 之间链路能一直撑着不超时，于是出现"显示连着、crc/n 却有 15~25%"
 * 的链路 —— 按"数据能正常收发"的标准这已经不合格。
 * 这条补上保持侧：连上后若**丢包率连续超标**就主动断开。断开后准入门限自然拦住重连
 * （同一条 -52 线），链路停在"未连接"，直到信号恢复。
 * 判据用实测结果 crc/n 而不是 RSSI —— 它直接对应"数据到底跑不跑得动"。
 *
 * 【为什么是 40 而不是 10】曾经取 10（与准入同一条线），台架实测发现**代价太大**：
 * 断开距离从 fix4 的 -83 缩到 -62 ~ -75（三次实测断点：crc 190/951@-62、
 * 198/1081@-75、145/963），"能跑但慢"的那一大段可用距离被砍掉了。
 * 取 40 后：只在链路**真的坏了**才断（实测 -75~-76 才有 40~80% 丢包，物理极限是 -83），
 * 既恢复了绝大部分可用距离，又仍能挡住"连上却基本没数据"的状态。
 * 一句话：**准入严格（-52，保证连上就能用），保持宽松（40%，不替用户砍距离）。** */
#define LINK_QUAL_MAX_PCT   40      /* 丢包率上限（%）：只抓"链路真的坏了"，不砍可用距离 */
#define LINK_QUAL_BAD_SECS  3       /* 连续超标这么多秒才断（防抖）*/
#define LINK_QUAL_MIN_PKTS  100     /* 样本太少不判（比值没意义）*/

/*******************************************************************************
 * @fn      rfDiagReport
 *
 * @brief   主循环诊断打印：每秒最多一行，把 ISR 里累加的计数报出来。
 *          刻意 noinline 且**不加** __HIGH_CODE（RAM 省着用）。
 *
 * @return  None.
 */
static void __attribute__((noinline)) rfDiagReport( void )
{
    uint32_t now = SysTick->CNT;

    if( gDiagLastTick == 0 )
    {
        gDiagLastTick = now;
        return;
    }
    if( (uint32_t)( now - gDiagLastTick ) < LedMsToTicks( 1000 ) )
    {
        return;
    }
    gDiagLastTick = now;
    /* ---- fix5 保持侧质量判据 -------------------------------------------------
     * 必须在下面把 gCrcErrCnt / gRxRssiCnt 清零【之前】抓样本。
     * 用 nBad/(nGood+nBad) 作为丢包率（crc 错 = 收到但坏了，n = 收到且好的）。
     * 断开走既有路径：置 DATA_STATUS_TIMEOUT -> 下一次 RF_RxQuery() 调 rf_disconnect()，
     * 于是冷却 + 准入门限一起生效。
     * 【注意】若当前有下行数据正等 USB 取走（DATA_STATUS_RCV），先不断 —— 让它送达，
     *         下一圈再判（要求的是"连续 3 秒超标"，晚一秒无妨），避免丢掉在手数据。 */
    {
        uint16_t nGood = gRxRssiCnt;
        uint16_t nBad  = gCrcErrCnt;
        uint32_t nTot  = (uint32_t)nGood + nBad;

        if( RF_bound_Flag && nTot >= LINK_QUAL_MIN_PKTS && gRxDataStatus != DATA_STATUS_RCV )
        {
            if( (uint32_t)nBad * 100 > nTot * LINK_QUAL_MAX_PCT )
            {
                if( ++gBadQualCnt >= LINK_QUAL_BAD_SECS )
                {
                    gBadQualCnt = 0;
                    PRINT( "drop: bad quality crc %u/%u\n", (unsigned)nBad, (unsigned)nTot );
                    gRxDataStatus = DATA_STATUS_TIMEOUT;
                }
            }
            else
            {
                gBadQualCnt = 0;
            }
        }
        else
        {
            gBadQualCnt = 0;
        }
    }

    if( gCrcErrCnt || gRxErrCnt || gUlostCnt || gUsbOvfCnt )
    {
        PRINT( "crc %u rxerr %u ulost %u usbovf %u\n",
               (unsigned)gCrcErrCnt, (unsigned)gRxErrCnt,
               (unsigned)gUlostCnt, (unsigned)gUsbOvfCnt );
        gCrcErrCnt = 0;
        gRxErrCnt  = 0;
        gUlostCnt  = 0;
        gUsbOvfCnt = 0;
    }
    if( gRejCnt )
    {
        PRINT( "rej %u local %x remote %x rssi %d\n",
               (unsigned)gRejCnt, (unsigned)gRejLocal, (unsigned)gRejRemote, (int)gLastRssi );
        gRejCnt = 0;
    }
    /* ---- RSSI 观测（fix3）：只打印，不影响任何判断 ---- */
    if( gJoinRssiNew )
    {
        PRINT( "join rssi %d\n", (int)gJoinRssi );
        gJoinRssiNew = 0;
    }
    if( gRxRssiCnt )
    {
        PRINT( "rx rssi %d min %d n %u\n",
               (int)gRxRssiLast, (int)gRxRssiMin, (unsigned)gRxRssiCnt );
        gRxRssiCnt = 0;
        gRxRssiMin = 0;
    }
}

static void rfProcessRx( rfPackage_t *pPkt );
static void rfProcessTx( void );
static void rfProcessCrcError( void );
static  void rfProcessTimeout( void );
// GAP Service Callbacks
rfStatusCBs_t rfCBs =
{
    rfProcessRx,
    rfProcessTx,
    rfProcessCrcError,
    rfProcessTimeout,
};

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

/*******************************************************************************
 * @fn      rf_rand
 *
 * @brief   随机数生成函数
 *
 * @return  None.
 */
__HIGH_CODE
uint32_t rf_rand16( uint32_t seed )
{
    static uint32_t holdrand;
    uint64_t tmp;

    holdrand += seed;
    holdrand = holdrand * 1664525 + 1013904223;
    tmp = holdrand;
    tmp = (tmp*0xFFFF)>>32;
    return tmp;
}

/*******************************************************************************
 * @fn      rf_rand
 *
 * @brief   随机数生成函数
 *
 * @return  None.
 */
__HIGH_CODE
uint32_t rf_rand_aa( uint16_t rand )
{
    uint32_t aa = rand;

    aa = (aa<<8)|0x6E0000B6;
    return aa;
}

/*******************************************************************************
 * @fn      rfCacheDownlink
 *
 * @brief   把刚组装好、带下行数据的应答缓存一份，供"从机重发同一 seq"时原样重发。
 *
 *          刻意 noinline 且**不加** __HIGH_CODE：调用点 rfProcessRx() 在 .highcode(RAM)，
 *          内联进去会白占 RAM；本函数只做一次拷贝，时序不敏感。
 *
 * @param   pPkt 组装好的发送包（gTxBuf.TxBuf）
 * @return  None.
 */
static void __attribute__((noinline)) rfCacheDownlink( rfPackage_t *pPkt )
{
    if( pPkt->length <= sizeof(gLastDataRsp) )
    {
        __MCPY( gLastDataRsp, pPkt, (uint8_t *)pPkt + pPkt->length );
        gLastDataRspLen = pPkt->length;
    }
}

/*******************************************************************************
 * @fn      rfResendCached
 *
 * @brief   从机重发同一个 seq 时，把缓存的那包写回发送缓冲。
 *
 * @param   pPkt   刚收到的包（取它的 seq 与缓存比对）
 * @param   pPkt_t 发送缓冲（gTxBuf.TxBuf）
 * @return  1 = 已把缓存写回，调用者直接发即可；0 = 没命中，调用者照旧回空 ACK
 */
static uint8_t __attribute__((noinline)) rfResendCached( rfPackage_t *pPkt, rfPackage_t *pPkt_t )
{
    if( gLastDataRspLen == 0 )
    {
        return 0;
    }
    if( ((rfPackage_t *)gLastDataRsp)->seq != pPkt->seq )
    {
        return 0;
    }
    __MCPY( pPkt_t, gLastDataRsp, gLastDataRsp + gLastDataRspLen );
    return 1;
}

/* ---- 断连时怎么处置射频 / 掉电后怎么恢复 ------------------------------------
 * 【背景（实测现象）】拉距把链路拉断之后再也回连不上，只能给**主机**断电重上电；
 * 重启从机没有任何用。根因就在下面这个 rf_disconnect()：
 * RFRole_Shut() 关的是射频**电源**，而它之后工程只用 RFIP_SetRx()（= rf_rx_start()）
 * 重新起收 —— 掉电弄丢的那组"唤醒寄存器"与射频前端校准值都不会被 SetRx 写回去
 * （反汇编 libCH57xRF.a：RFRole_BasicInit → RFIP_RegInit = RFEND_DevInit +
 *  RFIP_WakeUpRegInit + RFIP_Calibration，只有上电那条路才会做），于是主机变成"聋子"：
 *   · 收不到任何包 → RF_bound_Flag 永远为 0 → LED 一直快闪、3 s 后 USB 也撤掉；
 *   · gBoundStatus 已回 BOUND_STATUS_IDLE，超时回调只把 gRfStatus 置 WAIT、
 *     **不打任何日志** → 现场看就是"静默死亡"，只能断电。
 * 【从机不背这个锅】从机全工程没有 RFRole_Shut() 调用，它一直在广播 BOUND_REQ。
 *
 * RF_DISCONNECT_SHUT = 0（默认，推荐）：断连只停当前收发（RFRole_Stop），不关电源；
 * RF_DISCONNECT_SHUT = 1：保留老行为（关电源），但关完立刻唤醒＋重新校准。
 * 两个值可在台架上 A/B，不用改别的代码。 */
#ifndef RF_DISCONNECT_SHUT
#define RF_DISCONNECT_SHUT   0
#endif

/* ---- 重连准入的 RSSI 质量门限（fix4）-----------------------------------------
 * 目标：**只让"数据能正常收发"的信号建链/回连**。
 * [实测] 连续灌数据、以 crc/n 计丢包率（本对板）：
 *     rssi ≥ -40 dBm  -> 丢包 ≤ 3%
 *     rssi ≈ -52 dBm  -> 丢包 ≈ 10%     <== 取这条线
 *     rssi ≤ -73 dBm  -> 丢包 ≥ 40% 且 n 崩塌（连上也几乎没有数据）-> 抖动的唯一来源
 * 取 -52 之后：-73 以下那种"接上 300ms 就掉"会被**直接拒绝**，链路停在"未连接"
 * （LED 快闪），而不是每 300ms 连断一次（PC 端 USB 还会跟着反复枚举）。
 * [注意] 首次配对那道 `rssi > -58` 语义不同（是"贴近"，用来人工指定连哪一台），
 *        保持不动。它比本门限松，所以可能出现"配得上、但回连不上" -- 这是有意的：
 *        那种位置本来就跑不动数据，让用户走近一点比假装连上更诚实。
 * [日志判读] 被本门限拒时，`rej` 行的 local 与 remote **相等**（身份是匹配的，只是信号差）；
 *        身份不符才是 local != remote。 */
#define RSSI_JOIN_MIN       (-52)   /* dBm：低于此值不接受回连（约 10% 丢包线）*/
#define JOIN_GOOD_N         3       /* 需连续这么多次广播都达标才接受（滤深衰落毛刺）*/
#define JOIN_COOLDOWN_MS    1000    /* 掉链后冷却，避免在门限附近来回抖 */
/*******************************************************************************
 * @fn      rfRadioRecover
 *
 * @brief   射频掉电后的恢复：补上 RFIP_SetRx() 不会做的那一步。
 *
 *          RFRole_Shut() 之后必须重新初始化 RFIP 寄存器并重做前端校准，否则收不到
 *          任何包（见上面 RF_DISCONNECT_SHUT 的说明）。这里用库导出的
 *          RFIP_WakeUpRegInit() + RFIP_Calibration()；RFIP_RegInit() 里还有一步
 *          RFEND_DevInit()，但 CH572rf.h 未导出、调不到 —— 所以默认方案是
 *          "干脆不关电源"，本函数只作为老行为（SHUT=1）与起收失败时的兜底。
 *
 *          刻意 noinline 且不加 __HIGH_CODE：只在主循环的异常路径上跑，校准本身耗时，
 *          放进 RAM 白占内存（主机 RAM 已 78%）。【注意】绝不能在中断里调用。
 *
 * @param   None.
 * @return  None.
 */
static void __attribute__((noinline)) rfRadioRecover( void )
{
    RFIP_WakeUpRegInit( );      /* 恢复 LLE/BB 的唤醒寄存器 */
    RFIP_Calibration( );        /* 射频前端重新校准（耗时，主循环专用） */
    PRINT("rf recover.\n" );    /* 只在真出问题时打一行，现场据此判断有没有走到这条路 */
}

/*******************************************************************************
 * @fn      rfRxStartChecked
 *
 * @brief   主循环起收：RFIP_SetRx() 返回非 0（射频不在可收状态）时先 rfRadioRecover()
 *          再重试一次。把一直丢弃的返回值用起来，避免再次出现"静默变聋"。
 *
 *          RFIP_SetRx() 只在"参数指针为空 / rxDMA==0"（= 射频没被初始化过）时才返回
 *          非 0。走到恢复分支就重新初始化一次；并做 1 秒限速，万一射频真的起不来，
 *          也不会每圈都跑一遍校准把主循环卡死。起收失败时把 gRfStatus 交回 WAIT，
 *          让下一圈还能再试（否则会停在 RX 上永远不再起收）。
 *
 *          【注意】只能从主循环调（内部可能跑 RFIP_Calibration）；中断里的起收
 *          （rfProcessTx()）仍然直接用 rf_rx_start()。
 *
 * @param   rx_us  接收窗口，单位微秒（与 rf_rx_start() 同一语义）
 * @return  None.
 */
static uint32_t gRadioRecoverTick = 0;          /* 上次射频恢复的时刻（SysTick 计数） */

static void __attribute__((noinline)) rfRxStartChecked( uint32_t rx_us )
{
    uint32_t now;

    gRxParam.timeOut = rx_us * 2;               /* 与 rf_rx_start() 同一换算 */
    if( RFIP_SetRx( &gRxParam ) == 0 )
    {
        return;
    }

    now = SysTick->CNT;
    if( gRadioRecoverTick && ( (uint32_t)( now - gRadioRecoverTick ) < LedMsToTicks( 1000 ) ) )
    {
        gRfStatus = RF_STATUS_WAIT;             /* 限速期内先不动，下一圈再来 */
        return;
    }
    gRadioRecoverTick = now;
    rfRadioRecover( );
    gRxParam.timeOut = rx_us * 2;
    if( RFIP_SetRx( &gRxParam ) != 0 )
    {
        gRfStatus = RF_STATUS_WAIT;             /* 恢复后仍起不来：交回主循环重试 */
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
/* 【A/B-3 用】断连后**完整重建射频** = 复刻"上电那条路"：RFRole_Init() → RFRole_BasicInit()
 * 内含 RFEND_DevInit + RFIP_RegInit（唤醒寄存器 + 前端校准），这正是上电时才会做、而
 * RFIP_SetRx() 不会补的那一段。
 *
 * 为什么要有这一档：实测"拉距断连后只有**重启主机**才能回连"，说明 RFRole_Stop()/Shut()
 * 之后单靠 RFIP_SetRx() 恢复不了接收，只有完整重建能救回来。
 *
 * 【必须补回应用回调】RFRole_Init() 结尾有一句 `pCbs = NULL`（见 rf.c），它清掉的正是
 * RFRole_RegisterStatusCbs() 注册的那份回调 → 不补的话射频"在收"但没人处理包，
 * 现象和"聋了"完全一样。所以重建后立刻重新注册。
 * 重建期间关掉两个射频中断：避免库在半初始化状态被中断重入（RFRole_Init() 结尾会自己
 * 重新打开，因此这里在它之后再关一次，注册完再开）。 */
/* 掉链冷却是否仍在生效。只在 ISR（rfProcessRx）里调用；计时用 SysTick，
 * LedMsToTicks() 已在 rf_uart_rx.h 里声明，任何位置都能调。 */
static uint8_t rfJoinCooling( void )
{
    if( !gJoinCooling )
    {
        return 0;
    }
    if( (uint32_t)( SysTick->CNT - gJoinCoolTick ) >= LedMsToTicks( JOIN_COOLDOWN_MS ) )
    {
        gJoinCooling = 0;               /* 冷却结束 */
        return 0;
    }
    return 1;
}

static void __attribute__((noinline)) rfRadioHardReInit( void )
{
    PFIC_DisableIRQ( BLEB_IRQn );
    PFIC_DisableIRQ( BLEL_IRQn );
    RFRole_Init( );
    PFIC_DisableIRQ( BLEB_IRQn );       /* RFRole_Init() 内部又打开了，这里再关一次 */
    PFIC_DisableIRQ( BLEL_IRQn );
    RFRole_RegisterStatusCbs( &rfCBs ); /* ← 别省：RFRole_Init() 把 pCbs 清成了 NULL */
    PFIC_EnableIRQ( BLEB_IRQn );
    PFIC_EnableIRQ( BLEL_IRQn );
}

static void rf_disconnect( void )
{
    RF_bound_Flag = 0;
    PRINT("disconnect.\n" );
#if ( RF_DISCONNECT_SHUT == 1 )
    /* A/B-1（最早的老行为）：关射频电源。关完**必须**立刻唤醒＋重新校准，否则主机再也
     * 收不到包 —— 原先只有这一句 RFRole_Shut()，就是"拉距断连后无法回连、只能重启主机"
     * 的根因。 */
    RFRole_Shut( );
    rfRadioRecover( );
#elif ( RF_DISCONNECT_SHUT == 2 )
    /* A/B-2【最小干预】：断连时**完全不碰射频状态**（不 Stop、不 Shut）—— 与从机的做法
     * 一致（从机 rf_disconnect() 全工程不调 Stop/Shut，它的回连一直是好的），只把收发参数
     * 复位回广播值，由主循环的 rfRxStartChecked() 重新起收。 */
    ( void )0;
#elif ( RF_DISCONNECT_SHUT == 3 )
    /* A/B-3【完整重建】：Stop + 复刻上电那条路（见 rfRadioHardReInit 的说明）。 */
    RFRole_Stop( );
    rfRadioHardReInit( );
#else
    /* A/B-0（当前默认）：只停当前收发，不动射频电源 —— 后续 rfRxStartChecked()/RFIP_SetRx()
     * 就能正常恢复，不需要重跑初始化。**实测这一档回连不上，仍需重启主机。** */
    RFRole_Stop( );
#endif
    gBoundStatus = BOUND_STATUS_IDLE;
    gRxDataStatus = DATA_STATUS_START;
    TMR_ITCfg(DISABLE, TMR_IT_CYC_END); // 关闭中断

    rf_tx_set_sync_word( AA );
    rf_tx_set_frequency( DEF_FREQUENCY );
    rf_rx_set_sync_word( AA );
    rf_rx_set_frequency( DEF_FREQUENCY );
    /* 【回连关键】断开后按"未配对"的方式**连续收包**（gIntervalTimer=0 → RFIP_SetRx 的
     * timeOut=0 = 不超时、一直听）—— 上电时就是这条（gIntervalTimer 初值 0），而连接期
     * 是 10ms 窗口。既然"重启主机"能回连，就把断开后的收听方式对齐上电那一档。 */
    gIntervalTimer = 0;
    gRfStatus = RF_STATUS_WAIT;
    gLastDataRspLen = 0;        /* 重连后序号从 0 起，旧缓存必须作废 */
    /* 【fix4】进入回连冷却：JOIN_COOLDOWN_MS 内不接受回连，并把"连续达标"计数清零。
     * 目的：即使信号刚好在门限附近来回飘，也不会每几百毫秒就连断一次。 */
    gJoinCooling  = 1;
    gJoinCoolTick = SysTick->CNT;
    gJoinGoodCnt  = 0;
    gBadQualCnt   = 0;          /* fix5：新链路重新计数 */
    /* 【注意】此处不能清 gServerData：断开连接 != 解除绑定。
     * 之前每次断开都清零，主机一掉线就"失忆"，任何从机都能重新接上。 */
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
static void rf_bound( bound_rsp_t *rsp )
{
    gTimeout = 0;
    gBoundStatus = BOUND_STATUS_WAIT;
    rf_tx_set_sync_word( rsp->accessaddr );
    rf_tx_set_frequency( rsp->channel );
    rf_tx_set_phy_type( rsp->phy );

    rf_rx_set_sync_word( rsp->accessaddr );
    rf_rx_set_frequency( rsp->channel );
    rf_rx_set_phy_type( rsp->phy );
    gLastDataRspLen = 0;        /* 新绑定：gDataSeq 归零，旧缓存作废 */
    PRINT("bound rsp sent.%X %x\n",rsp->accessaddr,rsp->channel );
    /* 【这里**不**置 RF_bound_Flag、**不**落盘】把 BOUND_RSP 发出去只是"单向假定"：
     * 从机可能没收到（空口丢包 / 它还在旧参数上重试）。主机若就此自认已连上，就会
     * "LED 熄灭 + 枚举 USB + 守着新参数等"，而从机还在旧参数上广播 —— 直到主机超时断开，
     * 表现就是"灯说连上了，数据却一直不通"。所以"已连接"改由 rfBoundConfirm() 在
     * **收到从机在新参数上的第一包**时认定。*/
}

/* 【真正建链成功的判定】在新参数上收到从机的第一包 = 链路确实双向通了 —— 此刻才做两件事：
 *   ① 置 RF_bound_Flag：LED 由快闪转为熄灭（LedStatusQuery 的 gLinkStable 去抖也基于它，
 *      USB 枚举同理）→ 从此"熄灭"才真的等于"连上了"；
 *   ② 落盘 serverData：此时存下的必然是**从机真正采纳**的那一份（配对阶段从机会反复广播，
 *      早落盘会存成"主机最后一次生成、从机却没采纳"的值 → 两边不一致、永久 reject）。
 * 若那包 BOUND_RSP 丢了：从机不会用新参数发包 → 本函数不会被调用 → 主机超时走
 * rf_disconnect()，LED 回到快闪并重新配对 —— 这正是应有的表现。*/
static void rfBoundConfirm( void )
{
    RF_bound_Flag = 1;
    PRINT("bound success (link up).\n" );
    rfSaveServerData( );
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
    rfPackage_t *pPkt_t = (rfPackage_t *)gTxBuf.TxBuf;

    if( gBoundStatus == BOUND_STATUS_IDLE )
    {
        if( pPkt->type == PKT_CMD_BOUND_REQ )
        {
            BOOL reg=0;
            uint8_t firstPair = 0;      /* 1 = 首次配对（从机 severData==0）：本次才生成新身份 */
            bound_req_t  *pReq_t =  ( bound_req_t  *)(pPkt+1);
            if( pPkt->length == sizeof(bound_req_t) + 2 )
            {
                int8_t rssi  = *(uint8_t* )((uint8_t* )pPkt + pPkt->length + 2 +2 );

                /* 【fix4 RSSI 质量门限】先更新"连续达标"计数：达标才累加，一掉就清零。
                 * 用"连续几次"而不是单次读数，是因为 RSSI 有 ±3dB 抖动与深衰落毛刺，
                 * 单次采样会把"刚好飘上来一下"的弱信号放进链路。 */
                if( rssi >= RSSI_JOIN_MIN )
                {
                    if( gJoinGoodCnt < 255 )
                    {
                        gJoinGoodCnt++;
                    }
                }
                else
                {
                    gJoinGoodCnt = 0;
                }

                // 非第一次连接，地址匹配可连
                if( pReq_t->severData )
                {
                    /* 【严格绑定】必须与本机记录的 serverData 完全一致才接受。
                         * 原实现为 `!gServerData || 相等`，即"本机未绑定就无条件接受"，
                         * 导致任何一台新主机都能接住已绑定的从机。主机绑定已持久化，
                         * 所以去掉该分支不会造成"主机重启后连不上自己的从机"。 */
                    if( pReq_t->severData == gServerData
                        && gJoinGoodCnt >= JOIN_GOOD_N      /* ② 信号够好（连续达标）*/
                        && !rfJoinCooling() )               /* ③ 不在掉链冷却期 */
                    {
                        reg = 1;
                    }
                }
                else
                {
                    // 第一次连接，需靠近连接
                    /* 首次配对要求"贴近"（用物理靠近来人工指定连哪一台）。
                     * 门槛几经调整：原设计 -35dBm 过严（天线不理想时几厘米也常只有
                     * -40~-60dBm，会永远 reject）；曾放宽到 -60dBm，但那让"解绑后两板
                     * 仍放在一起"时会被立刻重新绑走。实测本对板近距离 RSSI 仅 -56dBm（天线所限），-40 在物理上达不到、永远 reject，
                     * 故取 -58dBm（略严于最初的 -60）。若贴近仍配不上可放宽到 -60；
                     * 解绑后把两板稍分开即可保持未绑定。若贴近仍配不上，把它放宽到 -50。
                     * 实际 RSSI 会打印在 reject 日志里（本例 -56），可据此校准。 */
                    if( rssi > -58 )
                    {
                        reg = 1;
                        firstPair = 1;
                    }
                }
                if( reg )
                {
                    bound_rsp_t *pRsp_t = (bound_rsp_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];

                    gRfStatus = RF_STATUS_TXRSP;
                    gDataSeq = 0;
                    /* 【RSSI 观测】记下"在多少 dBm 上接受了这次建链/回连"——
                     * 这是 R_JOIN（重连准入门限）唯一的标定点。只赋值，不判断、不打印。 */
                    gJoinRssi    = rssi;
                    gJoinRssiNew = 1;
                    /* 【回连时绝不改身份 —— 这才是"拉距断连后永久回连不上"的正解】
                     * 原实现在这里**无条件** gServerData = rf_rand16(rssi)，而新身份只靠一包
                     * 可能丢掉的 BOUND_RSP 传达，于是必然出现"两侧绑定值不一致 -> 永久 reject"。
                     * 两种失败方向都实测到了，说明只要还在"换身份"，分歧就无法根除：
                     *   · 只改本机、RSP 丢了    -> local 新值 / remote 旧值（local dd28 / remote a2a2）
                     *   · "提议+成功才提交"一版 -> RSP 送达、从机已落盘，但本机 confirm 没跑到
                     *                             -> local 旧值 / remote 新值（local 38c2 / remote 9927）
                     * 正解：**回连（severData 已匹配）时一个字都不动 gServerData**。
                     * 本机只接受 severData == gServerData 的请求，回的 RSP 里带的也还是
                     * gServerData -> 从机赋值前后相等。因此无论 RSP 丢没丢、rfBoundConfirm()
                     * 跑没跑到，两侧永远是同一个值；一次失败只是"再试一次"，不会永久 reject。
                     * 只有**首次配对**（从机 severData==0）才生成新身份：那条路也没有永久分歧 --
                     * RSP 丢了从机仍上报 0，永远走上面的 RSSI 分支重新配对。
                     * 代价：回连沿用原 accessaddr/频点（rf_rand_aa() 是 gServerData 的纯函数），
                     * 不再每次轮换；对"回连"本就该稳定，反而更可预期。
                     * 严格绑定的安全性不变：身份不匹配的陌生主机/从机依旧被拒。 */
                    if( firstPair )
                    {
                        gServerData = rf_rand16( rssi );
                    }
                    /* 【注意】不在此处保存！配对阶段从机会反复广播请求，这里每应答
                     * 一次就生成新值并覆盖 Flash，而主机最终保存的是"最后一次"、
                     * 从机保存的却可能是"更早某一次"，导致两边绑定值不同 -> 永久
                     * reject（实测 local=a33a / remote=c242）。持久化改到
                     * rf_bound()：只有真正建链成功那一刻才落盘。 */
                    pPkt_t->type = PKT_CMD_BOUND_RSP;
                    pPkt_t->length = PKT_DATA_OFFSET+sizeof(bound_rsp_t);
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                    pRsp_t->accessaddr = rf_rand_aa( gServerData );
                    {
                        /* 不再拿随机数低 6 位当频点（会随机撞 WiFi ch1/6/11），
                         * 改为从候选表里挑，见 rf.h 的 CH_HOP_TBL 说明。
                         * 仍用 gServerData 取模，保持"每次配对换个频点"的随机性。 */
                        static const uint8_t chHopTbl[CH_HOP_TBL_LEN] = CH_HOP_TBL;
                        pRsp_t->channel = chHopTbl[ gServerData % CH_HOP_TBL_LEN ];
                    }
                    pRsp_t->phy = CONN_PHY_TYPE; // 2M
                    pRsp_t->severData = gServerData;
                    pRsp_t->interval = POLL_INTERVAL_MS;    /* 从机轮询周期，与 CONN_INTERVAL 解耦 */
                    pRsp_t->timeout = CONN_TIMEOUT;
                    rf_tx_start( pPkt_t, 20 );
                    gDataSeq++;
                    gIntervalTimer = pReq_t->interval*1000;
                    gTimeoutMax = BOUND_EST_COUNT;
                    return ;
                }
                else
                {
                    /* 只计数，不打印：配对阶段从机每 20ms 广播一次，在 ISR 里打印会把
                     * 主机拖垮（同 crc err 那类问题）。主循环汇总行会带 local/remote/rssi。 */
                    gRejCnt++;
                    gRejLocal  = gServerData;
                    gRejRemote = pReq_t->severData;
                    /* local = 本机记录的绑定；remote = 从机带来的绑定。
                     * 两者不等且 remote != 0 → 严格绑定生效（拒绝），RSSI 不参与判断；
                     * remote == 0 才是"从机未绑定"（走上面的 RSSI 首次配对分支）。 */
                }
                gLastRssi = rssi;
            }
        }
        gRfStatus = RF_STATUS_WAIT;
    }
    else
    {
        /* 【RSSI 观测】连接态逐包采样，供 R_KEEP（保持门限）标定。
         * 偏移与配对分支完全一致（pPkt->length + 2 + 2）；pPkt->length 是 uint8_t（≤255），
         * 而 RxBuf 是 264 字节 → 最大下标 259，不会越界。
         * 同样只赋值、不判断、不打印（打印在主循环里）。 */
        {
            int8_t r = *(int8_t *)( (uint8_t *)pPkt + pPkt->length + 2 + 2 );

            gRxRssiLast = r;
            if( gRxRssiCnt == 0 || r < gRxRssiMin )
            {
                gRxRssiMin = r;
            }
            gRxRssiCnt++;
        }

        /* 【收到新参数上的第一包 = 真连上了】见 rfBoundConfirm() 的说明 */
        if( !RF_bound_Flag )
        {
            rfBoundConfirm( );
        }

        rfRsp_t *pRsp_t = (rfRsp_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];
        if( pPkt->type == PKT_CMD_GET_STATUS )
        {
            gRfStatus = RF_STATUS_TX;
            if( pPkt->seq == gDataSeq )
            {
                uint8_t s;
                typeBufSize len = RSP_DATA_MAX;     /* 250：rspData 从 TxBuf[5] 起，取 251 会越界 1 字节 */
                pPkt_t->type = PKT_CMD_RSP_STATUS;
                /* 【先作废旧缓存 —— 修「最后一包多发 1~3 次」】8 位 seq 每 256 次交换
                 * 就绕回一圈（空闲轮询约 2.5 s），而旧缓存只有在「又发了一包下行数据」
                 * 时才会被覆盖。于是从机绕回后重发同一个 seq 时，rfResendCached() 会把
                 * 几秒前那包数据当成新应答重发出去 —— 从机照收，目标设备就多收一个
                 * 重复包（实测 300 帧里多出 1 帧，100 B/帧 → 多 99 B）。所以每条
                 * 【新序号】的应答都先把缓存作废，只有本次真的捎带了下行数据时才由
                 * rfCacheDownlink() 重新填上。 */
                gLastDataRspLen = 0;
                if( gBoundStatus < BOUND_STATUS_EST  )
                {
                    // 发送串口波特率
                    pPkt_t->length = PKT_DATA_OFFSET+1+sizeof(Uart0Para);
                    pRsp_t->opcode = OPCODE_BSP;
                    pRsp_t->buad_t.BaudRate = Uart0Para.BaudRate;
                    pRsp_t->buad_t.StopBits = Uart0Para.StopBits;
                    pRsp_t->buad_t.ParityType = Uart0Para.ParityType;
                    pRsp_t->buad_t.DataBits = Uart0Para.DataBits;
                    pRsp_t->buad_t.ioStaus = Uart0Para.ioStaus;
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                }
                else
                {
                    /* 【DTR 极性指令优先】一包只带一个 opcode：先把切换指令发下去（从机收到即
                     * 改电平 + 写 Flash 掉电保存），下一包再走 BSP 把当前 DTR 电平按新极性重发
                     * （切极性的那两条路径都会置 UART_Status，见 usb_uart.c）。
                     * 缓存一份：万一应答丢了、从机按同一 seq 重发，rfResendCached() 会原样重发；
                     * 从机侧对"极性没变"是幂等的，不会重复擦 Flash。 */
                    if( gDtrPolPending )
                    {
                        gDtrPolPending = 0;
                        pPkt_t->length = PKT_DATA_OFFSET+1+1;
                        pRsp_t->opcode = OPCODE_DTR_POL;
                        pRsp_t->other.rspData[0] = gDtrPol;
                        pPkt_t->seq = gDataSeq;
                        pPkt_t->resv = 0;
                        rfCacheDownlink( pPkt_t );
                    }
                    else
                    {
                        s = USB_RxQuery( pRsp_t->other.rspData, &len );
                        if( s == 0 )
                        {
                            pPkt_t->length = PKT_DATA_OFFSET+1+len;
                            pRsp_t->opcode = OPCODE_DATA;
                            pPkt_t->seq = gDataSeq;
                            pPkt_t->resv = 0;
                            gRfTxCount += len;
                            rfCacheDownlink( pPkt_t );      /* 缓存，供丢包时重发 */
                        }
                        else if( s == 0x80 )
                        {
                            // 发送串口波特率
                            pPkt_t->length = PKT_DATA_OFFSET+1+sizeof(Uart0Para);
                            pRsp_t->opcode = OPCODE_BSP;
                            pRsp_t->buad_t.BaudRate = Uart0Para.BaudRate;
                            pRsp_t->buad_t.StopBits = Uart0Para.StopBits;
                            pRsp_t->buad_t.ParityType = Uart0Para.ParityType;
                            pRsp_t->buad_t.DataBits = Uart0Para.DataBits;
                            pRsp_t->buad_t.ioStaus = Uart0Para.ioStaus;
                            pPkt_t->seq = gDataSeq;
                            pPkt_t->resv = 0;
                        }
                        else
                        {
                            pPkt_t->length = PKT_DATA_OFFSET;
                            pRsp_t->opcode = OPCODE_ACK;
                            pPkt_t->seq = gDataSeq;
                            pPkt_t->resv = 0;
                        }
                    }
                }
                gDataSeq++;
            }
            else if( pPkt->seq == (uint8_t)(gDataSeq - 1) )
            {
                /* 【失步保护 + 下行丢包重发】重复包：上次的应答丢了、从机在重传。
                 * 若上次那包**带着下行数据**，这里原样重发它 —— 否则那部分数据就
                 * 永久丢了（实测 1.5M 轻载下 1.08% 的帧都丢在这条路上）。
                 * 没命中缓存（上次是空 ACK / 线码包）才回到老行为：只回空 ACK，
                 * seq 回显从机的值，让它的 gTxDataSeq 能正常推进、避免序号失配死锁。 */
                if( !rfResendCached( pPkt, pPkt_t ) )
                {
                    pPkt_t->length = PKT_DATA_OFFSET;
                    pRsp_t->opcode = OPCODE_ACK;
                    pPkt_t->seq = pPkt->seq;
                    pPkt_t->resv = 0;
                }
            }
            rf_tx_start( pPkt_t, 20 );
            if( pRsp_t->opcode == OPCODE_BSP )
            {
                PRINT("uart param.\n");
            }
        }
        else if( pPkt->type == PKT_DATA_FLAG )
        {

            gRfStatus = RF_STATUS_TX;
            if( pPkt->seq == gDataSeq )
            {
                typeBufSize len = pPkt->length;
                len -= PKT_DATA_OFFSET;
                /* 同 GET_STATUS 分支：新序号 = 上一次的缓存已过期，先作废 */
                gLastDataRspLen = 0;
                write_buf( pRfBuf, (pPkt+1), &len );
                if( !len )
                {
                    /* 上行数据被丢：RF 环满（PC 侧来不及取）。只计数 —— 主循环汇总行里的
                     * `ulost`，用来判断"上行到底有没有丢"，以及是否需要给上行加流控。 */
                    gUlostCnt++;
                }
                gRxDataStatus = DATA_STATUS_RCV;
                // 如果接收缓存满，则会丢数据
                len = RSP_DATA_MAX;                 /* 同上：下行单次最长 250 字节 */
                pPkt_t->type = PKT_DATA_RSP_ACK;
                
                if( gBoundStatus == BOUND_STATUS_EST && !USB_RxQuery( pRsp_t->other.rspData, &len ) )
                {
                    pPkt_t->length = PKT_DATA_OFFSET+1+len;
                    pRsp_t->opcode = OPCODE_DATA;
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                    gRfTxCount += len;
                    rfCacheDownlink( pPkt_t );          /* 缓存，供丢包时重发 */
                }
                else
                {
                    pPkt_t->length = PKT_DATA_OFFSET;
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                }
                gDataSeq++;
            }
            else if( pPkt->seq == (uint8_t)(gDataSeq - 1) )
            {
                /* 【失步保护 + 下行丢包重发】同上：重复的**上行**包不再写入 USB 侧
                 * （重复写会让上位机收到重复数据），但这一包的**应答里可能捎带了下行
                 * 数据** —— 那部分若丢了同样永久消失，所以命中缓存就原样重发。 */
                if( !rfResendCached( pPkt, pPkt_t ) )
                {
                    pPkt_t->length = PKT_DATA_OFFSET;
                    pRsp_t->opcode = OPCODE_ACK;
                    pPkt_t->seq = pPkt->seq;
                    pPkt_t->resv = 0;
                }
            }
            rf_tx_start( pPkt_t, 20 );
            
        }
        else
        {
            gRxErrCnt++;        /* 只计数：在主循环里每秒汇总一次（同上，ISR 里不打印） */
            if( ++gTimeout > gTimeoutMax )
            {
                gRxDataStatus = DATA_STATUS_TIMEOUT;
                PRINT("connect timeout.\n");
            }
            else
            {
                gRfStatus = RF_STATUS_WAIT;
            }
            return ;
        }
        if( gBoundStatus == BOUND_STATUS_WAIT )
        {
            // 连接确认，启动连接通信超时
            gBoundStatus = BOUND_STATUS_EST;
            gIntervalTimer = CONN_INTERVAL*1000;
            gTimeoutMax = (CONN_TIMEOUT*10/CONN_INTERVAL);
       }
       gTimeout = 0;
    }
    
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
static void rfProcessTx( void )
{
    if( gRfStatus == RF_STATUS_TXRSP )
    {
         rf_bound( (bound_rsp_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN] );
    }
    gRfStatus = RF_STATUS_RX;
    rf_rx_start( gIntervalTimer );
}

/*******************************************************************************
 * @fn      RF_ProcessRxError
 *
 * @brief
 *
 * @param   None.
 *
 * @return  None.
 */
__HIGH_CODE
static void rfProcessCrcError( void )
{
    gRfStatus = RF_STATUS_WAIT;
    /* 只计数、不打印：见 gCrcErrCnt 定义处的说明（在 ISR 里 PRINT 会自我放大错误率） */
    gCrcErrCnt++;
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
static void rfProcessTimeout( void )
{
    // PRINT("r -timeout %x\n",gRfStatus);
    if( gBoundStatus && ++gTimeout > gTimeoutMax )
    {
        gRxDataStatus = DATA_STATUS_TIMEOUT;
        PRINT("connect timeout.\n");
    }
    else
    {
        gRfStatus = RF_STATUS_WAIT;
    }
}

/* LED 时基：SysTick 自由计数 + 启动标定，使闪烁周期是真实时间 */
static uint32_t gLedHalfTicks  = 0;
static uint32_t gLedPulseTicks = 0;             /* 数据提示脉冲宽度（计数） */
/* 【与从机同一套改法：序号闩锁替代"时刻比对"】原写法"ISR 记时刻 + 主循环用早先采样的 now 比"，
 * 中断落在采样点与读时刻之间时会下溢 → 误判过期并清掉脉冲 → 数据收到了灯偶尔不闪。
 * 现在 ISR 只累加计数，主循环看到计数变化才用自己采样的 SysTick 起一段完整提示。 */
static volatile uint8_t  gLedPulseCnt  = 0;     /* ISR：有新数据活动就 +1 */
static uint8_t           gLedPulseSeen = 0;     /* 主循环：已消费到哪个计数 */
static uint32_t          gLedLitStart  = 0;     /* 主循环：本次提示起点（SysTick 计数） */

uint32_t gLedTicksPerMs = 0;                    /* 每毫秒的 SysTick 计数（启动标定） */

/* 去抖后的连接状态：连上立即置 1；断开要持续 LINK_DEBOUNCE_MS 才清 0。
 * USB 枚举与 LED 都以它为准，避免信号临界时反复抖动 */
volatile uint8_t  gLinkStable     = 0;
static   uint8_t  gLinkDownActive = 0;
static   uint32_t gLinkDownTick   = 0;

/*******************************************************************************
 * @fn      LedMsToTicks
 *
 * @brief   毫秒 -> SysTick 计数（用启动标定出的 gLedTicksPerMs）
 *
 * @param   ms  毫秒数
 *
 * @return  对应的 SysTick 计数
 */
uint32_t LedMsToTicks( uint32_t ms )
{
    if( gLedTicksPerMs == 0 )
    {
        return ms;          /* 尚未标定（正常不会走到，标定在 process_main 之前完成） */
    }
    return gLedTicksPerMs * ms;
}

/*******************************************************************************
 * @fn      LedTimerInit
 *
 * @brief   初始化 LED 时基：SysTick 自由计数（不使能中断），并用 mDelaymS(10)
 *          标定出 LED_BLINK_MS 毫秒对应的计数值。
 *          这样闪烁周期 = LED_BLINK_MS*2 毫秒，与主循环跑多快无关。
 *
 * @return  None.
 */
void LedTimerInit( void )
{
    uint32_t t0, t1, sysclk;

    SysTick->CNTL = 0;
    SysTick->CMP  = 0xFFFFFFFF;                 /* 最大重载值，不使能中断 */
    SysTick->SR   = 0;
    SysTick->CTLR = SysTick_CTLR_STRE | SysTick_CTLR_STCLK | SysTick_CTLR_STE;

    t0 = SysTick->CNT;
    mDelaymS( 10 );                             /* 软件延时 10ms 作参考 */
    t1 = SysTick->CNT;

    /* 注意：mDelaymS() 的软件循环次数是按编译期 FREQ_SYS 算的【固定值】，
     * 主频越低，同样循环越慢：真实时长 = 10ms × FREQ_SYS / sysclk。
     * 故"LED_BLINK_MS 毫秒的计数" = ticks × sysclk × LED_BLINK_MS / (10 × FREQ_SYS)。
     * （主机 sysclk==FREQ_SYS 时系数为 1；从机跑 24MHz，差异很大，必须这样算） */
    sysclk = GetSysClock( );
    if( sysclk == 0 )
    {
        sysclk = FREQ_SYS;
    }
    {
        /* 先算出"每毫秒的 SysTick 计数"，再分别乘上各时间段 */
        uint32_t ticks_per_ms = (uint32_t)( (uint64_t)( t1 - t0 ) * sysclk
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
 * @fn      LedStatusQuery
 *
 * @brief   LED 指示：未连接从机时快闪（周期 = LED_BLINK_MS*2 毫秒）；
 *          连接成功后熄灭，但收到/发出数据时亮 LED_DATA_PULSE_MS 毫秒。
 *          基于 SysTick 真实时间，不依赖 USB / 主循环速度。
 *
 * @return  None.
 */
void LedStatusQuery( void )
{
    uint32_t now = SysTick->CNT;

    /* ---- 连接状态去抖（与 LED_FUNC 无关：USB 枚举也依赖 gLinkStable） ---- */
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

#if(defined(LED_FUNC)) && (LED_FUNC == TRUE)
    {
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
        return;
    }

    /* 未连接 → 按真实时间翻转（ledcount 当"上次翻转时的 SysTick 计数"用） */
    if( (uint32_t)( now - ledcount ) >= gLedHalfTicks )
    {
        GPIOA_InverseBits(LED_PIN);
        ledcount = now;
    }
    }
#endif
}

/*******************************************************************************
 * @fn      RF_RxQuery
 *
 * @brief   主循环里唯一的"无线接收侧"入口：取上行数据 + **维持射频状态机**。
 *
 *          ★ 这两件事必须分开看，否则会踩一个很隐蔽的坑：
 *          · 取数据：只有 USB 侧收得下时才把 RF 环里的数据取走（收不下就留在环里下一圈再试）；
 *          · 状态机：`WAIT -> RX` 的自愈**必须无条件执行**。`rfProcessTx()` 每次应答完确实会
 *            重开收包窗口，但 **CRC 错误（rfProcessCrcError）和接收超时（rfProcessTimeout）
 *            之后 gRfStatus 会停在 WAIT，只有这里能把它拉回 RX**。
 *            若"USB 侧没读走上一包(EP2 压着数据)"时连这一步也跳过，那么**一次 CRC 错误
 *            之后射频就永久停摆**：从机的轮询再也收不到应答 → 从机 1 秒后判"断开"(LED 快闪)，
 *            而 dongle 3 秒去抖后还会 USB_DeInit() 把设备从主机撤掉（WSL 上表现为
 *            /dev/ttyUSB0 凭空消失、usbip attach 失效，且只能靠重新枚举自愈）。
 *            典型触发：映射到 WSL 后**没有程序打开 /dev/ttyUSB0**（内核不发 bulk-IN，
 *            EP2 一直压着），此时从机只要上送过一个字节，就进入上述状态。
 *
 * @param   buf       USB 侧上行数据的落点（EP2 缓冲）
 * @param   *len      入：buf 容量；出：实际长度
 * @param   usb_free  0 = USB 侧还压着没被读走的一包：**不取数据，但状态机照跑**
 * @return  实际取到的字节数（usb_free=0 时恒为 0，调用方据此不会去装填 EP2）
 */
__HIGH_CODE
uint8_t RF_RxQuery( void *buf, typeBufSize *len, uint8_t usb_free )
{
    rfDiagReport( );        /* 诊断汇总（每秒最多一行），只在主循环里做 */

    if( gRxDataStatus == DATA_STATUS_RCV )
    {
        if( usb_free )
        {
            if( read_buf( pRfBuf, buf, len ) == 0 )
            {
#if(defined(LED_FUNC)) && (LED_FUNC == TRUE)
                LedDataPulse( );        /* 无线侧收到数据 → LED 亮一下 */
#endif
                PFIC_DisableIRQ(BLEL_IRQn);
                gRxDataStatus = DATA_STATUS_START;
                PFIC_EnableIRQ(BLEL_IRQn);
            }
        }
        else
        {
            /* 【数据先不取，但自愈照跑】*len 必须清零，否则调用方会把入参容量当成数据长度 */
            *len = 0;
            if( gRfStatus == RF_STATUS_WAIT )
            {
                gRfStatus = RF_STATUS_RX;
                rfRxStartChecked( gIntervalTimer );     /* 带返回值检查（见函数说明） */
            }
        }
    }
    else if( gRxDataStatus == DATA_STATUS_TIMEOUT )
    {
        *len = 0;
        rf_disconnect( );
    }
    else
    {
        *len = 0;
        if( gRfStatus == RF_STATUS_WAIT )
        {
            gRfStatus = RF_STATUS_RX;
            rfRxStartChecked( gIntervalTimer );         /* 带返回值检查（见函数说明） */
        }
    }
    return *len;
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
/* 固件标识：开机打印，便于从日志一眼确认烧的是哪一版（别再出现"不知道烧的是哪版"） */
#define RF_FW_TAG   "dongle reconnect-fix6: join>=-52dBm + keep-side crc/n>40%x3s"

__HIGH_CODE
void RF_UartRxInit( void )
{
    PRINT("----------------- rf uart rx mode -----------------\n");
    PRINT("build: %s\n", RF_FW_TAG);
    gRxDataStatus = DATA_STATUS_IDLE;
    rf_buffer_create(&pRfBuf);
    gDataSeq = 0;
    rfLoadServerData( );             /* 从 Flash 读回绑定（严格绑定） */
    gBoundStatus = BOUND_STATUS_IDLE;
    gTxBuf.status = 0;
    gIntervalTimer = CONN_INTERVAL*1000;
    gSysClock = GetSysClock( );
    RFRole_RegisterStatusCbs( &rfCBs );
    gRfStatus = RF_STATUS_WAIT;
}

/******************************** endfile @rf ******************************/
