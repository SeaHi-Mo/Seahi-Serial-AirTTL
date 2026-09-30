# 2.4G 协议栈 + RISC-V 内核抽象层 API 参考

> 本文收录两份**沁恒官方**头文件里的全部对外接口：
>
> | 头文件 | 对应二进制 | 角色 |
> |---|---|---|
> | `LIB/CH572rf.h`（V1.10，2024/11/07） | `LIB/libCH57xRF.a` → `VER_RF_LIB` = `"CH57x_RF_BASIC_LIB_V1.1"` | 2.4G 射频协议栈（RF 角色 + RFIP 寄存器级驱动） |
> | `RVMSIS/core_riscv.h`（V1.0.0，2024/07/25） | 无（纯头文件，全为 `static inline`） | RISC-V 内核抽象层：CSR、PFIC 中断控制器、xw 扩展、SysTick |
>
> **这些接口来自沁恒官方库，属于预编译静态库（`libCH57xRF.a`）与内核头，不要修改，只能调用。**
> 两个工程各有一份副本，**内容一致**：`RF_Uart/LIB/CH572rf.h` / `RF_UartDongle/LIB/CH572rf.h`、`RF_Uart/RVMSIS/core_riscv.h` / `RF_UartDongle/RVMSIS/core_riscv.h`。要升级只能整体替换官方文件，**不要在头文件里改字段或加宏**。
>
> 本项目自己包在协议栈外面的一层（`rf_tx_start()` / `rf_rx_start()` / `RFRole_Init()` …）见 [app-api.md](./app-api.md)；上层帧格式见 [protocol.md](./protocol.md)。
>
> 凡本文未在头文件里找到的符号，一律明确标注为"**不存在 / 以头文件为准**"，不做推测补充。

---

## 0. 动手前必须知道的四件事

### 0.1 通用类型（两个头文件共同的基础）

| 类型 | 定义位置 | 含义 |
|---|---|---|
| `bStatus_t` | `CH572rf.h` `typedef uint8_t bStatus_t;`（有 `#ifndef` 保护） | 协议库统一返回类型：**`0` = 成功**，非 0 = 失败/忙 |
| `rfRole_States_t` | `CH572rf.h` `typedef uint32_t rfRole_States_t;` | 射频状态位集合（`RF_STATE_*` 的按位或），回调参数类型 |
| `IRQn_Type` | `StdPeriphDriver/inc/CH572SFR.h`（**不在这两个头文件里**） | `PFIC_*IRQ()` 系列的中断号枚举；本项目用到 `BLEB_IRQn = 20`、`BLEL_IRQn = 21`、`SysTick_IRQn` |
| `FunctionalState` | `core_riscv.h` | `DISABLE = 0` / `ENABLE = 1`（`ENABLE` 由 `!DISABLE` 定义） |
| `FlagStatus` / `ITStatus` | `core_riscv.h` | `RESET = 0` / `SET = 1`（同一个 `typedef` 行里声明了两个名字） |

### 0.2 单位换算：`waitTime` 与 `timeOut` **都是"微秒 × 2"**

这是本项目**最容易写错**的一条约定，两个参数都以 0.5 µs 为 1 个计数：

| 参数 | 所在结构 | 头文件原文 | 计数单位 | 项目内的换算 |
|---|---|---|---|---|
| `waitTime` | `rfipTx_t` | `wait tx PLL lock time` | 0.5 µs | `gTxParam.waitTime = rfon_us * 2;` |
| `timeOut` | `rfipRx_t` | `Rx wait timeout, 0:No timeout others: N*0.5us` | 0.5 µs | `gRxParam.timeOut = rx_us * 2;` |

项目实例（`APP/rf.c`）：

```c
void rf_tx_start( void *pBuf, uint16_t rfon_us )
{
    gTxParam.txDMA = (uint32_t)pBuf;
    gTxParam.waitTime = rfon_us*2;   // 60us → waitTime = 120  ← 调 API 时不要自己再乘 2
    RFIP_StartTx( &gTxParam );
}

void rf_rx_start( uint32_t rx_us )
{
    gRxParam.timeOut = rx_us*2;      // 150us → timeOut = 300
    RFIP_SetRx( &gRxParam );
}
```

> **结论：调用 `rf_tx_start(buf, 60)` / `rf_rx_start(150)` 时传的是微秒；直接调 `RFIP_StartTx()` / `RFIP_SetRx()` 时，`waitTime` / `timeOut` 字段必须自己传"微秒 × 2"。**
> `RFRole_Init()` 里预置的 `waitTime = 80*2` 即 80 µs，来自 `rf.c` 顶部注释"如果需要切换通道发送，稳定时间不低于 80us"。

### 0.3 收发缓冲区硬性要求

| 要求 | 出处 | 说明 |
|---|---|---|
| RX DMA 缓冲 **不能小于 264 字节** 且 **4 字节对齐** | 项目 `APP/rf.c` 注释 + 数组定义 | `__attribute__((__aligned__(4))) uint8_t RxBuf[264];` —— 协议栈的 DMA 会按最长帧写入，**给小块会踩内存** |
| 单包最大长度 `RF_MAX_DATA_LEN` = **251** | `CH572rf.h` | 协议栈定义的上限；本项目自己另有 `DATA_LEN_MAX_RX` 限制（见 [app-api.md](./app-api.md)） |
| `txLen` 只在 **2.4G nondpl 模式**下表示长度 | `rfipTx_t.txLen` 注释 | 其它模式下该字段 **resv（保留、无意义）**，长度由 DMA 缓冲的包头/硬件决定 |

### 0.4 为什么必须用沁恒定制的 GCC（否则链接/编译过不去）

`core_riscv.h` 里的 `__MCPY()` 直接内联了一条**非标准自定义指令 `mcpy`**（沁恒 xw 扩展），协议库内部也大量依赖它。因此：

| 项 | 本项目实际取值（`RF_Uart/CMakeLists.txt`） |
|---|---|
| 工具链 | 仓库内 `tools/toolchain` 子模块 = **沁恒定制版 `riscv-wch-elf` GCC 12.2.0** |
| `-march` | `rv32imc_zba_zbb_zbc_zbs_xw` ← **关键：末尾的 `xw` 扩展**，上游 riscv-gnu-toolchain 不支持 |
| `-mabi` | `ilp32` |
| 其它相关 | `-mcmodel=medany`、`-mno-save-restore`、`--param=highcode-gen-section-name=1`（配合 `.highcode` 段） |

> 用普通 `riscv64-unknown-elf-gcc` 或发行版工具链会直接报"unrecognized instruction `mcpy`"或 `-march` 不合法。见 `CMakeLists.txt` 第 44~45 行注释原文："必须是沁恒定制版：`-march` 的 xw 扩展（`mcpy` 等自定义指令）只有它支持。"

---

## 一、2.4G 协议栈（`LIB/CH572rf.h`）

### 1.1 版本与库信息

| 宏 / 符号 | 定义 | 类型 | 含义 |
|---|---|---|---|
| `VER_RF_FILE` | `"CH57x_RF_BASIC_LIB_V1.1"` | `#define` | **头文件**侧版本字符串（编译期常量） |
| `VER_RF_LIB` | `extern const uint8_t VER_RF_LIB[];` | **extern 符号**（不是宏） | **静态库**内的版本数组（链接期符号）。运行时可读它确认烧进去的 `.a` 版本与头文件是否匹配 |

> 排查"库和头文件对不上"时，读 `VER_RF_LIB` 是最直接的手段。`RFRole_BasicInit()` 之外没有任何版本校验。

### 1.2 类型与结构体

收录 7 个：`bStatus_t`、`rfRole_States_t`、`pfnRfRoleProcess`、`rfRoleConfig_t`、`TPROPERTIES_CFG`、`rfipTx_t`、`rfipRx_t`。

#### 1.2.1 `rfRoleConfig_t` —— 角色配置（唯一的入参结构）

```c
typedef void (*pfnRfRoleProcess)( rfRole_States_t status, uint8_t id );

typedef struct
{
    pfnRfRoleProcess rfProcessCB;   /* 状态回调函数指针 */
    uint32_t         processMask;    /* 关心哪些状态位（RF_STATE_* 的或）*/
} rfRoleConfig_t;
```

| 字段 | 类型 | 含义 | 注意 |
|---|---|---|---|
| `rfProcessCB` | `pfnRfRoleProcess` | 协议栈中断里回调的函数；**第一个参数是状态位集合**，第二个参数 `id` 头文件标注为保留（项目注释写"id - 保留"） | 回调**运行在中断上下文**（由 `LLE_IRQHandler`/`BB_IRQHandler` 进来），必须 `__HIGH_CODE`，**不能阻塞、不能长耗时** |
| `processMask` | `uint32_t` | 只把关心的状态位回调上来，其它位不上报 | 项目用 4 位全开：`RF_STATE_RX \| RF_STATE_RX_CRCERR \| RF_STATE_TX_FINISH \| RF_STATE_TIMEOUT` |

**回调原型**：`typedef void (*pfnRfRoleProcess)( rfRole_States_t status, uint8_t id );`

#### 1.2.2 `TPROPERTIES_CFG` —— properties 位域的**联合体视图**

