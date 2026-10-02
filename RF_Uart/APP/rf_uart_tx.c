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
uint8_t gRfStatus;
uint8_t gBoundStatus;
uint8_t getDataProbe;
uint32_t  gRfRxFlag;
rfPackage_t *pPkt_t;

static void rfProcessRx( rfPackage_t *pPkt );
static void rfProcessTx( void );
static void rfProcessTimeout( void );
void LedTimerCalibBegin( void );
void LedTimerCalibEnd( void );

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
    FLASH_ROM_ERASE( BOUND_INFO_FLASH_ADDR, 4096 );
    FLASH_ROM_WRITE( BOUND_INFO_FLASH_ADDR,&info,4 );

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
            rf_bound( (bound_rsp_t *)(pPkt+1) );
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
                // DTR 电平状态
                if( pRsp_t->buad_t.ioStaus&0x20 )
                {
                    GPIOA_SetBits( DTR_PIN );
                }
                else
                {
                    GPIOA_ResetBits( DTR_PIN );
                }
                // RTS 电平状态
                if( pRsp_t->buad_t.ioStaus&0x40 )
                {
                    GPIOA_SetBits( RTS_PIN );
                }
                else
                {
                    GPIOA_ResetBits( RTS_PIN );
                }
#endif
            }
            else if( pRsp_t->opcode == OPCODE_DATA )
            {
                typeBufSize len;

                pPkt_t = pPkt;
                getDataProbe = 6;


#if(DTR_RTS_FUNC == FALSE)
                //RESET与BOOT引脚控制协议解析 仅在连接下载时触发
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
    rfBoundInfo_t *pInfo;
    PRINT("----------------- rf uart tx mode -----------------\n");
    gTxDataSeq = 0;
    gRfRxFlag = 0;
    gBoundStatus = BOUND_STATUS_IDLE;
    gTxBuf.status = 0;
    rf_buffer_create(&pRfBuf);

    pInfo = (rfBoundInfo_t *)(BOUND_INFO_FLASH_ADDR);
    if( pInfo->head == BOUND_INFO_HEAD )
    {
        gServerData = pInfo->serverData;
    }
    else {
        gServerData = 0;
    }
    PRINT("gServerData = %x \n",gServerData);
    RFRole_RegisterStatusCbs( &rfCBs );
}

/******************************** endfile @rf ******************************/
