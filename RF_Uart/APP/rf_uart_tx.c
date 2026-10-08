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

/*******************************************************************************
 * @fn      rfApplyDtrRts
 *
 * @brief   把主机下发的 modem 输出位搬到 PA2/PA3（DTR/RTS）。
 *
 *          刻意**不加** __HIGH_CODE 且 noinline：本函数只在收到 OPCODE_BSP 时执行，
 *          时序不敏感；而调用它的 rfProcessRx() 本身在 .highcode（RAM），
 *          内联回去会让这几十字节白占 RAM（从机 RAM 已到 95.9%，96% 出头就不稳）。
 *
 * @param   ioStaus 主机下发的 modem 输出位：bit5=DTR、bit6=RTS，1 = 未断言（输出高）
 *          —— 映射到 **DTR->PA3、RTS->PA2**（见 uart.h 的引脚定义）
 * @return  None.
 */
#if((defined(DTR_RTS_FUNC)) && (DTR_RTS_FUNC == TRUE))
static void __attribute__((noinline)) rfApplyDtrRts( uint8_t ioStaus )
{
    // DTR 电平状态（PA3）
    if( ioStaus & 0x20 ) GPIOA_SetBits( DTR_PIN );
    else                 GPIOA_ResetBits( DTR_PIN );

    // RTS 电平状态（PA2）
    if( ioStaus & 0x40 ) GPIOA_SetBits( RTS_PIN );
    else                 GPIOA_ResetBits( RTS_PIN );
}
#endif

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
#if(defined(LED_FUNC)) && (LED_FUNC == TRUE)
            /* 下行数据有两条捎带路径（本包 / PKT_CMD_RSP_STATUS），这里补上本包这一条，
             * 否则"数据收到了但灯不闪" —— 因为主机看从机当时在发什么来选择捎带路径 */
            LedDataPulse( );
