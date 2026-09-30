---
name: coder-ch570q-airttl
description: SeaHi-Serial-AirTTL 项目开发指南——基于沁恒 CH570Q 的 2.4G 无线串口调试器（"无线串口延长线"），含从机 RF_Uart（UART⇄2.4G）与主机 RF_UartDongle（USB⇄2.4G）两个固件。当需要修改本项目的无线透传协议、串口线码无线同步、ST 一键下载时序、USB（CH341 兼容/CDC）实现、从机自适应主频，或编译、烧写、排查无线连不上/乱码/丢数据等问题时使用。
---

# SeaHi-Serial-AirTTL 开发指南（沁恒 CH570Q）

## 代码风格

**本项目是沁恒（WCH）官方 SDK 风格的工程：不使用 `axk` / `AXK` 前缀命名规范，也不套用 `ai-thinker-c-coding-standard`。一切以项目既有风格为准。**

命名约定（沿用现有代码，别自创一套）：

| 类别 | 约定 | 例子 |
|---|---|---|
| 全局变量 | `g` 前缀 + 驼峰 | `gTxBuf`、`gBoundStatus`、`gServerData` |
| 函数 | 小驼峰 / 下划线混用（历史遗留，保持原样） | `rfProcessRx()`、`UART_SetBuad()`、`RF_StatusQuery()` |
| 宏 / 常量 | 全大写 + 下划线 | `PKT_CMD_BOUND_REQ`、`DATA_LEN_MAX_TX` |
| 类型 | `_t` 后缀 | `rfPackage_t`、`rfTxBuf_t` |
| 文件内私有函数 | `static` + 小写 | `static void rf_disconnect(void)` |

要求：

1. **就近一致**：改哪个文件就沿用该文件的既有风格；**不要为了"统一"而大范围重命名既有标识符**
2. **格式**：4 空格缩进、K&R 大括号；既有代码是沁恒原始风格，**不强求重排**
3. **注释**：既有代码多为英文注释，**新增/修改的代码建议用中文注释**说明用途，不必回头重写老注释
4. **保留 `__HIGH_CODE`**：时序敏感函数（RF/UART 中断、缓冲读写、收发处理）的原有标注不要删（见第六节）

---

## 一、项目是什么

把一根串口线拆成两半，中间用 2.4G 私有无线连接，对上层软件**完全透传**：

```
上行：被调试设备 ──UART──▶ RF_Uart(从机) ──2.4G──▶ RF_UartDongle(主机) ──USB──▶ PC
下行：PC ──USB──▶ RF_UartDongle(主机) ──2.4G──▶ RF_Uart(从机) ──UART──▶ 被调试设备
```

| 固件 | 角色 | 物理接口 | 入口 |
|---|---|---|---|
| `RF_Uart` | **从机**，放被调试设备旁 | UART（TTL） | `APP/main.c` → `RF_StatusQuery()` 死循环 |
| `RF_UartDongle` | **主机**，插电脑 | USB（虚拟串口） | `APP/main.c` → `USB_StatusQuery()` 死循环 |

> **两个工程源码几乎对称**：`rf.c` / `rf.h` / `buf.c` / `my_printf.c` / `log.h` 是共用底座，差异只在业务层——从机 `rf_uart_tx.c` + `uart.c`，主机 `rf_uart_rx.c` + `usb_uart.c`。
> **改无线协议时两个工程都要动，且 `rf.h` 里的常量必须保持一致。**

---

## 二、硬件与引脚

### 从机 `RF_Uart`