```c
typedef union {
    struct {
        uint32_t whitOff        : 1;   /* 0     whitening off enable */
        uint32_t resv0          : 1;   /* 1 */
        uint32_t whitChannel    : 1;   /* 2     set the channel index of Data whitening enable */
        uint32_t resv1          : 1;   /* 3 */
        uint32_t modePHY        : 2;   /* 4-5   00-1M  01-2M  11-2.4G */
        uint32_t resv2          :10;   /* 6-15 */
        uint32_t lengthCrc      : 2;   /* 16-17 00:0 bytes, 01:1 bytes, 10:2 bytes */
        uint32_t ctlFiled       : 2;   /* 18-19 00:no control field, 01:9 bits, 10:10 bits */
        uint32_t lengthAA       : 2;   /* 20-21 Access length, 00:resv, 01:4 bytes, 10:5 bytes */
        uint32_t lengthPreamble : 2;   /* 22-23 Preamble length, 01:1 byte, 11:3 bytes */
        uint32_t dplEnable      : 1;   /* 24    0:nondpl, 1: dpl */
        uint32_t mode2G4        : 2;   /* 25-26 00: 2M, 01: 1M */
        uint32_t bitOrderData   : 1;   /* 27    0: MSB first, 1: LSB first */
        uint32_t resv3          : 1;   /* 28 */
        uint32_t resv4          : 1;   /* 29 */
        uint32_t crcXOREnable   : 1;   /* 30    the CRC result inverted enable */
        uint32_t resv5          : 1;   /* 31 */
    };
    uint32_t cfgVal;    /* ← 整个 32 位字，直接喂给 rfipTx_t.properties / rfipRx_t.properties */
} TPROPERTIES_CFG;
```

| 位域名 | 位 | 取值 | 含义 |
|---|---|---|---|
| `whitOff` | 0 | 0/1 | 0 = 开白化，1 = 关白化 |
| `resv0` | 1 | — | 保留 |
| `whitChannel` | 2 | 0/1 | 1 = 白化使用 `whiteChannel` 指定的信道号 |
| `resv1` | 3 | — | 保留 |
| `modePHY` | 5:4 | `00`=1M / `01`=2M / `11`=2.4G | **PHY 模式**。写这个字段用 `PHY_MODE_PHY_1M` / `PHY_MODE_PHY_2M` / `PHY_MODE_2G4`（它们已经是"已经左移好的位置值"） |
| `resv2` | 15:6 | — | 保留（**注意**：`rfipTx_t.properties` 的注释说 BIT[10:8] 是 `MODE_INDEX`，但本结构体**没有**对应位域，只能靠 `cfgVal` 整字写。以头文件为准） |
| `lengthCrc` | 17:16 | `00`=0 / `01`=1 / `10`=2 字节 | CRC 长度 |
| `ctlFiled` | 19:18 | `00`=无 / `01`=9 bit / `10`=10 bit | 控制字段长度（头文件原文拼写就是 `ctlFiled`，**不是** `ctrlField`） |
| `lengthAA` | 21:20 | `00`=resv / `01`=4 字节 / `10`=5 字节 | 接入地址长度 |
| `lengthPreamble` | 23:22 | `01`=1 字节 / `11`=3 字节 | 前导码长度（`00`/`10` 未定义） |
| `dplEnable` | 24 | 0/1 | 0 = 非 DPL，1 = DPL |
| `mode2G4` | 26:25 | `00`=2M / `01`=1M | **仅 2.4G PHY 模式有效**；用 `PHY_2G4_2M` / `PHY_2G4_1M` 写（**注意这两个宏的值是 `0` 和 `1`，与位域"取值"一致，不是位置值** —— 别和 `PHY_MODE_*` 混用） |
| `bitOrderData` | 27 | 0/1 | 数据位序：0 = MSB first，1 = LSB first |
| `resv3`/`resv4` | 28/29 | — | 保留（对应 `properties` 注释里的 `HEAD_BIT_ORDER`(28) 与 `AA_CRC_EN`(29)，**本结构体未暴露**） |
| `crcXOREnable` | 30 | 0/1 | 1 = CRC 结果取反 |
| `resv5` | 31 | — | 保留 |

> **陷阱**：`PHY_MODE_*`（带位偏移，供 `modePHY`/`properties` 位 5:4 用）与 `PHY_2G4_*`（裸取值 0/1，供 `mode2G4` 位 26:25 用）**两套宏语义不同**，混用会得到错误 PHY。

#### 1.2.3 `rfipTx_t` —— 发送参数（全局一份，改字段后调 `RFIP_StartTx`）

```c
typedef struct
{
    uint32_t accessAddress;     /* access address,32bit PHY address */
    uint8_t  accessAddressEx;   /* 40-bit 地址模式下的高 8 位 */
    uint32_t crcInit;           /* crc initial value */
    uint32_t frequency;         /* rf frequency (2400000kHz-2483500kHz) */
    uint32_t properties;        /* 见下方位表 */
    uint32_t txDMA;             /* Tx DMA begin address —— 发送缓冲地址 */
    uint8_t  whiteChannel;      /* whitening channel */
    uint8_t  txLen;             /* 2.4G nondpl: Tx data 长度；其它模式 resv */
    uint8_t  waitTime;          /* wait tx PLL lock time，单位 0.5us */
    int8_t   txPowerVal;        /* Tx power value，用 LL_TX_POWEER_* */
    uint32_t crcPoly;           /* crc poly value */
} rfipTx_t;
```

| 字段 | 类型 | 含义 | 单位 / 取值 | 注意 |
|---|---|---|---|---|
| `accessAddress` | `uint32_t` | 32 位接入地址 | 任意 32 位值 | 收发两端必须完全一致，否则永远收不到 |
| `accessAddressEx` | `uint8_t` | 40 位地址模式下的高 8 位 | `lengthAA = 10`（5 字节）时才有效 | 4 字节模式忽略；项目里填 `AA_EX = 0` |
| `crcInit` | `uint32_t` | CRC 初始值 | 项目 `CRC_INIT = 0xFFFF` | 收发必须一致 |
| `frequency` | `uint32_t` | 射频频点 | **kHz**，范围 2400000~2483500 kHz | 换算：`f(MHz) = 2402 + ch*2`（见 `RFIP_SingleChannel` 注释）。项目 `DEF_FREQUENCY = 17` → 2436 MHz |
| `properties` | `uint32_t` | 位配置字 | 把 `TPROPERTIES_CFG.cfgVal` 直接赋给它 | 位表见下方 |
| `txDMA` | `uint32_t` | **发送缓冲的地址**（强制 `uint32_t`，传指针要显式转换） | `(uint32_t)pBuf` | 缓冲内容 = 完整帧（本项目是 `rfPackage_t` 头 + 载荷）；**必须是有效 RAM 地址**，别传栈上已失效的局部数组 |
| `whiteChannel` | `uint8_t` | 白化信道号 | `properties.bit2 = 1` 时生效 | 项目未使用（`whitChannel` 位未置） |
| `txLen` | `uint8_t` | 发送数据长度 | **仅 2.4G 非 DPL 模式**有意义，其它模式保留 | 项目用 2M PHY，**不设此字段** |
| `waitTime` | `uint8_t` | 等 PLL 锁定时间 | **0.5 µs 计数**（微秒×2） | ⚠️ **`uint8_t`：最大 255 → 换算后最大约 127.5 µs**。切通道发送时官方注释要求不低于 80 µs（`160`）；填过大会溢出回绕 |
| `txPowerVal` | `int8_t` | 发射功率 | 用 `LL_TX_POWEER_*` 宏（这些宏的值是正的，赋给 `int8_t` 没问题） | 精度 ±2 dBm；项目用 `LL_TX_POWEER_0_DBM` (0x12) |
| `crcPoly` | `uint32_t` | CRC 多项式 | 项目 `CRC_POLY = 0x8810` | 收发必须一致 |

`properties` 位定义（头文件注释原文，**Tx 与 Rx 略有差异**）：

| 位 | Tx (`rfipTx_t`) | Rx (`rfipRx_t`) |
|---|---|---|
| bit0 | 0 = 白化开，1 = 白化关 | 同 |
| bit2 | 1 = 启用白化信道号 | 同 |
| 5:4 | `MODE_PHY`：`00`=1M / `01`=2M / `11`=2.4G | 同 |
| 10:8 | `MODE_INDEX`：`00`=0.5 / `01`=0.375 / `10`=0.3125 / `11`=0.25 | — |
| 17:16 | `CRC_LEN`：`00`=0 / `01`=1 / `10`=2 字节 | CRC length，同 |
| 19:18 | `CTL_FILED`：`00`=无 / `01`=9 bit / `10`=10 bit | Control filed，同 |
| 21:20 | `AA_LEN`：`00`=resv / `01`=4 字节 / `10`=5 字节 | Access_code，同 |
| 23:22 | `PRE_LEN`：`01`=1 字节 / `11`=3 字节 | Preamble length，同 |
| 24 | `DPL_EN`：0 = non-dpl，1 = dpl | DPL enable，同 |
| 26:25 | `MODE_2G4`：`00`=2M / `01`=1M | 2G4_MODE，同 |
| 27 | `DATA_BIT_ORDER`：0 = MSB first，1 = LSB first | 同 |
| 28 | — | `HEAD_BIT_ORDER`：0 = MSB first，1 = LSB first |
| 29 | `AA_CRC_EN`：1 = 接入地址计入 CRC | 同 |
| 30 | `CRC_XOR_EN`：1 = CRC 结果取反 | 同 |

#### 1.2.4 `rfipRx_t` —— 接收参数（全局一份，改字段后调 `RFIP_SetRx`）

```c
typedef struct
{
    uint32_t accessAddress;     /* access address,32bit PHY address */
    uint8_t  accessAddressEx;   /* 40-bit 地址模式下的高 8 位 */
    uint32_t crcInit;           /* crc initial value */
    uint32_t frequency;         /* rf frequency (2400000kHz-2483500kHz) */
    uint32_t properties;        /* 位定义见上表右列 */
    uint32_t rxDMA;             /* Rx DMA address —— 接收缓冲地址 */
    uint8_t  whiteChannel;      /* white channel(properties bit2 = 1) */
    uint8_t  rxMaxLen;          /* 2.4G nondpl: Rx 数据长度；其它模式：Rx 最大长度 */
    uint32_t timeOut;           /* Rx wait timeout, 0:No timeout 其它: N*0.5us */
    uint32_t crcPoly;           /* crc poly value */
} rfipRx_t;
```

