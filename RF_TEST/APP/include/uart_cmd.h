/********************************** (C) COPYRIGHT *******************************
 * File Name          : uart_cmd.h
 * Description        : RF_TEST 的调试串口命令行接口
 *
 *   PA0 = TXD / PA1 = RXD，115200-8-N-1（与 RF_Uart 从机的串口引脚一致）
 *   PA7 = LED（高电平点亮，与 RF_Uart / RF_UartDongle 一致）
 *
 *   这一层只负责"收字符、攒成一行、发字符串"，不掺任何射频逻辑，
 *   方便射频测试时用普通串口助手（minicom / screen / SSCOM）直接下命令。
 *******************************************************************************/

#ifndef __UART_CMD_H
#define __UART_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CH57x_common.h"

/* 引脚定义（与从机 RF_Uart 相同） */
#define RFTEST_TXD_PIN      ( 1 << 0 )      /* PA0 */
#define RFTEST_RXD_PIN      ( 1 << 1 )      /* PA1 */
#define RFTEST_LED_PIN      ( 1 << 7 )      /* PA7，高电平点亮 */

#define RFTEST_LINE_MAX     64              /* 命令行缓冲长度（含结尾 '\0'） */

/*********************************************************************
 * @fn      UART_CmdInit
 *
 * @brief   初始化调试串口与 LED。必须在 SetSysClock() 之后调用。
 *
 * @return  none
 */
void    UART_CmdInit( void );

/*********************************************************************
 * @fn      UART_CmdGetLine
 *
 * @brief   从接收环形缓冲里取出一条完整命令行（以 CR/LF 结尾，不含换行符）。
 *          非阻塞：没有完整行时返回 0，主循环可以继续做别的事。
 *
 * @param   line   - 输出缓冲
 * @param   maxlen - 缓冲长度（含 '\0'）
 *
 * @return  1 = 取到一行；0 = 还没有完整行
 */
uint8_t UART_CmdGetLine( char *line, uint8_t maxlen );

/*********************************************************************
 * @fn      UART_SendStr
 *
 * @brief   发送一个以 '\0' 结尾的字符串（阻塞到写完）
 *
 * @param   s - 字符串（建议只用 ASCII，避免终端编码问题）
 *
 * @return  none
 */
void    UART_SendStr( const char *s );

/*********************************************************************
 * @fn      UART_SendDec
 *
 * @brief   发送一个有符号十进制整数
 *
 * @param   v - 待发送的值
 *
 * @return  none
 */
void    UART_SendDec( int32_t v );

#ifdef __cplusplus
}
#endif

#endif /* __UART_CMD_H */
