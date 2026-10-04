/********************************** (C) COPYRIGHT *******************************
 * File Name          : rf_test.h
 * Description        : CH570Q 射频测试固件 —— 定频（单信道）发射
 *
 *   基于 RF 库的测试接口：
 *       RFIP_SingleChannel( ch )   进入单载波/单信道测试模式，ch = 0..39
 *                                  f = 2402 + ch * 2 (MHz)  ← 头文件原始注释
 *       RFIP_TestEnd( )            退出测试模式（必须与上者成对使用）
 *       RFIP_SetTxPower( val )     设置发射功率档
 *
 *   ⚠️ 注意 RFIP_SingleChannel 的信道编号是 **BLE 信道号 0..39（2MHz 步进）**，
 *      与常规收发时 rfipTx_t.frequency 用的编号不是同一套，别混用。
 *******************************************************************************/

#ifndef __RF_TEST_H
#define __RF_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CH57x_common.h"
#include "CH572rf.h"

/* 默认测试参数 */
/* 默认频点：37 → 2476 MHz，即应用频段 {74,76,78} = 2474/2476/2478 MHz 的中间那个。
 * 可用编译期宏覆盖，例如 -DRFTEST_DEF_CHANNEL=36（2474 MHz）/ 38（2478 MHz）。
 * 应用信道 → 定频信道换算：2474→36、2476→37、2478→38（2402 + 2*ch MHz）。 */
#ifndef RFTEST_DEF_CHANNEL
#define RFTEST_DEF_CHANNEL      37
#endif
#define RFTEST_DEF_POWER_IDX    15          /* 索引 15 = +7 dBm（芯片最高档） */
#define RFTEST_POWER_LEVELS     16
#define RFTEST_CHANNEL_MAX      39

/* 应用频段（CH_HOP_TBL = {74,76,78} → 2400+ch MHz）对应的定频信道 */
#define RFTEST_APP_CH_LOW       36          /* 2474 MHz */
#define RFTEST_APP_CH_MID       37          /* 2476 MHz */
#define RFTEST_APP_CH_HIGH      38          /* 2478 MHz */

/*********************************************************************
 * @fn      RFTestInit
 *
 * @brief   射频基础初始化（RFRole_BasicInit + 中断挂接）。只调一次。
 *
 * @return  none
 */
void     RFTestInit( void );

/*********************************************************************
 * @fn      RFTestBlinkChannel
 *
 * @brief   上电用 LED 闪 N 次提示当前频点：应用频段 ch 36/37/38（2474/2476/2478 MHz）
 *          分别闪 1/2/3 次；其它信道闪 1 次。不接串口时靠它认固件频点。
 *
 * @return  none
 */
void     RFTestBlinkChannel( void );

/*********************************************************************
 * @fn      RFTestStart
 *
 * @brief   按当前信道/功率进入定频发射；若射频忙会先 RFRole_Stop() 再重试一次。
 *
 * @return  0 = 成功；非 0 = 失败（射频忙）
 */
uint8_t  RFTestStart( void );

/*********************************************************************
 * @fn      RFTestStop
 *
 * @brief   退出定频测试模式（RFIP_TestEnd），LED 熄灭
 *
 * @return  none
 */
void     RFTestStop( void );

/*********************************************************************
 * @fn      RFTestIsRunning
 *
 * @brief   当前是否处于定频发射状态
 *
 * @return  1 = 正在发射；0 = 未发射
 */
uint8_t  RFTestIsRunning( void );

/*********************************************************************
 * @fn      RFTestSetChannel
 *
 * @brief   设置信道；若正在发射则立即按新信道重配
 *
 * @param   ch - 信道号 0..39
 *
 * @return  0 = 成功；非 0 = 失败（参数越界或重配失败）
 */
uint8_t  RFTestSetChannel( uint8_t ch );

/*********************************************************************
 * @fn      RFTestSetPowerDbm
 *
 * @brief   按 dBm 设置发射功率（就近取档，库支持 -25 ~ +7 dBm 共 16 档）
 *
 * @param   dbm - 期望功率
 *
 * @return  0 = 成功
 */
uint8_t  RFTestSetPowerDbm( int8_t dbm );

/*********************************************************************
 * @fn      RFTestChannel
 *
 * @brief   当前信道号 0..39
 */
uint8_t  RFTestChannel( void );

/*********************************************************************
 * @fn      RFTestPowerDbm
 *
 * @brief   当前功率档对应的 dBm 值
 */
int8_t   RFTestPowerDbm( void );

/*********************************************************************
 * @fn      RFTestFreqMHz
 *
 * @brief   当前信道对应的频率（MHz）= 2402 + 2 * ch
 */
uint16_t RFTestFreqMHz( void );

#ifdef __cplusplus
}
#endif

#endif /* __RF_TEST_H */
