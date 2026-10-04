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

        /* gLinkStable 是去抖后的连接状态（由上面的 LedStatusQuery 更新）：
         * 连上立即生效；断开要持续 LINK_DEBOUNCE_MS 才认定。
         * 这样信号在临界值时不会让 USB 反复枚举（PC 上设备反复插拔）。 */
        if( gLinkStable )
        {
            if( gUsbInited == 0 )
            {
                gUsbInited = 1;
                USB_Init( );
            }
        }
        else
        {
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
    HSECFG_Capacitance( HSECap_6p );   /* 频偏标定：外部 4.7pF + 片内 6p 档实测 31.999954MHz(-1.4ppm)；原为 HSECap_18p */
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