| 引脚 | 功能 | 说明 |
|---|---|---|
| `PA0` | UART TXD | 接目标板 RX |
| `PA1` | UART RXD | 接目标板 TX |
| `PA2` | `RESET_PIN`（默认） | 一键下载用，接目标板 RESET；`DTR_RTS_FUNC=TRUE` 时改为 `DTR_PIN` |
| `PA3` | `BOOT_PIN`（默认） | 接目标板 BOOT0；`DTR_RTS_FUNC=TRUE` 时改为 `RTS_PIN` |
| `PA7` | LED（`LED_FUNC`） | 收发翻转；绑定后用 50000 次循环的长亮表示 |
| 两线调试口 | WCH-Link | 据手册 §1.2：**PA0/PA1 上电后默认被仿真调试口占用**，所以固件运行时会**主动关闭**它（`R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN`）才能把 PA0/PA1 当串口用；代价是下载失败时要**断电重上电** |

从机默认 **115200-8-N-1**，上电后由主机下发的线码覆盖。**务必 GND 共地。**

### 主机 `RF_UartDongle`

| 引脚 | 功能 |
|---|---|
| USB D+/D- | USB 2.0 全速，枚举为虚拟串口 |
| `PA7` | LED（`LED_FUNC`） |
| `PA2`/`PA3` | 调试串口 RXD/TXD，**仅在 `DEBUG` 宏打开时**（本工程 `CMakeLists.txt` 已带 `-DDEBUG`，`UART_Remap` 到 PA3=TX / PA2=RX） |

主机启动即 `SetSysClock(CLK_SOURCE_HSE_PLL_100MHz)`；从机启动为 24MHz，运行中按波特率自适应切换。

---

## 三、无线链路参数（改动需两端同步）

集中定义在 `APP/include/rf.h` 与两端 `rf_uart_*.h`：

| 参数 | 值 | 位置 |
|---|---|---|
| 广播频点 `DEF_FREQUENCY` | `17` | `rf.h` |
| PHY | 2M（`TEST_PHY_MODE = PHY_MODE_PHY_2M`） | `rf.h` |
| 未绑定接入地址 `AA` | `0x57250425`（2M PHY 分支） | `rf.h` |
| CRC | `CRC_INIT=0x555555` / `CRC_POLY=0x80032d` | `rf.h` |
| 最大单包数据 | `DATA_LEN_MAX_TX = 251` 字节 | `rf.h` |
| 发射功率 | `LL_TX_POWEER_0_DBM`（0 dBm） | `rf.c` |
| 绑定请求间隔 `ADV_INTERVAL` | 20 ms | `RF_Uart/APP/include/rf_uart_tx.h` |
| 连接间隔 `CONN_INTERVAL` | 10 ms | `RF_UartDongle/APP/include/rf_uart_rx.h` |
| 断连超时 `CONN_TIMEOUT` | 100（×10ms = 1 s） | 同上 |
| 重传上限 `RESEND_COUNT` | 40（从机，超过则**丢包**） | `rf_uart_tx.h` |
| 绑定信息 Flash 偏移 | `1024*236`（=0xF0000，4KB 扇区，`0x55AA`+`serverData`） | `rf_uart_tx.h` |

> **PHY 分支的坑（已读源码确认）**：`rf.h` 里有多组 PHY 配置分支（`PHY_2G4_MODE` 0/1/2 与 `PHY_MODE_PHY_2M`），**`AA` / `CRC_*` 在不同分支下取值不同**（`0x94826E8E` 与 `0x57250425` 两套）。当前 `TEST_PHY_MODE = PHY_MODE_PHY_2M`，走 `#else` 分支，所以 **`AA=0x57250425`、`CRC_INIT=0x555555`、`CRC_POLY=0x80032d` 才是实际生效的值**。
>
> 另一个更容易误判的点：`RFRole_Init()` 里那段 2.4G 位域赋值（`whitOff`/`lengthCrc`/`ctlFiled`/`lengthAA`/`lengthPreamble`/`dplEnable`/`mode2G4`/`bitOrderData`/`crcXOREnable`）被 `#if (TEST_PHY_MODE == PHY_MODE_2G4)` **整体编译屏蔽**，`Properties.cfgVal` 实际只等于 `TEST_PHY_MODE`（`0x10`）；即 `CRC_LEN`/`CTL_FILED`/`AA_LEN`/`PRE_LEN`/`DPL_EN`/`MODE_2G4` 这批宏**当前并未生效**。改 PHY 时不要只改这些宏，必须两端一起改并实测。