| 字段 | 类型 | 含义 | 注意 |
|---|---|---|---|
| `accessAddress` / `accessAddressEx` / `crcInit` / `frequency` / `properties` / `crcPoly` | — | 与 `rfipTx_t` **同名同义**，收发必须成对一致 | 项目里 tx/rx 各存一份全局结构，**改一处忘另一处是最常见的"收得到但发不出去"/反之的原因** |
| `rxDMA` | `uint32_t` | 接收缓冲地址 | 项目指向 `__aligned__(4) uint8_t RxBuf[264]`；**不得小于 264 字节** |
| `whiteChannel` | `uint8_t` | 白化信道号 | `properties` bit2 = 1 时生效 |
| `rxMaxLen` | `uint8_t` | 非 DPL 模式 = 期望接收长度；其它模式 = **允许接收的最大长度** | 项目在 `RFRole_Init()` 里一次性设为 `DATA_LEN_MAX_RX`，之后不再改 |
| `timeOut` | `uint32_t` | 接收超时 | **`0` = 不超时**（会一直等）；其余为 **0.5 µs 计数** → `time_us * 2`。项目 `rf_rx_start(150)` → `timeOut = 300`。`uint32_t` 不会像 `waitTime` 那样溢出，但仍别传天文数字 |

### 1.3 函数 API（共 13 个）

返回 `bStatus_t` 的：**`0` = 成功**。

#### 1.3.1 中断转发函数（**应用侧必须自己实现同名中断服务函数**）

| 原型 | 参数 | 返回 | 作用 | 注意 |
|---|---|---|---|---|
| `void LLE_LibIRQHandler( void );` | 无 | 无 | 协议栈的 **LLE**（链路层）中断处理体 | **只能在 `LLE_IRQHandler()` 里调用**。协议栈不提供 `LLE_IRQHandler` 本体，由应用实现后转调这里 |
| `void BB_LibIRQHandler( void );` | 无 | 无 | 协议栈的 **BB**（基带）中断处理体 | 同上，在 `BB_IRQHandler()` 里调用 |

项目实现（`APP/rf.c`，两个中断服务函数都带 `__INTERRUPT` + `__HIGH_CODE`）：

```c
__INTERRUPT
__HIGH_CODE
void LLE_IRQHandler( void ) { LLE_LibIRQHandler( ); }

__INTERRUPT
__HIGH_CODE
void BB_IRQHandler( void )  { BB_LibIRQHandler( ); }
```

> **头文件中没有 `LLE_IRQHandler` / `BB_IRQHandler` 的声明** —— 它们是应用/启动侧的中断向量入口，名字必须与 `Startup/startup_CH572.S` 的向量表一致，再从里面调用 `*_LibIRQHandler()`。`RF_ProcessCallBack()` 就是由这两个中断最终回调出来的。

#### 1.3.2 角色生命周期

| 原型 | 参数 | 返回 | 作用 | 注意 |
|---|---|---|---|---|
| `bStatus_t RFRole_BasicInit( rfRoleConfig_t *pConf );` | `pConf`：角色配置（回调 + 状态掩码），**不能为 NULL** | `0` = 成功 | **基础模式初始化**：装入协议栈中断处理、注册回调与状态掩码。**上电必须先调它**，之后才能收发 | 必须在使能 `BLEB_IRQn`/`BLEL_IRQn` **之前或同时**完成；`pConf` 可以是指向局部变量的指针（协议栈只取回调与掩码，不长期持有）—— 但项目出于习惯用了一个局部 `rfRoleConfig_t conf = {0}` |
| `bStatus_t RFRole_Stop( void );` | 无 | `0` = 成功 | 停止射频（退出收发状态） | 与 `RFRole_Shut()` 的区别：`Stop` 是停当前动作，`Shut` 是**关射频电源**。两者都无需重新 `BasicInit` 即可再起收发（**以库实现为准**） |
| `bStatus_t RFRole_Shut( void );` | 无 | `0` = 成功 | **关射频（power off rf）** | ⚠️ 关闭后寄存器状态可能丢失，**唤醒/重启射频前要考虑是否需要 `RFIP_WakeUpRegInit()` / `RFIP_Calibration()`**。本项目主机在**与从机断连**时调用它（`RF_UartDongle/APP/rf_uart_rx.c` 的 `rf_disconnect()`） |

#### 1.3.3 收发（每包都要重新调）

| 原型 | 参数 | 返回 | 作用 | 注意 |
|---|---|---|---|---|
| `bStatus_t RFIP_StartTx( rfipTx_t *pParm );` | `pParm`：发送参数（通常是全局 `gTxParam`） | `0` = 成功 | **设置发送参数并启动一次发送**。发完（或超时）经回调上报 `RF_STATE_TX_FINISH` / `RF_STATE_TIMEOUT` | 头文件注释把 `@param` 误写成 "rfip rx parameter"，**以类型 `rfipTx_t*` 为准**。`txDMA` / `waitTime` 是每次发送前最常改的两个字段；**发送不是阻塞的**，必须靠回调（或状态机）判断完成 |
| `bStatus_t RFIP_SetRx( rfipRx_t *pParm );` | `pParm`：接收参数（通常是全局 `gRxParam`） | `0` = 成功 | **设置接收参数并启动一次接收**。收到包 → `RF_STATE_RX`；CRC 错 → `RF_STATE_RX_CRCERR`；超时 → `RF_STATE_TIMEOUT` | `timeOut` 含 0.5 µs 换算与"0 = 不超时"语义（见 1.2.4）。收到包后需要**再次调用**才会继续收下一包（项目就是靠状态机反复 `rf_rx_start()`） |

#### 1.3.4 校准 / 唤醒 / 测试 / 功率

| 原型 | 参数 | 返回 | 作用 | 注意 |
|---|---|---|---|---|
| `void RFIP_Calibration( void );` | 无 | 无 | 射频校准 | 一般在射频初始化阶段或温度/电压大幅变化后调用；**耗时、不可在中断里调**。本项目未调用 |
| `void RFIP_WakeUpRegInit( void );` | 无 | 无 | **睡眠唤醒后重新初始化 RFIP 寄存器** | 低功耗场景专用；如果做过 `RFRole_Shut()` + 低功耗，唤醒后应调用。本项目未调用 |
| `uint8_t RFIP_ReadCrc( void );` | 无 | CRC 状态值 | 读 CRC 状态 | 头文件 `@return` 注释写的是 "the value of crc state"。本项目未调用；正常判 CRC 用回调里的 `RF_STATE_RX_CRCERR` |
| `void RFIP_SetTxPower( uint8_t val );` | `val`：功率档位（`LL_TX_POWEER_*`） | 无 | 设置发射功率档位 | **本项目未调用**（仅在 `APP/rf.c` 顶部注释里被提到）。项目改功率走的是 `gTxParam.txPowerVal = LL_TX_POWEER_0_DBM;` 然后由 `RFIP_StartTx()` 生效 —— 两种方式不要叠加使用，二选一 |
| `bStatus_t RFIP_SingleChannel( uint8_t ch );` | `ch`：信道号 `0..39`，`f = 2402 + ch*2` MHz | `0` = 成功，`1` = phy busy | 进入**单载波（单信道）测试模式** | 生产测试/定频用。使用前要停止正常收发；`phy busy` 说明射频还在忙，需先 `RFRole_Stop()`。本项目未调用 |
| `void RFIP_TestEnd( void );` | 无 | 无 | **退出单载波测试模式** | 必须与 `RFIP_SingleChannel()` 成对使用，否则回不到正常收发。本项目未调用 |

> **头文件里不存在 `RFIP_SetTxDelayTime()`**：`APP/rf.c` 第 13 行注释提到它，但 `CH572rf.h` 中**没有这个原型**。切通道的稳定时间实际是通过 `rfipTx_t.waitTime` 表达的（`rf_tx_start(buf, 60)` → 120 = 60 µs）。**以头文件为准，不要照着注释去调这个函数。**

### 1.4 宏（共 44 个 `#define`）

> 计数不含头文件包含保护 `__CH572_RF_H`（不是对外接口）；版本符号 `VER_RF_LIB` 是 `extern` 数组，已在 §1.1 单列，不计入宏。

#### 1.4.1 状态位 `RF_STATE_*`（回调用，也是 `processMask` 的取值）

| 宏 | 值 | 含义 | 触发时机 |
|---|---|---|---|
| `RF_STATE_RX` | `(1<<0)` = 0x01 | Receive packet | 收到一包（CRC 正确） |
| `RF_STATE_TIMEOUT` | `(1<<2)` = 0x04 | Transmit timeout, or received timeout（发送超时，或接收超时 / 接入地址匹配接收超时） | 发送没等到完成，或接收等满 `timeOut` |
| `RF_STATE_RX_CRCERR` | `(1<<3)` = 0x08 | Received a packet with a CRC error | 收到包但 CRC 校验失败 |
| `RF_STATE_TX_FINISH` | `(1<<4)` = 0x10 | Transmit packet | 一包发完 |

> `(1<<1)` 在头文件里**没有定义**（位 1 空缺），别自行编号。

#### 1.4.2 PHY / 白化模式

