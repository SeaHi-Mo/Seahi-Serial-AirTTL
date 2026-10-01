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
 * @brief   DTR/RTS功能与ST一键下载功能
 * @note    默认开启ST一键下载功能且关闭DTR/RTS功能；
 *          如需开启DTR/RTS功能，将DTR_RTS_FUNC置1即可；
 *          DTR / RESET_PIN     PA2
 *          RTS / BOOT_PIN      PA3
 */

 
#ifndef DTR_RTS_FUNC
// #define DTR_RTS_FUNC     TRUE
#define DTR_RTS_FUNC     FALSE
#endif 
#if((defined(DTR_RTS_FUNC)) && (DTR_RTS_FUNC == TRUE))
#define    DTR_PIN        (1<<2)
#define    RTS_PIN        (1<<3)
#else
#define    RESET_PIN      (1<<2)
#define    BOOT_PIN       (1<<3)
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
#define    LED_DATA_PULSE_MS 30



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
