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

/* 连接状态去抖时间（毫秒）：连上立即生效；断开要持续这么久才认定断开（之后才
 * USB_DeInit，PC 上的串口才会消失）。
 * 【2026-10-15 从 3000 调到 1000】原设 3 秒的理由是"切极性会让链路短暂断一下，
 * 别让 PC 上的设备反复插拔"—— 那个前提已经不存在了：从机侧 fix12 让切极性**只改电平、
 * 不复位**，链路零中断（台架实测：跨切极性 n 全程 1064~1101，一次都没断）。
 * 降到 1000ms 后与主机侧的 JOIN_COOLDOWN_MS(1000) 同量级 —— 断链后 1 秒内本来也
 * 不允许重新建链（准入要连续 3 包达标 + 1 秒冷却），所以不会因临界信号抖动而让串口
 * 反复插拔。实感延迟 = CONN_TIMEOUT(≈1s) + 本值 ≈ 2 秒（原约 4 秒）。
 * ⚠️ 若把从机改回 DTR_POL_SELF_REBOOT=1（自复位），那条路会带来 2.7~13 秒链路空档，
 *    必须同时把本值调回 ≥3000、并把 DTRPOL_GRACE_MS 调回 12000，否则 USB 会掉。 */
#define    LINK_DEBOUNCE_MS  1000




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

/* 【DTR 极性指令】见 rf.h 的 OPCODE_DTR_POL 与 README「修改 DTR 的默认电平」。
 *   gDtrPol        主机当前要下发给从机的极性：0 = 默认极性，1 = 反相
 *   gDtrPolPending 有一条极性指令还没下发（usb_uart.c 置位，rf_uart_rx.c 发完清 0）
 * 主机**不保存**这个值：指令是绝对值，掉电保存在**从机** Flash 里，主机每次上电不需要知道
 * （所以主机换一台/重上电都不会把从机的设定冲回去）。 */
extern uint8_t gDtrPol;
extern uint8_t gDtrPolPending;

void USB_Init( void );
void USB_StatusQuery( void );
void USB_DeInit( void );
uint8_t USB_RxQuery( void *buf, typeBufSize *len );

#ifdef __cplusplus
}
#endif

#endif