| 宏 | 值 | 用在哪 | 含义 |
|---|---|---|---|
| `PHY_MODE_MASK` | `0x30` | `properties` 位 5:4 的掩码 | 项目里用来做 read-modify-write：`gTxParam.properties = (gTxParam.properties & ~PHY_MODE_MASK) \| (phy << 4);` |
| `PHY_MODE_PHY_1M` | `(0<<4)` = 0x00 | `properties` / `TPROPERTIES_CFG.modePHY` | 1M PHY |
| `PHY_MODE_PHY_2M` | `(1<<4)` = 0x10 | 同上 | 2M PHY（**本项目 `TEST_PHY_MODE` 就是它**） |
| `PHY_MODE_2G4` | `(3<<4)` = 0x30 | 同上 | 2.4G 私有模式（**注意值 `3<<4`，不是 `2<<4`**） |
| `PHY_2G4_1M` | `1` | `TPROPERTIES_CFG.mode2G4`（位 26:25） | 2.4G 模式下用 1M |
| `PHY_2G4_2M` | `0` | 同上 | 2.4G 模式下用 2M |
| `BB_WHITENING_OFF` | `(1<<0)` | `properties` bit0 | 白化关 |
| `BB_WHITENING_CH` | `(1<<2)` | `properties` bit2 | 启用白化信道号 |

> **`PHY_MODE_PHY_2M` 的值恰好等于 `PHY_MODE_2G4` 的位域偏移前的裸值 `1`**，别把 `1` 直接写进 `mode2G4` 就以为设了 2M PHY —— 位域不同、语义不同。

#### 1.4.3 发射功率 `LL_TX_POWEER_*`（**头文件原拼写是 `POWEER`，三个 E，别"修正"成 POWER**）

`tfxPowerVal` 直接填这些宏，精度标注 **±2 dBm**：

| 宏 | 值 | 宏 | 值 |
|---|---|---|---|
| `LL_TX_POWEER_MINUS_25_DBM` | `0x01` | `LL_TX_POWEER_0_DBM` | `0x12` |
| `LL_TX_POWEER_MINUS_20_DBM` | `0x02` | `LL_TX_POWEER_1_DBM` | `0x15` |
| `LL_TX_POWEER_MINUS_15_DBM` | `0x03` | `LL_TX_POWEER_2_DBM` | `0x18` |
| `LL_TX_POWEER_MINUS_10_DBM` | `0x05` | `LL_TX_POWEER_3_DBM` | `0x1B` |
| `LL_TX_POWEER_MINUS_8_DBM` | `0x07` | `LL_TX_POWEER_4_DBM` | `0x1F` |
| `LL_TX_POWEER_MINUS_5_DBM` | `0x0A` | `LL_TX_POWEER_5_DBM` | `0x25` |
| `LL_TX_POWEER_MINUS_3_DBM` | `0x0C` | `LL_TX_POWEER_6_DBM` | `0x2D` |
| `LL_TX_POWEER_MINUS_1_DBM` | `0x10` | `LL_TX_POWEER_7_DBM` | `0x3B` |

> 注意：宏名写的是 `MINUS_*`（不含 `_DASH_`），且档位**不是等差**（寄存器值非线性）。**没有 `LL_TX_POWEER_8_DBM`**，最大档是 `7_DBM`。项目用 `LL_TX_POWEER_0_DBM`。

#### 1.4.4 长度

| 宏 | 值 | 含义 |
|---|---|---|
| `RF_MAX_DATA_LEN` | `251` | 协议栈支持的**单包最大数据长度**。项目实际另有更小的应用层上限（`DATA_LEN_MAX_TX` / `DATA_LEN_MAX_RX`），见 [app-api.md](./app-api.md) |

#### 1.4.5 字节 / 位操作工具宏

| 宏 | 定义 | 作用 | 注意 |
|---|---|---|---|
| `BREAK_UINT32( var, ByteNum )` | `(uint8_t)((uint32_t)(((var) >>((ByteNum) * 8)) & 0x00FF))` | 取 `uint32_t` 的第 `ByteNum`（0~3）个字节 | 参数会被求值多次，**别传带副作用的表达式** |
| `HI_UINT16( a )` | `(((a) >> 8) & 0xFF)` | 取 16 位的高 8 位 | 与下一条配套处理 16 位接入地址 |
| `LO_UINT16( a )` | `((a) & 0xFF)` | 取 16 位的低 8 位 | |
| `HI_UINT8( a )` | `(((a) >> 4) & 0x0F)` | 取 8 位的**高 4 位** | ⚠️ 名字叫 UINT8 但语义是"取半字节"，容易误用 |
| `LO_UINT8( a )` | `((a) & 0x0F)` | 取 8 位的**低 4 位** | 同上 |
| `BUILD_UINT32( Byte0, Byte1, Byte2, Byte3 )` | 四个字节按小端拼成 `uint32_t` | 组 32 位 | |
| `BUILD_UINT16( loByte, hiByte )` | `((uint16_t)(((loByte) & 0x00FF) \| (((hiByte) & 0x00FF)<<8)))` | 组 16 位 | **参数顺序是 lo 在前** |
| `ACTIVE_LOW` | `!` | 低有效取反 | 仅取反，不做布尔化 |
| `ACTIVE_HIGH` | `!!` | 高有效布尔化 | 双重取反强制为 0/1 |
| `BV( n )` | `(1 << (n))` | 位掩码 | 有 `#ifndef` 保护 |
| `BF( x, b, s )` | `(((x) & (b)) >> (s))` | 取位域 | 有 `#ifndef` 保护 |
| `MIN( n, m )` | `(((n) < (m)) ? (n) : (m))` | 取小 | 有 `#ifndef` 保护；**宏，参数重复求值** |
| `MAX( n, m )` | `(((n) < (m)) ? (m) : (n))` | 取大 | 同上 |
| `ABS( n )` | `(((n) < 0) ? -(n) : (n))` | 取绝对值 | 同上；对 `INT_MIN` 未做保护 |

> 这些宏大多带 `#ifndef` 保护（`BV`/`BF`/`MIN`/`MAX`/`ABS`），说明**别的头文件可能已定义过同名宏**。若出现某个宏行为与本文不符，先查是否有别的头抢先定义（**以头文件为准**）。

### 1.5 本项目实际用到的（来自 `APP/rf.c` + 主机 `APP/rf_uart_rx.c`）

#### 1.5.1 初始化：`RFRole_BasicInit(&conf)`

```c
void RFRole_Init(void)
{
    {
        rfRoleConfig_t conf = {0};
        conf.rfProcessCB  = RF_ProcessCallBack;
        conf.processMask  = RF_STATE_RX | RF_STATE_RX_CRCERR
                          | RF_STATE_TX_FINISH | RF_STATE_TIMEOUT;
        RFRole_BasicInit( &conf );          /* ← 4 个状态位全开 */
    }
    TPROPERTIES_CFG Properties;
    {
        Properties.cfgVal = TEST_PHY_MODE;   /* 注意：不是 0，直接就是 PHY 位值 */
        /* ... 见下 §1.5.4 的编译条件说明 ... */
    }
    /* gTxParam / gRxParam 全局参数预置 */
    pCbs = NULL;
    PFIC_EnableIRQ( BLEB_IRQn );   /* ← 20，协议栈 BB 中断 */
    PFIC_EnableIRQ( BLEL_IRQn );   /* ← 21，协议栈 LLE 中断 */
}
```

要点：

| 项 | 说明 |
|---|---|
| `conf` 是**局部变量** + `= {0}` | 协议栈在 `BasicInit` 内取走回调与掩码，不长期持有该指针 —— 所以项目这样写是安全的 |
| `processMask` 只开 4 位 | `RF_STATE_RX`(0x01) \| `RF_STATE_RX_CRCERR`(0x08) \| `RF_STATE_TX_FINISH`(0x10) \| `RF_STATE_TIMEOUT`(0x04) = `0x1D`。**想收的每种事件都得在掩码里开，否则回调不报** |
| 中断号 | `BLEB_IRQn = 20`、`BLEL_IRQn = 21`（定义在 `StdPeriphDriver/inc/CH572SFR.h`，**不在这两个头文件里**）。**不开中断则回调永远不来**，表现就是"发出去了但状态机卡住" |

#### 1.5.2 回调分发：`RF_ProcessCallBack()`

```c
__HIGH_CODE
void RF_ProcessCallBack( rfRole_States_t sta, uint8_t id )
{
    if( sta & RF_STATE_RX )         { if(pCbs && pCbs->pfnRxCB)       pCbs->pfnRxCB( (rfPackage_t *)RxBuf ); }
    if( sta & RF_STATE_RX_CRCERR )  { if(pCbs && pCbs->pfnCrcErrCB)   pCbs->pfnCrcErrCB( ); }
    if( sta & RF_STATE_TX_FINISH )  { if(pCbs && pCbs->pfnTxCB)       pCbs->pfnTxCB( ); }
    if( sta & RF_STATE_TIMEOUT )    { if(pCbs && pCbs->pfnTimeoutCB)  pCbs->pfnTimeoutCB( ); }
}
```

要点：

| 项 | 说明 |
|---|---|
| 用 `if` 而非 `switch` | `sta` 是**位集合**，多个状态位可能同时置位，所以四个 `if` 会依次判（不是 `else if`）—— 照抄这个结构，别改成 `switch` |
| `RxBuf` 强转 `rfPackage_t *` | 收到的裸数据直接按项目的 4 字节包头解释，见 [protocol.md](./protocol.md) |
| `pCbs` 空指针检查 | 业务回调由 `RFRole_RegisterStatusCbs()` 注册，注册前为 `NULL` —— **这个判空不能删** |
| `__HIGH_CODE` | 回调在中断上下文执行，必须放 RAM 段（见 §2.7） |

#### 1.5.3 发送 / 接收调用链

```c
/* 发送：rf_tx_start(buf, 60) → waitTime = 120（= 60us × 2） */
void rf_tx_start( void *pBuf, uint16_t rfon_us )
{
    gTxParam.txDMA    = (uint32_t)pBuf;
    gTxParam.waitTime = rfon_us*2;
    RFIP_StartTx( &gTxParam );
}

/* 接收：rf_rx_start(150) → timeOut = 300（= 150us × 2），从机用 150us */
void rf_rx_start( uint32_t rx_us )
{
    gRxParam.timeOut = rx_us*2;
    RFIP_SetRx( &gRxParam );
}
```

