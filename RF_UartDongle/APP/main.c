/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.1
 * Date               : 2025/06/27
 * Description        :
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/******************************************************************************/
/* 头文件包含 */
#include "rf.h"
#include "rf_uart_rx.h"
#include "usb_uart.h"



/*********************************************************************
 * GLOBAL TYPEDEFS
 */
static uint8_t gUsbInited = 0;

__attribute__((used))
__HIGH_CODE
void process_main( void )
{
    while(1)
    {
        /* LED 独立于 USB：未连接从机 → 快闪；连接成功 → 熄灭 */
        LedStatusQuery( );

        if( RF_bound_Flag )
        {
            /* 与从机连接成功后才启动 USB 枚举，
             * 免得电脑上先冒出一个还没配上对的空串口 */
            if( gUsbInited == 0 )
            {
                gUsbInited = 1;
                USB_Init( );
            }
        }
        else
        {
            /* 从机断开 → 收回串口（主机侧看到"设备已拔出"），
             * 回到"未连接"状态，等待从机重新广播、自动重连 */
            if( gUsbInited )
            {
                gUsbInited = 0;
                USB_DeInit( );
            }
        }

        USB_StatusQuery();
    }
}

/*********************************************************************
 * @fn      main
 *
 * @brief   主函数
 *
 * @return  none
 */
int main(void)
{
    HSECFG_Capacitance( HSECap_18p );
    SetSysClock( CLK_SOURCE_HSE_PLL_100MHz );
#ifdef DEBUG
    GPIOA_SetBits( bTXD_0 );
    GPIOA_ModeCfg( bTXD_0, GPIO_ModeOut_PP_5mA ); // TXD-配置推挽输出，注意先让IO口输出高电平
    UART_Remap( ENABLE, UART_TX_REMAP_PA3, UART_RX_REMAP_PA2 );
    UART_DefInit( );
#endif

    //设置LED_PIN
#if(defined(LED_FUNC)) && (LED_FUNC == TRUE)
    GPIOA_ResetBits(LED_PIN);
    GPIOA_ModeCfg(LED_PIN, GPIO_ModeOut_PP_5mA);
#endif

    PRINT("start.\n");
    PRINT("%s\n", VER_RF_LIB);
    /* USB 暂不初始化：未与从机连接时不枚举（见 process_main） */
    RFRole_Init( );
    RF_UartRxInit( );
    LedTimerInit( );            /* 标定 LED 时基：周期 = LED_BLINK_MS*2 ms */
    process_main( );
}

/******************************** endfile @ main ******************************/