#endif
            if( pPkt->length > PKT_DATA_OFFSET+1 )
            {
                typeBufSize len;

                pPkt_t = pPkt;
                {
                    rfRsp_t *pRsp_t = (rfRsp_t *)(pPkt_t+1);
                    len = pPkt_t->length-PKT_DATA_OFFSET-1;
                    gRfRxFlag = write_buf( pRfBuf, pRsp_t->other.rspData, &len );
                    if( !len )
                    {
                        UART_Send(pRsp_t->other.rspData,pPkt_t->length-PKT_DATA_OFFSET-1);
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
                /* 【波特率上限 1500000】从机 UART 只有**整数**分频（DL = round(Fsys/8/baud)），
                 * 而它的串口直接对目标设备（用户按标准值设置），所以"实际码率"必须尽量等于请求值：
                 *   1.5 Mbps @24M → DL=2 → 1.500000M（0%）
                 *   3   Mbps @24M → DL=1 → 3.000000M（0%，但链路吞吐只有 27~50 KB/s，毫无收益）
                 *   2   Mbps      → 24M 只能 1.5M（-25%）、100M 只能 2.083M（+4.2%）→ 都不准，实测乱码
                 *   4   Mbps      → 100M 只能 4.1667M（+4.2%），且电脑端端口根本打不开
                 * 所以直接把上限收到 **1.5 Mbps**：超过就夹到 1.5 Mbps，不让从机悄悄跑在
                 * 一个偏差 25% 的码率上（那种情况下上位机只会看到乱码，很难查）。 */
                if( pRsp_t->buad_t.BaudRate > 1500000u )
                {
                    pRsp_t->buad_t.BaudRate = 1500000u;
                }

                if((pRsp_t->buad_t.BaudRate > 400000) && (pRsp_t->buad_t.BaudRate < 1000000))
                {
                    SetSysClock(CLK_SOURCE_HSE_PLL_100MHz);
                }
                else
                {
                    SetSysClock(CLK_SOURCE_HSE_PLL_24MHz);
                }
                LedTimerCalibBegin( );              /* 顺便用这次延时重新标定 LED 时基 */
                mDelaymS(10);
                LedTimerCalibEnd( );                /* 主频变了必须实测重标，不能按比例推算 */

                UART_SetBuad( pRsp_t->buad_t.BaudRate );
                // 停止位
                if( pRsp_t->buad_t.StopBits )
                {
                    R8_UART_LCR |= RB_LCR_STOP_BIT ; // 2个停止位
                }
                else
                {
                    R8_UART_LCR &= ~RB_LCR_STOP_BIT;// 1个停止位
                }

                // 奇偶校验
                if( pRsp_t->buad_t.ParityType )
                {
                    R8_UART_LCR &= ~RB_LCR_PAR_MOD;
                    R8_UART_LCR |= ((pRsp_t->buad_t.ParityType-1)&3)<<4;
                    R8_UART_LCR |= RB_LCR_PAR_EN;
                }
                else
                {
                    R8_UART_LCR &= ~RB_LCR_PAR_EN;
                }

                // 数据位
                R8_UART_LCR &= ~RB_LCR_WORD_SZ;
                R8_UART_LCR |= (pRsp_t->buad_t.DataBits-5);
                if( pRsp_t->buad_t.DataBits )
                {

                }

#if((defined(DTR_RTS_FUNC)) && (DTR_RTS_FUNC == TRUE))
                rfApplyDtrRts( pRsp_t->buad_t.ioStaus );    /* PA3 <- DTR、PA2 <- RTS */
#endif
            }
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
                    //BOOT脚拉高、RESET低电平复位进BOOT
                    GPIOA_SetBits( BOOT_PIN );
                    mDelaymS(1);
                    GPIOA_ResetBits( RESET_PIN );
                    mDelaymS(1);
                    GPIOA_SetBits( RESET_PIN );
                    mDelaymS(1);
                    GPIOA_ResetBits( BOOT_PIN );
                    mDelaymS(50);
                }
#endif
                {
                    rfRsp_t *pRsp_t = (rfRsp_t *)(pPkt_t+1);
                    len = pPkt_t->length-PKT_DATA_OFFSET-1;
                    gRfRxFlag = write_buf( pRfBuf, pRsp_t->other.rspData, &len );
                    if( !len )
                    {
                        UART_Send(pRsp_t->other.rspData,pPkt_t->length-PKT_DATA_OFFSET-1);
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
        gTxDataSeq ++;
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
    gTxBuf.status = STA_RESEND;
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
static volatile uint32_t gLedDataTick  = 0;     /* 最近一次数据活动的时刻 */
static volatile uint8_t  gLedDataActive = 0;    /* 是否有数据活动待显示 */

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
    gLedDataTick   = SysTick->CNT;
    gLedDataActive = 1;
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
    /* LED 指示：未连接 → 快闪（周期 = LED_BLINK_MS*2 ms）；
     * 连接成功 → 熄灭，但收发数据时亮 LED_DATA_PULSE_MS 毫秒。
     * ledcount 当"上次翻转时的 SysTick 计数"用，与主循环速度无关。 */
    {
        uint32_t now = SysTick->CNT;
        uint8_t  lit = 0;

        if( gLinkStable )
        {
#if(LED_DATA_BLINK == 1)
            if( gLedDataActive )
            {
                if( (uint32_t)( now - gLedDataTick ) < gLedPulseTicks )
                {
                    lit = 1;
                }
                else
                {
                    gLedDataActive = 0;
                }
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
                GPIOA_InverseBits(LED_PIN);
                ledcount = now;
            }
        }
    }
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
            if( gTxBuf.resendCount != 0xFF ) gTxBuf.resendCount--;
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
        mDelaymS( 100 );
        GPIOA_ResetBits( LED_PIN );
        mDelaymS( 100 );
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
    if( FLASH_ROM_ERASE( BOUND_INFO_FLASH_ADDR, 4096 ) != 0 )
    {
        return -1;
    }
    if( FLASH_ROM_WRITE( BOUND_INFO_FLASH_ADDR, info, sizeof(*info) ) != 0 )
    {
        return -2;
    }
    if( FLASH_ROM_VERIFY( BOUND_INFO_FLASH_ADDR, info, sizeof(*info) ) != 0 )
    {
        return -3;
    }
    return 0;
}

/* 重试与报警：刻意**不加** __HIGH_CODE（省 RAM）。
 * 每次尝试返回时 Flash 操作已结束，此时执行 Flash 里的代码是安全的。 */
static int rfSaveBoundInfo( rfBoundInfo_t *info )
{
    int tries;
    int rc = 0;

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
 * @fn      rfLedDelayMs
 *
 * @brief   启动提示用的"真实毫秒"延时。
 *
 *          mDelaymS() 是按编译期 FREQ_SYS=100MHz 标定的软件循环，而从机实际跑
 *          24MHz，同样的循环会长约 3 倍 —— 用它做"数几次"的提示根本不准。
 *          这里用 LedTimerInit() 标定过的 SysTick，必须在 LedTimerInit() 之后调用。
 *
 * @param   ms 毫秒数
 * @return  None.
 */
#if(defined(LED_FUNC)) && (LED_FUNC == TRUE)
static void __attribute__((noinline)) rfLedDelayMs( uint32_t ms )
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
#endif

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
        rfLedDelayMs( 2000 );
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
                rfLedDelayMs( 100 );
                GPIOA_ResetBits( LED_PIN );
                rfLedDelayMs( 250 );
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

    rfBootCountStartup();               /* 【重启 N 次解绑】上电计数/解绑 */

    PRINT("gServerData = %x \n",gServerData);
    RFRole_RegisterStatusCbs( &rfCBs );
}

/******************************** endfile @rf ******************************/