项目内的调用点：

| 位置 | 调用 | 说明 |
|---|---|---|
| `APP/rf_uart_tx.c:414/440/453/468` | `rf_tx_start( gTxBuf.TxBuf, 60 )` | 从机发数据/应答/ACK 全部 60 µs 前置等待 |
| `APP/rf_uart_tx.c:324` | `rf_rx_start( 150 )` | 从机开 150 µs 接收窗口 |
| 主机 `APP/rf_uart_rx.c` | `rf_rx_start( gIntervalTimer )` | 主机窗口由连接间隔换算（见 [app-api.md](./app-api.md)） |
| `APP/rf_uart_tx.c:117/120` | `rf_tx_set_phy_type()` / `rf_rx_set_phy_type()` | 绑定后按 `bound_rsp_t.phy` 改 PHY —— 内部就是 `(properties & ~PHY_MODE_MASK) \| (phy<<4)` |

#### 1.5.4 全局参数预置（`RFRole_Init()` 内）

```c
gTxParam.accessAddress   = AA;                 /* 0x94826E8E（未绑定时） */
gTxParam.accessAddressEx = AA_EX;              /* 0 */
gTxParam.frequency       = DEF_FREQUENCY;      /* 17 → 2436 MHz */
gTxParam.crcInit         = CRC_INIT;           /* 0xFFFF */
gTxParam.crcPoly         = CRC_POLY;           /* 0x8810 */
gTxParam.properties      = Properties.cfgVal;
gTxParam.waitTime        = 80*2;               /* 80us ≥ 官方要求的通道切换稳定时间 */
gTxParam.txPowerVal      = LL_TX_POWEER_0_DBM; /* 0x12 → 0 dBm */

gRxParam.accessAddress   = AA;
gRxParam.accessAddressEx = AA_EX;
gRxParam.frequency       = DEF_FREQUENCY;
gRxParam.crcInit         = CRC_INIT;
gRxParam.crcPoly         = CRC_POLY;
gRxParam.properties      = Properties.cfgVal;
gRxParam.rxDMA           = (uint32_t)RxBuf;       /* __aligned__(4) uint8_t RxBuf[264] */
gRxParam.rxMaxLen        = DATA_LEN_MAX_RX;
```

**关于 `TPROPERTIES_CFG` 在本项目的实际生效情况（容易看漏）**：

`APP/include/rf.h` 里 `TEST_PHY_MODE` 定义为 **`PHY_MODE_PHY_2M`**，而 `rf.c` 中那些 `whitOff / lengthCrc / ctlFiled / lengthAA / lengthPreamble / dplEnable / mode2G4 / bitOrderData / crcXOREnable` 的赋值被包在

```c
#if (TEST_PHY_MODE == PHY_MODE_2G4)
    ...
#endif
```

里。因此**当前工程编译时那段 2.4G 位域赋值是不生效的**，`Properties.cfgVal` 就等于 `PHY_MODE_PHY_2M` = `0x10`（`modePHY = 01`，即 2M PHY）。
`rf.h` 里为 2.4G 模式准备的那批宏（`CRC_LEN=2`/`CTL_FILED=0`/`AA_LEN=1`/`PRE_LEN=1`/`DPL_EN=0`/`MODE_2G4=PHY_2G4_1M`/`DATA_ORDER=0`/`CRC_XOR_EN=0`）**只在把 `TEST_PHY_MODE` 改成 `PHY_MODE_2G4` 后才会被用上**。

> 改 PHY 时**收发两端必须一起改**：`TEST_PHY_MODE`、`AA`、`DEF_FREQUENCY`、`CRC_INIT/CRC_POLY` 任何一项两端不一致 → 表现为"完全收不到"或"一直 CRC 错"。

#### 1.5.5 `RFRole_Shut()`：主机断连时关射频

调用点在**主机** `RF_UartDongle/APP/rf_uart_rx.c` 的 `rf_disconnect()`（第 120 行）：

```c
static void rf_disconnect( void )
{
    RF_bound_Flag = 0;
    PRINT("disconnect.\n" );
    RFRole_Shut( );                                  /* ← 关射频 */
    gBoundStatus  = BOUND_STATUS_IDLE;
    gRxDataStatus = DATA_STATUS_START;
    TMR_ITCfg(DISABLE, TMR_IT_CYC_END);              /* 关定时器中断 */
    rf_tx_set_sync_word( AA );    rf_tx_set_frequency( DEF_FREQUENCY );
    rf_rx_set_sync_word( AA );    rf_rx_set_frequency( DEF_FREQUENCY );
    gRfStatus   = RF_STATUS_WAIT;
    gServerData = 0;
}
```

要点：

| 项 | 说明 |
|---|---|
| 关射频的时机 | 主机判定与从机失联后，**先 `RFRole_Shut()`，再把收发参数复位回广播值**，然后重新进入 `RF_STATUS_WAIT` 找设备 |
| 从机侧 | 抽出的 `RF_Uart/APP/*.c` 中**未检索到** `RFRole_Shut()` 调用；从机没有"关射频"这个动作 |
| `RFRole_Stop()` | 全仓库（含 `APP/`）**未检索到调用点**，仅头文件有声明 |

### 1.6 协议栈陷阱清单

| # | 陷阱 | 后果 | 规避 |
|---|---|---|---|
| 1 | `waitTime` / `timeOut` 忘了 ×2 | 等待/超时窗口只有期望的一半 | 直接调 `RFIP_StartTx/SetRx` 时记得 ×2；走 `rf_tx_start()/rf_rx_start()` 则**传微秒** |
| 2 | `waitTime` 是 `uint8_t` | 传 >127 µs 的值溢出，PLL 没锁定就发 | 保持 ≤ 127 µs；切通道用 80 µs（`160`） |
| 3 | `RxBuf` 小于 264 字节 | DMA 越界写，随机崩溃 | 保持 `RxBuf[264]` + `__aligned__(4)` |
| 4 | `processMask` 少开某位 | 该事件回调永远不来（最典型：忘了 `RF_STATE_TIMEOUT` → 状态机永久卡住） | 4 位全开，或明确知道自己不需要哪一位 |
| 5 | 忘了 `PFIC_EnableIRQ(BLEB_IRQn/BLEL_IRQn)` | 完全无回调，收发都不上报 | 在 `RFRole_BasicInit()` 之后使能两个中断 |
| 6 | 回调里做耗时操作 | 中断延迟，收发窗口错过 | 回调只置标志/派发，重活丢到主循环 |
| 7 | 只改 `gTxParam` 忘了 `gRxParam`（或反之） | 单向不通 / 一直 CRC 错 | 接入地址、频点、PHY、CRC 成对设置（参考 `rf_disconnect()` 的 6 行连写） |
| 8 | `PHY_MODE_*` 与 `PHY_2G4_*` 混用 | PHY 设错 | `modePHY` 用 `PHY_MODE_*`；`mode2G4` 用 `PHY_2G4_*` |
| 9 | `txPowerVal` 的宏名 `POWEER` 拼错 | 编译不过 | 照抄 `LL_TX_POWEER_*`（三个 E） |
| 10 | 照着 `rf.c` 注释找 `RFIP_SetTxDelayTime()` | 编译/链接失败 | 该函数**在头文件里不存在**，用 `gTxParam.waitTime` |
| 11 | 改头文件里的结构体/字段 | `libCH57xRF.a` 是按旧布局编译的 → 参数错位、行为诡异 | **头文件与库必须整体成套**，只读不改 |

---

## 二、内核抽象层（`RVMSIS/core_riscv.h`）

> 全部是 `static inline` 或宏，**没有对应的 `.c`**，编译进调用方。这层是官方 RVMSIS，**不要修改**。

### 2.1 基础宏与类型

| 宏 / 类型 | 定义 | 作用 |
|---|---|---|
| `__ASM` | `__asm`（GCC 分支 `#define __ASM __asm`） | 内联汇编关键字。编译器自适应：`__CC_ARM` / `__ICCARM__` / `__GNUC__` / `__TASKING__` 各有一条分支，**本项目走 `__GNUC__`** |
| `__INLINE` | `inline`（GCC 分支） | 内联关键字（同上一套编译器分支） |
| `__I` | C 下 `volatile const`，C++ 下 `volatile` | "只读"权限标注（寄存器 `__I` 字段） |
| `__O` | `volatile` | "只写"权限标注 |
| `__IO` | `volatile` | "读写"权限标注 |
| `RV_STATIC_INLINE` | `static inline` | 本头文件所有内联函数的统一前缀。**注意：它不带 `__attribute__((always_inline))`**，多数函数额外单独写了 `always_inline` |
| `__nop()` | `__asm__ volatile("nop")` | 空操作，用于精确延时/占位 |
| `FunctionalState` | `enum { DISABLE = 0, ENABLE = !DISABLE }` | 使能/禁止 |
| `FlagStatus`, `ITStatus` | `enum { RESET = 0, SET = !RESET }` | 标志/中断状态（同一行 `typedef` 出两个名字） |
| `PFIC` | `((PFIC_Type *)0xE000E000)` | PFIC 中断控制器寄存器基址 |
| `SysTick` | `((SysTick_Type *)0xE000F000)` | SysTick 寄存器基址 |
| `PFIC_KEY1` | `((uint32_t)0xFA050000)` | 解锁 key1 |
| `PFIC_KEY2` | `((uint32_t)0xBCAF0000)` | 解锁 key2 |
| `PFIC_KEY3` | `((uint32_t)0xBEEF0000)` | 解锁 key3 |

