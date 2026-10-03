/********************************** (C) COPYRIGHT *******************************
* File Name          : rf_uart_tx.h
* Author             : WCH
* Version            : V1.0
* Date               : 2022/03/10
* Description        : 
*******************************************************************************/

#ifndef __RF_UART_TX_H
#define __RF_UART_TX_H

#ifdef __cplusplus
extern "C"
{
#endif
#include <CH572rf.h>
#include "CH57x_common.h"
#include "buf.h"


#define  ADV_INTERVAL        20

#define  RESEND_COUNT        40

#define  BOUND_INFO_FLASH_ADDR         (1024*236)

/* 【连续重启 N 次解绑】—— 从机换主机时不必再刷固件
 *
 * 机制：每次上电把 bootCount+1 写进 Flash；若本次上电后能连续运行
 * BOOT_FAST_RESET_SEC 秒，说明是正常使用（不是"快速重启"），由主循环把计数清零。
 * 于是只有"上电后很快又断电"才会累积，累计到 BOOT_UNBIND_TIMES 次 → 解绑。
 *
 * ★ 清零**只认运行时长，不认"配对成功"**：旧版本在 rf_bound() 里清零，而换主机时
 *   旧主机往往还插在电脑上、从机每次上电都先回连成功 → 计数永远被抹掉，
 *   表现就是"怎么重启都解不了绑、只能刷固件"。详见 rf_uart_tx.c。 */
#define  BOOT_UNBIND_TIMES      5
#define  BOOT_FAST_RESET_SEC    15    /* 上电后连续运行满该秒数即视为"正常启动"并清零计数；
                                        * 只有每次都赶在这之前断电（快速重启）才会累积。
                                        * 15s 是折中：太短来不及操作、太长会误判正常使用。 */

extern uint32_t  gRfRxFlag;
extern struct simple_buf *pRfBuf;

/* rf tx status */
#define   STA_IDLE          0x00
#define   STA_BUSY          0x01
#define   STA_RESEND        0x02

void RF_UartTxInit( void );
void RF_StatusQuery( void );
void LedTimerInit( void );
void LedTimerCalibBegin( void );
void LedTimerCalibEnd( void );
uint32_t LedMsToTicks( uint32_t ms );


#ifdef __cplusplus
}
#endif

#endif
