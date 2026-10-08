/********************************** (C) COPYRIGHT *******************************
* File Name          : rf_uart_rx.h
* Author             : WCH
* Version            : V1.0
* Date               : 2022/03/10
* Description        : 
*******************************************************************************/

#ifndef __RF_UART_RX_H
#define __RF_UART_RX_H

#ifdef __cplusplus
extern "C"
{
#endif
#include <CH572rf.h>
#include "CH57x_common.h"
#include "buf.h"

#define   RF_BUF_LEN   512


#define  CONN_INTERVAL     10
#define  CONN_TIMEOUT      100
#define  CONN_PHY_TYPE     1   // 2M

/* 从机轮询间隔（毫秒）—— 决定下行"首字节"延迟：空闲时从机每隔这么久探一次，主机才有机会捎带数据。
 *
 * 与 CONN_INTERVAL **解耦**：CONN_INTERVAL 现在只管主机自己的接收窗口长度与超时计数
 * （gIntervalTimer = CONN_INTERVAL*1000 微秒、gTimeoutMax = CONN_TIMEOUT*10/CONN_INTERVAL），
 * 不再等于从机轮询周期；从机只认本字段下发的值（UART_SetTimer(interval)）。
 *
 * 关于"两个 tick 才轮询一次"：从机 UART_SetTimer(ms) 的 tick = ms/2，而轮询状态机
 * START -> SENDING -> SEND 要走两个 tick，所以**本值就是从机真实的轮询周期**：
 *   1 -> 轮询 1ms（下行首字节约 0.5ms 平均 + 空口 0.3ms + 主机 USB 约 1ms，量级 2ms）
 *   5 -> 5ms     10 -> 10ms（原始设计值）
 * 代价：空口占用随速率线性上升（一轮"轮询+应答"约 0.2ms：1ms 档约 20% 占空，10ms 档约 2%），
 *       干扰与功耗相应增加，嫌吵就调大。**必须 >= 1**（0 会让从机定时器重载值变 0，中断连发）。 */
#define  POLL_INTERVAL_MS   1

void RF_UartRxInit( void );
uint8_t RF_RxQuery( void *buf, typeBufSize *len );
void LedStatusQuery( void );
void LedTimerInit( void );
void LedDataPulse( void );
uint32_t LedMsToTicks( uint32_t ms );

extern volatile uint8_t gLinkStable;

extern uint8_t volatile RF_bound_Flag;


#ifdef __cplusplus
}
#endif

#endif
