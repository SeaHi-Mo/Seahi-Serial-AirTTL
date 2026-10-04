/********************************** (C) COPYRIGHT *******************************
 * File Name          : rf_test.c
 * Description        : 射频定频测试（单信道发射）实现
 *******************************************************************************/

#include "rf_test.h"
#include "uart_cmd.h"

/*********************************************************************
 * 功率档位表：索引 0..15 ↔ 库宏 ↔ dBm
 * 库头文件注释：RFIP_SetTxPower 支持 -20 ~ +4 dBm 动态调整；
 * 而 LL_TX_POWEER_* 档位表一路给到 +7 dBm（本项目正式固件就用 +7dBm）。
 * 这里按档位表全量给出，实测功率以综测仪读数为准。
 *********************************************************************/
typedef struct
{
    int8_t  dbm;
    uint8_t val;
} rftest_pwr_t;

static const rftest_pwr_t s_pwrTbl[RFTEST_POWER_LEVELS] =
{
    { -25, LL_TX_POWEER_MINUS_25_DBM },
    { -20, LL_TX_POWEER_MINUS_20_DBM },
    { -15, LL_TX_POWEER_MINUS_15_DBM },
    { -10, LL_TX_POWEER_MINUS_10_DBM },
    {  -8, LL_TX_POWEER_MINUS_8_DBM  },
    {  -5, LL_TX_POWEER_MINUS_5_DBM  },
    {  -3, LL_TX_POWEER_MINUS_3_DBM  },
    {  -1, LL_TX_POWEER_MINUS_1_DBM  },
    {   0, LL_TX_POWEER_0_DBM        },
    {   1, LL_TX_POWEER_1_DBM        },
    {   2, LL_TX_POWEER_2_DBM        },
    {   3, LL_TX_POWEER_3_DBM        },
    {   4, LL_TX_POWEER_4_DBM        },
    {   5, LL_TX_POWEER_5_DBM        },
    {   6, LL_TX_POWEER_6_DBM        },
    {   7, LL_TX_POWEER_7_DBM        },
};

static uint8_t s_ch      = RFTEST_DEF_CHANNEL;
static uint8_t s_pwrIdx  = RFTEST_DEF_POWER_IDX;
static uint8_t s_running = 0;

/*********************************************************************
 * 协议栈状态回调：定频模式不需要任何收发状态，占位即可
 *********************************************************************/
static void RFTestProcessCB( rfRole_States_t sta, uint8_t id )
{
    (void)sta;
    (void)id;
}

/*********************************************************************
 * 中断转发：协议栈只提供 LLE_LibIRQHandler / BB_LibIRQHandler 本体，
 * 向量表里的 LLE_IRQHandler / BB_IRQHandler 必须由应用实现
 *********************************************************************/
__INTERRUPT
__HIGH_CODE
void LLE_IRQHandler( void )
{
    LLE_LibIRQHandler( );
}

__INTERRUPT
__HIGH_CODE
void BB_IRQHandler( void )
{
    BB_LibIRQHandler( );
}

void RFTestInit( void )
{
    rfRoleConfig_t conf;

    conf.rfProcessCB = RFTestProcessCB;
    conf.processMask = 0;                   /* 定频模式不订阅状态回调 */

    RFRole_BasicInit( &conf );
}

uint8_t RFTestStart( void )
{
    bStatus_t s;

    RFIP_SetTxPower( s_pwrTbl[s_pwrIdx].val );

    s = RFIP_SingleChannel( s_ch );
    if( s != 0 )
    {
        /* phy busy：先把射频停下来，稍等再试一次 */
        RFRole_Stop( );
        mDelaymS( 2 );
        s = RFIP_SingleChannel( s_ch );
    }

    s_running = ( s == 0 ) ? 1 : 0;

    if( s_running )
    {
        GPIOA_SetBits( RFTEST_LED_PIN );        /* 发射中：LED 常亮 */
    }
    else
    {
        GPIOA_ResetBits( RFTEST_LED_PIN );
    }

    return (uint8_t)s;
}

void RFTestStop( void )
{
    RFIP_TestEnd( );
    s_running = 0;
    GPIOA_ResetBits( RFTEST_LED_PIN );
}

uint8_t RFTestIsRunning( void )
{
    return s_running;
}

uint8_t RFTestSetChannel( uint8_t ch )
{
    if( ch > RFTEST_CHANNEL_MAX )
    {
        return 1;
    }

    s_ch = ch;

    if( s_running )
    {
        return RFTestStart( );              /* 发射中：立即按新信道重配 */
    }

    return 0;
}

uint8_t RFTestSetPowerDbm( int8_t dbm )
{
    uint8_t i, best = 0;
    int16_t bestDiff = 1000;

    for( i = 0; i < RFTEST_POWER_LEVELS; i++ )
    {
        int16_t d = (int16_t)s_pwrTbl[i].dbm - (int16_t)dbm;

        if( d < 0 )
        {
            d = -d;
        }
        if( d < bestDiff )
        {
            bestDiff = d;
            best     = i;
        }
    }

    s_pwrIdx = best;

    if( s_running )
    {
        return RFTestStart( );              /* 发射中：立即按新功率重配 */
    }

    return 0;
}

uint8_t RFTestChannel( void )
{
    return s_ch;
}

int8_t RFTestPowerDbm( void )
{
    return s_pwrTbl[s_pwrIdx].dbm;
}

uint16_t RFTestFreqMHz( void )
{
    return (uint16_t)( 2402 + ( s_ch * 2 ) );
}

/******************************** endfile @ rf_test **************************/