---

## 四、无线协议

> 本节是**速览**；帧的字节级布局、连接/透传时序、超时与重传、状态机见 **[references/protocol.md](./references/protocol.md)**。

### 包格式（4 字节头，小端）

```c
typedef struct {
    uint8_t type;    //!< 包类型
    uint8_t length;  //!< 数据长度
    uint8_t seq;     //!< 序号（用于 ACK 匹配）
    uint8_t resv;    //!< 保留
} rfPackage_t;

#define  PKT_HEAD_LEN      sizeof(rfPackage_t)   // 4
#define  PKT_DATA_OFFSET   (PKT_HEAD_LEN-2)      // 2
#define  BUF_LEN_TX        (DATA_LEN_MAX_TX+PKT_HEAD_LEN)  // 255
```

> **`PKT_DATA_OFFSET` 是 2 不是 4**，所以代码里常见 `length = payload_len + PKT_DATA_OFFSET`、`len = length - PKT_DATA_OFFSET - 1`（那个 `-1` 是 opcode 字节）。改协议时这里最容易算错。

### 包类型

| 常量 | 值 | 方向 | 含义 |
|---|---|---|---|
| `PKT_CMD_BOUND_REQ` | `0x01` | 从→主 | 绑定请求 |
| `PKT_CMD_BOUND_RSP` | `0x81` | 主→从 | 绑定应答（接入地址/信道/PHY/间隔/超时） |
| `PKT_CMD_GET_STATUS` | `0x02` | 从→主 | 轮询状态（顺带取下行数据） |
| `PKT_CMD_RSP_STATUS` | `0x82` | 主→从 | 状态应答 |
| `PKT_DATA_FLAG` | `0x7E` | 从→主 | 上行串口数据 |
| `PKT_DATA_RSP_ACK` | `0xFE` | 主→从 | 数据确认（可捎带下行数据） |

应答/承载区首字节是 opcode：

| opcode | 值 | 含义 |
|---|---|---|
| `OPCODE_DATA` | `0x00` | 串口数据 |
| `OPCODE_BSP` | `0x01` | 线码设置（`BaudRate/StopBits/ParityType/DataBits/ioStaus`） |
| `OPCODE_ACK` | `0xF0` | 空应答 |

### 连接流程（**从机主动**）

1. 从机未绑定时每 20 ms 广播 `PKT_CMD_BOUND_REQ`，带 `interval` 与自己 Flash 里的 `serverData`
2. 主机 `rfProcessRx()` 判定是否接受：
   - `severData != 0`（回连）：与本机 `gServerData` 匹配即可连，**或主机刚上电（`gServerData==0`）也放行**
   - `severData == 0`（首次）：要求 **RSSI > -35 dBm**（贴在一起），否则打印 `reject..`
3. 接受则主机生成随机信息并回 `PKT_CMD_BOUND_RSP`：
   - `serverData = rf_rand16(rssi)`、`accessaddr = rf_rand_aa(serverData)`、`channel = serverData & 0x3F`
   - `phy = CONN_PHY_TYPE(1=2M)`、`interval = 10`、`timeout = 100`
4. 从机收到后切换到新接入地址/信道/PHY，并把 `{0x55AA, serverData}` 写入 Flash（掉电不丢）
5. 之后从机**主动轮询** `PKT_CMD_GET_STATUS`，主机应答捎带线码或下行数据或空 ACK
6. 从机有上行数据时发 `PKT_DATA_FLAG`，主机回 `PKT_DATA_RSP_ACK`（可顺带捎一段下行数据，省一次往返）

> 主机侧 `gServerData` 注释写着"需要掉电保存，需保存至flash"，但**实际并未写入 Flash**——主机每次上电 `gServerData=0`，靠上面第 2 条的"主机刚上电也放行"来恢复连接。改这块要留意。

---

## 五、源码架构

