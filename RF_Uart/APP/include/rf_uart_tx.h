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

/* 【重启 N 次解绑】
 * 每次上电把 bootCount+1 写进 Flash；若本次能持续运行 BOOT_FAST_RESET_SEC 秒，
 * 由主循环把计数清零。于是只有"上电后很快又断电"才会累积，
 * 累计到 BOOT_UNBIND_TIMES 次 → 清除绑定信息（解绑）。 */
#define  BOOT_UNBIND_TIMES      5
#define  BOOT_FAST_RESET_SEC    15    /* 上电后跑满该秒数即视为"正常启动"并清零计数；
                                        * 取 15s 是实测校准：启动约 1.5s + LED 慢闪(0.7s/次)
                                        * 判断并断电，5s 窗口太窄会把操作难度拉满、
                                        * 一旦超时就清零导致前面几次重启全白做。 */

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
