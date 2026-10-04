/********************************** (C) COPYRIGHT *******************************
 * File Name          : uart.h
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2022/06/30
 * Description        : 
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/


#ifndef BLE_DIRECTTEST_APP_INCLUDE_UART_H
#define BLE_DIRECTTEST_APP_INCLUDE_UART_H

#include "buf.h"


#define  UART_BUF_LEN   (1024*3)

#define    TXD_PIN   (1<<0)  // PA0
#define    RXD_PIN   (1<<1)  // PA1                                                 


/**
 * @brief   PA2/PA3 复用：DTR/RTS 直控 与 ST 一键下载的 RESET/BOOT
 * @note    PA2/PA3 是**同一对物理引脚的两个用途**，所以两组宏名在任何配置下都定义：
 *              PA2 = RESET_PIN = RTS_PIN
 *              PA3 = BOOT_PIN  = DTR_PIN
 *
 *          DTR_RTS_FUNC = TRUE（默认）
 *              PC 端串口工具的 DTR/RTS 经无线下发，直接驱动这两个脚：
 *              **DTR -> PA3**、**RTS -> PA2**（ioStaus 对应位为 1 = 未断言 = 输出高）。
 *              ST 一键下载（下行单字节 0x7F）依然可用 —— 那一刻由下载时序临时接管。
 *          DTR_RTS_FUNC = FALSE
 *              两个脚只做 ST 一键下载的 RESET/BOOT，PC 的 DTR/RTS 不影响它们。
 *
 *          【注意】多数串口工具**一打开端口就会拉 DTR/RTS**（断言 = 输出低）。
 *             若这两脚接着目标板的 RESET/BOOT0，开端口就可能把目标按住 ——
 *             上位机要自行设置 DTR、RTS 的初始电平。
 */

#ifndef DTR_RTS_FUNC
#define DTR_RTS_FUNC     TRUE
// #define DTR_RTS_FUNC  FALSE
#endif

#define    DTR_PIN        (1<<3)    /* PA3：PC 的 DTR（也是 ST 下载的 BOOT0） */
#define    RTS_PIN        (1<<2)    /* PA2：PC 的 RTS（也是 ST 下载的 RESET） */
#define    RESET_PIN      (1<<2)    /* PA2 */
#define    BOOT_PIN       (1<<3)    /* PA3 */

/**
 * @brief   ST 一键下载（下行恰好一个字节 0x7F 时跑 RESET/BOOT 时序）
 * @note    与上面的 DTR/RTS 直控**复用同一对引脚、互不冲突**：平时引脚跟随 PC 的
 *          DTR/RTS，收到 0x7F 时由下载时序临时接管。不需要就置 FALSE（省一点 Flash）。
 */
#ifndef ST_ISP_FUNC
#define ST_ISP_FUNC      TRUE
#endif


/**
 * @brief   LED 功能与定义
 * @note    LED_PIN     PA7
 */
#ifndef LED_FUNC
#define LED_FUNC     TRUE
// #define LED_FUNC     FALSE
#endif
#if((defined(LED_FUNC)) && (LED_FUNC == TRUE))
#define    LED_PIN        (1<<7) 
#endif

/* LED 指示策略（从机定制）
 *   未与主机连接 —— LED 快闪
 *   连接成功后   —— LED 熄灭
 * LED_BLINK_MS = 翻转一次 LED 的间隔（毫秒），闪烁周期 = 2 × 该值。
 * 基于 SysTick 真实时间（启动时由 LedTimerInit 标定，切主频时自动补偿）。
 */
#define    LED_BLINK_MS      100

/* 数据收发时的 LED 提示：
 *   1 = 收到/发出数据时让 LED 亮 LED_DATA_PULSE_MS 毫秒（肉眼可见的闪一下）
 *   0 = 不提示，连接成功后 LED 保持熄灭 */
#define    LED_DATA_BLINK    1
#define    LED_DATA_PULSE_MS 80

/* 连接状态去抖时间（毫秒）：连上立即生效；断开要持续这么久才认定断开，
 * 抑制信号临界值时的抖动（LED 不会反复切换） */
#define    LINK_DEBOUNCE_MS  3000



enum uart_status
{
    UART_STATUS_IDLE,
    UART_STATUS_START,
    UART_STATUS_RCVING,
    UART_STATUS_RCV_END,
    UART_STATUS_SENDING,
    UART_STATUS_SEND,
    UART_STATUS_NUM,
};

#define  DATA_LEN_UART       (32)

extern uint8_t gBoundStatus;

void UART_Init(void);
uint8_t UART_RxQuery( void *buf, typeBufSize *len );
void UART_SetBuad( uint32_t buad );
void UART_SetTimer( uint16_t ms );
void UART_Send( char* data, uint16_t size);

#endif /* BLE_DIRECTTEST_APP_INCLUDE_UART_H */