### 从机 `RF_Uart/APP/`

| 文件 | 职责 |
|---|---|
| `main.c` | 时钟/串口/射频初始化 → `RF_StatusQuery()` 死循环 |
| `rf.c` | 2.4G 底层封装：`rf_tx_start()` / `rf_rx_start()`、PHY/频点/接入地址配置、中断回调分发（`RF_ProcessCallBack`） |
| `rf_uart_tx.c` | **从机业务核心**：绑定应答、轮询状态、UART↔RF 双向转发、线码落地、一键下载时序 |
| `uart.c` | 串口驱动：中断收发、100bit 超时组包、`UART_SetBuad()`、DTR/RTS 或 RESET/BOOT 引脚 |
| `buf.c` | `simple_buf` 环形缓冲（读/写，`__MCPY` 加速） |
| `my_printf.c` / `log.h` | `PRINT()` 调试打印（`DEBUG` 宏控制） |

### 主机 `RF_UartDongle/APP/`

| 文件 | 职责 |
|---|---|
| `main.c` | 100MHz 时钟、可选调试串口、USB/射频初始化 → `USB_StatusQuery()` 死循环 |
| `rf.c` | 与从机完全一致 |
| `rf_uart_rx.c` | **主机业务核心**：绑定判定与应答、`PKT_CMD_GET_STATUS` 应答、USB↔RF 转发、`RF_RxQuery()` |
| `usb_uart.c` | **USB 设备实现**（~1980 行，篇幅大头）：描述符、EP0 请求、CH341 厂商请求解析、双模式 |
| `buf.c` / `my_printf.c` | 同上 |

### 数据流

```
【主机】USB OUT 中断 → write_buf(pUsbBuf) ──┐
                                          ├─ RF_RxQuery() → PKT_CMD_RSP_STATUS/OPCODE_DATA ─▶ 2.4G
【主机】2.4G 收到 PKT_DATA_FLAG → write_buf(pRfBuf) ─▶ RF_RxQuery() → Ep2Buffer → EP2 IN ─▶ USB
【从机】UART 中断 → write_buf(pUartbuf) ─▶ UART_RxQuery() → PKT_DATA_FLAG ─▶ 2.4G
【从机】2.4G 收到 OPCODE_DATA → write_buf(pRfBuf) ─▶ UART_IRQHandler 默认分支 → UART_THR ─▶ 目标板
```

**缓冲区三级**：UART `3KB`（`UART_BUF_LEN = 1024*3`）、RF `512B`（`RF_BUF_LEN`）、USB `512B`（`USB_BUF_LEN`）。`write_buf()` 满时打印 `#ERR` 并**直接丢数据**（返回已有长度、把 `*len` 置 0）。

---

## 六、关键机制

### 1. 串口线码无线同步（本项目的招牌功能）

```
PC 改串口参数
  └─▶ USB 厂商请求(0x9A/0xA1) 或 CDC SET_LINE_CODING
        └─▶ 更新主机 Uart0Para + 置 UART_Status=1
              └─▶ USB_RxQuery() 返回 0x80
                    └─▶ 主机发 OPCODE_BSP（携带 5 个线码字段）
                          └─▶ 从机 rfProcessRx() 写 UART 寄存器
```

从机落地线码的关键代码（`rf_uart_tx.c` 收到 `OPCODE_BSP`）：
- **自适应主频**：波特率在 `400000 < bps < 1000000` 之间 → 切 `CLK_SOURCE_HSE_PLL_100MHz`，否则 `24MHz`；切换后 `mDelaymS(10)` 再设波特率
- `UART_SetBuad()` 按 `gSysClock` 重算分频写 `R16_UART_DL`
- 停止位/校验位/数据位分别写 `R8_UART_LCR` 的 `RB_LCR_STOP_BIT` / `RB_LCR_PAR_MOD`+`RB_LCR_PAR_EN` / `RB_LCR_WORD_SZ`
- `DTR_RTS_FUNC=TRUE` 时还会按 `ioStaus` 的 bit5/bit6 控制 DTR/RTS 电平

