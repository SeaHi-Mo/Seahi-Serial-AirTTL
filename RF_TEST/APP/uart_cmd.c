/********************************** (C) COPYRIGHT *******************************
 * File Name          : uart_cmd.c
 * Description        : RF_TEST 的调试串口命令行接口实现
 *******************************************************************************/

#include "uart_cmd.h"

/*********************************************************************
 * 接收环形缓冲：中断只负责"塞字符"，主循环负责"攒行 + 解析"
 *********************************************************************/
#define RX_RING_SIZE        128

static volatile uint8_t s_ring[RX_RING_SIZE];
static volatile uint8_t s_head;             /* 中断写入位置 */
static volatile uint8_t s_tail;             /* 主循环读取位置 */

/*********************************************************************
 * @fn      UART_CmdInit
 *
 * @brief   初始化调试串口（PA0/PA1，115200-8-N-1）与 LED（PA7）
 *
 * @return  none
 */
void UART_CmdInit( void )
{
    s_head = 0;
    s_tail = 0;

    /* LED 初始熄灭（高电平点亮，这里先给低） */
    GPIOA_ResetBits( RFTEST_LED_PIN );
    GPIOA_ModeCfg( RFTEST_LED_PIN, GPIO_ModeOut_PP_5mA );

    /* 关掉两线调试功能：CH572 上电默认开启，会占住复用引脚 */
    R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;

    /* PA0 = TXD（推挽输出，先给高）、PA1 = RXD（上拉输入） */
    GPIOA_SetBits( RFTEST_TXD_PIN );
    GPIOA_ModeCfg( RFTEST_TXD_PIN, GPIO_ModeOut_PP_5mA );
    GPIOA_ModeCfg( RFTEST_RXD_PIN, GPIO_ModeIN_PU );
    UART_Remap( ENABLE, UART_TX_REMAP_PA0, UART_RX_REMAP_PA1 );

    /* UART_DefInit() 内部已按 115200 配好波特率、并打开 FIFO（触发点 4 字节） */
    UART_DefInit( );

    /* 1 字节触发接收中断，尽量不丢命令字符。
     * 注意：CH572 的 FCR 只有 FIFO 使能/触发点两个字段，**没有"清 RX FIFO"位**
     * （所以不能用 CH57x_uart.h 里的 UART_CLR_RXFIFO() 宏，它对本芯片不成立）。
     * 这里不需要清：FIFO 刚初始化，本来就是空的。 */
    UART_ByteTrigCfg( UART_1BYTE_TRIG );
    UART_INTCfg( ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT );
}

/*********************************************************************
 * @fn      UART_IRQHandler
 *
 * @brief   串口中断：把 FIFO 里的字符搬进环形缓冲
 *
 * @return  none
 */
__INTERRUPT
__HIGH_CODE
void UART_IRQHandler( void )
{
    uint8_t c, next;

    switch( UART_GetITFlag( ) )
    {
        case UART_II_LINE_STAT:
            (void)UART_GetLinSTA( );        /* 读一下清标志 */
            break;

        case UART_II_RECV_RDY:
        case UART_II_RECV_TOUT:
            while( R8_UART_RFC )
            {
                c    = UART_RecvByte( );
                next = (uint8_t)( ( s_head + 1 ) % RX_RING_SIZE );
                if( next != s_tail )        /* 缓冲满则丢弃新字符，绝不覆盖未读数据 */
                {
                    s_ring[s_head] = c;
                    s_head = next;
                }
            }
            break;

        default:
            break;
    }
}

/*********************************************************************
 * @fn      UART_CmdGetLine
 *
 * @brief   取出一条完整命令行（CR/LF 结尾；忽略空行；支持退格）
 *
 * @return  1 = 取到一行；0 = 还没有完整行
 */
uint8_t UART_CmdGetLine( char *line, uint8_t maxlen )
{
    static uint8_t pos = 0;
    uint8_t c;

    while( s_tail != s_head )
    {
        c      = s_ring[s_tail];
        s_tail = (uint8_t)( ( s_tail + 1 ) % RX_RING_SIZE );

        if( c == '\r' || c == '\n' )
        {
            if( pos == 0 )
            {
                continue;                   /* 空行直接跳过 */
            }
            line[pos] = '\0';
            pos       = 0;
            return 1;
        }

        if( c == 0x08 || c == 0x7F )        /* 退格 / DEL */
        {
            if( pos )
            {
                pos--;
            }
            continue;
        }

        if( pos < (uint8_t)( maxlen - 1 ) )
        {
            line[pos++] = (char)c;
        }
    }

    return 0;
}

/*********************************************************************
 * @fn      uart_putc
 *
 * @brief   阻塞发送一个字符（等发送 FIFO 有空位，带兜底计数防止死等）
 *
 * @return  none
 */
static void uart_putc( char c )
{
    uint16_t guard = 20000;

    while( ( R8_UART_TFC >= UART_FIFO_SIZE ) && guard-- )
    {
        ;
    }
    UART_SendByte( (uint8_t)c );
}

void UART_SendStr( const char *s )
{
    while( *s )
    {
        uart_putc( *s++ );
    }
}

void UART_SendDec( int32_t v )
{
    char     buf[12];
    uint8_t  i = 0;
    uint32_t u;
    uint8_t  neg = 0;

    if( v < 0 )
    {
        neg = 1;
        u   = (uint32_t)( -v );
    }
    else
    {
        u = (uint32_t)v;
    }

    do
    {
        buf[i++] = (char)( '0' + ( u % 10 ) );
        u /= 10;
    } while( u && ( i < sizeof( buf ) ) );

    if( neg )
    {
        uart_putc( '-' );
    }

    while( i )
    {
        uart_putc( buf[--i] );
    }
}

/******************************** endfile @ uart_cmd **************************/