> `PFIC_KEY1/2/3` 是沁恒的清/置位保护 key，**本头文件里没有任何函数用到它们** —— 需要动受保护寄存器时要按官方例程的固定顺序写，**不要凭猜**。

#### 两个寄存器映射结构体

| 类型 | 成员 | 作用 |
|---|---|---|
| `PFIC_Type` | `ISR[8]`(只读)、`IPR[8]`(只读)、`ITHRESDR`、`GISR`(只读)、`VTFIDR[4]`、`VTFADDR[4]`、`IENR[8]`(只写)、`IRER[8]`(只写)、`IPSR[8]`(只写)、`IPRR[8]`(只写)、`IACTR[8]`、`IPRIOR[256]`、`SCTLR` | PFIC 中断控制器寄存器组，`__I`/`__O` 标注了访问方向（**写只读寄存器无意义，读只写寄存器无意义**）。`IENR`/`IRER`/`IPSR`/`IPRR` 各有 8 个字 = 最多 256 个中断号，按 `IRQn>>5` 选字、`IRQn&0x1F` 选位 |
| `SysTick_Type` | `CTLR`、`SR`、`CNT`/`CNTL`（联合）、`CMP`/`CMPL`（联合） | SysTick 寄存器组。`CNT` 与 `CNTL` 是**同一地址的两个名字**（低 32 位），`CMP` 与 `CMPL` 同理 |

### 2.2 CSR 读写

#### 2.2.1 通用宏（两个）

| 宏 | 原型 / 展开 | 参数 | 返回 | 作用 | 注意 |
|---|---|---|---|---|---|
| `read_csr(reg)` | `({unsigned long __tmp; __asm__ volatile ("csrr %0, " #reg : "=r"(__tmp)); __tmp; })` | `reg`：**CSR 寄存器名本身**（不是字符串），如 `read_csr(mstatus)` | 读到的值 | 读任意 CSR | 用 GCC 语句表达式 + 字符串化 `#reg`，**只能在 GCC 下用**；名字必须是汇编器认识的 CSR 名 |
| `write_csr(reg, val)` | 常量且 `< 32` 时用立即数形式 `csrw reg, imm`，否则用寄存器形式 `csrw reg, %0` | `reg`：CSR 名；`val`：要写的值 | 无 | 写任意 CSR | `__builtin_constant_p` 优化：小常量走立即数编码。**没有读改写语义，会把整个 CSR 覆盖掉** |

#### 2.2.2 已封装好的 CSR 存取函数（`__get_*` / `__set_*`，共 18 个）

全部签名形如 `RV_STATIC_INLINE uint32_t __get_<CSR名>(void)` / `RV_STATIC_INLINE void __set_<CSR名>(uint32_t value)`（`<CSR名>` 就是下表里的 CSR 寄存器名），均带 `__attribute__((always_inline))`。

| 函数对 | CSR 名 | 作用 | 注意 |
|---|---|---|---|
| `__get_MSTATUS()` / `__set_MSTATUS(uint32_t)` | `mstatus` | 机器状态寄存器（全局中断使能位 MIE 在自己的位域里） | **改它等于动全局中断**，通常用下面的 `PFIC_EnableAllIRQ()` 而不是自己拼位 |
| `__get_MISA()` / `__set_MISA(uint32_t)` | `misa` | 机器 ISA 寄存器 | 有 `__set_MISA` 但**几乎不该改**，改错会破坏指令集扩展（含 `xw`） |
| `__get_MTVEC()` / `__set_MTVEC(uint32_t)` | `mtvec` | 异常/中断向量基址 | 启动代码设置；**应用不要碰** |
| `__get_MSCRATCH()` / `__set_MSCRATCH(uint32_t)` | `mscratch` | 机器暂存寄存器 | 通常给 trap 处理程序存上下文指针 |
| `__get_MEPC()` / `__set_MEPC(uint32_t)` | `mepc` | 异常程序计数器（出错/中断时的 PC） | 异常处理里用于定位与恢复 |
| `__get_MCAUSE()` / `__set_MCAUSE(uint32_t)` | `mcause` | 异常原因 | **头文件里 `__set_MCAUSE` 的 Doxygen 注释标题错写成 `__set_MEPC`**、`@brief` 也只写 "Set the Machine Cause Register" —— 以函数名为准 |
| `__get_MTVAL()` / `__set_MTVAL(uint32_t)` | `mtval` | 异常附加信息（出错地址等） | |
| `__get_MVENDORID()` | `mvendorid` | 厂商 ID | **只有 get，没有 set** |
| `__get_MARCHID()` | `marchid` | 架构 ID | **只有 get** |
| `__get_MIMPID()` | `mimpid` | 实现 ID | **只有 get** |
| `__get_MHARTID()` | `mhartid` | 硬件线程 ID | **只有 get** |
| `__get_SP()` | `sp`（`mv %0, sp`） | 读当前栈指针 | **只有 get**；用 `mv` 而非 `csrr`（`sp` 不是 CSR）。调试/栈溢出排查用 |

> **只有 get 的 5 个**：`__get_MVENDORID` / `__get_MARCHID` / `__get_MIMPID` / `__get_MHARTID` / `__get_SP` —— 头文件里**没有**对应的 `__set_*`，别照着规律猜。

### 2.3 中断控制（PFIC）

#### 2.3.1 全局开/关（宏，两个）

| 宏 | 展开 | 作用 | 注意 |
|---|---|---|---|
| `PFIC_EnableAllIRQ()` | `{write_csr(0x800, 0x88);}` | 开全局中断（`mstatus` 的 `MIE` 等位一起置） | 直接把 `mstatus` 写成 `0x88`，**不是读改写** —— 会覆盖掉 `mstatus` 里的其它位 |
| `PFIC_DisableAllIRQ()` | `{write_csr(0x800, 0x80); asm volatile("fence.i");}` | 关全局中断 + `fence.i` 同步指令流 | 同样是整字写；**返回前有 `fence.i`**，因为改中断使能会影响取指/I-cache 一致性 |

#### 2.3.2 需要保存/恢复现场时用这两个函数（**能返回旧状态，可嵌套**）

| 原型 | 参数 | 返回 | 作用 | 注意 |
|---|---|---|---|---|
| `RV_STATIC_INLINE uint32_t __risc_v_disable_irq(void)` | 无 | **被清掉之前的 `mpie`/`mie` 位**（`result & 0x88`） | 关全局中断，**并把旧状态返回给你** | 用 `csrrc`（清位）而非整字写，**不会破坏 `mstatus` 其它位**。临界区应该用它，而不是 `PFIC_DisableAllIRQ()` |
| `RV_STATIC_INLINE uint32_t __risc_v_enable_irq(uint32_t mpie_mie)` | `mpie_mie`：`__risc_v_disable_irq()` 返回的旧值 | 操作前的 CSR 原值 | 恢复全局中断 | 用 `csrrs`（置位）。**典型用法**：`uint32_t s = __risc_v_disable_irq(); /*临界区*/ __risc_v_enable_irq(s);` |

> 这两个是**唯一一对可以安全嵌套/恢复的开关中断接口**。项目里也可以用（`core_riscv.h` 已全局可见）。

#### 2.3.3 单个中断号操作（共 8 个）

| 原型 | 参数 | 返回 | 作用 | 注意 |
|---|---|---|---|---|
| `void PFIC_EnableIRQ(IRQn_Type IRQn)` | `IRQn`：中断号 | 无 | **使能**指定中断 | 写 `PFIC->IENR[IRQn>>5] = 1<<(IRQn&0x1F)`。**本项目用它开 `BLEB_IRQn`(20) / `BLEL_IRQn`(21)**，不开就收不到 RF 回调 |
| `void PFIC_DisableIRQ(IRQn_Type IRQn)` | 同上 | 无 | **禁止**指定中断 | 内部带 `asm volatile("fence.i")` |
| `uint32_t PFIC_GetStatusIRQ(IRQn_Type IRQn)` | 同上 | `1` = 已使能，`0` = 未使能 | 查该中断的**使能**状态 | 读 `ISR[]`（只读寄存器） |
| `uint32_t PFIC_GetPendingIRQ(IRQn_Type IRQn)` | 同上 | `1` = 有挂起，`0` = 无 | 查该中断**是否挂起**（已触发待处理） | 读 `IPR[]`。调试"中断没进来"时先查它 |
| `void PFIC_SetPendingIRQ(IRQn_Type IRQn)` | 同上 | 无 | **软件置挂起位**（手动触发一次中断） | 写 `IPSR[]`。用它可以软件模拟一次中断；**别在中断里对自己置位**（会立刻重进） |
| `void PFIC_ClearPendingIRQ(IRQn_Type IRQn)` | 同上 | 无 | 清该中断的挂起位 | 写 `IPRR[]`。边沿触发中断漏处理时的补救手段 |
| `uint32_t PFIC_GetActive(IRQn_Type IRQn)` | 同上 | `1` = 正在执行，`0` = 未执行 | 查该中断**是否正在被处理** | 读 `IACTR[]`。可用于防重入判断 |
| `void PFIC_SetPriority(IRQn_Type IRQn, uint8_t priority)` | `IRQn`：中断号；`priority`：注释写 "bit7: pre-emption priority" | 无 | 设置中断优先级 | ⚠️ **实现是 `PFIC->IPRIOR[IRQn] = priority ? 0x80 : 0;`** —— 即**只区分"非 0 优先级 / 0 优先级"两档**，传入的具体数值被丢弃。别以为能设 0~255 的细腻优先级；**以头文件实现为准** |

#### 2.3.4 向量中断 VTF