### 2. ST 一键下载（`0x7F` 握手）

从机在收到下行数据且**恰好是单字节 `0x7F`** 时（且 `DTR_RTS_FUNC == FALSE`），执行 BOOT/RESET 时序：

```c
GPIOA_SetBits(BOOT_PIN);   mDelaymS(1);
GPIOA_ResetBits(RESET_PIN); mDelaymS(1);
GPIOA_SetBits(RESET_PIN);   mDelaymS(1);
GPIOA_ResetBits(BOOT_PIN);  mDelaymS(50);
```

配合 Flash Loader / STM32CubeProgrammer 的 UART 模式即可**免按 BOOT 键**下载。

### 3. `__HIGH_CODE`

时序敏感的代码（RF 中断、UART 中断、缓冲读写、绑定/收发处理）都标了 `__HIGH_CODE`，链接后放进 `.highcode` 段——**VMA 在 RAM、LMA 在 Flash**，启动时由 `startup_CH572.S` 拷到 RAM 执行。

> 改这些函数时不要随手删 `__HIGH_CODE`，否则时序可能因 Flash 取指变慢而出问题。同时注意它**占用宝贵的 RAM**（见第七节）。

### 4. USB 双模式（仅主机）

`usb_uart.c:26` → `#define USB_WORK_MODE USB_VENDOR_MODE`

| 模式 | 宏 | VID / PID | 说明 |
|---|---|---|---|
| **厂商模式（默认）** | `USB_VENDOR_MODE` | `0x1A86` / `0x7523` | CH341 兼容，Linux 内核自带 `ch341` 驱动，**免驱**；`bInterfaceClass=0xFF` |
| CDC-ACM | `USB_CDC_MODE` | `0x1A86` / `0x8040` | 标准 CDC，需系统驱动 |

两种模式的**产品名都是** `USB2.0 To Serial Port`，厂商 `wch.cn`；厂商模式的接口类是 `0xFF / 子类 0x01 / 协议 0x02`。

**端点（两种模式不同，由各自描述符决定，别混）**：

| 模式 | 数据端点 | 中断端点 |
|---|---|---|
| VENDOR（默认） | **EP2 OUT + EP2 IN**，bulk，各 **32 字节** | EP1 IN，8 字节 |
| CDC | **EP1 OUT + EP1 IN**，bulk，各 **64 字节** | EP4 IN，8 字节 |

> 缓冲 `Ep2Buffer[2*MAX_PACKET_SIZE]`（128B）给 EP2 收发，`MAX_PACKET_SIZE = 64` 是 CDC 的单包上限；主机收发数据放在 `Ep2Buffer[64]` 起的位置，`MAX_PACKET_SIZE/2 = 32` 正好是 **VENDOR 模式的单包上限**。
> USB 总线复位/挂起后靠 `VENSer0ParaChange` / `CDCSer0ParaChange` 标志重新初始化端点并清缓冲。

CH341 厂商请求要在 EP0 里自己解析：`0x9A` 写寄存器（设波特率，`wIndex` 是分频值）、`0xA1` 初始化串口（线码位域在 `Ep0Buffer[3]`）、`0xA4` MODEM 输出、`0xB2` 清缓冲、`0x5F` 取版本。

---

## 七、编译与烧写

### 编译（Linux，工具链随仓库提供）

工具链是 **git 子模块 `tools/toolchain`**（沁恒定制的 `riscv-wch-elf` GCC 12.2.0，Linux x64）：

```bash
git clone --recurse-submodules git@github.com:SeaHi-Mo/Seahi-Serial-AirTTL.git
cd Seahi-Serial-AirTTL
# 已克隆过的：git submodule update --init --recursive

cd RF_Uart        && cmake -B build -G "Unix Makefiles" && cmake --build build -j$(nproc)
cd ../RF_UartDongle && cmake -B build -G "Unix Makefiles" && cmake --build build -j$(nproc)
```

