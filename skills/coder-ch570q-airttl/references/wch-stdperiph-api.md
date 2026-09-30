# CH57x（CH572 / CH570）沁恒标准外设库 API 参考

> 适用项目：**SeaHi-Serial-AirTTL**（基于沁恒 **CH570Q** 的 2.4G 无线串口调试器）
> 依据文件：`.readtmp/StdPeriphDriver/inc/` 下的 16 个 StdPeriphDriver 头文件（另用同目录 `CH572SFR.h` 与 `RVMSIS/core_riscv.h` 校验寄存器/位宏的真实存在性）
> 头文件版本：多数为 `V1.2 / 2021-11-17`；`CH57x_i2c.h` 为 `V1.0 / 2024-08-22`；`ISP572.h` 为 `V1.0 / 2024.12`
> 标记 **★** = 本项目（AirTTL 固件）实际使用的接口，正文给出更详细的说明与典型调用片段。

**阅读约定**

- 本文档只收录**头文件中真实存在**的函数原型与宏；未在头文件中出现的实现细节一律标注「以源码/头文件为准」。
- 表头中的「参数」列与原型保持一致；`FunctionalState` 取 `ENABLE` / `DISABLE`。
- 本文档中的代码片段是**依据本项目所用接口组合整理的典型调用序列**，用于说明调用顺序与参数取值，不是逐字拷贝的工程源码；移植时请以工程实际代码为准。

---

## 目录