| 原型 | 参数 | 返回 | 作用 | 注意 |
|---|---|---|---|---|
| `void SetVTFIRQ(uint32_t addr, IRQn_Type IRQn, uint8_t num, FunctionalState NewState)` | `addr`：VTF 中断服务函数**基地址**；`IRQn`：中断号；`num`：**VTF 中断编号，只支持 0~3**；`NewState`：`ENABLE`/`DISABLE` | 无 | 把某个中断号绑定到 4 个 VTF 向量槽之一，实现**免查表的直达中断** | `num > 3` **直接 `return`，静默失败**（不报错！）。`addr` 会 `& 0xFFFFFFFE`（最低位留给使能位）：`ENABLE` 时末位置 1，`DISABLE` 时清 0。**本项目未使用** |

### 2.4 事件 / 睡眠 / 复位

| 原型 | 参数 | 返回 | 作用 | 注意 |
|---|---|---|---|---|
| `void _SEV(void)` | 无 | 无 | 置事件（Send Event）：`PFIC->SCTLR \|= (1<<3) \| (1<<5)` | 与 `_WFE` 配合；单独用无意义 |
| `void _WFE(void)` | 无 | 无 | 置位后执行 `wfi`，等事件唤醒 | 先 `SCTLR \|= (1<<3)` 再 `wfi` |
| `void __WFE(void)` | 无 | 无 | `_SEV(); _WFE(); _WFE();` —— **等事件**（含"先发一次事件防止立刻睡死"的竞态处理） | 唤醒条件是**事件**，不是中断 |
| `void __WFI(void)` | 无 | 无 | 清 `SCTLR` 的 bit3 后执行 `wfi` —— **等中断** | 与 `__WFE` 的唤醒条件不同：`__WFI` 靠中断唤醒。**睡觉前确认有中断能叫醒你**（最经典的是 SysTick 或 UART/RF 中断），否则真睡死 |
| `void PFIC_SystemReset(void)` | 无 | 无 | **发起系统复位**：`PFIC->SCTLR = 0x80000000;` | 整字写 `SCTLR`（会丢掉其它位）。**函数不返回**，后面的代码不会执行 —— 需要落盘/存 Flash 的话必须放在它前面 |

> 低功耗唤醒后 **RF 寄存器可能需要重新初始化** → 见 `RFIP_WakeUpRegInit()`（§1.3.4）。本项目从机有自适应主频/时钟相关处理，涉及睡眠时要连起来看 [app-api.md](./app-api.md)。

### 2.5 xw 扩展内联函数（`__MCPY`）

| 原型 | 参数 | 返回 | 作用 | 注意 |
|---|---|---|---|---|
| `void __MCPY(void *dst, void *start, void *end)` | `dst`：目标地址；`start`：源起始；`end`：源**结束**地址 | 无 | **一条指令的快速内存拷贝**：拷贝范围是 `[start, end)`，长度 = `end - start` | 内联汇编：`__asm volatile("mcpy %2, %0, %1" : "+r"(start), "+r"(dst) : "r"(end) : "memory")`。**三个参数都是指针，"长度"要用 `start + len` 表达**，不是传字节数！`"memory"` clobber 保证不被打乱顺序。**必须用沁恒定制的 `riscv-wch-elf` GCC 且 `-march` 带 `xw`**（见 §0.4） |

**关于用户可能预期的 `__SMC()`：本头文件中不存在，本仓库（含 `LIB/`、`RVMSIS/`、`StdPeriphDriver/`、`APP/`、`Profile/`）全仓库检索也没有 `__SMC` 符号。**
`core_riscv.h` 的 xw 自定义指令封装**只有 `__MCPY()` 这一个**（另外 `read_csr`/`write_csr` 用的是标准 `csrr`/`csrw`，不属于 xw 扩展）。若需要使用其它 xw 指令，**以沁恒官方最新头文件为准**，不要自行拼内联汇编。

> 顺带：`mcpy` 也是"为什么不能用发行版 RISC-V 工具链"的根因 —— 官方 `libCH57xRF.a` 内部同样包含 `mcpy`，上游 GCC 不认这条指令，会报 `unrecognized instruction`。

### 2.6 SysTick

| 宏 | 值 | 作用 |
|---|---|---|
| `SysTick_SR_SWIE` | `(1 << 31)` | 状态寄存器：软件中断使能 |
| `SysTick_SR_CNTIF` | `(1 << 0)` | 状态寄存器：计数中断标志 |
| `SysTick_LOAD_RELOAD_Msk` | `(0xFFFFFFFF)` | 重载值上限掩码 |
| `SysTick_CTLR_MODE` | `(1 << 4)` | 计数模式（周期/单次） |
| `SysTick_CTLR_STRE` | `(1 << 3)` | 自动重载使能 |
| `SysTick_CTLR_STCLK` | `(1 << 2)` | 时钟源选择 |
| `SysTick_CTLR_STIE` | `(1 << 1)` | 计数中断使能 |
| `SysTick_CTLR_STE` | `(1 << 0)` | 计数器使能 |

| 原型 | 参数 | 返回 | 作用 | 注意 |
|---|---|---|---|---|
| `RV_STATIC_INLINE uint32_t SysTick_Config(uint32_t ticks)` | `ticks`：重载值（**计数值，不是时间**） | `0` = 成功；`1` = 重载值不可用（`ticks-1 > 0xFFFFFFFF`） | 配置 SysTick 并**使能 `SysTick_IRQn`**，然后启动（`STRE \| STCLK \| STIE \| STE`） | **会调用 `PFIC_EnableIRQ(SysTick_IRQn)`** —— 它是唯一一个自带中断使能的内联函数。**注意它没带 `__attribute__((always_inline))`**，只有 `RV_STATIC_INLINE` |
| `RV_STATIC_INLINE uint32_t __SysTick_Config(uint32_t ticks)` | 同上 | 同上 | **与 `SysTick_Config()` 唯一区别：不使能 `SysTick_IRQn`**，只配置并启动定时器 | 想自己控制中断使能时机（比如先在别处 `PFIC_EnableIRQ`）时用它 |

> 两者都会把 `CTLR` **整字覆盖**为 `STRE|STCLK|STIE|STE`（丢弃 `MODE` 等其它位）。计数值换算要乘以时钟频率：`ticks = 期望秒数 × FREQ_SYS`（`FREQ_SYS` 默认 `100000000`，定义在 `CH57x_common.h`；本项目从机主频是**运行时自适应**的，换算前先确认当前主频）。
> 本项目的中断服务函数名是 `SysTick_Handler()`（**不在本头文件里**，由应用实现，需 `__INTERRUPT`）。

### 2.7 属性宏 `__HIGH_CODE` / `__INTERRUPT` 与 `.highcode` 段

> ⚠️ **这两个宏不在 `core_riscv.h` 里。** 它们定义在 `StdPeriphDriver/inc/CH57x_common.h`（同一个官方 SDK，属于本文补充说明部分）：

```c
#ifndef __HIGH_CODE
#define __HIGH_CODE   __attribute__((section(".highcode")))
#endif

#ifndef __INTERRUPT
#ifdef INT_SOFT
#define __INTERRUPT   __attribute__((interrupt()))
#else
#define __INTERRUPT   __attribute__((interrupt("WCH-Interrupt-fast")))
#endif
#endif
```

| 宏 | 定义 | 作用 | 注意 |
|---|---|---|---|
| `__HIGH_CODE` | `__attribute__((section(".highcode")))` | 把函数放进 **`.highcode` 段**（VMA 在 RAM） | 用在**时序敏感**函数上：RF/UART 中断服务、收发处理、缓冲读写（项目里 `rf.c` 几乎每个函数都标了）。**删掉它 → 函数回到 Flash 执行，中断响应变慢，可能丢包/丢字节** |
| `__INTERRUPT` | 默认 `__attribute__((interrupt("WCH-Interrupt-fast")))`；定义 `INT_SOFT` 宏时为 `__attribute__((interrupt()))` | 声明为中断服务函数，让编译器生成**保存/恢复现场 + `mret`** 的正确代码 | 必须和启动文件向量表里的名字一致。`WCH-Interrupt-fast` 是沁恒的硬件压栈（HPE）快速中断模式，**比软件压栈快，但要求中断函数只用被硬件保存的寄存器（写复杂函数时要注意）**。项目未定义 `INT_SOFT`，用的是 fast 模式 |

#### `.highcode` 段的意义（VMA 在 RAM，LMA 在 Flash，启动时拷贝）

| 项 | 事实 |
|---|---|
| 链接脚本 | `RF_Uart/Ld/Link.ld`（主机同结构）：`.highcode : { ... *(.vector); *(.highcode); *(.highcode.*); } >RAM AT>FLASH` |
| VMA / LMA | **VMA 在 RAM**（函数按 RAM 地址链接、按 RAM 地址跳转执行）；**LMA 在 Flash**（内容存放处） |
| 符号 | `_highcode_lma`（Flash 侧源地址）、`_highcode_vma_start` / `_highcode_vma_end`（RAM 侧起止） |
| 拷贝时机 | `Startup/startup_CH572.S` 复位流程里，**在 C 运行环境建立过程中**、`data` 段搬运之前：`la a0,_highcode_lma; la a1,_highcode_vma_start; la a2,_highcode_vma_end;` 然后按 4 字节 `lw`/`sw` 循环搬完 |
| 为什么必须放 RAM | Flash 取指在这个内核上**比 RAM 慢且可能有等待**，中断延迟大。RF 收发窗口只有几十微秒，ISR 从 Flash 跑容易错过时序 |
| 附带效果 | 标了 `__HIGH_CODE` 的函数**占用 RAM**（本工程 `.highcode` 是 RAM 里一个受限的段），**数量有限，不能滥用**；`__HIGH_CODE` 的**函数里调用的函数也最好在同段**，否则还是要去 Flash 取指 |
| 相关编译参数 | `--param=highcode-gen-section-name=1`（`CMakeLists.txt`）：让编译器为带该属性的函数生成 `.highcode.*` 形式的段名，正好被 `Link.ld` 的 `*(.highcode.*)` 收集 |
| 本项目另有 | `__HIGH_CODE_PRINT`（`APP/include/log.h`）= 同一个 `__attribute__((section(".highcode")))`，用于把打印函数也搬进 RAM —— 但它**会吃 RAM**，日志函数越多占用越大 |

