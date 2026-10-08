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
static uint32_t          gDiagLastTick = 0;

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
    if( gCrcErrCnt || gRxErrCnt )
    {
        PRINT( "crc %u rxerr %u\n", (unsigned)gCrcErrCnt, (unsigned)gRxErrCnt );
        gCrcErrCnt = 0;
        gRxErrCnt  = 0;
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
    gLastDataRspLen = 0;        /* 重连后序号从 0 起，旧缓存必须作废 */
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
    RF_bound_Flag = 1;
    gLastDataRspLen = 0;        /* 新绑定：gDataSeq 归零，旧缓存作废 */
    PRINT("bound success.%X %x\n",rsp->accessaddr,rsp->channel );
    /* 建链成功才持久化：此时采用的 serverData 与从机保存的必然一致 */
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
            bound_req_t  *pReq_t =  ( bound_req_t  *)(pPkt+1);
            if( pPkt->length == sizeof(bound_req_t) + 2 )
            {
                int8_t rssi  = *(uint8_t* )((uint8_t* )pPkt + pPkt->length + 2 +2 );

                // 非第一次连接，地址匹配可连
                if( pReq_t->severData )
                {
                    /* 【严格绑定】必须与本机记录的 serverData 完全一致才接受。
                         * 原实现为 `!gServerData || 相等`，即"本机未绑定就无条件接受"，
                         * 导致任何一台新主机都能接住已绑定的从机。主机绑定已持久化，
                         * 所以去掉该分支不会造成"主机重启后连不上自己的从机"。 */
                    if( pReq_t->severData == gServerData )
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
                    }
                }
                if( reg )
                {
                    bound_rsp_t *pRsp_t = (bound_rsp_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];

                    gRfStatus = RF_STATUS_TXRSP;
                    // 生成下次回连的随机信息
                    gDataSeq = 0;
                    gServerData = rf_rand16( rssi );
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
                    PRINT(" reject.. local=%x remote=%x\n", gServerData, pReq_t->severData);
                    /* local = 本机记录的绑定；remote = 从机带来的绑定。
                     * 两者不等且 remote != 0 → 严格绑定生效（拒绝），RSSI 不参与判断；
                     * remote == 0 才是"从机未绑定"（走上面的 RSSI 首次配对分支）。 */
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
 * @brief
 *
 * @return  None.
 */
__HIGH_CODE
uint8_t RF_RxQuery( void *buf, typeBufSize *len )
{
    uint8_t *p;

    rfDiagReport( );        /* 诊断汇总（每秒最多一行），只在主循环里做 */

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
    rfLoadServerData( );             /* 从 Flash 读回绑定（严格绑定） */
    gBoundStatus = BOUND_STATUS_IDLE;
    gTxBuf.status = 0;
    gIntervalTimer = CONN_INTERVAL*1000;
    gSysClock = GetSysClock( );
    RFRole_RegisterStatusCbs( &rfCBs );
    gRfStatus = RF_STATUS_WAIT;
}

/******************************** endfile @rf ******************************/
