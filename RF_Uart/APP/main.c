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
#include <rf.h>
#include <rf_uart_tx.h>
#include <uart.h>


/*********************************************************************
 * GLOBAL TYPEDEFS
 */
__attribute__((used))
__HIGH_CODE
void process_main( void )
{
    while(1)
    {
        RF_StatusQuery( );
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
    HSECFG_Capacitance(HSECap_18p);
    SetSysClock(CLK_SOURCE_HSE_PLL_24MHz);
    UART_Init( );
    PRINT("start.\n");
    PRINT("%s\n", VER_RF_LIB);
    RFRole_Init( );
    LedTimerInit( );            /* 先标定 LED 时基：下面的启动计数提示要用真实毫秒延时 */
    RF_UartTxInit( );           /* 内含「连续重启 N 次解绑」的上电计数/解绑 */
    process_main();
}

/******************************** endfile @ main ******************************/