> **`.highcode` 段超了 RAM 怎么办**：现象是链接报 region `RAM` overflowed 或运行异常。对策是**减少** `__HIGH_CODE` 标注，而不是去改 `Link.ld`（官方文件，**不要改**）。详见 [app-api.md](./app-api.md) 与 `SKILL.md` 第六节"保留 `__HIGH_CODE`"的约定。

### 2.8 内核层陷阱清单

| # | 陷阱 | 后果 | 规避 |
|---|---|---|---|
| 1 | 用 `PFIC_DisableAllIRQ()` 当临界区，之后 `PFIC_EnableAllIRQ()` 无条件开 | 破坏嵌套、把调用者原本关着的中断打开 | 用 `__risc_v_disable_irq()` / `__risc_v_enable_irq(saved)` 成对，保存返回值再恢复 |
| 2 | `PFIC_EnableAllIRQ()` / `PFIC_DisableAllIRQ()` 是整字写 `mstatus` | 覆盖 `mstatus` 其它位 | 只在**启动阶段/明确知道现状**时用；运行期优先用 `__risc_v_*` 那对 |
| 3 | 以为 `PFIC_SetPriority()` 能设 0~255 优先级 | 实际只有两档（0 与 0x80） | 需要更细的优先级协调时，靠**关中断 + 软件状态机**，别指望它 |
| 4 | `SetVTFIRQ()` 传 `num > 3` | **静默 return，什么也没做** | 自己保证 `num ∈ 0..3`，并在返回后 `PFIC_GetStatusIRQ()` 复核 |
| 5 | `__MCPY()` 把长度当第三个参数 | 拷贝越界/少拷 | 第三个参数是**结束地址** `start + len` |
| 6 | 用了发行版 RISC-V GCC | `mcpy` 报"unrecognized instruction"、`-march` 不认 `xw` | 必须用 `tools/toolchain`（沁恒定制 `riscv-wch-elf`），见 §0.4 |
| 7 | `__WFI()` 前没有中断源 | **睡死**，只能复位 | 睡之前确认至少有一个中断能在预期时间内触发（SysTick / UART / RF） |
| 8 | `SysTick_Config()` 忘了它自己会开 `SysTick_IRQn`，随后又按别的顺序操作 | 中断在未准备好的状态下进来 | 需要自己控时序就用 `__SysTick_Config()`（不使能中断） |
| 9 | 中断服务函数忘了 `__INTERRUPT` | 编译器不生成保存/恢复与 `mret` → 跑飞 | ISR 必须 `__INTERRUPT`；时序敏感的再加 `__HIGH_CODE` |
| 10 | 中断服务函数忘了 `__HIGH_CODE` | 能跑但变慢，RF 收发时序余量变小 | 保持项目既有标注，**不要为了"代码整洁"删掉** |
| 11 | 大幅加 `__HIGH_CODE` / `__HIGH_CODE_PRINT` 函数 | RAM 段溢出、链接失败 | 只给真正时序敏感的函数加 |
| 12 | `PFIC_SystemReset()` 后面还写了"保存数据"的代码 | 那句永远不执行 | 复位前的落盘/存 Flash 必须放在它**之前** |

---

## 三、速查

### 3.1 一次完整的 RF 上电 → 收发调用链

```
启动
 └─ Startup/startup_CH572.S：拷贝 .highcode(Flash→RAM) → 建 C 环境

射频初始化（只做一次）
 ├─ RFRole_BasicInit( &conf )             conf.rfProcessCB = RF_ProcessCallBack
 │                                        conf.processMask = RX|RX_CRCERR|TX_FINISH|TIMEOUT
 ├─ 填 gTxParam / gRxParam 的公共字段      accessAddress / frequency / crcInit / crcPoly / properties
 │    （properties 来自 TPROPERTIES_CFG.cfgVal）
 ├─ gRxParam.rxDMA = (uint32_t)RxBuf       必须是 __aligned__(4) 且 ≥264 字节
 ├─ gRxParam.rxMaxLen = DATA_LEN_MAX_RX
 └─ PFIC_EnableIRQ( BLEB_IRQn )            20
    PFIC_EnableIRQ( BLEL_IRQn )            21

发送一包
 └─ rf_tx_start( buf, 60 )  →  waitTime = 120  →  RFIP_StartTx( &gTxParam )
                                                   └─ 中断 → LLE/BB_IRQHandler → *_LibIRQHandler
                                                        → RF_ProcessCallBack(RF_STATE_TX_FINISH, id)
                                                        → pCbs->pfnTxCB()

接收
 └─ rf_rx_start( 150 )      →  timeOut = 300   →  RFIP_SetRx( &gRxParam )
                                                   └─ 中断 → RF_ProcessCallBack(sta, id)
                                                        sta & RF_STATE_RX        → pfnRxCB( (rfPackage_t*)RxBuf )
                                                        sta & RF_STATE_RX_CRCERR → pfnCrcErrCB()
                                                        sta & RF_STATE_TIMEOUT   → pfnTimeoutCB()

主机断连
 └─ RFRole_Shut()  →  参数复位回广播值  →  gRfStatus = RF_STATUS_WAIT
```

### 3.2 头文件里有、但本项目**没用**的接口（用之前先想清楚）

| 接口 | 归属 | 本项目状态 |
|---|---|---|
| `RFRole_Stop()` | 协议栈 | 全仓库无调用点 |
| `RFIP_Calibration()` | 协议栈 | 无调用点 |
| `RFIP_WakeUpRegInit()` | 协议栈 | 无调用点（项目未进入低功耗睡眠） |
| `RFIP_ReadCrc()` | 协议栈 | 无调用点（判 CRC 走 `RF_STATE_RX_CRCERR`） |
| `RFIP_SetTxPower()` | 协议栈 | 无调用点（改功率走 `gTxParam.txPowerVal`） |
| `RFIP_SingleChannel()` / `RFIP_TestEnd()` | 协议栈 | 无调用点（定频测试用） |
| `TPROPERTIES_CFG` 的 2.4G 位域分支 | 协议栈 | 被 `#if (TEST_PHY_MODE == PHY_MODE_2G4)` 编译屏蔽（当前 `TEST_PHY_MODE = PHY_MODE_PHY_2M`） |
| `SetVTFIRQ()` | 内核 | 未使用 |
| `PFIC_SetPendingIRQ()` / `PFIC_ClearPendingIRQ()` / `PFIC_GetActive()` / `PFIC_GetStatusIRQ()` | 内核 | 未使用（可用作调试手段） |
| `__MCPY()` | 内核 | 项目源码未直接调用（但**协议库内部依赖它**，所以工具链仍是硬要求） |
| `__SysTick_Config()` | 内核 | 未使用（用了 `SysTick_Config()` 或项目定时器封装） |
| `_SEV()` / `_WFE()` / `__WFE()` / `__WFI()` | 内核 | 未使用 |
| `__risc_v_disable_irq()` / `__risc_v_enable_irq()` | 内核 | 未使用（值得在临界区改造时引入） |

### 3.3 收录计数

| 部分 | 类型/结构体 | 函数 | 宏（`#define`） | 小计 |
|---|---|---|---|---|
| 一、2.4G 协议栈（`LIB/CH572rf.h`） | 7 | 13 | 44 | **64** |
| 二、内核抽象层（`RVMSIS/core_riscv.h`） | 4 | 38 | 24 | **66** |
| 补充（属性宏，定义在 `StdPeriphDriver/inc/CH57x_common.h`，非上述两文件） | — | — | 2 | **2** |
| **合计** | **11** | **51** | **70** | **132** |

> 另单列 **1 个非宏符号**：`extern const uint8_t VER_RF_LIB[]`（协议库版本数组，§1.1）。
>
> 计数口径：**头文件里出现的每一个 `typedef` 类型名 / 函数原型 / `#define` 各算 1 条**（`FlagStatus` 与 `ITStatus` 同属一行 `typedef`，算 1 条类型）。不计头文件包含保护宏（`__CH572_RF_H`、`__CORE_RISCV_H__`）。`__SMC` 因**不存在**而未计入；`RFIP_SetTxDelayTime` 因**只有注释提及、头文件无原型**而未计入。

### 3.4 与本文相关但不在两个输入头文件里的符号（用时查它们自己的头文件）

| 符号 | 实际所在 | 说明 |
|---|---|---|
| `IRQn_Type`、`BLEB_IRQn`(20)、`BLEL_IRQn`(21)、`SysTick_IRQn` | `StdPeriphDriver/inc/CH572SFR.h` | 中断号枚举 |
| `__HIGH_CODE`、`__INTERRUPT`、`FREQ_SYS`、`SAFEOPERATE`、`PRINT` | `StdPeriphDriver/inc/CH57x_common.h` | 已见 §2.7 |
| `LLE_IRQHandler()`、`BB_IRQHandler()` | 应用侧实现（本项目 `APP/rf.c`） | 中断向量入口，名字须与启动文件一致 |
| `rfPackage_t`、`rfStatusCBs_t`、`DATA_LEN_MAX_RX`、`AA`、`DEF_FREQUENCY`、`TEST_PHY_MODE` 等 | 本项目 `APP/include/rf.h` | 见 [app-api.md](./app-api.md) 与 [protocol.md](./protocol.md) |
| `_highcode_lma` / `_highcode_vma_start` / `_highcode_vma_end` | `RF_Uart/Ld/Link.ld`、`RF_UartDongle/Ld/Link.ld` | 链接脚本符号，被启动文件引用 |
