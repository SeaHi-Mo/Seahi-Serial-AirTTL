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
uint16_t gServerData; // 需要掉电保存，需保存至flash
uint16_t gTimeoutMax;
uint16_t gTimeout;

uint8_t gDataSeq;
uint8_t gRfStatus;
uint8_t gBoundStatus;
uint8_t gRxDataStatus;

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
 * @fn      rf_disconnect
 *
 * @brief   断开连接
 *
 * @param   None.
 *
 * @return  None.
 */
static void rf_disconnect( void )
{
    RF_bound_Flag = 0;
    PRINT("disconnect.\n" );
    RFRole_Shut( );
    gBoundStatus = BOUND_STATUS_IDLE;
    gRxDataStatus = DATA_STATUS_START;
    TMR_ITCfg(DISABLE, TMR_IT_CYC_END); // 关闭中断

    rf_tx_set_sync_word( AA );
    rf_tx_set_frequency( DEF_FREQUENCY );
    rf_rx_set_sync_word( AA );
    rf_rx_set_frequency( DEF_FREQUENCY );
    gRfStatus = RF_STATUS_WAIT;
    gServerData = 0;
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
    RF_bound_Flag = 1;
    PRINT("bound success.%X %x\n",rsp->accessaddr,rsp->channel );
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
            bound_req_t  *pReq_t =  ( bound_req_t  *)(pPkt+1);
            if( pPkt->length == sizeof(bound_req_t) + 2 )
            {
                int8_t rssi  = *(uint8_t* )((uint8_t* )pPkt + pPkt->length + 2 +2 );

                // 非第一次连接，地址匹配可连
                if( pReq_t->severData )
                {
                    // 信息匹配，或者dongle重新上电了
                    if( !gServerData || pReq_t->severData == gServerData )
                    {
                        reg = 1;
                    }
                }
                else
                {
                    // 第一次连接，需靠近连接
                    /* 【临时测试用】门槛放宽到 -110dBm = 实际不限制 RSSI，
                     * 这样即使没焊天线（实测只有 -96dBm）也能配对成功，
                     * 便于先验证 LED 指示 / USB 枚举 / 断连重连等逻辑。
                     * 正式使用时应改回 -35（严格贴近）或 -60。 */
                    if( rssi > -110 )
                    {
                        reg = 1;
                    }
                }
                if( reg )
                {
                    bound_rsp_t *pRsp_t = (bound_rsp_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];

                    gRfStatus = RF_STATUS_TXRSP;
                    // 生成下次回连的随机信息
                    gDataSeq = 0;
                    gServerData = rf_rand16( rssi );
                    pPkt_t->type = PKT_CMD_BOUND_RSP;
                    pPkt_t->length = PKT_DATA_OFFSET+sizeof(bound_rsp_t);
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                    pRsp_t->accessaddr = rf_rand_aa( gServerData );
                    pRsp_t->channel = gServerData&0x3F;
                    pRsp_t->phy = CONN_PHY_TYPE; // 2M
                    pRsp_t->severData = gServerData;
                    pRsp_t->interval = CONN_INTERVAL;
                    pRsp_t->timeout = CONN_TIMEOUT;
                    rf_tx_start( pPkt_t, 20 );
                    gDataSeq++;
                    gIntervalTimer = pReq_t->interval*1000;
                    gTimeoutMax = BOUND_EST_COUNT;
                    return ;
                }
                else
                {
                    PRINT(" reject..\n");
                }
                PRINT( "rssi=%d \n",rssi);
            }
        }
        gRfStatus = RF_STATUS_WAIT;
    }
    else
    {
        rfRsp_t *pRsp_t = (rfRsp_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];
        if( pPkt->type == PKT_CMD_GET_STATUS )
        {
            gRfStatus = RF_STATUS_TX;
            if( pPkt->seq == gDataSeq )
            {
                uint8_t s;
                typeBufSize len = DATA_LEN_MAX_TX;
                pPkt_t->type = PKT_CMD_RSP_STATUS;
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
                    s = USB_RxQuery( pRsp_t->other.rspData, &len );
                    if( s == 0 )
                    {
                        pPkt_t->length = PKT_DATA_OFFSET+1+len;
                        pRsp_t->opcode = OPCODE_DATA;
                        pPkt_t->seq = gDataSeq;
                        pPkt_t->resv = 0;
                        gRfTxCount += len;
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
                gDataSeq++;
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
                write_buf( pRfBuf, (pPkt+1), &len );
                gRxDataStatus = DATA_STATUS_RCV;
                // 如果接收缓存满，则会丢数据
                len = DATA_LEN_MAX_TX;
                pPkt_t->type = PKT_DATA_RSP_ACK;
                
                if( gBoundStatus == BOUND_STATUS_EST && !USB_RxQuery( pRsp_t->other.rspData, &len ) )
                {
                    pPkt_t->length = PKT_DATA_OFFSET+1+len;
                    pRsp_t->opcode = OPCODE_DATA;
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                    gRfTxCount += len;
                }
                else
                {
                    pPkt_t->length = PKT_DATA_OFFSET;
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                }
                gDataSeq++;
            }
            else
            {
                // 重传
            }
            rf_tx_start( pPkt_t, 20 );
            
        }
        else
        {
            PRINT("error data.\n");
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
static volatile uint32_t gLedDataTick  = 0;     /* 最近一次数据活动的时刻 */
static volatile uint8_t  gLedDataActive = 0;    /* 是否有数据活动待显示 */

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
    gLedDataTick   = SysTick->CNT;
    gLedDataActive = 1;
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
#if(defined(LED_FUNC)) && (LED_FUNC == TRUE)
    uint32_t now = SysTick->CNT;
    uint8_t  lit = 0;

    if(RF_bound_Flag)
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
        return;
    }

    /* 未连接 → 按真实时间翻转（ledcount 当"上次翻转时的 SysTick 计数"用） */
    if( (uint32_t)( now - ledcount ) >= gLedHalfTicks )
    {
        GPIOA_InverseBits(LED_PIN);
        ledcount = now;
    }
#endif
}

/*******************************************************************************
 * @fn      RF_RxQuery
 *
 * @brief
 *
 * @return  None.
 */
__HIGH_CODE
uint8_t RF_RxQuery( void *buf, typeBufSize *len )
{


    uint8_t *p;

    if( gRxDataStatus == DATA_STATUS_RCV )
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
            rf_rx_start( gIntervalTimer );
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
__HIGH_CODE
void RF_UartRxInit( void )
{
    PRINT("----------------- rf uart rx mode -----------------\n");
    gRxDataStatus = DATA_STATUS_IDLE;
    rf_buffer_create(&pRfBuf);
    gDataSeq = 0;
    gServerData = 0;
    gBoundStatus = BOUND_STATUS_IDLE;
    gTxBuf.status = 0;
    gIntervalTimer = CONN_INTERVAL*1000;
    gSysClock = GetSysClock( );
    RFRole_RegisterStatusCbs( &rfCBs );
    gRfStatus = RF_STATUS_WAIT;
}

/******************************** endfile @rf ******************************/