| 章节 | 模块 | 头文件 |
| --- | --- | --- |
| [1](#1-时钟clk) | 时钟（系统主频 / HSE / RTC / 低频时钟） | `CH57x_clk.h`（+ `CH57x_sys.h`） |
| [2](#2-gpiogpio) | GPIO | `CH57x_gpio.h` |
| [3](#3-串口uart) | UART | `CH57x_uart.h`（+ SFR 直操） |
| [4](#4-flashflash) | Flash | `CH57x_flash.h` |
| [5](#5-系统与延时sys) | 系统与延时 | `CH57x_sys.h` |
| [6](#6-定时器timer) | 定时器 / 捕捉 / PWM(TMR) / 编码器 | `CH57x_timer.h` |
| [7](#7-pwmpwm) | PWM（PWM1~5 独立通道） | `CH57x_pwm.h` |
| [8](#8-spispi) | SPI | `CH57x_spi.h` |
| [9](#9-i2ci2c) | I2C | `CH57x_i2c.h` |
| [10](#10-电源管理pwr) | 电源管理 / 低功耗 | `CH57x_pwr.h` |
| [11](#11-usb-设备usbdev) | USB 设备 | `CH57x_usbdev.h` |
| [12](#12-usb-主机usbhost) | USB 主机 | `CH57x_usbhost.h` |
| [13](#13-比较器cmp) | 比较器 | `CH57x_cmp.h` |
| [14](#14-按键扫描keyscan) | 按键扫描 | `CH57x_keyscan.h` |
| [15](#15-ispflash-操作库isp572) | ISP（Flash 操作库） | `ISP572.h` |
| [16](#16-常用宏与枚举速查) | **常用宏与枚举速查** | 全部 |
| [附录 A](#附录-a头文件包含关系) | 头文件包含关系 | `CH57x_common.h` |
| [附录 B](#附录-b本版头文件的已知不一致处必读) | **本版头文件的已知不一致处（必读）** | — |
| [附录 C](#附录-c中断号与向量地址) | 中断号与向量地址 | `CH572SFR.h` |

---

## 1. 时钟（CLK）

`CH57x_clk.h` 提供 **HSE 晶振配置** 与 **RTC**；**系统主频** 的设置/读取函数声明在 `CH57x_sys.h`（本章一并收录，因为它们属于同一个时钟概念）。

### 1.1 系统主频（★ 本项目使用）

| 函数 | 原型 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| **★ `SetSysClock`** | `void SetSysClock(SYS_CLKTypeDef sc);` | `sc`：时钟源，见 [1.4 表](#14-时钟源-sys_clktypedef) | 无 | 切换系统主频（HSE 直出 / HSE+PLL / LSI） |
| **★ `GetSysClock`** | `uint32_t GetSysClock(void);` | — | 当前系统主频，单位 **Hz** | 运行时读取主频（算波特率分频、延时、超时常数时需要） |

**使用注意（★）**

- `SetSysClock()` **必须在** `HSECFG_Capacitance()` 之后调用，否则 PLL 可能起不来或频率不稳。
- 改主频会连带影响：UART 波特率分频（`UART_BaudRateCfg` 依赖 `FREQ_SYS`）、`mDelaymS/uS` 的校准值、TMR/PWM 周期。**主频切换后需要重新配置依赖时钟的外设**。
- 头文件里 `FREQ_SYS` 默认 `100000000`（`CH57x_common.h`），编译期常数；运行时真实主频用 `GetSysClock()` 取，两者不一致时以 `GetSysClock()` 为准。
- 从低功耗模式（Halt/Sleep/Shutdown）唤醒后**必须重新调用** `SetSysClock()`（见 [10](#10-电源管理pwr)）。

**本项目典型调用片段**

```c
/* 上电初始化顺序：释放调试引脚 → 配置 HSE 负载电容 → 切换主频 */
R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;   /* 关掉两线调试（占用 PA7/PA8 等复用脚），见 5.3 */
HSECFG_Capacitance(HSECap_18p);          /* 32MHz 晶振内部负载电容 18pF，按实际晶振调整 */
SetSysClock(CLK_SOURCE_HSE_PLL_24MHz);   /* 低速档：PLL 24MHz（低功耗 / 保守时序） */
/* 或高速档：SetSysClock(CLK_SOURCE_HSE_PLL_100MHz); */

FREQ_SYS = GetSysClock();   /* 若工程用变量而非宏保存主频，运行时取真实值 */
```

### 1.2 HSE 晶振配置（★ 本项目使用）

| 函数 | 原型 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `HSECFG_Current` | `void HSECFG_Current(HSECurrentTypeDef c);` | `c`：偏置电流挡位，见 [1.5](#15-hse-配置枚举) | 无 | 配置 32MHz 晶振偏置电流（75% / 100% / 125% / 150%） |
| **★ `HSECFG_Capacitance`** | `void HSECFG_Capacitance(HSECapTypeDef c);` | `c`：内部负载电容挡位，见 [1.5](#15-hse-配置枚举) | 无 | 配置 32MHz 晶振内部负载电容（6pF~20pF，步进 2pF） |

**使用注意（★）**

- **电容必须匹配实物晶振的 CL 值**。选大/选小会造成频偏、起振慢甚至不起振；`HSECap_18p` 是本项目常用的取值，换料时必须复核。
- 必须在 `SetSysClock()` **之前**调用。
- `HSECFG_Current()` 一般保持默认即可；仅在晶振起振困难（负载重、ESR 高）时加大。

### 1.3 RTC 与低频时钟

| 函数 | 原型 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `RTC_InitClock` | `uint32_t RTC_InitClock(RTC_OSCCntTypeDef cnt);` | `cnt`：振荡器捕获周期数，见 [1.5](#15-hse-配置枚举) | RTC 时钟频率（24~42KHz） | 初始化 RTC 时钟；**捕获周期数越高，初始化耗时越长、精度越高** |
| `RTC_InitTime` | `void RTC_InitTime(uint16_t y, uint16_t mon, uint16_t d, uint16_t h, uint16_t m, uint16_t s);` | 年（MAX_Y = `BEGYEAR`+44 = 2064）、月 1~12、日 1~31、时 0~23、分 0~59、秒 0~59 | 无 | 设置 RTC 当前时间 |
| `RTC_GetTime` | `void RTC_GetTime(uint16_t *py, uint16_t *pmon, uint16_t *pd, uint16_t *ph, uint16_t *pm, uint16_t *ps);` | 6 个输出指针 | 无 | 读取 RTC 当前时间 |
| `RTC_SetCycleLSI` | `void RTC_SetCycleLSI(uint32_t cyc);` | 周期计数初值，**MAX_CYC = 0xA8BFFFFF = 2831155199** | 无 | 基于 LSI 时钟设置 RTC 周期计数值 |
| `RTC_GetCycleLSI` | `uint32_t RTC_GetCycleLSI(void);` | — | 当前周期计数值 | 基于 LSI 时钟读取 RTC 周期计数值 |
| `RTC_TRIGFunCfg` | `void RTC_TRIGFunCfg(uint32_t cyc);` | `cyc`：触发计数值（头文件注释写 `@param t refer to RTC_TMRCycTypeDef`，但**实际形参为 `uint32_t cyc`**） | 无 | RTC 触发（闹钟）模式配置 |
| `RTC_TMRFunCfg` | `void RTC_TMRFunCfg(RTC_TMRCycTypeDef t);` | `t`：周期，见 [1.5](#15-hse-配置枚举) | 无 | RTC 周期定时模式配置（**基准固定 32768Hz**） |
| `RTC_ModeFunDisable` | `void RTC_ModeFunDisable(RTC_MODETypeDef m);` | `m`：要关闭的模式（`RTC_TRIG_MODE` / `RTC_TMR_MODE`） | 无 | 关闭 RTC 指定模式功能 |
| `RTC_GetITFlag` | `uint8_t RTC_GetITFlag(RTC_EVENTTypeDef f);` | `f`：`RTC_TRIG_EVENT` / `RTC_TMR_EVENT` | 中断标志状态 | 查询 RTC 中断标志 |
| `RTC_ClearITFlag` | `void RTC_ClearITFlag(RTC_EVENTTypeDef f);` | 同上 | 无 | 清除 RTC 中断标志 |
| `LClk_Cfg` | `void LClk_Cfg(FunctionalState s);` | `ENABLE`/`DISABLE` | 无 | 低频时钟（LSI）电源配置 |

**RTC 相关常量（`CH57x_clk.h`）**

| 宏 | 值 | 说明 |
| --- | --- | --- |
| `RTC_MAX_COUNT` | `0xA8C00000` | RTC 计数上限 |
| `MAX_DAY` | `0x00004000` | 一天对应的计数值 |
| `MAX_2_SEC` | `0x0000A8C0` | 2 秒对应的计数值 |
| `BEGYEAR` | `2020` | 年份基准（MAX_Y = 2064） |
| `IsLeapYear(yr)` | 宏 | 闰年判断 |
| `YearLength(yr)` | 宏 | 该年天数（366/365） |
| `monthLength(lpyr, mon)` | 宏 | 该月天数 |
| `Freq_LSI` | `extern uint32_t`（`CH57x_common.h`） | LSI 实测频率变量 |

### 1.4 时钟源 `SYS_CLKTypeDef`

| 枚举值 | 数值 | 说明 |
| --- | --- | --- |
| `CLK_SOURCE_LSI` | `0xC0` | LSI 低频时钟 |
| `CLK_SOURCE_HSE_16MHz` | `0x02` | HSE 直出 16MHz |
| `CLK_SOURCE_HSE_8MHz` | `0x04` | HSE 直出 8MHz |
| `CLK_SOURCE_HSE_6_4MHz` | `0x05` | HSE 直出 6.4MHz（复位默认） |
| `CLK_SOURCE_HSE_4MHz` | `0x08` | HSE 直出 4MHz |
| `CLK_SOURCE_HSE_2MHz` | `0x10` | HSE 直出 2MHz |
| `CLK_SOURCE_HSE_1MHz` | `0x0` | HSE 直出 1MHz |
| **★ `CLK_SOURCE_HSE_PLL_100MHz`** | `0x40｜6` | PLL 100MHz（本项目高速档） |
| `CLK_SOURCE_HSE_PLL_75MHz` | `0x40｜8` | PLL 75MHz |
| `CLK_SOURCE_HSE_PLL_60MHz` | `0x40｜10` | PLL 60MHz |
| `CLK_SOURCE_HSE_PLL_50MHz` | `0x40｜12` | PLL 50MHz |
| `CLK_SOURCE_HSE_PLL_40MHz` | `0x40｜15` | PLL 40MHz |
| `CLK_SOURCE_HSE_PLL_30MHz` | `0x40｜20` | PLL 30MHz |
| `CLK_SOURCE_HSE_PLL_25MHz` | `0x40｜24` | PLL 25MHz |
| **★ `CLK_SOURCE_HSE_PLL_24MHz`** | `0x40｜25` | PLL 24MHz（本项目低速档） |
| `CLK_SOURCE_HSE_PLL_20MHz` | `0x40｜30` | PLL 20MHz |

### 1.5 HSE 配置枚举

| 枚举 | 成员（按声明顺序） | 说明 |
| --- | --- | --- |
| `HSECurrentTypeDef` | `HSE_RCur_75`(0)、`HSE_RCur_100`、`HSE_RCur_125`、`HSE_RCur_150` | 偏置电流 75% / 100% / 125% / 150% |
| `HSECapTypeDef` | `HSECap_6p`(0)、`HSECap_8p`、`HSECap_10p`、`HSECap_12p`、`HSECap_14p`、`HSECap_16p`、**`HSECap_18p`**、`HSECap_20p` | 内部负载电容 6/8/10/12/14/16/18/20 pF |
| `RTC_OSCCntTypeDef` | `Count_1`(0)、`Count_2`、`Count_4`、`Count_32`、`Count_64`、`Count_128`、`Count_1024`、`Count_2047` | RTC 初始化捕获周期数 |
| `RTC_TMRCycTypeDef` | `Period_4096`(0)、`Period_8192`、`Period_16384`、`Period_32768`、`Period_65536`、`Period_131072`、`Period_262144`、`Period_524288` | RTC 周期定时档位（基准 32768Hz） |
| `RTC_EVENTTypeDef` | `RTC_TRIG_EVENT`(0)、`RTC_TMR_EVENT` | RTC 中断事件类型 |
| `RTC_MODETypeDef` | `RTC_TRIG_MODE`(0)、`RTC_TMR_MODE` | RTC 中断模式 |

---

## 2. GPIO（GPIO）

`CH57x_gpio.h` **只提供 GPIOA（PA0~PA15）** 的操作，没有 GPIOB 一类的接口。

### 2.1 函数与宏

| 接口 | 原型 / 定义 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| **★ `GPIOA_ModeCfg`** | `void GPIOA_ModeCfg(uint32_t pin, GPIOModeTypeDef mode);` | `pin`：`GPIO_Pin_0`~`GPIO_Pin_15`（可或）；`mode`：见 [2.2](#22-gpio-模式-gpiomodetypedef) | 无 | 配置 PA 引脚输入/输出模式 |
| **★ `GPIOA_SetBits`** | `#define GPIOA_SetBits(pin) (R32_PA_SET = pin)` | `pin` | 无 | 引脚输出置**高**（写 1 有效） |
| **★ `GPIOA_ResetBits`** | `#define GPIOA_ResetBits(pin) (R32_PA_CLR = pin)` | `pin` | 无 | 引脚输出置**低**（写 1 有效） |
| **★ `GPIOA_InverseBits`** | `#define GPIOA_InverseBits(pin) (R32_PA_OUT ^= pin)` | `pin` | 无 | 引脚输出电平**翻转** |
| `GPIOA_ReadPort` | `#define GPIOA_ReadPort() (R32_PA_PIN)` | — | PA 端口 32 位数据（**低 16 位有效**） | 读整个 PA 端口 |
| `GPIOA_ReadPortPin` | `#define GPIOA_ReadPortPin(pin) (R32_PA_PIN & (pin))` | `pin` | `0`=低电平，`!0`=高电平 | 读指定引脚电平 |
| `GPIOA_ITModeCfg` | `void GPIOA_ITModeCfg(uint32_t pin, GPIOITModeTpDef mode);` | `pin`；`mode`：低/高电平、下降/上升沿 | 无 | 配置 PA 引脚中断触发方式 |
| `GPIOA_ReadITFlagPort` | `#define GPIOA_ReadITFlagPort() (R16_PA_INT_IF)` | — | 端口中断标志 | 读 PA 端口中断标志 |
| `GPIOA_ReadITFlagBit` | `#define GPIOA_ReadITFlagBit(pin) (R16_PA_INT_IF & (pin))` | `pin` | 引脚中断标志 | 读指定引脚中断标志 |
| `GPIOA_ClearITFlagBit` | `#define GPIOA_ClearITFlagBit(pin) (R16_PA_INT_IF = pin)` | `pin` | 无 | 清除指定引脚中断标志 |
| `GPIOPinRemap` | `void GPIOPinRemap(FunctionalState s, uint16_t perph);` | `s`：使能/关闭；`perph`：映射关系，见 [2.3](#23-引脚重映射宏) | 无 | 外设功能引脚映射（TMR / I2C / SPI / UART） |
| `GPIOADigitalCfg` | `void GPIOADigitalCfg(FunctionalState s, uint16_t pin);` | `s`：开/关数字功能；`pin` | 无 | I/O 引脚数字功能控制 |

**使用注意（★）**

- `GPIOA_SetBits` / `GPIOA_ResetBits` 是宏，参数 `pin` **必须**是 `GPIO_Pin_x` 常量或变量；写的是 `R32_PA_SET`/`R32_PA_CLR`（write-1-action），不会读-改-写，**多引脚操作互不干扰**。
- `GPIOA_InverseBits` 走 `R32_PA_OUT ^= pin`，是**读-改-写**；若在多任务/中断里翻转，注意临界区。
- `GPIO_Pin_All` 是 `0xFFFFFFFF`，而 PA 只有 16 个引脚，实际生效的是低 16 位。
- 用作 GPIO 输入前要确认该引脚**没有**被外设复用（UART/TMR/I2C/SPI 的 remap）占用；本项目会先关两线调试（见 5.3）。
- GPIO 中断号 `GPIO_A_IRQn = 17`，需 `PFIC_EnableIRQ(GPIO_A_IRQn)`。

**本项目典型调用片段**

```c
/* 输出：LED / 使能脚 / 电源控制脚（推挽，最大 5mA） */
GPIOA_ModeCfg(GPIO_Pin_4, GPIO_ModeOut_PP_5mA);
GPIOA_ResetBits(GPIO_Pin_4);      /* 输出低 */
GPIOA_SetBits(GPIO_Pin_4);        /* 输出高 */
GPIOA_InverseBits(GPIO_Pin_4);    /* 翻转，做心跳闪烁 */

/* 输入：按键（上拉，按下读到 0） */
GPIOA_ModeCfg(GPIO_Pin_5, GPIO_ModeIN_PU);
if (GPIOA_ReadPortPin(GPIO_Pin_5) == 0) {
    /* 按键按下 */
}

/* 需要中断时：配触发方式 -> 清标志 -> 开 IRQ */
GPIOA_ITModeCfg(GPIO_Pin_5, GPIO_ITMode_FallEdge);
GPIOA_ClearITFlagBit(GPIO_Pin_5);
PFIC_EnableIRQ(GPIO_A_IRQn);
```

### 2.2 GPIO 模式 `GPIOModeTypeDef`

| 枚举值 | 数值 | 说明 |
| --- | --- | --- |
| `GPIO_ModeIN_Floating` | 0 | 浮空输入 |
| **★ `GPIO_ModeIN_PU`** | 1 | 上拉输入（按键/悬空检测常用） |
| `GPIO_ModeIN_PD` | 2 | 下拉输入 |
| **★ `GPIO_ModeOut_PP_5mA`** | 3 | 推挽输出，最大 5mA（默认首选，EMI 小） |
| `GPIO_ModeOut_PP_20mA` | 4 | 推挽输出，最大 20mA（驱动 LED / 需要大电流时） |

**中断触发方式 `GPIOITModeTpDef`**：`GPIO_ITMode_LowLevel`(0)、`GPIO_ITMode_HighLevel`(1)、`GPIO_ITMode_FallEdge`(2)、`GPIO_ITMode_RiseEdge`(3)。

### 2.3 引脚重映射宏

| 宏 | 值 | 说明 |
| --- | --- | --- |
| `REMAP_RXD_PA2` … `REMAP_RXD_PA11` | `0x00`~`0x07` | UART RXD 默认 / 重映射（PA2 默认） |
| `REMAP_TXD_PA3` … `REMAP_TXD_PA10` | `0x00`、`0x08`、`0x10`、`0x18`、`0x20`、`0x28`、`0x30`、`0x38` | UART TXD 默认 / 重映射（PA3 默认） |
| `REMAP_TMR_DEFAULT` | `0x00` | PWM0/PA7，CAP_IN1/PA7，CAP_IN2/PA2 |
| `REMAP_TMR_MODE1` | `0x40` | PWM0/PA2，CAP_IN1/PA2，CAP_IN2/PA7 |
| `REMAP_TMR_MODE2` | `0x80` | PWM0/PA4，CAP_IN1/PA4，CAP_IN2/PA9 |
| `REMAP_TMR_MODE3` | `0xC0` | PWM0/PA9，CAP_IN1/PA9，CAP_IN2/PA4 |
| `REMAP_I2C_DEFAULT` | `0x00` | SCL/PA8，SDA/PA9 |
| `REMAP_I2C_MODE1` | `0x200` | SCL/PA0，SDA/PA1 |
| `REMAP_I2C_MODE2` | `0x400` | SCL/PA3，SDA/PA2 |
| `REMAP_I2C_MODE3` | `0x600` | SCL/PA5，SDA/PA6 |

> UART 的引脚重映射建议直接用 `UART_Remap()`（见 [3.1](#31-函数与宏)），它封装了 `REMAP_TXD_*`/`REMAP_RXD_*` 的组合。

### 2.4 GPIO 引脚位宏 `GPIO_Pin_x`

`GPIO_Pin_0` = `0x00000001` … `GPIO_Pin_23` = `0x00800000`，`GPIO_Pin_All` = `0xFFFFFFFF`。位次即引脚号（`GPIO_Pin_n` = `1 << n`）。CH57x 上 PA 只到 PA15，`GPIO_Pin_16` 以上在本芯片无实际引脚。

---

## 3. 串口（UART）

`CH57x_uart.h` 提供库函数；本项目同时**直接操作串口寄存器**实现自定义帧格式（停止位/校验位/字长），所以本章把「库函数」与「寄存器直操」分开列。

### 3.1 函数与宏

| 接口 | 原型 / 定义 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| **★ `UART_DefInit`** | `void UART_DefInit(void);` | — | 无 | 串口**默认初始化**（8N1、8MHz 参考下的常规配置，具体见 `CH57x_uart.c`） |
| `UART_BaudRateCfg` | `void UART_BaudRateCfg(uint32_t baudrate);` | `baudrate`：波特率 | 无 | 配置波特率（内部按 `FREQ_SYS` 算分频，写 `R16_UART_DL`） |
| **★ `UART_ByteTrigCfg`** | `void UART_ByteTrigCfg(UARTByteTRIGTypeDef b);` | `b`：`UART_1BYTE_TRIG`(0) / `UART_2BYTE_TRIG` / **`UART_4BYTE_TRIG`** / `UART_7BYTE_TRIG` | 无 | 配置接收 FIFO **触发中断的字节数** |
| **★ `UART_INTCfg`** | `void UART_INTCfg(FunctionalState s, uint8_t i);` | `s`：使能/关闭；`i`：`RB_IER_LINE_STAT`(0x04) / `RB_IER_THR_EMPTY`(0x02) / `RB_IER_RECV_RDY`(0x01)（`RB_IER_MODEM_CHG` 仅 UART0，见附录 B） | 无 | 配置串口中断源使能 |
| `UART_CLR_RXFIFO` | `#define UART_CLR_RXFIFO() (R8_UART_FCR \|= RB_FCR_RX_FIFO_CLR)` | — | 无 | 清接收 FIFO（**该位宏本版未定义，见附录 B**） |
| `UART_CLR_TXFIFO` | `#define UART_CLR_TXFIFO() (R8_UART_FCR \|= RB_FCR_TX_FIFO_CLR)` | — | 无 | 清发送 FIFO（**同上**） |
| **★ `UART_GetITFlag`** | `#define UART_GetITFlag() (R8_UART_IIR & RB_IIR_INT_MASK)` | — | 中断标志（低 4 位有效） | 读**当前中断类型**，与 `UART_II_*` 比较做分派 |
| **★ `UART_GetLinSTA`** | `#define UART_GetLinSTA() (R8_UART_LSR)` | — | 线路状态字节 | 读通讯状态（帧错/校验错/溢出/收发 FIFO 空/有数据） |
| `UART_SendByte` | `#define UART_SendByte(b) (R8_UART_THR = b)` | `b`：待发字节 | 无 | 单字节发送（写 THR） |
| `UART_SendString` | `void UART_SendString(uint8_t *buf, uint16_t l);` | `buf`：数据首地址；`l`：长度 | 无 | 多字节发送 |
| **★ `UART_RecvByte`** | `#define UART_RecvByte() (R8_UART_RBR)` | — | 收到的字节 | 单字节接收（读 RBR） |
| `UART_RecvString` | `uint16_t UART_RecvString(uint8_t *buf);` | `buf`：接收缓存首地址 | 实际读取长度 | 多字节接收（一次读空接收 FIFO） |
| **★ `UART_Remap`** | `void UART_Remap(FunctionalState s, UARTTxPinRemapDef u_tx, UARTRxPinRemapDef u_rx);` | `s`：使能/关闭映射；`u_tx`：`UART_TX_REMAP_PA3`(0，默认)…`UART_TX_REMAP_PA10`；`u_rx`：`UART_RX_REMAP_PA2`(0，默认)…`UART_RX_REMAP_PA11` | 无 | 串口 TX/RX 引脚映射 |

**使用注意（★）**

- **调用顺序**：先关调试引脚 → `SetSysClock()` → `UART_Remap()` → `UART_DefInit()`（或 `UART_BaudRateCfg()`）→ 改 `R8_UART_LCR` 帧格式（如需）→ `UART_ByteTrigCfg()` → `UART_INTCfg()` → `PFIC_EnableIRQ(UART_IRQn)`。**先改帧格式/触发点再开中断**，避免配置过程中产生脏中断。
- `UART_BaudRateCfg()` 的分频依赖 `FREQ_SYS`：**主频变了就必须重新配波特率**。
- `UART_GetITFlag()` 返回的是 `IIR & 0x0F` 的**掩码值**（如 `0x04`），不是「第几位」；所以必须用 `==`/`switch` 与 `UART_II_*` 常量比较，**不要**当位掩码用。
- `UART_GetLinSTA()` 返回整字节 `R8_UART_LSR`；`STA_ERR_*` / `STA_TX*` / `STA_RECV_DATA` 都是它的**位掩码**，要用 `&` 判断。其中 `RB_LSR_BREAK_ERR`/`FRAME_ERR`/`PAR_ERR`/`OVER_ERR` 是 **RZ（读后自动清）** 位，读一次就清，别重复读丢失信息。
- 中断里**必须**清掉触发源：`UART_II_RECV_RDY`/`UART_II_RECV_TOUT` 靠读空 `R8_UART_RBR`（FIFO）自然清除，`UART_II_LINE_STAT` 靠读 `R8_UART_LSR` 清除，`UART_II_THR_EMPTY` 靠写 THR 或改中断使能清除。否则会**反复进中断**。
- 本项目为「调试器」场景，收发路径要尽量**少拷贝、不吃中断太长时间**；大块数据建议在中断里只入环形缓冲，主循环再处理。
- 串口中断号 `UART_IRQn = 27`。

### 3.2 寄存器直操（本项目用于自定义帧格式）

| 寄存器 | 地址 | 读写 | 作用 |
| --- | --- | --- | --- |
| `R8_UART_MCR` | `0x40003400` | RW | 调制解调器控制（`RB_MCR_OUT2`/`RB_MCR_INT_OE` = `0x08`） |
| `R8_UART_IER` | `0x40003401` | RW | 中断使能（`RB_IER_TXD_EN`=0x40、`RB_IER_LINE_STAT`=0x04、`RB_IER_THR_EMPTY`=0x02、`RB_IER_RECV_RDY`=0x01） |
| `R8_UART_FCR` | `0x40003402` | RW | FIFO 控制（`RB_FCR_FIFO_TRIG`=0xC0 触发等级 1/2/4/7 字节、`RB_FCR_FIFO_EN`=0x01） |
| **★ `R8_UART_LCR`** | `0x40003403` | RW | **线路控制**：字长 / 停止位 / 校验位（见 [3.3](#33-lcr-位定义)） |
| `R8_UART_IIR` | `0x40003404` | RO | 中断识别 |
| `R8_UART_LSR` | `0x40003405` | RO | 线路状态 |
| `R8_UART_RBR` | `0x40003408` | RO | 接收缓冲（读一个字节） |
| **★ `R8_UART_THR`** | `0x40003408` | WO | **发送保持寄存器**（写一个字节；与 RBR 同地址） |
| **★ `R8_UART_RFC`** | `0x4000340A` | RO | **接收 FIFO 当前字节数** |
| **★ `R8_UART_TFC`** | `0x4000340B` | RO | **发送 FIFO 当前字节数** |
| **★ `R16_UART_DL`** | `0x4000340C` | RW | **波特率分频锁存**（16 位；等价 `R8_UART_DLL`+`R8_UART_DLM`） |
| `R8_UART_DLL` / `R8_UART_DLM` | `0x4000340C` / `0x4000340D` | RW | 分频值低/高字节 |
| `R8_UART_DIV` | `0x4000340E` | RW | 预分频（低 7 位有效，1~128） |

相关常量：`UART_FIFO_SIZE` = 8（FIFO 深度）、`UART_RECV_RDY_SZ` = 7（接收 FIFO 最大触发等级）。

**使用注意（★）**

- 写 `R16_UART_DL` **必须先置 `RB_LCR_DLAB`(0x80)**（DLAB 与 `RB_LCR_GP_BIT` 同值），否则会写进 THR/RBR。改完帧格式后记得**清 `RB_LCR_DLAB`**。
- `R8_UART_RFC` / `R8_UART_TFC` 是**只读计数器**，用来判断「还有几个字节没收/没发」——用于自定义帧的定长收包、发完再切方向（半双工 485）等场景，比轮询 LSR 更精确。
- `R8_UART_THR` 是只写寄存器，**不能** `|=`/`&=`，只能整体赋值。
- 深度：FIFO 只有 8 字节（`UART_FIFO_SIZE`），`UART_7BYTE_TRIG` 已是最大触发等级；写 `R8_UART_THR` 前建议看 `R8_UART_TFC`，避免溢出丢数据。

**本项目典型调用片段**

```c
/* ---- 初始化 ---- */
UART_Remap(ENABLE, UART_TX_REMAP_PA2, UART_RX_REMAP_PA2);  /* 映射到实际硬件引脚 */
UART_DefInit();                    /* 默认 8N1 初始化 */
UART_BaudRateCfg(115200);          /* 主频变更后必须重配 */

/* 自定义帧格式：8 数据位 + 1 停止位 + 偶校验（直接改 LCR） */
R8_UART_LCR = RB_LCR_WORD_SZ                  /* 11 = 8bit */
            | 0                                 /* 无 RB_LCR_STOP_BIT = 1 位停止位 */
            | RB_LCR_PAR_EN                     /* 使能校验 */
            | (RB_LCR_PAR_MOD & 0x10);          /* 01 = 偶校验（PAR_MOD 位域取偶校验值） */
/* 波特率分频：DLAB 置位期间访问 DLL/DLM */
R8_UART_LCR |= RB_LCR_DLAB;
R16_UART_DL = (uint16_t)(10 * FREQ_SYS / 8 / 115200);  /* 分频算法以 CH57x_uart.c 为准 */
R8_UART_LCR &= ~RB_LCR_DLAB;

/* 中断配置：4 字节触发 + 接收/线路状态中断 */
UART_ByteTrigCfg(UART_4BYTE_TRIG);
UART_INTCfg(ENABLE, RB_IER_RECV_RDY);
UART_INTCfg(ENABLE, RB_IER_LINE_STAT);
PFIC_EnableIRQ(UART_IRQn);

/* ---- 发送（半双工场景：等发完再切换） ---- */
while (R8_UART_TFC) { /* 等待发送 FIFO 排空 */ }
R8_UART_THR = buf[i];                    /* 等价 UART_SendByte(buf[i]) */

/* ---- 中断服务（函数名须与工程启动文件/向量表绑定一致，以工程为准） ---- */
__INTERRUPT
void UART_IRQHandler(void)
{
    switch (UART_GetITFlag()) {
    case UART_II_RECV_RDY:                       /* 收到 >= 触发字节数 */
    case UART_II_RECV_TOUT:                      /* 超时（不足触发字节数也会给） */
        while (R8_UART_RFC) {                    /* 读空接收 FIFO */
            rx_push(UART_RecvByte());
        }
        break;
    case UART_II_LINE_STAT: {                    /* 帧错 / 校验错 / 溢出 —— 读 LSR 即清 */
        uint8_t sta = UART_GetLinSTA();
        if (sta & (STA_ERR_FRAME | STA_ERR_PAR | STA_ERR_FIFOOV)) {
            /* 记录/上报错误；注意这些是 RZ 位，读一次就没了 */
        }
        break;
    }
    default:
        break;
    }
}
```

### 3.3 LCR 位定义

| 位宏 | 值 | 说明 |
| --- | --- | --- |
| `RB_LCR_DLAB` / `RB_LCR_GP_BIT` | `0x80` | 分频锁存访问位（=通用位，两名字同值） |
| `RB_LCR_BREAK_EN` | `0x40` | Break 控制使能 |
| **★ `RB_LCR_PAR_MOD`** | `0x30` | 校验模式：`00`=奇、`01`=偶、`10`=mark、`11`=space |
| **★ `RB_LCR_PAR_EN`** | `0x08` | 校验使能 |
| **★ `RB_LCR_STOP_BIT`** | `0x04` | 停止位：`0`=1 位、`1`=2 位 |
| **★ `RB_LCR_WORD_SZ`** | `0x03` | 字长：`00`=5bit、`01`=6bit、`10`=7bit、`11`=8bit |

### 3.4 中断标志与线路状态

| 常量 | 值 | 说明 |
| --- | --- | --- |
| **★ `UART_II_LINE_STAT`** | `0x06` | 接收线路状态中断（帧/校验/溢出/Break） |
| **★ `UART_II_RECV_RDY`** | `0x04` | 接收数据可用（达到触发字节数） |
| **★ `UART_II_RECV_TOUT`** | `0x0C` | 接收 FIFO 超时（不足触发字节数但已空闲） |
| `UART_II_THR_EMPTY` | `0x02` | 发送保持寄存器空 |
| `UART_II_NO_INTER` | `0x01` | 无中断 |
| `RB_IIR_INT_MASK` | `0x0F` | IIR 中断标志掩码 |
| `RB_IIR_FIFO_ID` | `0xC0` | FIFO 使能标志 |
| `RB_IIR_NO_INT` | `0x01` | 无中断标志位 |
| `STA_ERR_BREAK` | `RB_LSR_BREAK_ERR`(0x10) | 数据间隔错误 |
| `STA_ERR_FRAME` | `RB_LSR_FRAME_ERR`(0x08) | 数据帧错误 |
| `STA_ERR_PAR` | `RB_LSR_PAR_ERR`(0x04) | 校验位出错 |
| `STA_ERR_FIFOOV` | `RB_LSR_OVER_ERR`(0x02) | 接收溢出 |
| `STA_TXFIFO_EMP` | `RB_LSR_TX_FIFO_EMP`(0x20) | 发送 FIFO 空，可继续填 |
| `STA_TXALL_EMP` | `RB_LSR_TX_ALL_EMP`(0x40) | 全部发送完成 |
| `STA_RECV_DATA` | `RB_LSR_DATA_RDY`(0x01) | 有接收到数据 |

### 3.5 引脚重映射枚举

| 枚举 | 成员（按顺序，值 0~7） | 说明 |
| --- | --- | --- |
| `UARTTxPinRemapDef` | `UART_TX_REMAP_PA3`(默认)、`_PA2`、`_PA1`、`_PA0`、`_PA7`、`_PA8`、`_PA11`、`_PA10` | 按枚举序号递增 |
| `UARTRxPinRemapDef` | `UART_RX_REMAP_PA2`(默认)、`_PA3`、`_PA0`、`_PA1`、`_PA6`、`_PA9`、`_PA10`、`_PA11` | 按枚举序号递增 |
| `UARTByteTRIGTypeDef` | `UART_1BYTE_TRIG`(0)、`UART_2BYTE_TRIG`、`UART_4BYTE_TRIG`、`UART_7BYTE_TRIG` | 接收 FIFO 触发字节数 |

> 注意 TX 与 RX 的枚举**不同值、也不同可用引脚集合**（例如 RX 有 `PA6`，TX 没有），且两者都能映射到同一个 PA2/PA3 —— 半双工单线场景才这样用。

---

## 4. Flash（FLASH）

`CH57x_flash.h` 只声明 **读 + 选项字节 + 唯一 ID**；**擦除/写入**在 `ISP572.h`（见 [15](#15-ispflash-操作库isp572)）。两章要合起来看。

| 接口 | 原型 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| **★ `FLASH_ROM_READ`** | `void FLASH_ROM_READ(uint32_t StartAddr, void *Buffer, uint32_t len);` | `StartAddr`：源地址；`Buffer`：目的缓冲；`len`：字节数 | 无（直接读，无返回码） | 从 Flash-ROM **读取**数据（最常用、最快的读法，直接当内存读） |
| `UserOptionByteConfig` | `uint8_t UserOptionByteConfig(FunctionalState RESET_EN, FunctionalState UART_NO_KEY_EN, uint32_t FLASHProt_Size);` | `RESET_EN`：外部手动复位输入；`UART_NO_KEY_EN`：串口免按键下载；`FLASHProt_Size`：代码区保护大小 | `0` 成功 / `!0` 失败 | 配置用户选项字节（**改完需复位/激活生效**） |
| `UserOptionByteClose_SWD` | `uint8_t UserOptionByteClose_SWD(void);` | — | `0` 成功 / `!0` 失败 | 关闭 SWD（两线调试）功能位 |
| `UserOptionByte_Active` | `void UserOptionByte_Active(void);` | — | 无 | 使选项字节配置**生效** |
| `GET_UNIQUE_ID` | `void GET_UNIQUE_ID(uint8_t *Buffer);` | `Buffer`：输出缓冲 | 无 | 读取芯片唯一 ID（做设备标识/密钥派生） |

**使用注意（★）**

- **读用 `FLASH_ROM_READ()`，写用 `FLASH_ROM_WRITE()`（ISP572.h）**：两者命名相近但来源不同，别混。
- 选项字节与 SWD 关闭是**不可逆风险操作**：`UserOptionByteClose_SWD()` 一旦生效，两线调试就没了，量产前务必确认；开发阶段不要调用。
- 写 Flash 的硬约束（详见 15）：**擦除粒度 4096 字节（`FLASH_BLOCK_SIZE`）、写入最小 4 字节（`FLASH_MIN_WR_SIZE`）、Buffer 必须在 RAM 且 4 字节对齐**。
- 代码区最大 240KB（`FLASH_ROM_MAX_SIZE` = `0x03C000`），引导区 `BOOT_LOAD_ADDR = 0x3C000`（大小 `0x2000`）。**不要把参数区放在用户代码正在使用的区段**。
- 擦/写期间 CPU 取指会受影响——本项目是无线串口调试器，**不要在收发中断服务里做 Flash 擦写**，会丢串口数据。

**本项目典型调用片段**

```c
/* 读参数（最常用） */
uint8_t cfg[32];
FLASH_ROM_READ(PARAM_ADDR, cfg, sizeof(cfg));

/* 读唯一 ID 做设备标识 */
uint8_t uid[8];
GET_UNIQUE_ID(uid);
```

---

## 5. 系统与延时（SYS）

### 5.1 延时（★ 本项目使用）

| 函数 | 原型 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| **★ `mDelaymS`** | `void mDelaymS(uint16_t t);` | `t`：毫秒（**uint16_t，最大 65535**） | 无 | 毫秒级**忙等**延时 |
| **★ `mDelayuS`** | `void mDelayuS(uint16_t t);` | `t`：微秒（**uint16_t**） | 无 | 微秒级**忙等**延时 |
| `DelayMs(x)` / `DelayUs(x)` | （`CH57x_common.h` 里的宏） | 同上 | 无 | `mDelaymS` / `mDelayuS` 的别名 |

**使用注意（★）**

- 都是**阻塞忙等**，不省电、不进低功耗；长延时（如秒级等待）不要用它们，改用 TMR + 中断或 `SYS_GetSysTickCnt()` 判时。
- 延时的实际长度由校准常量决定，**改主频后延时精度会变**（库按固定主频标定）。若发现明显偏差，说明当前 `SetSysClock()` 与库的标定主频不一致。
- 参数是 `uint16_t`：`mDelaymS(60000)` 合法，`mDelaymS(100000)` 会**溢出截断**。
- 微秒延时存在函数开销与循环粒度，`mDelayuS(1)` 之类极短延时不准；需要精确窄脉冲用 TMR/PWM。

### 5.2 系统信息、复位、看门狗、中断保护

| 接口 | 原型 / 定义 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `SYS_GetChipID` | `#define SYS_GetChipID() R8_CHIP_ID` | — | 芯片 ID（CH572=`0x72`，CH570=`0x70`） | 读芯片 ID |
| `SYS_GetAccessID` | `#define SYS_GetAccessID() R8_SAFE_ACCESS_ID` | — | 安全访问 ID | 读安全访问 ID（一般固定值） |
| `SYS_GetInfoSta` | `uint8_t SYS_GetInfoSta(SYS_InfoStaTypeDef i);` | `i`：`INFO_RESET_EN`(0x4)、`INFO_BOOT_EN`(0x8)、`INFO_RST_PIN`(0x10)、`INFO_LOADER`(0x20)、`STA_SAFEACC_ACT`(0x30) | 是否开启 | 读系统信息状态（复位脚、BootLoader、是否在引导区/安全访问态） |
| `SYS_GetLastResetSta` | `#define SYS_GetLastResetSta() (R8_RESET_STATUS & RB_RESET_FLAG)` | — | 复位状态 | 读上次复位来源，与 `SYS_ResetStaTypeDef` 比较 |
| `SYS_ResetExecute` | `void SYS_ResetExecute(void);` | — | 无 | 执行系统软件复位 |
| `SYS_ResetKeepBuf` | `#define SYS_ResetKeepBuf(d) (R8_GLOB_RESET_KEEP = d)` | `d`：要保存的值 | 无 | 写复位保持寄存器（**不受手动/软件/看门狗/普通唤醒复位影响**） |
| `SYS_DisableAllIrq` | `void SYS_DisableAllIrq(uint32_t *pirqv);` | `pirqv`：**保存**当前中断状态的变量地址 | 无 | 关闭所有中断并把原状态存入 `*pirqv` |
| `SYS_RecoverIrq` | `void SYS_RecoverIrq(uint32_t irq_status);` | `irq_status`：之前保存的值 | 无 | 恢复中断状态 |
| `SYS_GetSysTickCnt` | `uint32_t SYS_GetSysTickCnt(void);` | — | 当前 SYSTICK 计数值 | 读系统 tick（做非阻塞计时/超时） |
| `WWDG_SetCounter` | `#define WWDG_SetCounter(c) (R8_WDOG_COUNT = c)` | `c`：计数初值（**递增型**） | 无 | 加载看门狗计数值（喂狗/设初值） |
| `WWDG_ITCfg` | `void WWDG_ITCfg(FunctionalState s);` | `ENABLE`/`DISABLE` | 无 | 看门狗溢出**中断**使能 |
| `WWDG_ResetCfg` | `void WWDG_ResetCfg(FunctionalState s);` | `ENABLE`/`DISABLE` | 无 | 看门狗溢出**复位**使能 |
| `WWDG_GetFlowFlag` | `#define WWDG_GetFlowFlag() (R8_RST_WDOG_CTRL & RB_WDOG_INT_FLAG)` | — | 溢出标志 | 查看看门狗溢出标志 |
| `WWDG_ClearFlag` | `void WWDG_ClearFlag(void);` | — | 无 | 清看门狗中断标志（重新加载计数值也可清） |
| `sys_safe_access_enable` | `#define sys_safe_access_enable() do{...}while(0)` | — | 无 | 进入安全访问模式（**成对使用**，见下） |
| `sys_safe_access_disable` | `#define sys_safe_access_disable() ...while(0)` | — | 无 | 退出安全访问模式 |

**使用注意**

- `SYS_DisableAllIrq(&v)` / `SYS_RecoverIrq(v)` 是**成对**的临界区保护写法，比裸 `__risc_v_disable_irq()` 更贴近库风格。
- `sys_safe_access_enable()` / `sys_safe_access_disable()` **必须成对**，且是**同一对宏**（`disable` 依赖 `enable` 里声明的局部变量 `mpie_mie`，单独用 `disable` 编译不过）。头文件明确说明：进入后约 **16 个系统时钟周期**内可改写一个或多个**安全寄存器**（SFR 注释里标 `SAM` 的那些），超期自动失效；**再次 `enable` 之前必须先 `disable`**。
- 看门狗是**递增型**：给 `WWDG_SetCounter(c)` 一个初值，溢出后按 `WWDG_ResetCfg(ENABLE)` 决定是否复位、按 `WWDG_ITCfg(ENABLE)` 决定是否中断。喂狗就是重新 `WWDG_SetCounter()`。
- `SYS_ResetKeepBuf()` 写的是**复位保持寄存器**，适合记录「本次是异常复位」之类跨复位信息；读回需要直接读 `R8_GLOB_RESET_KEEP`（头文件未提供读宏）。
- 复位来源 `SYS_ResetStaTypeDef`：`RST_STATUS_SW`(0) 软件复位、`RST_STATUS_RPOR`(1) 上电复位、`RST_STATUS_WTR`(2) 看门狗超时、`RST_STATUS_MR`(3) 外部手动复位、`RST_STATUS_LRM0`(4) 唤醒复位-软复位引起、`RST_STATUS_GPWSM`(5) 下电模式唤醒复位、`RST_STATUS_LRM1`(6) 唤醒复位-看门狗引起、`RST_STATUS_LRM2`(7) 唤醒复位-手动复位引起。

### 5.3 关闭两线调试复用引脚（★ 本项目使用）

| 寄存器 / 位 | 地址 / 值 | 说明 |
| --- | --- | --- |
| **★ `R16_PIN_ALTERNATE`** | `0x40001018`（RW） | 功能引脚复用配置**低字**。低字里同时包含 `RB_UART_TXD`(0x0038)、`RB_UART_RXD`(0x0007)、`RB_PIN_DEBUG_EN`(0x4000)、`RB_PIN_USB_EN`(0x2000)、`RB_UDP_PU_EN`(0x1000)、`RB_PA_DI_DIS`(0x0FFF) |
| **★ `RB_PIN_DEBUG_EN`** | `0x4000` | 调试接口使能位；**清 0 = 关闭两线调试**，把被调试占用的复用引脚释放给应用 |
| `R16_PIN_ALTERNATE_H` | `0x4000101A`（RW） | 复用配置**高字**：`RB_25M_EN`(0x1000)、`RB_SPI_CLK`(0x0800)、`RB_I2C_PIN`(0x0600)、`RB_SPI_CS`(0x0100)、`RB_TMR_PIN`(0x00C0)、`RB_UART_TXD`/`RB_UART_RXD` 相关 |
| `RB_PA_DI_DIS` | `0x0FFF` | 1 = 关闭对应 PA 引脚的数字输入 |

**使用注意（★）**

- 关闭两线调试后**不能再调试/下载**（需要重新上电进入下载模式或改选项字节），所以本项目把它放在**初始化早期按需调用**；开发阶段建议用宏开关包起来。
- 该寄存器在 SFR 头文件里**没有标 `SAM`**，通常直接读改写即可：`R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;`。若写入不生效，用 `sys_safe_access_enable()` / `sys_safe_access_disable()` 包裹（以头文件/驱动实现为准）。
- 改它之前先确认**没有别的模块**同时在写 `R16_PIN_ALTERNATE`（`GPIOPinRemap()`、`UART_Remap()` 也写这里），避免互相覆盖。

### 5.4 其它系统枚举

| 枚举 | 成员 |
| --- | --- |
| `SYS_ResetStaTypeDef` | 见 5.2 说明（7 项，0~7） |
| `SYS_InfoStaTypeDef` | `INFO_RESET_EN`(0x4)、`INFO_BOOT_EN`(0x8)、`INFO_RST_PIN`(0x10，1=PA7 / 0=PA8)、`INFO_LOADER`(0x20)、`STA_SAFEACC_ACT`(0x30) |

**本项目典型调用片段**

```c
/* 上电早期：关两线调试 + 切主频 */
R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;
SetSysClock(CLK_SOURCE_HSE_PLL_24MHz);
mDelaymS(10);                 /* 上电稳定等待 */

/* 临界区保护 */
uint32_t irq;
SYS_DisableAllIrq(&irq);
/* ... 不可被打断的操作 ... */
SYS_RecoverIrq(irq);

/* 记录「本次是看门狗复位」并跨复位保留 */
if (SYS_GetLastResetSta() == RST_STATUS_WTR) {
    SYS_ResetKeepBuf(1);
}
```

---

## 6. 定时器（TIMER）

`CH57x_timer.h` 一个 TMR 单元复用为：定时 / 外部边沿计数 / 捕捉 / PWM0 / DMA / 编码器（ENC）。

### 6.1 定时与计数

| 接口 | 原型 / 定义 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `TMR_TimerInit` | `void TMR_TimerInit(uint32_t t);` | `t`：定时时间（**基于当前系统时钟 Tsys**，最长周期 67108864） | 无 | 定时功能初始化 |
| `TMR_EXTSingleCounterInit` | `void TMR_EXTSingleCounterInit(CapModeTypeDef cap);` | `cap`：采集计数类型 | 无 | 外部边沿计数功能初始化 |
| `TMR_CountOverflowCfg` | `#define TMR_CountOverflowCfg(cyc) (R32_TMR_CNT_END = (cyc + 2))` | `cyc`：溢出大小，最大 67108863 | 无 | 设计数溢出阈值（**内部会 +2**） |
| `TMR_GetCurrentCount` | `#define TMR_GetCurrentCount() R32_TMR_COUNT` | — | 当前计数值（最大 67108863） | 读当前计数 |
| `TMR_Enable` / `TMR_Disable` | `#define TMR_Enable() (R8_TMR_CTRL_MOD \|= RB_TMR_COUNT_EN)` / `... &= ~RB_TMR_COUNT_EN` | — | 无 | 开启 / 关闭 TMR 计数 |

### 6.2 捕捉

| 接口 | 原型 / 定义 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `TMR_CapInit` | `void TMR_CapInit(CapModeTypeDef cap);` | `cap`：`CAP_NULL` / `Edge_To_Edge` / `FallEdge_To_FallEdge` / `RiseEdge_To_RiseEdge` | 无 | 外部信号捕捉初始化 |
| `TMR_CAPTimeoutCfg` | `#define TMR_CAPTimeoutCfg(cyc) (R32_TMR_CNT_END = cyc)` | `cyc`：捕捉电平超时，最大 33554432 | 无 | 捕捉超时配置 |
| `TMR_CAPGetData` | `#define TMR_CAPGetData() R32_TMR_FIFO` | — | 脉冲数据 | 读捕捉到的数据 |
| `TMR_CAPDataCounter` | `#define TMR_CAPDataCounter() R8_TMR_FIFO_COUNT` | — | 已捕获数据个数 | 读捕捉 FIFO 里的数据个数 |

### 6.3 中断（★ 本项目使用）

| 接口 | 定义 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| **★ `TMR_ITCfg`** | `#define TMR_ITCfg(s, f) ((s) ? (R8_TMR_INTER_EN \|= f) : (R8_TMR_INTER_EN &= ~f))` | `s`：使能/关闭；`f`：`TMR_IT_*` 位或组合 | 无 | 配置 TMR 中断使能 |
| **★ `TMR_GetITFlag`** | `#define TMR_GetITFlag(f) (R8_TMR_INT_FLAG & f)` | `f`：`TMR_IT_*` 位 | `0` 未置位 / `!0` 触发 | 查询中断标志 |
| **★ `TMR_ClearITFlag`** | `#define TMR_ClearITFlag(f) (R8_TMR_INT_FLAG = f)` | `f`：要清的标志位 | 无 | 清除中断标志 |

**中断标志位（`TMR_IT_*`）**

| 宏 | 值 | 说明 |
| --- | --- | --- |
| **★ `TMR_IT_CYC_END`** | `0x01` | 周期结束：捕捉-超时，定时-周期结束，PWM-周期结束 |
| `TMR_IT_DATA_ACT` | `0x02` | 数据有效：捕捉-新数据，PWM-有效电平结束 |
| `TMR_IT_FIFO_HF` | `0x04` | FIFO 使用过半：捕捉-`FIFO>=4`，PWM-`FIFO<4` |
| `TMR_IT_DMA_END` | `0x08` | DMA 结束（**仅 TMR~TMR3 支持**） |
| `TMR_IT_FIFO_OV` | `0x10` | FIFO 溢出：捕捉-FIFO 满，PWM-FIFO 空 |

**使用注意（★）**

- `TMR_ClearITFlag(f)` 是**整字节赋值**（写 1 清对应位），不要写成 `|=`（会连带清掉别的标志）。
- `TMR_GetITFlag(f)` 返回的是**掩码值**（如 `0x01`），判断要用 `if (TMR_GetITFlag(TMR_IT_CYC_END))`，不要写成 `== 1`。
- 时基依赖**当前系统主频**：`TMR_TimerInit(t)` 的 `t` 是以 Tsys 为单位的计数，主频变了定时值就变，需重算。
- 定时器中断号 `TMR_IRQn = 24`；PWM1~5 是另一个中断 `PWMX_IRQn = 31`；编码器是 `ENCODE_IRQn = 34`。
- 头文件里的 `DataBit_25` = `1 << 25`，配合捕捉/计数做长周期换算用。

**本项目典型调用片段**

```c
/* 定时中断：TMR_TimerInit 之后开周期结束中断 */
TMR_TimerInit(FREQ_SYS / 1000);          /* 约 1ms 周期（按当前主频换算） */
TMR_ITCfg(ENABLE, TMR_IT_CYC_END);
TMR_ClearITFlag(TMR_IT_CYC_END);
PFIC_EnableIRQ(TMR_IRQn);
TMR_Enable();

/* 中断服务（函数名以工程启动文件/向量表为准） */
__INTERRUPT
void TMR_IRQHandler(void)
{
    if (TMR_GetITFlag(TMR_IT_CYC_END)) {
        TMR_ClearITFlag(TMR_IT_CYC_END);
        /* 周期性任务：软件计时、超时判链、超时上报等 */
    }
}
```

### 6.4 TMR 的 PWM0 模式

| 接口 | 原型 / 定义 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `TMR_PWMInit` | `void TMR_PWMInit(PWMX_PolarTypeDef pr, PWM_RepeatTsTypeDef ts);` | `pr`：`High_Level`(0，默认低电平、高有效) / `Low_Level`(1)；`ts`：`PWM_Times_1/4/8/16` 有效输出重复次数 | 无 | PWM 输出初始化 |
| `TMR_PWMCycleCfg` | `#define TMR_PWMCycleCfg(cyc) (R32_TMR_CNT_END = cyc)` | 波形周期，最大 67108863 | 无 | PWM0 周期配置 |
| `TMR_PWMActDataWidth` | `#define TMR_PWMActDataWidth(d) (R32_TMR_FIFO = d)` | 有效数据脉宽，最大 67108864 | 无 | PWM0 有效脉宽 |
| `TMR_PWMEnable` / `TMR_PWMDisable` | `#define TMR_PWMEnable() (R8_TMR_CTRL_MOD \|= RB_TMR_OUT_EN)` / `... &= ~RB_TMR_OUT_EN` | — | 无 | 开启 / 关闭 PWM 输出 |

> `PWMX_PolarTypeDef` 与 `PWM_RepeatTsTypeDef` 分别声明在 `CH57x_pwm.h` 与 `CH57x_timer.h`（`CH57x_common.h` 两个都包含，直接可用）。
> PWM0 引脚默认为 PA7，可用 `GPIOPinRemap()` + `REMAP_TMR_MODE1~3` 改到 PA2/PA4/PA9。

### 6.5 DMA 与编码器（ENC）

| 接口 | 原型 / 定义 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `TMR_DMACfg` | `void TMR_DMACfg(uint8_t s, uint32_t startAddr, uint32_t endAddr, DMAModeTypeDef m);` | `s`：开关；`startAddr`/`endAddr`：DMA 起止地址；`m`：`Mode_Single` / `Mode_LOOP` | 无 | 配置 TMR 的 DMA |
| `ENC_Config` | `void ENC_Config(uint8_t s, uint32_t encReg, ENCModeTypeDef m);` | `s`：开关；`encReg`：编码器模式终值（**最大 0xFFFF**）；`m`：`Mode_IDLE`/`Mode_T2`/`Mode_T1`/`Mode_T1T2` | 无 | 配置编码器功能 |
| `ENC_GetCurrentDir` | `#define ENC_GetCurrentDir (R8_ENC_REG_CTRL>>5 & 0x01)` | — | `0` 前进 / `1` 后退 | 读编码器方向（**注意是宏，无括号**） |
| `ENC_GetCurrentCount` | `#define ENC_GetCurrentCount R32_ENC_REG_CCNT` | — | 当前计数值 | 读编码器计数 |
| `ENC_GetCountandReset` | `#define ENC_GetCountandReset() R8_ENC_REG_CTRL \|= RB_RD_CLR_EN` | — | 无 | 读计数的同时清 0 |
| `ENC_ITCfg` | `#define ENC_ITCfg(s, f) ((s) ? (R8_ENC_INTER_EN \|= f) : (R8_ENC_INTER_EN &= ~f))` | `f`：`RB_IE_DIR_INC`(0x01) / `RB_IE_DIR_DEC`(0x02) | 无 | 编码器中断配置 |
| `ENC_ClearITFlag` | `#define ENC_ClearITFlag(f) (R8_ENC_INT_FLAG = f)` | 同上 | 无 | 清编码器中断标志 |
| `ENC_GetITFlag` | `#define ENC_GetITFlag(f) (R8_ENC_INT_FLAG & f)` | 同上 | 标志状态 | 查询编码器中断标志 |

---

## 7. PWM（PWM）

`CH57x_pwm.h` 是 **PWM1~5 独立通道**（与 [6.4](#64-tmr-的-pwm0-模式) 的 TMR-PWM0 是不同单元）。

### 7.1 时基与周期

| 接口 | 原型 / 定义 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `PWMX_CLKCfg` | `#define PWMX_CLKCfg(d) (R16_PWM_CLOCK_DIV = d)` | `d`：通道基准时钟 = `d × Tsys` | 无 | PWM 通道基准时钟分频 |
| `PWMX_CycleCfg` | `void PWMX_CycleCfg(PWMX_CycleTypeDef cyc);` | `cyc`：8 位周期档位，见 [7.3](#73-枚举) | 无 | 8 位周期配置 |
| `PWMX_16bit_CycleCfg` | `void PWMX_16bit_CycleCfg(uint8_t ch, uint16_t cyc);` | `ch`：`CH_PWM1`~`CH_PWM5`（可或）；`cyc`：16 位周期 | 无 | 16 位周期配置 |
| `PWM_16bit_CycleEnable` / `PWM_16bit_CycleDisable` | `#define PWM_16bit_CycleEnable() (R8_PWM_CONFIG \|= (3 << 1))` / `... &= ~(3 << 1)` | — | 无 | 16 位数据位宽使能 / 失能 |

### 7.2 输出与脉宽

| 接口 | 原型 / 定义 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `PWMX_ACTOUT` | `void PWMX_ACTOUT(uint8_t ch, uint8_t da, PWMX_PolarTypeDef pr, FunctionalState s);` | `ch`：通道；`da`：8 位有效脉宽；`pr`：极性；`s`：`ENABLE`/`DISABLE` | 无 | 8 位输出波形配置（一次配通道+脉宽+极性+开关） |
| `PWMX_16bit_ACTOUT` | `void PWMX_16bit_ACTOUT(uint8_t ch, uint16_t da, PWMX_PolarTypeDef pr, FunctionalState s);` | 同上，`da` 为 16 位 | 无 | 16 位输出波形配置 |
| `PWM1_ActDataWidth`、`PWM2_ActDataWidth`、`PWM3_ActDataWidth`、`PWM4_ActDataWidth`、`PWM5_ActDataWidth` | `#define PWM1_ActDataWidth(d) (R8_PWM1_DATA = d)` … `#define PWM5_ActDataWidth(d) (R8_PWM5_DATA = d)` | `d`：8 位有效脉宽 | 无 | 单独改某通道 8 位脉宽（**运行中调速用这个**） |
| `PWM1_16bit_ActDataWidth`、`PWM2_16bit_ActDataWidth`、`PWM3_16bit_ActDataWidth`、`PWM4_16bit_ActDataWidth`、`PWM5_16bit_ActDataWidth` | `#define PWM1_16bit_ActDataWidth(d) (R16_PWM1_DATA = d)` … `#define PWM5_16bit_ActDataWidth(d) (R16_PWM5_DATA = d)` | `d`：16 位脉宽 | 无 | 单独改某通道 16 位脉宽 |
| `PWMX_AlterOutCfg` | `void PWMX_AlterOutCfg(uint8_t ch, FunctionalState s);` | `ch`：`RB_PWM4_5_STAG_EN`（PWM4/PWM5 交替输出）；`s`：开关 | 无 | PWM 交替输出模式配置 |
| `PWMX_SyncOutCfg` | `void PWMX_SyncOutCfg(FunctionalState s);` | `s`：开关 | 无 | PWM 同步输出模式配置 |
| `PWM_DMACfg` | `void PWM_DMACfg(uint8_t s, uint32_t startAddr, uint32_t endAddr, PWM_DMAModeTypeDef m, PWM_DMAChannel ch);` | `s`：开关；起止地址；`m`：`PWM_ModeSINGLE`/`PWM_ModeLOOP`；`ch`：`Mode_DMACH1_3`/`Mode_DMACH4_5`/`Mode_DMACH1_5` | 无 | 配置 PWM 的 DMA |

### 7.3 枚举

| 枚举 | 成员（按顺序） | 说明 |
| --- | --- | --- |
| `PWMX_PolarTypeDef` | `High_Level`(0)、`Low_Level`(1) | 0=默认低电平、高电平有效；1=默认高电平、低电平有效 |
| `PWMX_CycleTypeDef` | `PWMX_Cycle_256`(0)、`_255`、`_128`、`_127`、`_64`、`_63` | PWM4_11 周期档位（PWMX 周期个数） |
| `PWM_DMAModeTypeDef` | `PWM_ModeSINGLE`(0)、`PWM_ModeLOOP`(1) | DMA 单次 / 循环 |
| `PWM_DMAChannel` | `Mode_DMACH1_3`(0)、`Mode_DMACH4_5`(1)、`Mode_DMACH1_5`(2) | DMA 通道组选择 |

**通道宏**：`CH_PWM1`=0x01、`CH_PWM2`=0x02、`CH_PWM3`=0x04、`CH_PWM4`=0x08、`CH_PWM5`=0x10、`CH_PWM_ALL`=0x1F。

**使用注意**

- 占空比 = `da / 周期`；改占空比只写 `PWMn_ActDataWidth()` 即可，**不用**重跑 `PWMX_ACTOUT()`。
- `PWMX_ACTOUT()` 的 `s` 传 `DISABLE` 就是**关掉该通道输出**（不是恢复默认），这是本模块的开关方式。
- 基准时钟 `PWMX_CLKCfg(d)` 决定分辨率：`d` 越大频率越低、可调级数越多（8 位模式 0~255）。
- PWM1~5 共用中断号 `PWMX_IRQn = 31`。

---

## 8. SPI（SPI）

| 接口 | 原型 / 定义 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `SPI_MasterDefInit` | `void SPI_MasterDefInit(void);` | — | 无 | 主机默认初始化：**模式0 + 3 线全双工 + 8MHz** |
| `SPI_2WIRE_MasterOutputInit` | `void SPI_2WIRE_MasterOutputInit(void);` | — | 无 | 主机 2 线发送模式：**模式1 + 2 线半双工 + 8MHz** |
| `SPI_2WIRE_MasterReceiveInit` | `void SPI_2WIRE_MasterReceiveInit(void);` | — | 无 | 主机 2 线接收模式：模式1 + 2 线半双工 + 8MHz |
| `SPI_2WIRE_SlaveInputInit` | `void SPI_2WIRE_SlaveInputInit(void);` | — | 无 | 从机 2 线接收模式初始化 |
| `SPI_2WIRE_SlaveOutputInit` | `void SPI_2WIRE_SlaveOutputInit(void);` | — | 无 | 从机 2 线发送模式初始化 |
| `SPI_CLKCfg` | `void SPI_CLKCfg(uint8_t c);` | `c`：分频系数，基准时钟 = `c × Tsys` | 无 | SPI 时钟配置 |
| `SPI_DataMode` | `void SPI_DataMode(ModeBitOrderTypeDef m);` | `m`：模式0/3 × 低位/高位在前 | 无 | 设置数据流模式 |
| `SPI_MasterSendByte` | `void SPI_MasterSendByte(uint8_t d);` | `d`：发送字节 | 无 | 主机发单字节（走 buffer） |
| `SPI_MasterRecvByte` | `uint8_t SPI_MasterRecvByte(void);` | — | 收到的字节 | 主机收单字节 |
| `SPI_MasterTrans` | `void SPI_MasterTrans(uint8_t *pbuf, uint16_t len);` | `pbuf`：数据首地址；`len`：**最大 4095** | 无 | 主机用 FIFO 连续发多字节 |
| `SPI_MasterRecv` | `void SPI_MasterRecv(uint8_t *pbuf, uint16_t len);` | 同上 | 无 | 主机用 FIFO 连续收多字节 |
| `SPI_MasterDMATrans` | `void SPI_MasterDMATrans(uint8_t *pbuf, uint16_t len);` | `pbuf`：**需 4 字节对齐** | 无 | 主机 DMA 连续发送 |
| `SPI_MasterDMARecv` | `void SPI_MasterDMARecv(uint8_t *pbuf, uint16_t len);` | 同上 | 无 | 主机 DMA 连续接收 |
| `SetFirstData` | `#define SetFirstData(d) (R8_SPI_SLAVE_PRE = d)` | `d`：首字节内容 | 无 | 从机首字节命令模式下加载首字节 |
| `SPI_SlaveInit` | `void SPI_SlaveInit(void);` | — | 无 | 从机模式初始化 |
| `SPI_2WIRE_SlaveInit` | `void SPI_2WIRE_SlaveInit(void);` | — | 无 | 从机 2 线模式初始化 |
| `SPI_SlaveSendByte` | `void SPI_SlaveSendByte(uint8_t d);` | `d`：数据 | 无 | 从机发单字节 |
| `SPI_SlaveRecvByte` | `uint8_t SPI_SlaveRecvByte(void);` | — | 收到数据 | 从机收单字节 |
| `SPI_SlaveTrans` | `void SPI_SlaveTrans(uint8_t *pbuf, uint16_t len);` | `len` 最大 4095 | 无 | 从机多发（FIFO） |
| `SPI_SlaveRecv` | `void SPI_SlaveRecv(uint8_t *pbuf, uint16_t len);` | 同上 | 无 | 从机多收（FIFO） |
| `SPI_SlaveDMATrans` / `SPI_SlaveDMARecv` | `void SPI_SlaveDMATrans/DMARecv(uint8_t *pbuf, uint16_t len);` | **4 字节对齐** | 无 | 从机 DMA 收发 |
| `SPI_ITCfg` | `#define SPI_ITCfg(s, f) ((s) ? (R8_SPI_INTER_EN \|= f) : (R8_SPI_INTER_EN &= ~f))` | `f`：`SPI_IT_*` | 无 | SPI 中断使能配置 |
| `SPI_GetITFlag` | `#define SPI_GetITFlag(f) (R8_SPI_INT_FLAG & f)` | `f`：`SPI_IT_*` | `0` 未置位 / `!0` 触发 | 查中断标志 |
| `SPI_ClearITFlag` | `#define SPI_ClearITFlag(f) (R8_SPI_INT_FLAG = f)` | `f` | 无 | 清中断标志 |
| `SPI_Disable` | `#define SPI_Disable() (R8_SPI_CTRL_MOD &= ~(RB_SPI_MOSI_OE \| RB_SPI_SCK_OE \| RB_SPI_MISO_OE))` | — | 无 | 关闭 SPI（清三个输出使能位） |

**中断位宏 `SPI_IT_*`**

| 宏 | 来源位宏 | 说明 |
| --- | --- | --- |
| `SPI_IT_FST_BYTE` | `RB_SPI_IE_FST_BYTE` | 从机首字节命令模式下收到首字节 |
| `SPI_IT_FIFO_OV` | `RB_SPI_IE_FIFO_OV` | FIFO 溢出 |
| `SPI_IT_DMA_END` | `RB_SPI_IE_DMA_END` | DMA 传输结束 |
| `SPI_IT_FIFO_HF` | `RB_SPI_IE_FIFO_HF` | FIFO 使用过半 |
| `SPI_IT_BYTE_END` | `RB_SPI_IE_BYTE_END` | 单字节传输完成 |
| `SPI_IT_CNT_END` | `RB_SPI_IE_CNT_END` | 全部字节传输完成 |

**枚举**

| 枚举 | 成员 | 说明 |
| --- | --- | --- |
| `ModeBitOrderTypeDef` | `Mode0_LowBitINFront`(0)、`Mode0_HighBitINFront`(1)、`Mode3_LowBitINFront`(2)、`Mode3_HighBitINFront`(3) | 模式 0/3 × 位序 |
| `Slave_ModeTypeDef` | `Mode_DataStream`(0)、`Mose_FirstCmd`(1) | 从机数据流 / 首字节命令模式（成员名即头文件原样，注意是 `Mose_` 不是 `Mode_`） |

**使用注意**

- `_Trans`/`_Recv` 的 `len` **最大 4095**（FIFO 方式）；DMA 方式的缓冲**必须 4 字节对齐**。
- 默认初始化都是 **8MHz**（`SPI_MasterDefInit`）；要别的速率用 `SPI_CLKCfg(c)` 按 `c × Tsys` 算，**主频变化后要重算**。
- 模式 1 只在 2 线半双工初始化里出现（`SPI_2WIRE_*`），模式 0/3 通过 `SPI_DataMode()` 选。
- SPI 中断号 `SPI_IRQn = 19`。
- SPI 引脚复用可以用 `GPIOPinRemap()` + `R16_PIN_ALTERNATE_H` 的 `RB_SPI_CLK`(0x0800)/`RB_SPI_CS`(0x0100)。

---

## 9. I2C（I2C）

`CH57x_i2c.h` 是 **V1.0 / 2024-08-22** 的新版实现（事件驱动的经典 STM32 风格状态机）。

### 9.1 初始化与控制

| 函数 | 原型 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `I2C_Init` | `void I2C_Init(I2C_ModeTypeDef I2C_Mode, uint32_t I2C_ClockSpeed, I2C_DutyTypeDef I2C_DutyCycle, I2C_AckTypeDef I2C_Ack, I2C_AckAddrTypeDef I2C_AckAddr, uint16_t I2C_OwnAddress1);` | 模式 / 时钟速率 / 快速模式占空比 / ACK 使能 / 地址位宽 / 本机地址 1 | 无 | I2C 初始化 |
| `I2C_Cmd` | `void I2C_Cmd(FunctionalState NewState);` | `ENABLE`/`DISABLE` | 无 | I2C 外设开关 |
| `I2C_GenerateSTART` | `void I2C_GenerateSTART(FunctionalState NewState);` | 同上 | 无 | 产生 START |
| `I2C_GenerateSTOP` | `void I2C_GenerateSTOP(FunctionalState NewState);` | 同上 | 无 | 产生 STOP |
| `I2C_AcknowledgeConfig` | `void I2C_AcknowledgeConfig(FunctionalState NewState);` | 同上 | 无 | ACK 使能/关闭 |
| `I2C_OwnAddress2Config` | `void I2C_OwnAddress2Config(uint8_t Address);` | 从机地址 2 | 无 | 配置第二本机地址 |
| `I2C_DualAddressCmd` | `void I2C_DualAddressCmd(FunctionalState NewState);` | 同上 | 无 | 双地址模式开关 |
| `I2C_GeneralCallCmd` | `void I2C_GeneralCallCmd(FunctionalState NewState);` | 同上 | 无 | 广播呼叫（General Call）开关 |
| `I2C_ITConfig` | `void I2C_ITConfig(I2C_ITTypeDef I2C_IT, FunctionalState NewState);` | `I2C_IT_BUF`(0x0400)/`I2C_IT_EVT`(0x0200)/`I2C_IT_ERR`(0x0100) | 无 | 中断使能配置 |
| `I2C_SendData` | `void I2C_SendData(uint8_t Data);` | 待发字节 | 无 | 发送一个字节 |
| `I2C_ReceiveData` | `uint8_t I2C_ReceiveData(void);` | — | 收到的字节 | 接收一个字节 |
| `I2C_Send7bitAddress` | `void I2C_Send7bitAddress(uint8_t Address, uint8_t I2C_Direction);` | `I2C_Direction_Transmitter`(0)/`I2C_Direction_Receiver`(1) | 无 | 发送 7 位从机地址 + 方向位 |
| `I2C_SoftwareResetCmd` | `void I2C_SoftwareResetCmd(FunctionalState NewState);` | 同上 | 无 | 软件复位 I2C |
| `I2C_NACKPositionConfig` | `void I2C_NACKPositionConfig(uint16_t I2C_NACKPosition);` | `I2C_NACKPosition_Next` / `_Current` | 无 | NACK 位置配置 |
| `I2C_SMBusAlertConfig` | `void I2C_SMBusAlertConfig(uint16_t I2C_SMBusAlert);` | `I2C_SMBusAlert_Low` / `_High` | 无 | SMBus Alert 引脚电平 |
| `I2C_TransmitPEC` | `void I2C_TransmitPEC(FunctionalState NewState);` | 同上 | 无 | PEC 发送开关 |
| `I2C_PECPositionConfig` | `void I2C_PECPositionConfig(uint16_t I2C_PECPosition);` | `I2C_PECPosition_Next` / `_Current` | 无 | PEC 位置配置 |
| `I2C_CalculatePEC` | `void I2C_CalculatePEC(FunctionalState NewState);` | 同上 | 无 | PEC 计算开关 |
| `I2C_GetPEC` | `uint8_t I2C_GetPEC(void);` | — | PEC 值 | 读 PEC |
| `I2C_ARPCmd` | `void I2C_ARPCmd(FunctionalState NewState);` | 同上 | 无 | ARP（SMBus 地址解析协议）开关 |
| `I2C_StretchClockCmd` | `void I2C_StretchClockCmd(FunctionalState NewState);` | 同上 | 无 | 时钟延展开关 |
| `I2C_FastModeDutyCycleConfig` | `void I2C_FastModeDutyCycleConfig(uint16_t I2C_DutyCycle);` | 占空比 | 无 | 快速模式占空比配置 |

### 9.2 状态监控

| 函数 | 原型 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `I2C_CheckEvent` | `uint8_t I2C_CheckEvent(uint32_t I2C_EVENT);` | `I2C_EVENT_*` | 事件是否满足 | 检查事件（推荐的主循环等待方式） |
| `I2C_GetLastEvent` | `uint32_t I2C_GetLastEvent(void);` | — | 最近事件 | 读最近一次事件 |
| `I2C_GetFlagStatus` | `FlagStatus I2C_GetFlagStatus(uint32_t I2C_FLAG);` | `I2C_FLAG_*` | `SET`/`RESET` | 读标志 |
| `I2C_ClearFlag` | `void I2C_ClearFlag(uint32_t I2C_FLAG);` | 同上 | 无 | 清标志 |
| `I2C_GetITStatus` | `ITStatus I2C_GetITStatus(uint32_t I2C_IT);` | `I2C_IT_*` | `SET`/`RESET` | 读中断状态 |
| `I2C_ClearITPendingBit` | `void I2C_ClearITPendingBit(uint32_t I2C_IT);` | 同上 | 无 | 清中断挂起位 |

### 9.3 枚举与常量

| 名称 | 成员 / 值 | 说明 |
| --- | --- | --- |
| `I2C_ModeTypeDef` | `I2C_Mode_I2C`(0x0000)、`I2C_Mode_SMBusDevice`(0x0002)、`I2C_Mode_SMBusHost`(0x000A) | I2C / SMBus 从机 / SMBus 主机 |
| `I2C_DutyTypeDef` | `I2C_DutyCycle_16_9`(`RB_I2C_DUTY`)、`I2C_DutyCycle_2`(0x0000) | 快速模式 Tlow/Thigh |
| `I2C_AckTypeDef` | `I2C_Ack_Enable`(`RB_I2C_ACK`)、`I2C_Ack_Disable`(0x0000) | ACK 使能 |
| `I2C_AckAddrTypeDef` | `I2C_AckAddr_7bit`(0x4000)、`I2C_AckAddr_10bit`(0xC000) | 地址位宽 |
| `I2C_ITTypeDef` | `I2C_IT_BUF`(0x0400)、`I2C_IT_EVT`(0x0200)、`I2C_IT_ERR`(0x0100) | 中断类型（供 `I2C_ITConfig`） |
| 方向宏 | `I2C_Direction_Transmitter`(0x00)、`I2C_Direction_Receiver`(0x01) | 收发方向 |
| 主机事件 | `I2C_EVENT_MASTER_MODE_SELECT`、`..._TRANSMITTER_MODE_SELECTED`、`..._RECEIVER_MODE_SELECTED`、`..._MODE_ADDRESS10`、`..._BYTE_RECEIVED`、`..._BYTE_TRANSMITTING`、`..._BYTE_TRANSMITTED` | `I2C_CheckEvent` 的实参 |
| 从机事件 | `I2C_EVENT_SLAVE_RECEIVER_ADDRESS_MATCHED`、`..._TRANSMITTER_ADDRESS_MATCHED`、`..._RECEIVER_SECONDADDRESS_MATCHED`、`..._TRANSMITTER_SECONDADDRESS_MATCHED`、`..._GENERALCALLADDRESS_MATCHED`、`..._BYTE_RECEIVED`、`..._STOP_DETECTED`、`..._BYTE_TRANSMITTED`、`..._BYTE_TRANSMITTING`、`..._ACK_FAILURE` | 同上 |
| 标志 / 中断 | `I2C_FLAG_*`、`I2C_IT_*`（各 14 项） | 见下方位值表 |

**标志与中断位值**（两者共用同一组状态位，仅基数不同：FLAG 用 `0x1000xxxx`，IT 的错误类用 `0x0100xxxx`、事件类用 `0x0600xxxx` / `0x0200xxxx`）：

| 类别 | 名称 | `I2C_FLAG_*` | `I2C_IT_*` |
| --- | --- | --- | --- |
| 错误 | `SMBALERT` | `0x10008000` | `0x01008000` |
| 错误 | `TIMEOUT` | `0x10004000` | `0x01004000` |
| 错误 | `PECERR` | `0x10001000` | `0x01001000` |
| 错误 | `OVR` | `0x10000800` | `0x01000800` |
| 错误 | `AF` | `0x10000400` | `0x01000400` |
| 错误 | `ARLO` | `0x10000200` | `0x01000200` |
| 错误 | `BERR` | `0x10000100` | `0x01000100` |
| 事件 | `TXE` | `0x10000080` | `0x06000080` |
| 事件 | `RXNE` | `0x10000040` | `0x06000040` |
| 事件 | `STOPF` | `0x10000010` | `0x02000010` |
| 事件 | `ADD10` | `0x10000008` | `0x02000008` |
| 事件 | `BTF` | `0x10000004` | `0x02000004` |
| 事件 | `ADDR` | `0x10000002` | `0x02000002` |
| 事件 | `SB` | `0x10000001` | `0x02000001` |

**主机事件值**（`I2C_CheckEvent()` 的实参）：

| 事件 | 值 | 含义 |
| --- | --- | --- |
| `I2C_EVENT_MASTER_MODE_SELECT` | `0x00030001` | BUSY + MSL + SB |
| `I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED` | `0x00070082` | BUSY + MSL + ADDR + TXE + TRA |
| `I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED` | `0x00030002` | BUSY + MSL + ADDR |
| `I2C_EVENT_MASTER_MODE_ADDRESS10` | `0x00030008` | BUSY + MSL + ADD10 |
| `I2C_EVENT_MASTER_BYTE_RECEIVED` | `0x00030040` | BUSY + MSL + RXNE |
| `I2C_EVENT_MASTER_BYTE_TRANSMITTING` | `0x00070080` | TRA + BUSY + MSL + TXE |
| `I2C_EVENT_MASTER_BYTE_TRANSMITTED` | `0x00070084` | TRA + BUSY + MSL + TXE + BTF |

**从机事件值**：

| 事件 | 值 |
| --- | --- |
| `I2C_EVENT_SLAVE_RECEIVER_ADDRESS_MATCHED` | `0x00020002` |
| `I2C_EVENT_SLAVE_TRANSMITTER_ADDRESS_MATCHED` | `0x00060082` |
| `I2C_EVENT_SLAVE_RECEIVER_SECONDADDRESS_MATCHED` | `0x00820000` |
| `I2C_EVENT_SLAVE_TRANSMITTER_SECONDADDRESS_MATCHED` | `0x00860080` |
| `I2C_EVENT_SLAVE_GENERALCALLADDRESS_MATCHED` | `0x00120000` |
| `I2C_EVENT_SLAVE_BYTE_RECEIVED` | `0x00020040` |
| `I2C_EVENT_SLAVE_STOP_DETECTED` | `0x00000010` |
| `I2C_EVENT_SLAVE_BYTE_TRANSMITTED` | `0x00060084` |
| `I2C_EVENT_SLAVE_BYTE_TRANSMITTING` | `0x00060080` |
| `I2C_EVENT_SLAVE_ACK_FAILURE` | `0x00000400` |

**使用注意**

- 引脚映射用 `GPIOPinRemap()` + `REMAP_I2C_DEFAULT/MODE1/MODE2/MODE3`（默认 SCL/PA8、SDA/PA9）。
- 标准流程：`I2C_Init()` → `I2C_Cmd(ENABLE)` → `I2C_GenerateSTART(ENABLE)` → 轮询 `I2C_CheckEvent(I2C_EVENT_MASTER_MODE_SELECT)` → `I2C_Send7bitAddress(addr, I2C_Direction_Transmitter)` → `I2C_CheckEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)` → `I2C_SendData()` … → `I2C_GenerateSTOP(ENABLE)`。**每一步都要等对应事件**，否则会丢数据。
- 阻塞式轮询没有超时保护，从机不在线时会**死等**；调试器固件里建议加计数超时并复位 I2C（`I2C_SoftwareResetCmd()`）。
- I2C 中断号 `I2C_IRQn = 30`。
- 本版 `CH57x_i2c.h` 版本号与其它头文件不同（V1.0 / 2024-08-22），移植时注意与工程里 `CH57x_i2c.c` 配套。

---

## 10. 电源管理（PWR）

| 接口 | 原型 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `PWR_PeriphClkCfg` | `void PWR_PeriphClkCfg(FunctionalState s, uint16_t perph);` | `s`：开/关外设时钟；`perph`：外设时钟控制位 | 无 | 外设时钟门控（省电） |
| `PWR_PeriphWakeUpCfg` | `void PWR_PeriphWakeUpCfg(FunctionalState s, uint8_t perph, WakeUP_ModeypeDef mode);` | `s`：开关；`perph`：`RB_SLP_USB_WAKE`(0x01)/`RB_SLP_RTC_WAKE`(0x08)/`RB_SLP_GPIO_WAKE`(0x10)/`RB_SLP_BAT_WAKE`(0x20)；`mode`：唤醒延时档位 | 无 | 睡眠唤醒源配置 |
| `PowerMonitor` | `void PowerMonitor(FunctionalState s, VolM_LevelypeDef vl);` | `s`：开关；`vl`：`LPLevel_1V8`(0)/`2V0`/`2V2`/`2V4` | 无 | 电源（低压）监控 |
| `LowPower_Idle` | `void LowPower_Idle(void);` | — | 无 | Idle 低功耗（唤醒后**时钟不变**） |
| `LowPower_Halt` | `void LowPower_Halt(void);` | — | 无 | Halt 低功耗，**切到 HSI/5 时钟运行** |
| `LowPower_Sleep` | `void LowPower_Sleep(uint16_t rm);` | `rm`：供电模块，见下 | 无 | Sleep 低功耗 |
| `LowPower_Shutdown` | `void LowPower_Shutdown(uint16_t rm);` | `rm`：供电模块 | 无 | Shutdown 低功耗（最深） |

**唤醒延时 `WakeUP_ModeypeDef`**：`Fsys_Delay_3584`(0)、`Fsys_Delay_512`、`Fsys_Delay_64`、`Fsys_Delay_1`、`Fsys_Delay_8191`、`Fsys_Delay_7168`、`Fsys_Delay_6144`、`Fsys_Delay_4096`。

**使用注意**

- **`LowPower_Halt` / `Sleep` / `Shutdown` 都会把时钟切到 HSI/5**，唤醒后**必须重新 `SetSysClock()`**，并重新配依赖主频的外设（UART 波特率、TMR 时基）。
- `LowPower_Sleep` / `LowPower_Shutdown` 会**强制关闭 DCDC**，唤醒后可手动再开。
- `PWR_PeriphClkCfg()` 的 `perph` 取值头文件注释写「refer to Peripher CLK control bit define」，**本版 `CH57x_pwr.h` 未列出具体常量**；SFR 头文件里对应的是 `R8_SLP_CLK_OFF0/1/2` 的位（如 `RB_SLP_CLK_UART`=0x10、`RB_SLP_CLK_TMR`=0x01、`RB_SLP_CLK_CMP`=0x02、`RB_SLP_CLK_SPI`=0x01、`RB_SLP_CLK_I2C`=0x08、`RB_SLP_CLK_PWMX`=0x04、`RB_SLP_CLK_USB`=0x10、`RB_SLP_CLK_BLE`=0x80、`RB_CLK_OFF_AESCCM`=0x02、`RB_CLK_OFF_HCLK`=0x10、`RB_CLK_OFF_DEBUG`=0x02、`RB_CLK_OFF_XROM`=0x01）。**注意这些位分布在不同字节里、含义是「1=关闭时钟」，与函数形参的拼接方式需以 `CH57x_pwr.c` 实现为准。**
- `rm` 参数头文件注释写 `RB_PWR_RAM2K` / `RB_PWR_RAM16K` / `RB_PWR_EXTEND` / `RB_PWR_XROM`，但**本版 SFR 头文件里实际只有 `RB_PWR_RAM12K`(0x02)、`RB_PWR_CORE`(0x04)、`RB_PWR_EXTEND`(0x08)、`RB_PWR_XROM`(0x01)、`RB_PWR_SYS_EN`(0x80)、`RB_PWR_LDO5V_EN`(0x0100)**。见 [附录 B](#附录-b本版头文件的已知不一致处必读)。
- 本项目是**USB/串口调试器**：低功耗前必须把 USB 断开、串口静默，否则唤醒源与唤醒延时（`Fsys_Delay_*`）不匹配会丢首字节。
- 相关 SFR：`ROM_CFG_ADR_HW` = `0x7F00C`（LDO/OSC 等硬件配置地址）、`R32_POWER_MANAG`(0x40001020)、`R32_SLEEP_CONTROL`(0x4000100C)、`R8_SLP_POWER_CTRL`(0x4000100F)、`R32_BATTERY_CTRL`(0x40001024)。

---

## 11. USB 设备（USBDEV）

### 11.1 函数

| 函数 | 原型 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `USB_DeviceInit` | `void USB_DeviceInit(void);` | — | 无 | USB 设备功能初始化（4 端点 / 8 通道） |
| `USB_DevTransProcess` | `void USB_DevTransProcess(void);` | — | 无 | USB 设备应答传输处理（**在 USB 中断里调用**） |
| `DevEP1_OUT_Deal` / `DevEP2_OUT_Deal` / `DevEP3_OUT_Deal` / `DevEP4_OUT_Deal` | `void DevEP1_OUT_Deal(uint8_t l);` … `void DevEP4_OUT_Deal(uint8_t l);`（4 个原型，仅编号不同） | `l`：待处理数据长度（**<64B**） | 无 | 端点 n 下传（OUT）数据处理回调 |
| `DevEP1_IN_Deal` / `DevEP2_IN_Deal` / `DevEP3_IN_Deal` / `DevEP4_IN_Deal` | `void DevEP1_IN_Deal(uint8_t l);` … `void DevEP4_IN_Deal(uint8_t l);`（4 个原型，仅编号不同） | `l`：上传数据长度（**<64B**） | 无 | 端点 n 上传（IN）数据 |
| `EP1_GetINSta`、`EP2_GetINSta`、`EP3_GetINSta`、`EP4_GetINSta` | `#define EP1_GetINSta() (R8_UEP1_CTRL & UEP_T_RES_NAK)` … `#define EP4_GetINSta() (R8_UEP4_CTRL & UEP_T_RES_NAK)` | — | `0` 未完成 / `!0` 已完成 | 查询端点 n 上传是否完成（可用于判断上一包是否发完） |
| `USB_DisablePin` | `#define USB_DisablePin() (R16_PIN_ANALOG_IE &= ~(RB_PIN_USB_IE \| RB_PIN_USB_DP_PU))` | — | 无 | 关闭 USB 上拉电阻（**依赖的 3 个宏本版未定义，见附录 B**） |
| `USB_Disable` | `#define USB_Disable() (R32_USB_CONTROL = 0)` | — | 无 | 关闭 USB |

### 11.2 端点缓冲区

| 宏 | 定义 | 说明 |
| --- | --- | --- |
| `pEP0_RAM_Addr`、`pEP1_RAM_Addr`、`pEP2_RAM_Addr`、`pEP3_RAM_Addr` | `extern uint8_t *` | 各通道 DMA 缓冲基址（**由应用分配**） |
| `pSetupReqPak` | `((PUSB_SETUP_REQ)pEP0_RAM_Addr)` | 控制传输的 SETUP 包 |
| `pEP0_DataBuf` | `(pEP0_RAM_Addr)` | EP0 数据缓冲 |
| `pEP1_OUT_DataBuf` / `pEP1_IN_DataBuf` | `pEP1_RAM_Addr` / `pEP1_RAM_Addr + 64` | EP1 收/发各 64B |
| `pEP2_OUT_DataBuf` / `pEP2_IN_DataBuf` | `pEP2_RAM_Addr` / `pEP2_RAM_Addr + 64` | EP2 收/发各 64B |
| `pEP3_OUT_DataBuf` / `pEP3_IN_DataBuf` | `pEP3_RAM_Addr` / `pEP3_RAM_Addr + 64` | EP3 收/发各 64B |
| `pEP4_OUT_DataBuf` / `pEP4_IN_DataBuf` | `pEP0_RAM_Addr+64` / `+128` | EP0(64)+EP4_OUT(64)+EP4_IN(64) 共用 EP0 块 |

**USB 类请求宏（HID）**：`DEF_USB_GET_IDLE`(0x02)、`DEF_USB_GET_PROTOCOL`(0x03)、`DEF_USB_SET_REPORT`(0x09)、`DEF_USB_SET_IDLE`(0x0A)、`DEF_USB_SET_PROTOCOL`(0x0B)。

**使用注意**

- 每包**最大 64 字节**（`l < 64B`），跨包发送要自己分包。
- `pEPn_RAM_Addr` 是**指针变量**，需要应用在初始化前把 DMA 缓冲挂上去（不在头文件里分配）。
- `USB_DevTransProcess()` 必须在 **USB 中断**（`USB_IRQn = 22`）里调用，它是整条 USB 状态机的驱动力。
- 收发方向要看清：`DevEPn_OUT_Deal()` 是**主机→设备**（设备接收），`DevEPn_IN_Deal()` 是**设备→主机**（设备发送）。
- `R32_USB_CONTROL`(0x40008000) 同时是 `R8_USB_CTRL`；`RB_UC_DEV_PU_EN`(0x20) 是设备上拉使能，`RB_UC_HOST_MODE`(0x80) 切主机模式。
- 本项目是「2.4G 无线串口调试器」，若同时跑 USB 与无线，注意 USB 中断优先级与收发缓冲的竞争。

---

## 12. USB 主机（USBHOST）

`CH57x_usbhost.h` 是 U 盘/HID 主机协议栈，**需要宏配置**：`DISK_LIB_ENABLE`、`DISK_WITHOUT_USB_HUB`、`DISK_BASE_BUF_LEN`（默认 512，建议 2048/4096 以支持大扇区 U 盘）。

### 12.1 根 HUB 与总线控制

| 函数 | 原型 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `DisableRootHubPort` | `void DisableRootHubPort(void);` | — | 无 | 关闭 ROOT-HUB 端口（硬件已自动关，这里只清结构状态） |
| `AnalyzeRootHub` | `uint8_t AnalyzeRootHub(void);` | — | `ERR_SUCCESS` 无事件 / `ERR_USB_CONNECT` 新连接 / `ERR_USB_DISCON` 断开 | 分析 ROOT-HUB 状态、处理插拔事件 |
| `SetHostUsbAddr` | `void SetHostUsbAddr(uint8_t addr);` | 设备地址 | 无 | 设置当前操作的 USB 设备地址 |
| `SetUsbSpeed` | `void SetUsbSpeed(uint8_t FullSpeed);` | 速度（0=低速，非 0=全速） | 无 | 设置当前 USB 速度 |
| `ResetRootHubPort` | `void ResetRootHubPort(void);` | — | 无 | 检测到设备后复位总线，为枚举做准备（默认全速） |
| `EnableRootHubPort` | `uint8_t EnableRootHubPort(void);` | — | `ERR_SUCCESS` 有连接 / `ERR_USB_DISCON` 无连接 | 使能 ROOT-HUB 端口（置 `bUH_PORT_EN`） |
| `WaitUSB_Interrupt` | `uint8_t WaitUSB_Interrupt(void);` | — | `ERR_SUCCESS` / `ERR_USB_UNKNOWN` | 等待 USB 中断完成 |
| `USBHostTransact` | `uint8_t USBHostTransact(uint8_t endp_pid, uint8_t tog, uint32_t timeout);` | `endp_pid`：高 4 位令牌 PID、低 4 位端点地址；`tog`：同步标志；`timeout`：以 **20uS** 为单位的 NAK 重试总时间（0 不重试 / `0xFFFF` 无限重试） | `ERR_SUCCESS` / `ERR_USB_UNKNOWN` / `ERR_USB_DISCON` / `ERR_USB_CONNECT` | 执行一次 USB 传输事务 |
| `HostCtrlTransfer` | `uint8_t HostCtrlTransfer(uint8_t *DataBuf, uint8_t *RetLen);` | `DataBuf`：可选收发缓冲；`RetLen`：实际成功长度输出 | `ERR_SUCCESS` / `ERR_USB_BUF_OVER` | 执行控制传输（8 字节请求码在 `pSetupReq` 中） |
| `CopySetupReqPkg` | `void CopySetupReqPkg(const uint8_t *pReqPkt);` | 请求包地址 | 无 | 复制控制传输请求包 |
| `USB_HostInit` | `void USB_HostInit(void);` | — | 无 | USB 主机功能初始化 |
| `EnumAllHubPort` | `uint8_t EnumAllHubPort(void);` | — | 错误码 | 枚举所有 ROOT-HUB 端口下、外部 HUB 后的二级 USB 设备 |
| `SelectHubPort` | `void SelectHubPort(uint8_t HubPortIndex);` | `0`=ROOT-HUB 端口；非 0=外部 HUB 的指定端口 | 无 | 选择后续操作的目标端口 |
| `SearchTypeDevice` | `uint16_t SearchTypeDevice(uint8_t type);` | 设备类型 | 端口号（**`0xFFFF` = 未找到**） | 在 ROOT-HUB 及外部 HUB 各端口搜索指定类型设备 |
| `SETorOFFNumLock` | `uint8_t SETorOFFNumLock(uint8_t *buf);` | 缓冲 | — | NumLock 点灯判断 |

### 12.2 描述符与控制传输

| 函数 | 原型 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `CtrlGetDeviceDescr` | `uint8_t CtrlGetDeviceDescr(void);` | — | `ERR_SUCCESS` / `ERR_USB_BUF_OVER` | 获取设备描述符（结果在 `pHOST_TX_RAM_Addr`） |
| `CtrlGetConfigDescr` | `uint8_t CtrlGetConfigDescr(void);` | — | 同上 | 获取配置描述符（结果在 `pHOST_TX_RAM_Addr`） |
| `CtrlSetUsbAddress` | `uint8_t CtrlSetUsbAddress(uint8_t addr);` | 设备地址 | `ERR_SUCCESS` | 设置 USB 设备地址 |
| `CtrlSetUsbConfig` | `uint8_t CtrlSetUsbConfig(uint8_t cfg);` | 配置值 | `ERR_SUCCESS` | 设置 USB 设备配置 |
| `CtrlClearEndpStall` | `uint8_t CtrlClearEndpStall(uint8_t endp);` | 端点地址 | `ERR_SUCCESS` | 清除端点 STALL |
| `CtrlSetUsbIntercace` | `uint8_t CtrlSetUsbIntercace(uint8_t cfg);` | 配置值 | `ERR_SUCCESS` | 设置 USB 设备接口配置（**函数名拼写为 `Intercace`，头文件原样**） |
| `InitRootDevice` | `uint8_t InitRootDevice(void);` | — | 错误码 | 初始化指定 ROOT-HUB 端口的 USB 设备（完整枚举流程） |
| `CtrlGetHIDDeviceReport` | `uint8_t CtrlGetHIDDeviceReport(uint8_t infc);` | 接口号 | 错误码 | 获取 HID 报表描述符（结果在 TxBuffer） |
| `CtrlGetHubDescr` | `uint8_t CtrlGetHubDescr(void);` | — | 错误码 | 获取 HUB 描述符（结果在 `Com_Buffer`） |
| `HubGetPortStatus` | `uint8_t HubGetPortStatus(uint8_t HubPortIndex);` | 端口号 | 错误码 | 查询 HUB 端口状态（结果在 `Com_Buffer`） |
| `HubSetPortFeature` | `uint8_t HubSetPortFeature(uint8_t HubPortIndex, uint8_t FeatureSelt);` | 端口号、特性 | 错误码 | 设置 HUB 端口特性 |
| `HubClearPortFeature` | `uint8_t HubClearPortFeature(uint8_t HubPortIndex, uint8_t FeatureSelt);` | 同上 | 错误码 | 清除 HUB 端口特性 |

### 12.3 状态码与设备表

| 宏 | 值 | 说明 |
| --- | --- | --- |
| `ERR_SUCCESS` | `0x00` | 操作成功 |
| `ERR_USB_CONNECT` | `0x15` | 检测到设备连接 |
| `ERR_USB_DISCON` | `0x16` | 检测到设备断开 |
| `ERR_USB_BUF_OVER` | `0x17` | 数据有误或缓冲区溢出 |
| `ERR_USB_DISK_ERR` | `0x1F` | 存储器操作失败 |
| `ERR_USB_TRANSFER` | `0x20` | NAK/STALL 等（**更多错误码在 0x20~0x2F**） |
| `ERR_USB_UNSUPPORT` | `0xFB` | 不支持的 USB 设备 |
| `ERR_USB_UNKNOWN` | `0xFE` | 设备操作出错 |
| `ERR_AOA_PROTOCOL` | `0x41` | 协议版本出错 |

| 宏 / 变量 | 说明 |
| --- | --- |
| `ROOT_DEV_DISCONNECT`(0) / `_CONNECTED`(1) / `_FAILED`(2) / `_SUCCESS`(3) | 设备状态（`DeviceStatus`）取值 |
| `DEV_TYPE_KEYBOARD` / `DEV_TYPE_MOUSE` | `(USB_DEV_CLASS_HID \| 0x20)` / `(USB_DEV_CLASS_HID \| 0x30)` |
| `DEF_AOA_DEVICE`(0xF0) / `DEV_TYPE_UNKNOW`(0xFF) | AOA 设备 / 未知类型 |
| `HUB_MAX_PORTS` | `4` |
| `WAIT_USB_TOUT_200US` | `800`（等待 USB 中断超时） |
| `ThisUsbDev` / `ThisUsb2Dev` | `_RootHubDev` 结构：`DeviceStatus`/`DeviceAddress`/`DeviceSpeed`/`DeviceType`/`DeviceVID`/`DevicePID`/`GpVar[4]`/`GpHUBPortNum` |
| `DevOnHubPort[]` / `DevOnU2HubPort[]` | `_DevOnHubPort` 结构（外部 HUB 各端口设备，最多 `HUB_MAX_PORTS` 个） |
| `UsbDevEndp0Size` / `Usb2DevEndp0Size` | 设备端点 0 最大包尺寸 |
| `FoundNewDev` / `FoundNewU2Dev` | 发现新设备标志 |
| `pHOST_RX_RAM_Addr` / `pHOST_TX_RAM_Addr`（及 `pU2HOST_*`） | 主机收发缓冲基址（由应用分配） |
| `pSetupReq` / `pU2SetupReq` | `((PUSB_SETUP_REQ)pHOST_TX_RAM_Addr)` —— SETUP 包 |
| `Com_Buffer[]` / `U2Com_Buffer[]` | 通用缓冲（HUB 描述符、端口状态等结果） |
| `SetupGetDevDescr[]` 等 | 预置的 USB 标准请求包常量（Get Device/Cfg Descr、Set Addr/Cfg/Interface、Clr Endp Stall，各有一份 `U2` 版本） |

**使用注意**

- **地址约定**：`0x02` = 内置 ROOT-HUB 下的设备或外部 HUB；`0x1x` = 内置 ROOT-HUB 下外部 HUB 的端口 x 上的设备（x=1~n）。
- 只支持**不超过 1 个外部 HUB**，每个外部 HUB 不超过 `HUB_MAX_PORTS`(4) 个端口。
- 典型流程：`USB_HostInit()` → 轮询 `AnalyzeRootHub()` 判连接 → `ResetRootHubPort()` → `EnableRootHubPort()` → `InitRootDevice()`（内部完成 `CtrlGetDeviceDescr`/`CtrlSetUsbAddress`/`CtrlGetConfigDescr`/`CtrlSetUsbConfig`）→ 按类型用 `SearchTypeDevice()` → 数据阶段用 `USBHostTransact()`。
- `USBHostTransact()` 的头文件注释明确提示：**该子程序为易理解而写，实际应用为提速应自行优化**。
- USB 中断号 `USB_IRQn = 22`。

---

## 13. 比较器（CMP）

| 接口 | 原型 / 定义 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `CMP_Init` | `void CMP_Init(CMPSwTypeDef s, CMPNrefLevelTypeDef v);` | `s`：通道选择；`v`：负端内部参考电平 | 无 | 比较器初始化 |
| `CMP_OutToTIMCAPCfg` | `void CMP_OutToTIMCAPCfg(FunctionalState s);` | `ENABLE`/`DISABLE` | 无 | 比较器输出送到 TIM 捕捉输入 |
| `CMP_INTCfg` | `void CMP_INTCfg(CMPOutSelTypeDef sel, FunctionalState s);` | `sel`：`cmp_out_sel_high`(0)/`_low`/`_fall`/`_rise`；`s`：开关 | 无 | 比较器中断配置 |
| `CMP_GetITStatus` | `#define CMP_GetITStatus() (R8_CMP_CTRL_2 & RB_CMP_IF)` | — | 中断状态 | 读比较器中断标志 |
| `CMP_ClearITStatus` | `#define CMP_ClearITStatus() (R8_CMP_CTRL_2 \|= RB_CMP_IF)` | — | 无 | 清比较器中断标志 |
| `CMP_ReadAPROut` | `#define CMP_ReadAPROut() (R8_CMP_CTRL_3 & RB_APR_OUT_CMP)` | — | 输出电平 | 读比较器（APR）输出 |
| `CMP_Enable` | `#define CMP_Enable() (R8_CMP_CTRL_0 \|= RB_CMP_EN)` | — | 无 | 使能比较器 |
| `CMP_Disable` | `#define CMP_Disable() (R8_CMP_CTRL_0 &= ~RB_CMP_EN)` | — | 无 | 关闭比较器 |

**枚举**

| 枚举 | 成员 | 说明 |
| --- | --- | --- |
| `CMPSwTypeDef` | `cmp_sw_0`(0)：P0=PA3, N=PA2；`cmp_sw_1`：P0=PA3, N=CMP_VERF；`cmp_sw_2`：P0=PA7, N=PA2；`cmp_sw_3`：P0=PA7, N=CMP_VERF | 输入通道选择 |
| `CMPNrefLevelTypeDef` | `cmp_nref_level_50`(0) ~ `cmp_nref_level_800`，**50mV 步进共 16 档** | 负端内部参考电平 |
| `CMPOutSelTypeDef` | `cmp_out_sel_high`(0)、`cmp_out_sel_low`、`cmp_out_sel_fall`、`cmp_out_sel_rise` | 输出/中断触发选择 |

**使用注意**

- 中断号 `CMP_IRQn = 29`。
- `CMP_Init()` 的 `s` 为 `cmp_sw_1`/`cmp_sw_3` 时负端用内部 `CMP_VERF`，此时 `v` 才真正决定阈值；选 `cmp_sw_0`/`cmp_sw_2` 时负端接外部引脚。
- 比较器时钟受 `RB_SLP_CLK_CMP`(0x02) 门控；低功耗前若不需要比较器可关它的时钟。

---

## 14. 按键扫描（KEYSCAN）

| 接口 | 原型 / 定义 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `KeyScan_Cfg` | `void KeyScan_Cfg(uint8_t s, uint16_t keyScanPin, uint16_t ClkDiv, uint16_t Rep);` | `s`：开关；`keyScanPin`：`KEYSCAN_PA2/PA3/PA8/PA10/PA11`/`KEYSCAN_ALL`（可或）；`ClkDiv`：`KEYSCAN_DIV1/2/4/8/16`；`Rep`：`KEYSCAN_REP1`~`KEYSCAN_REP7` | 无 | 按键扫描配置 |
| `KeyPress_Wake` | `void KeyPress_Wake(uint8_t s);` | 开关 | 无 | 按键唤醒使能 |
| `KeyValue` | `#define KeyValue (R32_KEY_SCAN_NUMB & RB_KEY_SCAN_NUMB)` | — | 按键值（低 20 位） | 读按键值 |
| `KeyScan_Cnt` | `#define KeyScan_Cnt (R32_KEY_SCAN_NUMB >> 20)` | — | 扫描计数 | 读扫描次数 |
| `KeyScan_ITCfg` | `#define KeyScan_ITCfg(s, f) ((s) ? (R8_KEY_SCAN_INT_EN \|= f) : (R8_KEY_SCAN_INT_EN &= ~f))` | `s`：开关；`f`：中断位 | 无 | 按键扫描中断配置 |
| `KeyScan_ClearITFlag` | `#define KeyScan_ClearITFlag(f) (R8_KEY_SCAN_INT_FLAG = f)` | `f`：中断位 | 无 | 清按键扫描中断标志 |
| `KeyScan_GetITFlag` | `#define KeyScan_GetITFlag(f) (R8_KEY_SCAN_INT_FLAG & f)` | `f`：中断位 | `0` 未置位 / `!0` 触发 | 查询中断标志 |

**引脚与分频/重复次数宏**

| 宏 | 值 | 说明 |
| --- | --- | --- |
| `KEYSCAN_PA2/PA3/PA8/PA10/PA11` | `0x100`/`0x200`/`0x400`/`0x800`/`0x1000` | 可扫描的引脚 |
| `KEYSCAN_ALL` | `0x1F00` | 全部 5 个引脚 |
| `KEYSCAN_DIV1/2/4/8/16` | `0x00`/`0x10`/`0x30`/`0x70`/`0xF0` | 扫描时钟分频 |
| `KEYSCAN_REP1`~`REP7` | `0x02`/`0x04`/`0x06`/`0x08`/`0x0A`/`0x0C`/`0x0E` | 重复次数 |
| `RB_KEY_SCAN_NUMB` | `0x0FFFFF` | 按键值掩码 |

**使用注意**

- `KeyScan_ITCfg` / `KeyScan_ClearITFlag` / `KeyScan_GetITFlag` 的头文件注释写的是「refer to ENC interrupt bit define」，**注释有误**（应指 KEYSCAN 自己的中断位）；具体位定义以 `CH572SFR.h` / 数据手册为准。
- 按键扫描可作为**唤醒源**（`R8_SLP_CLK_OFF0` 的 `RB_SLP_KEYSCAN_WAKE`=0x80），配合 `KeyPress_Wake()` 做低功耗唤醒。
- 中断号 `KEYSCAN_IRQn = 33`。

---

## 15. ISP（Flash 操作库，ISP572.h）

`ISP572.h` 用于**用户代码区与引导区**的 Flash-ROM 操作（IAP）。**擦除/写入的唯一入口是 `FLASH_EEPROM_CMD()`**，其余都是它的宏封装。

| 接口 | 原型 / 定义 | 参数 | 返回值 | 作用 |
| --- | --- | --- | --- | --- |
| `FLASH_EEPROM_CMD` | `extern uint32_t FLASH_EEPROM_CMD(uint8_t cmd, uint32_t StartAddr, void *Buffer, uint32_t Length);` | `cmd`：`CMD_*`；`StartAddr`：目标地址；`Buffer`：**必须在 RAM 且 4 字节对齐**；`Length`：字节数 | `0` 成功 / `!0` 失败 | 执行 Flash/EEPROM 命令（底层唯一接口） |
| `FLASH_ROM_START_IO` | `FLASH_EEPROM_CMD(CMD_FLASH_ROM_START_IO, 0, NULL, 0)` | — | `0`/`!0` | 启动 Flash-ROM I/O |
| `FLASH_ROM_SW_RESET` | `FLASH_EEPROM_CMD(CMD_FLASH_ROM_SW_RESET, 0, NULL, 0)` | — | `0`/`!0` | 软件复位 Flash-ROM（**每条命令前会自动执行**） |
| **★ `FLASH_ROM_ERASE`** | `FLASH_EEPROM_CMD(CMD_FLASH_ROM_ERASE, StartAddr, NULL, Length)` | `StartAddr`：擦除起始地址；`Length`：字节数（**建议 4096 的整数倍**） | `0` 成功 / `!0` 失败 | **按块擦除** Flash-ROM |
| **★ `FLASH_ROM_WRITE`** | `FLASH_EEPROM_CMD(CMD_FLASH_ROM_WRITE, StartAddr, Buffer, Length)` | `StartAddr`：写入地址；`Buffer`：源缓冲（**RAM + 4 字节对齐**）；`Length`：字节数（**最小 4 字节，建议 256 的整数倍**） | `0` 成功 / `!0` 失败 | **写 Flash-ROM 数据块** |
| `FLASH_ROM_VERIFY` | `FLASH_EEPROM_CMD(CMD_FLASH_ROM_VERIFY, StartAddr, Buffer, Length)` | 同上 | `0` 成功 / `!0` 失败 | **校验** Flash-ROM 数据块（最小 4 字节） |
| `GetMACAddress` | `FLASH_EEPROM_CMD(CMD_GET_ROM_INFO, ROM_CFG_MAC_ADDR, Buffer, 0)` | `Buffer`：输出缓冲（RAM + 4 字节对齐） | `0`/`!0` | 读 6 字节 MAC 地址 |
| `GET_BOOT_INFO` | `FLASH_EEPROM_CMD(CMD_GET_ROM_INFO, ROM_CFG_BOOT_INFO, Buffer, 0)` | 同上 | `0`/`!0` | 读 8 字节 BOOT 信息 |
| `FLASH_ROM_PWR_DOWN` | `FLASH_EEPROM_CMD(CMD_FLASH_ROM_PWR_DOWN, 0, NULL, 0)` | — | `0`/`!0` | Flash-ROM 掉电（省电） |
| `FLASH_ROM_PWR_UP` | `FLASH_EEPROM_CMD(CMD_FLASH_ROM_PWR_UP, 0, NULL, 0)` | — | `0`/`!0` | Flash-ROM 上电 |

**命令码与常量**

| 宏 | 值 | 说明 |
| --- | --- | --- |
| `CMD_FLASH_ROM_START_IO` | `0x00` | 启动 Flash-ROM I/O（无参数） |
| `CMD_FLASH_ROM_SW_RESET` | `0x04` | 软件复位（无参数） |
| `CMD_GET_ROM_INFO` | `0x06` | 取 ROM 信息（参数 `@Address, Buffer`） |
| `CMD_GET_UNIQUE_ID` | `0x07` | 取 64 bit 唯一 ID（参数 `@Buffer`） |
| `CMD_FLASH_ROM_PWR_DOWN` / `_PWR_UP` | `0x0D` / `0x0C` | 掉电 / 上电 |
| `CMD_FLASH_ROM_ERASE` | `0x01` | 擦除块（参数 `@StartAddr, Length`） |
| `CMD_FLASH_ROM_WRITE` | `0x02` | 写数据块（参数 `@StartAddr, Buffer, Length`） |
| `CMD_FLASH_ROM_VERIFY` | `0x03` | 校验数据块（参数 `@StartAddr, Buffer, Length`） |
| `FLASH_MIN_WR_SIZE` | `4` | **写入/校验最小单位（1 dword）** |
| `FLASH_BLOCK_SIZE` | `4096` | **擦除块大小（4KB）** |
| `FLASH_ROM_MAX_SIZE` | `0x03C000` | Flash-ROM 最大程序区（240KB） |
| `ROM_CFG_MAC_ADDR` | `0x3F018` | MAC 地址信息地址 |
| `ROM_CFG_BOOT_INFO` | `0x3DFF8` | BOOT 信息地址 |

**Flash-ROM 特性（头文件原文归纳）**

- 存放程序代码，支持**块擦除**、**dword/页写入**、**dword 校验**，`Length` 单位是**字节**。
- **写入或校验的最小单位是 1 个 dword（4 字节）**。
- **写入以 256 字节/页 为单位；`FLASH_ROM_WRITE` 支持 1 个或多个 dword，但 256 的整数倍最佳**。
- **擦除以 4KB（4096 字节）/块 为单位，4096 的整数倍最佳**。

**使用注意（★）**

- **顺序**：先 `FLASH_ROM_ERASE()`（擦成 0xFF），再 `FLASH_ROM_WRITE()`；`Buffer` **必须放在 RAM 且 4 字节对齐**（放 Flash 里或奇地址会失败）。
- `Length` 建议**按 4096 / 256 / 4 的粒度对齐**：写参数区时把参数结构体按 4 字节对齐并补齐长度到 4 的倍数。
- 擦写**耗时较长**（毫秒级），且会占用 Flash 总线：**不要在串口/USB 中断里做**，会丢数据；本项目是调试器，建议放在空闲态并临时禁止高频收发。
- `FLASH_ROM_ERASE`/`WRITE`/`VERIFY` 是**宏**，返回值就是 `FLASH_EEPROM_CMD()` 的返回值，**必须检查**（`0` 才算成功）。
- 与 `CH57x_flash.h` 的 `FLASH_ROM_READ()` 配合：**读用 `FLASH_ROM_READ()`，写/擦用这里**。
- 240KB 程序区与引导区边界：`BOOT_LOAD_ADDR = 0x3C000`、`BOOT_LOAD_SIZE = 0x2000`、`ROM_CFG_ADDR = 0x3F000`（`CH572SFR.h`）——参数区**不要**跨越或侵入这些区域。
- 头文件还说明：该库可用于**用户代码区**（可在用户代码中被调用，IAP 擦写自身），也可在**引导代码**中被调用（更新用户代码）。

**本项目典型调用片段**

```c
/* 保存参数：擦 1 个 4KB 块 -> 写（长度补到 4 的倍数） */
static uint8_t __attribute__((aligned(4))) wr_buf[256];

memcpy(wr_buf, &g_param, sizeof(g_param));       /* wr_buf 在 RAM 且 4 字节对齐 */
if (FLASH_ROM_ERASE(PARAM_ADDR, FLASH_BLOCK_SIZE) == 0) {   /* 必须先擦 */
    if (FLASH_ROM_WRITE(PARAM_ADDR, wr_buf, sizeof(g_param)) == 0) {
        /* 成功 */
    }
}

/* 校验写入结果 */
FLASH_ROM_VERIFY(PARAM_ADDR, wr_buf, sizeof(g_param));

/* 读回来（用 CH57x_flash.h 的读接口） */
FLASH_ROM_READ(PARAM_ADDR, &g_param, sizeof(g_param));

/* 读 MAC / BOOT 信息 */
uint8_t mac[8] __attribute__((aligned(4)));
GetMACAddress(mac);
```

---

## 16. 常用宏与枚举速查

### 16.1 本项目用到的（★ 高频）

| 类别 | 名称 | 值 / 原型 | 备注 |
| --- | --- | --- | --- |
| **时钟** | `SetSysClock` | `void SetSysClock(SYS_CLKTypeDef sc)` | 主频切换 |
| | `GetSysClock` | `uint32_t GetSysClock(void)` | 返回 Hz |
| | `CLK_SOURCE_HSE_PLL_24MHz` | `0x40｜25` | 低速档 |
| | `CLK_SOURCE_HSE_PLL_100MHz` | `0x40｜6` | 高速档 |
| | `HSECFG_Capacitance` | `void HSECFG_Capacitance(HSECapTypeDef c)` | 晶振负载电容 |
| | `HSECap_18p` | 6 | 18pF |
| | `FREQ_SYS` | 默认 `100000000` | 编译期主频常数（`CH57x_common.h`） |
| **GPIO** | `GPIOA_ModeCfg` | `void GPIOA_ModeCfg(uint32_t pin, GPIOModeTypeDef mode)` | — |
| | `GPIOA_SetBits` / `GPIOA_ResetBits` / `GPIOA_InverseBits` | 宏，写 `R32_PA_SET`/`CLR`/`OUT` | 置高/置低/翻转 |
| | `GPIO_ModeOut_PP_5mA` | 3 | 推挽 5mA |
| | `GPIO_ModeOut_PP_20mA` | 4 | 推挽 20mA |
| | `GPIO_ModeIN_PU` | 1 | 上拉输入 |
| | `GPIO_ModeIN_Floating` / `GPIO_ModeIN_PD` | 0 / 2 | 浮空 / 下拉 |
| | `GPIO_Pin_0` … `GPIO_Pin_15` | `1<<n` | 引脚位 |
| **UART** | `UART_DefInit` | `void UART_DefInit(void)` | 默认初始化 |
| | `UART_BaudRateCfg` | `void UART_BaudRateCfg(uint32_t baudrate)` | 依赖 `FREQ_SYS` |
| | `UART_Remap` | `void UART_Remap(FunctionalState s, UARTTxPinRemapDef u_tx, UARTRxPinRemapDef u_rx)` | 引脚映射 |
| | `UART_ByteTrigCfg` / `UART_4BYTE_TRIG` | 枚举 | 4 字节触发 |
| | `UART_INTCfg` | `void UART_INTCfg(FunctionalState s, uint8_t i)` | 中断使能 |
| | `UART_GetITFlag` | `R8_UART_IIR & 0x0F` | 中断类型（用 `==` 比） |
| | `UART_GetLinSTA` | `R8_UART_LSR` | 线路状态（用 `&` 比） |
| | `UART_RecvByte` / `UART_SendByte` | `R8_UART_RBR` / `R8_UART_THR` | 收发单字节 |
| | `UART_II_LINE_STAT` / `UART_II_RECV_RDY` / `UART_II_RECV_TOUT` | `0x06` / `0x04` / `0x0C` | 中断分派 |
| | `RB_LCR_STOP_BIT` / `RB_LCR_PAR_EN` / `RB_LCR_PAR_MOD` / `RB_LCR_WORD_SZ` / `RB_LCR_DLAB` | `0x04` / `0x08` / `0x30` / `0x03` / `0x80` | LCR 位 |
| | `R8_UART_THR` / `R8_UART_LCR` / `R16_UART_DL` | `0x40003408` / `0x40003403` / `0x4000340C` | 关键寄存器 |
| | `R8_UART_TFC` / `R8_UART_RFC` | `0x4000340B` / `0x4000340A` | 收发 FIFO 计数 |
| | `RB_IER_RECV_RDY` / `RB_IER_LINE_STAT` / `RB_IER_THR_EMPTY` | `0x01` / `0x04` / `0x02` | IER 位 |
| | `STA_ERR_FRAME` / `STA_ERR_PAR` / `STA_ERR_FIFOOV` / `STA_TXALL_EMP` | LSR 位 | 状态判断 |
| **系统/延时** | `mDelaymS` / `mDelayuS` | `void mDelay*(uint16_t t)` | 忙等延时（`uint16_t`！） |
| | `DelayMs` / `DelayUs` | 宏别名 | 同上 |
| | `R16_PIN_ALTERNATE` | `0x40001018` | 复用配置低字 |
| | `RB_PIN_DEBUG_EN` | `0x4000` | 清 0 关两线调试 |
| | `SYS_GetSysTickCnt` | `uint32_t SYS_GetSysTickCnt(void)` | 非阻塞计时 |
| **TMR** | `TMR_ITCfg` | `#define TMR_ITCfg(s, f)` | 中断使能 |
| | `TMR_GetITFlag` / `TMR_ClearITFlag` | 宏 | 查/清标志（返回掩码值！） |
| | `TMR_IT_CYC_END` | `0x01` | 周期结束 |
| | `TMR_IT_DATA_ACT` / `TMR_IT_FIFO_HF` / `TMR_IT_DMA_END` / `TMR_IT_FIFO_OV` | `0x02`/`0x04`/`0x08`/`0x10` | 其它标志 |
| | `TMR_TimerInit` / `TMR_Enable` / `TMR_Disable` | — | 定时三件套 |
| **Flash** | `FLASH_ROM_READ` | `void FLASH_ROM_READ(uint32_t StartAddr, void *Buffer, uint32_t len)` | 读（`CH57x_flash.h`） |
| | `FLASH_ROM_ERASE` | 宏 → `FLASH_EEPROM_CMD(0x01, ...)` | 擦（`ISP572.h`，4KB/块） |
| | `FLASH_ROM_WRITE` | 宏 → `FLASH_EEPROM_CMD(0x02, ...)` | 写（`ISP572.h`，4B 最小） |
| | `FLASH_BLOCK_SIZE` / `FLASH_MIN_WR_SIZE` / `FLASH_ROM_MAX_SIZE` | `4096` / `4` / `0x03C000` | 粒度与容量 |

### 16.2 通用类型与框架宏（`CH57x_common.h` / `core_riscv.h`）

| 名称 | 定义 | 说明 |
| --- | --- | --- |
| `FunctionalState` | `DISABLE`=0、`ENABLE`=`!DISABLE` | 库函数统一开关类型 |
| `FlagStatus` / `ITStatus` | `RESET`=0、`SET`=`!RESET` | 标志/中断状态类型 |
| `NULL` | `0` | — |
| `ALL` | `0xFFFF` | 全选 |
| `__HIGH_CODE` | `__attribute__((section(".highcode")))` | 放高频代码段（Flash 加速区） |
| `__INTERRUPT` | `__attribute__((interrupt("WCH-Interrupt-fast")))`（`INT_SOFT` 时为 `interrupt()`） | **中断服务函数必须加** |
| `PRINT(X...)` | `DEBUG` 时 `printf`，否则空 | 调试打印 |
| `SAFEOPERATE` | `asm volatile("fence.i")` | 安全访问指令同步 |
| `PFIC_EnableIRQ(IRQn)` / `PFIC_DisableIRQ(IRQn)` | `core_riscv.h` | 开/关中断 |
| `PFIC_SetPriority(IRQn, prio)` | `core_riscv.h` | 设优先级（`__PFIC_PRIO_BITS`=2） |
| `__risc_v_disable_irq()` / `__risc_v_enable_irq(mpie_mie)` | `core_riscv.h` | 裸中断开关（返回/传入保存值） |
| `Freq_LSI` | `extern uint32_t` | LSI 频率变量 |
| `ROM_CFG_VERISON` | `0x7F010` | ROM 配置版本地址 |

### 16.3 外设中断号速查（`CH572SFR.h`）

| 宏 | 值 | 宏 | 值 |
| --- | --- | --- | --- |
| `SysTick_IRQn` | 12 | `USB_IRQn` | 22 |
| `SWI_IRQn` | 14 | `TMR_IRQn` | 24 |
| `GPIO_A_IRQn` | 17 | `UART_IRQn` | 27 |
| `SPI_IRQn` | 19 | `RTC_IRQn` | 28 |
| `BLEB_IRQn` | 20 | `CMP_IRQn` | 29 |
| `BLEL_IRQn` | 21 | `I2C_IRQn` | 30 |
| | | `PWMX_IRQn` | 31 |
| | | `KEYSCAN_IRQn` / `ENCODE_IRQn` / `WDOG_BAT_IRQn` | 33 / 34 / 35 |

对应的向量地址宏：`INT_ADDR_*`（= `INT_ID_* × 4 + 64`）。

---

## 附录 A：头文件包含关系

`CH57x_common.h` 是**总入口**，包含全部外设头文件，建议应用只 `#include "CH57x_common.h"`：

```
CH57x_common.h
 ├─ <string.h> <stdint.h>
 ├─ CH572SFR.h          （寄存器与位定义、IRQn_Type）
 ├─ core_riscv.h        （FunctionalState / FlagStatus / PFIC_* ）
 ├─ CH57x_clk.h         CH57x_uart.h      CH57x_i2c.h      CH57x_pwm.h
 ├─ CH57x_cmp.h         CH57x_gpio.h      CH57x_flash.h    CH57x_sys.h
 ├─ CH57x_keyscan.h     CH57x_pwr.h      CH57x_timer.h     CH57x_spi.h
 ├─ CH57x_usbdev.h      CH57x_usbhost.h  ISP572.h
 └─ extern uint32_t Freq_LSI;
```

- `CH57x_common.h` 提供 `DelayMs()` / `DelayUs()` / `PRINT()` / `__INTERRUPT` / `__HIGH_CODE` / `FREQ_SYS`。
- `CH57x_timer.h` 用到 `PWMX_PolarTypeDef`（声明在 `CH57x_pwm.h`）——包含顺序由 `CH57x_common.h` 保证。
- **不要**单独包含 `CH572SFR.h` 以外的 SFR 头文件；`R8_UART_*` 一类寄存器宏全部来自 `CH572SFR.h`。

---

## 附录 B：本版头文件的已知不一致处（必读）

以下问题是通过**逐符号比对**这 16 个头文件与 `CH572SFR.h` / `RVMSIS/core_riscv.h` 得到的：头文件里**引用了但本版 SFR 头文件未定义**的宏。直接使用会**编译不过**，需要按数据手册补齐或改写法。

| 引用位置 | 未定义的符号 | 影响 | 建议 |
| --- | --- | --- | --- |
| `CH57x_uart.h:108` `UART_CLR_RXFIFO()` | `RB_FCR_RX_FIFO_CLR` | 清接收 FIFO 宏不可用 | 按数据手册 FCR 的「清接收 FIFO」位自行定义，或直接 `R8_UART_FCR` 赋值；**以数据手册/后续版本头文件为准** |
| `CH57x_uart.h:113` `UART_CLR_TXFIFO()` | `RB_FCR_TX_FIFO_CLR` | 清发送 FIFO 宏不可用 | 同上 |
| `CH57x_uart.h:98`（注释） | `RB_IER_MODEM_CHG` | 仅注释里提到（调制解调器状态变化中断，头文件注明**仅 UART0 支持**） | 无需处理；CH57x 只有一路 UART，用 `RB_IER_LINE_STAT`/`RB_IER_RECV_RDY`/`RB_IER_THR_EMPTY` |
| `CH57x_usbdev.h:141` `USB_DisablePin()` | `R16_PIN_ANALOG_IE`、`RB_PIN_USB_IE`、`RB_PIN_USB_DP_PU` | 关 USB 上拉宏不可用 | 改用 `R32_USB_CONTROL`/`R8_USB_CTRL` 的 `RB_UC_DEV_PU_EN`(0x20)，或 `R16_PIN_ALTERNATE` 的 `RB_PIN_USB_EN`(0x2000)/`RB_UDP_PU_EN`(0x1000)；**以头文件/驱动实现为准** |
| `CH57x_pwr.h:95`（注释） | `RB_PWR_RAM2K` | `LowPower_Sleep()` 的 `rm` 参数注释与 SFR 不符 | SFR 实为 `RB_PWR_RAM12K`(0x02)；见下方 |
| `CH57x_pwr.h:96`（注释） | `RB_PWR_RAM16K` | 同上 | SFR 实为 `RB_PWR_CORE`(0x04)；`RB_PWR_EXTEND`(0x08)、`RB_PWR_XROM`(0x01) 存在 |

**其它需要注意的文档/命名不一致（不影响编译，但容易踩）**

| 位置 | 现象 |
| --- | --- |
| `CH57x_clk.h` `RTC_TRIGFunCfg` | 注释写 `@param t refer to RTC_TMRCycTypeDef`，**实际形参是 `uint32_t cyc`** |
| `CH57x_usbhost.h` | 函数名 `CtrlSetUsbIntercace()`（拼写为 `Intercace`）、`SPI` 头文件的 `Mose_FirstCmd`（拼写为 `Mose_`）——按头文件原样使用 |
| `CH57x_keyscan.h` | `KeyScan_ITCfg`/`ClearITFlag`/`GetITFlag` 的注释写「refer to ENC interrupt bit define」，应为 KEYSCAN 自己的位 |
| `CH57x_flash.h` vs `ISP572.h` | **擦/写不在 `CH57x_flash.h`**：`FLASH_ROM_ERASE`/`WRITE`/`VERIFY` 是 `ISP572.h` 里的宏；`CH57x_flash.h` 只有 `FLASH_ROM_READ`。名字相似极易搞混 |
| `CH57x_common.h` | `FREQ_SYS` 默认硬编码 `100000000`，与实际 `SetSysClock()` 结果可能不一致；运行时请用 `GetSysClock()` |
| 库函数实现 | `UART_BaudRateCfg()` 的分频公式、`PWR_PeriphClkCfg()` 的位拼接、延时校准常量等**只存在于 `.c` 实现**中；本文档只依据头文件，未推断实现，**移植时以源码为准** |

---

## 附录 C：中断号与向量地址

| 外设 | `IRQn` | 值 | 向量地址宏 | 地址（= IRQn×4+64） |
| --- | --- | --- | --- | --- |
| GPIOA | `GPIO_A_IRQn` | 17 | `INT_ADDR_GPIO_A` | `0x54` |
| SPI | `SPI_IRQn` | 19 | `INT_ADDR_SPI` | `0x5C` |
| BLEBB | `BLEB_IRQn` | 20 | `INT_ADDR_BLEB` | `0x60` |
| BLELLE | `BLEL_IRQn` | 21 | `INT_ADDR_BLEL` | `0x64` |
| USB | `USB_IRQn` | 22 | `INT_ADDR_USB` | `0x68` |
| TMR | `TMR_IRQn` | 24 | `INT_ADDR_TMR` | `0x70` |
| UART | `UART_IRQn` | 27 | `INT_ADDR_UART` | `0x7C` |
| RTC | `RTC_IRQn` | 28 | `INT_ADDR_RTC` | `0x80` |
| CMP | `CMP_IRQn` | 29 | `INT_ADDR_CMP` | `0x84` |
| I2C | `I2C_IRQn` | 30 | `INT_ADDR_I2C` | `0x88` |
| PWM1~5 | `PWMX_IRQn` | 31 | `INT_ADDR_PWMX` | `0x8C` |
| KEYSCAN | `KEYSCAN_IRQn` | 33 | `INT_ADDR_KEYSCAN` | `0x94` |
| ENCODER | `ENCODE_IRQn` | 34 | `INT_ADDR_ENCODE` | `0x98` |
| WDOG/BAT | `WDOG_BAT_IRQn` | 35 | `INT_ADDR_WDOG_BAT` | `0x9C` |

- 中断优先级位宽 `__PFIC_PRIO_BITS` = 2（4 级）。
- 中断服务函数必须用 `__INTERRUPT`（即 `__attribute__((interrupt("WCH-Interrupt-fast")))`）修饰；**函数名须与工程启动文件/向量表中的绑定一致**（以工程为准）。

---

*文档依据 `StdPeriphDriver/inc/` 下 16 个头文件 + `CH572SFR.h` + `RVMSIS/core_riscv.h` 编写；所有原型与宏均来自头文件原文，未作推断。实现细节（`.c`）请以源码为准。*