产物：`build/{RF_Uart,RF_UartDongle}.{elf,hex,map,lst}`。

> **为什么必须用这套工具链**：`-march=rv32imc_zba_zbb_zbc_zbs_xw` 里的 `xw` 是沁恒自有扩展，`RVMSIS/core_riscv.h` 的 `__MCPY()` 直接内联了 `mcpy` 指令。**xPack / 发行版的 RISC-V GCC 不支持**——它们能接受 `-march=..._xw0p1` 这种写法，但汇编时会报 `unrecognized opcode 'mcpy'`，甚至触发 GCC ICE。
> `TOOLCHAIN_FOLDER` 默认已指向子模块；要换 MRS 自带工具链可覆盖 `-DTOOLCHAIN_FOLDER=...`。

### 资源红线（**改代码前必看**）

| 工程 | FLASH | RAM |
|---|---|---|
| `RF_Uart`（从机） | 9984 B / 240 KB（4.06%） | **11384 B / 12 KB（92.64%）** |
| `RF_UartDongle`（主机） | 19460 B / 240 KB（7.92%） | 9672 B / 12 KB（78.71%） |

- **RAM 只有 12 KB**，从机已用掉 92.6%（3 KB 串口环形缓冲 + `.highcode` 是大头），**新增全局变量/加大缓冲前先看 `build/*.map`**
- Flash 可用区实际为 **236 KB**（末尾 4 KB 存从机绑定信息）
- 链接脚本：`Ld/Link.ld`（FLASH 240K @0x00000000，RAM 12K @0x20000000）

### 烧写

两颗都是 CH570Q，用 **WCH-Link / WCH-LinkE**（SDI 单线调试接口）烧写。**主机烧 `RF_UartDongle`，从机烧 `RF_Uart`，别烧错。**

**完整烧录指南见 [references/flashing.md](./references/flashing.md)** —— 含"OpenOCD 从哪来"、各烧录模式（普通 / 擦除重写 / 解除读保护 / 全片擦除）、**别擦掉从机绑定信息**、编译自带 OpenOCD 的已知坑，以及验证状态说明。最常用的一条命令：

```bash
# skill 自带脚本：自动查找 OpenOCD 与 wch-riscv.cfg，烧录 + 校验 + 复位
skills/coder-ch570q-airttl/scripts/flash.sh RF_Uart/build/RF_Uart.hex
skills/coder-ch570q-airttl/scripts/flash.sh RF_UartDongle/build/RF_UartDongle.hex

# 连不上时先"停住"芯片；报 flash protected 时解除读保护
skills/coder-ch570q-airttl/scripts/flash.sh -m reset
skills/coder-ch570q-airttl/scripts/flash.sh -m unlock-program RF_Uart/build/RF_Uart.hex
```

⚠️ **两个前提**：① 两个固件运行后都会**关闭仿真调试接口**（手册 §1.2：PA0/PA1 默认是 SWDIO/SWCLK，不关就用不了串口），连不上时先给目标板**断电重上电**、趁复位瞬间抓；② 从机 PA2/PA3 被"一键下载"占用，烧写前先断开接目标板 RESET/BOOT 的线。

> 要单步调试时，用 OpenOCD 起 GDB Server（端口 3333）+ 子模块里的 `tools/toolchain/bin/riscv-wch-elf-gdb`，详见 flashing.md 第四节。

### 发版（CI）

`.github/workflows/release.yml`：推 `v*` tag 自动编译两个固件 → 产出 `RF_Uart_<版本>.hex` / `RF_UartDongle_<版本>.hex` → 创建（或更新）同名 Release 并成为 Latest。发布说明里会自动附上链接阶段的内存占用。

```bash
git tag -a v0.1.1 -m "..." && git push origin v0.1.1
```

---

## 八、二次开发指南

### 改无线协议

**两端都要改，且常量必须一致**：

