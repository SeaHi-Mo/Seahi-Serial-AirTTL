/********************************** (C) COPYRIGHT *******************************
* File Name          : usb_uart.h
* Author             : WCH
* Version            : V1.0
* Date               : 2025/04/22
* Description        : 
*******************************************************************************/

#ifndef __USB_UART_H
#define __USB_UART_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "CH57x_common.h"
#include "buf.h"


/**
 * @brief  LED 功能与定义 默认开启
 * @note   LED_PIN     PA7
 */
#ifndef LED_FUNC
#define LED_FUNC     TRUE
// #define LED_FUNC     FALSE
#endif 

#if((defined(LED_FUNC)) && (LED_FUNC == TRUE))
#define    LED_PIN        (1<<7) 
#endif

/* LED 指示策略（Dongle 定制）
 *   未与从机连接 —— LED 快闪
 *   连接成功后   —— LED 熄灭
 * LED_BLINK_MS = 翻转一次 LED 的间隔（毫秒），闪烁周期 = 2 × 该值。
 * 基于 SysTick 真实时间，与主循环速度无关（启动时由 LedTimerInit 标定）。
 */
#define    LED_BLINK_MS      100

/* 数据收发时的 LED 提示：
 *   1 = 收到/发出数据时让 LED 亮 LED_DATA_PULSE_MS 毫秒（肉眼可见的闪一下）
 *   0 = 不提示，连接成功后 LED 保持熄灭 */
#define    LED_DATA_BLINK    1
#define    LED_DATA_PULSE_MS 80

/* 连接状态去抖时间（毫秒）：连上立即生效；断开要持续这么久才认定断开。
 * 用来抑制信号临界值时的抖动 —— 否则 USB 会反复枚举（PC 上设备反复插拔）。 */
#define    LINK_DEBOUNCE_MS  3000




//Line Code结构
 typedef struct __PACKED _LINE_CODE
{
  uint32_t  BaudRate;   /* 波特率 */
  uint8_t StopBits;   /* 停止位计数，0：1停止位，1：1.5停止位，2：2停止位 */
  uint8_t ParityType;   /* 校验位，0：None，1：Odd，2：Even，3：Mark，4：Space */
  uint8_t DataBits;   /* 数据位计数：5，6，7，8，16 */
  uint8_t ioStaus;
}LINE_CODE, *PLINE_CODE;
extern LINE_CODE Uart0Para;

void USB_Init( void );
void USB_StatusQuery( void );
void USB_DeInit( void );
uint8_t USB_RxQuery( void *buf, typeBufSize *len );

#ifdef __cplusplus
}
#endif

#endif
