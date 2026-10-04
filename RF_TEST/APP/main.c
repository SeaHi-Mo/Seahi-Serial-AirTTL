/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Description        : CH570Q 射频测试固件（RF_TEST）
 *
 *   用途：把板子置于**定频发射**状态，配合频谱仪 / 综测仪测：
 *         载波频率（频偏）、发射功率、调制质量、谐波杂散等。
 *
 *   操作：PA0/PA1 接 USB-TTL（115200-8-N-1），用串口助手敲命令；
 *         命令见启动时打印的帮助。LED（PA7）常亮 = 正在发射。
 *
 *   ⚠️ 本固件是"测试件"，不进正常通信流程：
 *      上电后**不会**自动发射，必须先敲 't' 才会进定频发射。
 *******************************************************************************/

/******************************************************************************/
/* 头文件包含 */
#include "CH57x_common.h"
#include "CH57x_clk.h"
#include "CH57x_sys.h"
#include "rf_test.h"
#include "uart_cmd.h"

/*********************************************************************
 * @fn      rfTestPrintHelp
 *
 * @brief   打印命令帮助
 *
 * @return  none
 */
static void rfTestPrintHelp( void )
{
    UART_SendStr( "\r\n"
                  "commands (end with CR/LF):\r\n"
                  "  ?          show this help\r\n"
                  "  s          show status\r\n"
                  "  t          start fixed-frequency TX (single channel test)\r\n"
                  "  e          stop test mode\r\n"
                  "  c <0-39>   set channel: f = 2402 + 2*ch MHz\r\n"
                  "  p <-25-7>  set TX power in dBm (nearest step)\r\n" );
}

/*********************************************************************
 * @fn      rfTestPrintStatus
 *
 * @brief   打印当前状态
 *
 * @return  none
 */
static void rfTestPrintStatus( void )
{
    UART_SendStr( "status: TX=" );
    UART_SendStr( RFTestIsRunning( ) ? "ON " : "OFF" );
    UART_SendStr( " ch=" );
    UART_SendDec( (int32_t)RFTestChannel( ) );
    UART_SendStr( " freq=" );
    UART_SendDec( (int32_t)RFTestFreqMHz( ) );
    UART_SendStr( "MHz power=" );
    UART_SendDec( (int32_t)RFTestPowerDbm( ) );
    UART_SendStr( "dBm\r\n" );
}

/*********************************************************************
 * @fn      rfTestParseInt
 *
 * @brief   解析可选带负号的十进制整数（不做越界保护，命令已限长）
 *
 * @param   s - 字符串指针（会向后移动）
 * @param   v - 解析结果
 *
 * @return  1 = 解析到数字；0 = 不是数字
 */
static uint8_t rfTestParseInt( char **s, int32_t *v )
{
    char   *p = *s;
    int32_t sign = 1;
    int32_t val  = 0;

    if( *p == '-' )
    {
        sign = -1;
        p++;
    }

    if( ( *p < '0' ) || ( *p > '9' ) )
    {
        return 0;
    }

    while( ( *p >= '0' ) && ( *p <= '9' ) )
    {
        val = ( val * 10 ) + ( *p - '0' );
        p++;
    }

    *s = p;
    *v = val * sign;
    return 1;
}

/*********************************************************************
 * @fn      rfTestHandleLine
 *
 * @brief   处理一条命令行
 *
 * @param   line - 命令（不含换行）
 *
 * @return  none
 */
static void rfTestHandleLine( char *line )
{
    char    *p = line;
    char     cmd;
    int32_t  arg   = 0;
    uint8_t  hasArg;

    while( *p == ' ' )
    {
        p++;
    }
    if( *p == '\0' )
    {
        return;
    }

    cmd = *p++;

    while( *p == ' ' )
    {
        p++;
    }
    hasArg = rfTestParseInt( &p, &arg );

    switch( cmd )
    {
        case '?':
        case 'h':
            rfTestPrintHelp( );
            break;

        case 's':
            rfTestPrintStatus( );
            break;

        case 't':
            if( RFTestStart( ) == 0 )
            {
                UART_SendStr( "OK: fixed-frequency TX started\r\n" );
            }
            else
            {
                UART_SendStr( "ERR: start failed (RF phy busy)\r\n" );
            }
            rfTestPrintStatus( );
            break;

        case 'e':
            RFTestStop( );
            UART_SendStr( "OK: test mode stopped\r\n" );
            rfTestPrintStatus( );
            break;

        case 'c':
            if( !hasArg || ( arg < 0 ) || ( arg > (int32_t)RFTEST_CHANNEL_MAX ) )
            {
                UART_SendStr( "ERR: usage: c <0-39>\r\n" );
                break;
            }
            if( RFTestSetChannel( (uint8_t)arg ) == 0 )
            {
                UART_SendStr( "OK: " );
            }
            else
            {
                UART_SendStr( "ERR: set channel failed\r\n" );
            }
            rfTestPrintStatus( );
            break;

        case 'p':
            if( !hasArg )
            {
                UART_SendStr( "ERR: usage: p <-25-7>\r\n" );
                break;
            }
            RFTestSetPowerDbm( (int8_t)arg );
            UART_SendStr( "OK: " );
            rfTestPrintStatus( );
            break;

        default:
            UART_SendStr( "ERR: unknown command (press '?' for help)\r\n" );
            break;
    }
}

/*********************************************************************
 * @fn      main
 *
 * @brief   主函数
 *
 * @return  none
 */
int main( void )
{
    char line[RFTEST_LINE_MAX];

    /* 32MHz 晶振片内负载电容：本项目已标定为最小档（外部另配 4.7pF x 2，
     * 实测 31.999954MHz / -1.4ppm）。测试固件更要保证频偏准确，务必保持一致。 */
    HSECFG_Capacitance( HSECap_6p );
    SetSysClock( CLK_SOURCE_HSE_PLL_100MHz );

    UART_CmdInit( );

    UART_SendStr( "\r\n=== CH570Q RF TEST (" __DATE__ " " __TIME__ ") ===\r\n" );
    UART_SendStr( "UART: PA0=TXD PA1=RXD 115200-8-N-1, LED(PA7)=TX on\r\n" );

    RFTestInit( );

    rfTestPrintHelp( );
    rfTestPrintStatus( );

    for( ;; )
    {
        if( UART_CmdGetLine( line, RFTEST_LINE_MAX ) )
        {
            rfTestHandleLine( line );
        }
    }
}

/******************************** endfile @ main ******************************/