1. `rf.h`（两个工程各一份，内容需一致）：包类型、opcode、`AA`/`CRC_*`、`DATA_LEN_MAX_TX`
2. `rf_uart_tx.c` 的 `rfProcessRx()`（从机收）与 `RF_StatusQuery()`（从机发）
3. `rf_uart_rx.c` 的 `rfProcessRx()`（主机收）与 `PKT_CMD_GET_STATUS` 应答分支（主机发）
4. 注意 `seq` 匹配：双方都用 `gTxDataSeq`/`gDataSeq` 对 `pPkt->seq` 做校验，不匹配就重传/忽略——**加新包类型时别破坏序号推进逻辑**

### 加一个自定义控制命令

推荐复用 `OPCODE_BSP` 那条通道（主机→从机的"捎带"路径）：

1. `rf.h` 里加 opcode，如 `#define OPCODE_MYCMD 0x02`，并在 `rfRsp_t` 的 union 里加结构体
2. 主机 `rf_uart_rx.c` 的 `USB_RxQuery() == 0x80` 分支里，把 `opcode` 填成你的新命令并塞数据
3. 从机 `rf_uart_tx.c` 的 `rfProcessRx()` → `PKT_CMD_RSP_STATUS` 分支里加 `else if (pRsp_t->opcode == OPCODE_MYCMD)` 处理

### 调整缓冲/吞吐

- 从机串口缓冲 `UART_BUF_LEN`（`uart.h`，当前 3KB）——**调大前先算 RAM**，从机只剩约 880 B
- RF 缓冲 `RF_BUF_LEN`（512）、USB 缓冲 `USB_BUF_LEN`（512）
- 单包上限 `DATA_LEN_MAX_TX`（251）受 2.4G 包长与 `uint8_t` 缓冲限制，**不要超过 251**

### 改成 CDC 模式

把 `usb_uart.c:26` 的 `USB_WORK_MODE` 改成 `USB_CDC_MODE` 重编译即可（Linux 下会从免驱的 CH341 变成标准 CDC，Windows 需装驱动）。

### 调试

- 从机：`DTR_RTS_FUNC` 决定 PA2/PA3 是"一键下载"还是"DTR/RTS"；`LED_FUNC` 控制 PA7 指示
- 主机：本工程已带 `-DDEBUG`，PA3/PA2 是调试串口，`PRINT()` 输出启动信息与 RF 库版本
- `PRINT()` 走 `my_printf.c`，**注意它会在时序敏感路径上耗时**，排查性能问题时可临时关掉 `DEBUG`

---

## 九、常见问题排查

| 现象 | 排查方向 |
|---|---|
| 编译报 `unrecognized opcode 'mcpy'` / `xw` | 用错工具链了。必须用 `tools/toolchain` 里沁恒定制的 `riscv-wch-elf-gcc`，xPack/发行版都不行 |
| 没有 `/dev/ttyUSB*` | `dmesg \| tail` 看 `ch341` 枚举；确认插的是**主机**；换线换口 |
| 串口打印 `reject.. rssi=-xx` | 首次配对距离太远（要求 > -35 dBm，基本要贴一起）；或从机 Flash 有旧绑定信息，擦除重试 |
| 一直连不上 | 两端 `rf.h` 的频点/PHY/`AA`/`CRC_*` 是否一致；是否同批固件；**两个固件都刷新了吗** |
| 连上后约 1 秒断开 | `CONN_TIMEOUT`/`CONN_INTERVAL` 两端不匹配，或射频环境差导致连续丢包（从机重传 40 次后丢包） |
| 波特率不对 / 乱码 | 电脑端串口工具的设置会下发到从机，检查目标设备实际线码是否一致；高速档（400k~1M）会切 100MHz 主频 |
| 丢数据 | 三级缓冲任一满都会打印 `#ERR` 并丢包；从机 3KB 串口缓冲、RF/USB 各 512B，高波特率下要留意溢出 |
| 下载失败 / WCH-Link 连不上 | 固件运行中关了仿真调试接口（PA0/PA1 让给串口），**断电重上电**后再下载 |
| 内存不够 / 链接报错 | RAM 仅 12 KB（从机已用 92.6%）、Flash 可用 236 KB，按 `build/*.map` 精简 |
| Release 里没有固件 | workflow 只在 `v*` tag 上触发，且 tag 必须指向**含 workflow 文件**的提交 |

---

## 十、API 参考与资源

### 10.1 API 参考（`references/`）

**动手改代码前先查这几份文档**——它们是本 skill 的"接口说明书"：

| 文档 | 内容 | 何时看 |
|---|---|---|
| [app-api.md](./references/app-api.md) | **本项目 APP 层 API**：`rf.h` 收发接口与数据结构、`buf.h` 环形缓冲、从机 `uart.h`、从机 `rf_uart_tx.h`、主机 `rf_uart_rx.h`、主机 `usb_uart.h`、`log.h`，附全局状态变量速查 | 改业务逻辑、加功能、调缓冲 |
| [protocol.md](./references/protocol.md) | **无线协议字节级详解**：帧结构与 `length` 语义、每个命令的字节布局、连接/透传时序、超时与重传规则、状态机、改协议检查清单 | 改协议、分析抓包、排查连不上 |
| [wch-stdperiph-api.md](./references/wch-stdperiph-api.md) | 沁恒**标准外设库** API：CLK / GPIO / UART / Flash / SYS / TMR / PWM / SPI / I2C / PWR / USB设备 / USB主机 / CMP / KeyScan / ISP | 配引脚、设时钟、读写 Flash、开关中断 |
| [rf-stack-api.md](./references/rf-stack-api.md) | 沁恒 **2.4G 协议栈**（`CH572rf.h`）+ **RISC-V 内核层**（`core_riscv.h`）：`RFRole_*` / `RFIP_*`、CSR 操作、`PFIC_*` 中断控制、`__MCPY` 等 xw 扩展、`__HIGH_CODE` | 调射频参数、写中断、理解 `.highcode` |
| [chip-spec.md](./references/chip-spec.md) | **CH570Q 芯片规格**：系列差异、内核/存储与地址映射、外设基址、CH570Q 引脚表、PA0/PA1 调试口约束、复位脚可选 PA7/PA8、电气与低功耗参数、2.4G 射频参数 | 查硬件规格、核对接线、调低功耗 |
| [flashing.md](./references/flashing.md) | **烧录与调试指南**：OpenOCD 从哪来、各烧录模式、解除读保护、别擦掉绑定信息、GDB 调试、自带 OpenOCD 的编译坑与验证状态 | 烧写、排查烧录问题 |
| [resources.md](./references/resources.md) | 数据手册、工具链、烧写调试工具、外部资料入口 | 查手册、找工具 |

> **三层 API 的修改权限不同**：**APP 层**（本项目所写，可自由改）→ **协议栈 / 外设库**（沁恒预编译库与官方驱动，**只调用不修改**）→ **内核层**（`core_riscv.h`，RISC-V 抽象，只调用）。

### 10.2 其他资源

| 资源 | 位置 |
|---|---|
| 项目 README（接线/使用/烧写全流程） | 仓库根 `README.md` |
| 无线包格式速览 | 本文第四节（字节级详解见 `references/protocol.md`） |
| 沁恒 CH570Q 资料 | https://www.wch.cn/products/CH572.html |
| 工具链子模块 | `tools/toolchain`（`SeaHi-Mo/riscv-gun-toolchain-...`，GCC 12.2.0） |
| 发版工作流 | `.github/workflows/release.yml` |

> **SDK 文件名说明**：`CH572SFR.h` / `startup_CH572.S` / `CH572rf.h` / `libCH57xRF.a` / `libISP572.a` 是沁恒官方命名，**不要改**——CH570 与 CH572 共用这套 SDK（`CH572SFR.h:976` 里同时定义 `ID_CH570=0x70` 与 `ID_CH572=0x72`），本项目主控为 **CH570Q**。
