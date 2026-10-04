<h1 align="center">Seahi-Serial-AirTTL</h1>

<p align="center">
  <b>基于沁恒 CH570Q 的 2.4G 无线串口调试器</b><br>
  一根 USB 接电脑，一台设备放现场 —— 远程调试串口设备
</p>

<p align="center">
  <img alt="Build Platform" src="https://img.shields.io/badge/build%20platform-Linux-2b7489">
  <img alt="Toolchain" src="https://img.shields.io/badge/toolchain-riscv--wch--elf--gcc12-orange">
  <img alt="MCU" src="https://img.shields.io/badge/MCU-CH570Q%20%C2%B7%20RISC--V-green">
  <img alt="Wireless" src="https://img.shields.io/badge/2.4G-2M%20PHY-blueviolet">
  <img alt="Release" src="https://img.shields.io/github/v/release/SeaHi-Mo/Seahi-Serial-AirTTL?label=release">
  <img alt="License" src="https://img.shields.io/badge/license-MIT-blue">
</p>

---

## 一、项目概况

**Seahi-Serial-AirTTL** 是一套「**无线串口延长线**」。它把一根串口线拆成两半，中间用 2.4G 无线连接：

- **主机（`RF_UartDongle`）**：U 盘大小的 USB Dongle，插在**电脑**上。**与从机配对成功后**才枚举成一个**虚拟串口**（未配对时 PC 上看不到任何设备），电脑上的串口助手（minicom / screen / SSCOM / STM32CubeProgrammer…）像操作普通 USB 转串口一样操作它。
- **从机（`RF_Uart`）**：放在**被调试设备**旁边，用杜邦线接到目标板的串口上。

于是你可以坐在电脑前，调试放在另一张桌子、另一个房间、甚至挂在电机/机柜上的设备，**不用再拖着长串口线跑来跑去**。

对上层软件来说，它就是一根串口线，**完全透传**，无需改任何上位机代码。

### 核心特性

| 特性 | 说明 |
|---|---|
| 🔌 **USB 免驱形态** | 默认枚举为 CH341 兼容设备（VID `0x1A86` / PID `0x7523`），Linux 内核自带 `ch341` 驱动；**未与从机配对时不枚举**（PC 上看不到设备），配对成功才出现 `/dev/ttyUSB*`，断开立即收回 |
| 📡 **串口参数无线同步** | 电脑端串口工具改波特率 / 数据位 / 停止位 / 校验位，会通过无线**实时下发到从机**，从机的 UART 自动跟随，**不需要重新烧写固件** |
| 🔁 **双向透传** | 下行（电脑 → 目标设备）与上行（目标设备 → 电脑）双向同时工作，单包最大 251 字节 |
| 🎯 **一键下载（ST ISP）** | 电脑端发单字节 `0x7F` 时，从机会自动拉 `BOOT`/`RESET` 时序把目标 MCU 拽进 Bootloader，再转发该字节，配合 Flash Loader / STM32CubeProgrammer 可实现**免手动按键下载** |
| 🔗 **严格绑定与回连** | 首次配对要求**贴近**（RSSI > -58 dBm），之后自动回连；**主机与从机各自把绑定信息存进 Flash**（掉电不丢），只有两侧绑定值完全一致才互认，杜绝"任意新主机接走已绑定的从机" |
| 🔓 **连续重启解绑** | **换主机不必再刷固件**：从机连续快速重启 5 次即解绑（见「七、3 解绑（更换主机）」） |
| 💡 **LED 状态指示** | 未连接**快闪**（200 ms 周期）；连接成功**熄灭**；收发数据时亮 80 ms |
| 📶 **自适应主频** | 从机检测到波特率处于 400 kbps ~ 1 Mbps 时自动把系统时钟切到 100 MHz，否则用 24 MHz，兼顾高速与功耗 |
| 🧩 **干扰共存** | 2.4G 私有协议（非 BLE 连接态），接入地址随机、连接频点从候选表 `{74, 76, 78}`（WiFi 之上）里挑，多套设备同场可并存 |

### 链路示意

```
上行：被调试设备 ──UART──▶ RF_Uart(从机) ──2.4G 无线──▶ RF_UartDongle(主机) ──USB──▶ PC
下行：PC ──USB──▶ RF_UartDongle(主机) ──2.4G 无线──▶ RF_Uart(从机) ──UART──▶ 被调试设备
```

| 环节 | 设备 | 物理接口 | 承载 |
|---|---|---|---|
| 电脑侧 | PC | USB（虚拟串口） | 串口数据 + 波特率等线码设置 |
| 中间 | `RF_UartDongle` / `RF_Uart` | 2.4G 无线 | 自定义轮询协议（绑定 / 状态查询 / 数据 / ACK） |
| 设备侧 | 被调试设备 | UART（TTL 电平） | 透传的串口数据 |

---

## 二、目录结构

```
RF_Cmake/
├── RF_Uart/                     # ★ 从机（Slave）固件：UART ⇄ 2.4G
│   ├── APP/                     # 应用层代码
│   │   ├── main.c               # 入口：时钟/UART/射频初始化
│   │   ├── rf.c / include/rf.h  # 2.4G 底层封装（TX/RX 参数、中断回调、协议常量）
│   │   ├── rf_uart_tx.c         # 从机业务逻辑：绑定应答、轮询主机、UART↔RF 转发
│   │   ├── uart.c / include/uart.h  # 从机串口驱动、线码同步、一键下载时序
│   │   ├── buf.c / include/buf.h    # 环形缓冲区
│   │   ├── my_printf.c / include/log.h  # 调试打印（DEBUG 宏控制）
│   │   └── include/             # 各模块头文件
│   ├── CMakeLists.txt           # ★ CMake 构建脚本（本仓库唯一的构建入口）
│   ├── Ld/Link.ld               # 链接脚本：FLASH 240K @0x00000000，RAM 12K @0x20000000
│   ├── LIB/                     # CH572rf.h + libCH57xRF.a（沁恒 2.4G 协议栈静态库）
│   ├── RVMSIS/                  # RISC-V 内核头文件
│   ├── Startup/                 # 启动文件 startup_CH572.S
│   ├── StdPeriphDriver/         # 沁恒标准外设库 + libISP572.a（ISP / Flash 操作库）
│   └── RF_Uart.launch           # MounRiver Studio 调试配置（OpenOCD + GDB，端口 3333）
│
├── RF_UartDongle/               # ★ 主机（Dongle）固件：USB ⇄ 2.4G
│   ├── APP/
│   │   ├── main.c               # 入口：时钟/USB/射频初始化
│   │   ├── rf.c / include/rf.h  # 与从机完全一致的 2.4G 底层封装
│   │   ├── rf_uart_rx.c         # 主机业务逻辑：应答绑定、轮询应答、USB↔RF 转发
│   │   ├── usb_uart.c / include/usb_uart.h  # USB 设备实现（CH341 兼容模式 / CDC 模式）
│   │   ├── buf.c / my_printf.c  # 环形缓冲区与调试打印
│   │   └── include/
│   ├── CMakeLists.txt           # ★ CMake 构建脚本
│   ├── Ld/ · LIB/ · RVMSIS/ · Startup/ · StdPeriphDriver/   # 与从机同构
│   └── RF_UartDongle.launch     # MounRiver Studio 调试配置
│
├── RF_TEST/                     # ★ 射频测试固件：定频（单信道）发射，配频谱仪/综测仪用
│   ├── APP/
│   │   ├── main.c               # 入口：时钟/串口/射频初始化 + 命令行解析
│   │   ├── rf_test.c / include/rf_test.h    # 定频发射控制（SingleChannel / TestEnd / SetTxPower）
│   │   └── uart_cmd.c / include/uart_cmd.h  # 调试串口命令行（PA0/PA1，115200）
│   ├── CMakeLists.txt           # ★ CMake 构建脚本
│   ├── README.md                # 测试固件用法（命令表、接线、注意事项）
│   └── Ld/ · LIB/ · RVMSIS/ · Startup/ · StdPeriphDriver/   # 与从机同构
│
├── tools/toolchain/             # ★ git 子模块：沁恒定制的 riscv-wch-elf GCC 12.2.0（Linux x64）
│   └── bin/riscv-wch-elf-gcc    # 唯一支持 xw 扩展（mcpy 等指令）的编译器
│
├── skills/                      # ★ AI Agent 技能：本项目的开发指南
│   └── coder-ch570q-airttl/     #   SKILL.md——协议/架构/编译烧写/二次开发/排错
│
├── .gitignore
├── LICENSE                      # MIT
└── README.md
```

> 两个工程各自独立、源码几乎对称：`rf.c` / `rf.h` / `buf.c` / `my_printf.c` 是共用底座，差异只在业务层的 `rf_uart_tx.c`（从机）与 `rf_uart_rx.c` + `usb_uart.c`（主机）。**改协议时两个工程都要动。**

---

## 三、硬件与引脚

### 从机 `RF_Uart`（接被调试设备）

| 引脚 | 功能 | 说明 |
|---|---|---|
| `PA0` | UART TXD | 接目标板的 **RX** |
| `PA1` | UART RXD | 接目标板的 **TX** |
| `PA2` | `RESET_PIN` / `RTS_PIN` | **同一对物理引脚的两个用途**：电脑端 **RTS** 或 ST 一键下载的 **RESET**。接目标板 **RESET**（详见「七、5」） |
| `PA3` | `BOOT_PIN` / `DTR_PIN` | 同上：电脑端 **DTR** 或 ST 一键下载的 **BOOT0**。接目标板 **BOOT0** |
| `PA7` | LED | 未连接**快闪**（200 ms 周期）/ 连接成功**熄灭** / 收发数据亮 80 ms |
| 两线调试口 | WCH-Link | 烧写 / 调试；固件运行时会关闭两线调试功能（`RB_PIN_DEBUG_EN`）以复用引脚 |

- 默认串口参数：**115200-8-N-1**，上电后会被电脑端设置覆盖。
- 记得 **GND 共地**。
- **PA2/PA3 默认跟随电脑端的 DTR/RTS**（`DTR_RTS_FUNC=TRUE`）：**PA3 = DTR、PA2 = RTS**；**断言 = 输出低**，未断言 / 未配对 / 未打开串口时输出**高**。

### 主机 `RF_UartDongle`（插电脑）

| 引脚 | 功能 | 说明 |
|---|---|---|
| `USB D+ / D-` | USB 2.0 全速 | 枚举为虚拟串口，默认 VID `0x1A86` / PID `0x7523`，产品名 `USB2.0 To Serial Port` |
| `PA7` | LED | 同从机：未连接快闪 / 连接成功熄灭 / 收发数据亮 80 ms |
| `PA3` / `PA2` | 调试串口 TXD / RXD | 仅在 `DEBUG` 宏打开时存在（本工程 CMake 里已带 `-DDEBUG`），打印启动信息与 RF 库版本 |
| 两线调试口 | WCH-Link | 烧写 / 调试 |

> **USB 工作模式**：`RF_UartDongle/APP/usb_uart.c` 里的 `USB_WORK_MODE` 默认是 `USB_VENDOR_MODE`（CH341 兼容，Linux 免驱）。若想改成标准 CDC-ACM，把它改成 `USB_CDC_MODE` 重新编译即可。
>
> **枚举策略**：主机**未与从机连接时不初始化 USB**（PC 上看不到设备），连接成功才 `USB_Init()` 枚举出虚拟串口、断开则 `USB_DeInit()` 收回；断开判定有 **3 秒去抖**，避免信号临界时 USB 反复枚举（PC 上设备反复插拔）。

### 晶振与负载电容（32 MHz HSE）

两颗芯片都外接 **32 MHz 晶振**（`XO`/`XI`），无线载波由它倍频而来，**晶振频偏会原样传到 2.4G 载波上**（1 ppm @2400 MHz ≈ 2.4 kHz）。晶振的实际负载电容由两部分组成：

| 组成部分 | 本项目取值 | 作用 |
|---|---|---|
| 外部电容 | **4.7 pF × 2**（左右各一颗，串联等效 ≈ 2.35 pF） | 硬件**粗调**，决定频偏基准 |
| 片内可调电容 | **`HSECap_6p`**（最小档） | `HSECFG_Capacitance()` 设置，见下 |

两处配置分别写在 `RF_Uart/APP/main.c` 与 `RF_UartDongle/APP/main.c` 的 `main()` 里，**两个固件必须取同一个档位** —— 同一块板、同一个外部电容下，两端档位不同会让两侧频偏差一大截，直接吃掉链路余量。

**实测**（常温，频率计测晶振）：

| 配置 | 实测频率 | 频偏 |
|---|---|---|
| 外部 8.2 pF + 片内 18p 档（原设计） | 31.999364 MHz | −19.9 ppm |
| **外部 4.7 pF + 片内 6p 档（现行）** | **31.999954 MHz** | **−1.4 ppm** |

由此标出这颗晶振的**牵引率 ≈ 10.5 ppm/pF**，即**片内每档（2 pF）≈ 21 ppm** —— 比最终需要的精度粗得多，所以**基准靠外部电容定，片内档位只做取舍**。

**软件侧能调的只有片内这一档**（`R8_XT32M_TUNE` 的 bit6:4，8 档 6~20 pF、步进 2 pF）。另有两条看着像、其实不行的路：

- `R32_OSC_CALIB` / `R8_OSC_CAL_CTRL` / `R16_OSC_CAL_CNT` 是"用系统时钟去计数 LSI 周期"的校准计数器，**碰不到 XT32M**；
- RF 频点字段按本项目换算是 `f = 2400 + ch`（MHz），**粒度 1 MHz ≈ 417 ppm**，做不了 ppm 级补偿。

**换料后重标**：先把片内打到 `HSECap_6p` 测一次频率 —— **偏高**就逐档往上加（每档约 21 ppm）把频率压回来；**仍偏低**就减小外部电容。

> **调法上"宁小勿大"**：片内已是最小档时若还偏低就无牌可打了；而偏高总能靠加大片内档位收回来。

---

## 四、无线链路参数

以下参数集中在 `APP/include/rf.h` 与 `APP/rf.c`，两端必须一致：

| 参数 | 值 | 位置 |
|---|---|---|
| 广播频点 | `DEF_FREQUENCY = 17` | `rf.h` |
| 连接频点候选表 | `{74, 76, 78}`（`CH_HOP_TBL`，在 WiFi ch1/6/11 之上，由 `serverData` 取模挑选） | `rf.h` |
| PHY | 2M（`PHY_MODE_PHY_2M` / `CONN_PHY_TYPE = 1`） | `rf.h` / `rf_uart_rx.h` |
| 接入地址（未绑定时） | `0x57250425` | `rf.h` |
| 绑定请求间隔 | 20 ms（`ADV_INTERVAL`） | `rf_uart_tx.h` |
| 首次绑定条件 | RSSI > **-58 dBm**（需贴近，实测校准；非首次则要求两侧 `serverData` 完全一致，不看 RSSI） | `rf_uart_rx.c` |
| 连接间隔 | 10 ms（`CONN_INTERVAL`） | `rf_uart_rx.h` |
| 断连超时 | 100 × 10 ms = 1 s（`CONN_TIMEOUT`） | `rf_uart_rx.h` |
| 单包最大数据 | 251 字节（`DATA_LEN_MAX_TX`） | `rf.h` |
| 发射功率 | **+7 dBm**（`LL_TX_POWEER_7_DBM`，芯片最高档，EIRP ≈ +9 dBm，限值 20 dBm） | `rf.c` |
| 绑定信息存储 | **两端各自**存自己 Flash 偏移 `1024*236`（4 KB 扇区；8 字节：`head=0x55AA` + `serverData` + `bootCount` + 格式魔数 `0xA55A`） | `rf_uart_tx.h` / `rf_uart_rx.c` |

**连接流程**：从机周期性广播 `PKT_CMD_BOUND_REQ`（携带自己记着的 `serverData`）→ 主机校验通过后回 `PKT_CMD_BOUND_RSP`（随机接入地址、候选表里挑的信道、PHY、间隔、超时）→ 双方切到该接入地址通信。此后**从机主动轮询** `PKT_CMD_GET_STATUS`，主机在应答里捎带线码设置或下行数据，从机则用 `PKT_DATA_FLAG` 把上行数据推给主机，主机回 `PKT_DATA_RSP_ACK`。

**校验规则（严格绑定）**：

- 从机带来的 `serverData != 0`（回连）→ 必须与本机记录的**完全一致**才接受，否则打印 `reject.. local=xxxx remote=xxxx`；
- 从机带来的 `serverData == 0`（未绑定 / 首次）→ 要求 **RSSI > -58 dBm**（贴近），用"物理靠近"来人工指定连哪一台；
- 绑定值**只在建链成功那一刻**才写 Flash：配对阶段从机反复广播、主机反复应答，若每应答一次就落盘，两侧最终保存的可能是不同那一次，从而永久 `reject`。

---

## 五、编译（Linux）

> 本仓库的构建流程**固定为 Linux**（Windows 用户请在 WSL2 或虚拟机里操作）。工程用 CMake 构建，工具链是沁恒定制版 RISC-V GCC。

### 1. 环境要求

| 依赖 | 版本 / 说明 |
|---|---|
| 操作系统 | Linux x86_64（已在常见的 Ubuntu / Debian / Fedora 桌面发行版上验证思路） |
| 工具链 | 仓库自带的 **`tools/toolchain`** git 子模块（沁恒定制的 `riscv-wch-elf-` GCC 12.2.0，Linux x64），**无需本机安装 MounRiver Studio**。`-march` 里的 `xw` 扩展（`mcpy` 等指令）只有沁恒定制 GCC 支持，**不能换成发行版或 xPack 的 riscv-none-elf-gcc** |
| CMake | ≥ 3.16 |
| 构建器 | GNU Make（`Unix Makefiles`），一般发行版自带 |
| 烧写 | WCH-Link 调试器 + MounRiver Studio 或 OpenOCD |

### 2. 拉取代码（含工具链子模块）

```bash
git clone --recurse-submodules git@github.com:SeaHi-Mo/Seahi-Serial-AirTTL.git
cd Seahi-Serial-AirTTL
```

（没有配置 SSH Key 的话用 HTTPS：`git clone --recurse-submodules https://github.com/SeaHi-Mo/Seahi-Serial-AirTTL.git`）

已经克隆过、但没带子模块的，补一条：

```bash
git submodule update --init --recursive
```

### 3. 准备工具链

**不用另装 MounRiver Studio**：工具链已作为 git 子模块放在 `tools/toolchain/`，验证一下：

```bash
tools/toolchain/bin/riscv-wch-elf-gcc --version    # 应输出 12.2.0
```

> **为什么非它不可**：`-march` 里的 `xw` 是沁恒自有扩展，`RVMSIS/core_riscv.h` 的 `__MCPY()` 直接内联了 `mcpy` 指令。发行版仓库的 `riscv64-unknown-elf-gcc` 和 xPack 的 `riscv-none-elf-gcc` 都**不认识 `xw`**（会报 `unrecognized opcode 'mcpy'`，甚至触发 GCC ICE），必须用沁恒定制的这一套。
>
> 想改用 MounRiver Studio 2 自带的工具链也可以，配置时覆盖即可：`-DTOOLCHAIN_FOLDER="$HOME/MounRiver_Studio2/resources/app/resources/linux/components/WCH/Toolchain/RISC-V Embedded GCC12"`。

### 4. 编译从机

```bash
cd RF_Uart
cmake -B build -G "Unix Makefiles"
cmake --build build -j"$(nproc)"
```

### 5. 编译主机

```bash
cd ../RF_UartDongle
cmake -B build -G "Unix Makefiles"
cmake --build build -j"$(nproc)"
```

### 6. 编译射频测试固件（可选）

`RF_TEST` 是独立的射频测试固件（定频发射），只在需要测射频指标时才编：

```bash
cd ../RF_TEST
cmake -B build -G "Unix Makefiles"
cmake --build build -j"$(nproc)"
```

> `TOOLCHAIN_FOLDER` 默认已指向仓库内的 `tools/toolchain`，无需手动指定。

### 7. 编译产物

每个工程的 `build/` 目录下会生成：

| 文件 | 说明 |
|---|---|
| `RF_Uart.elf` / `RF_UartDongle.elf` / `RF_TEST.elf` | 带调试信息的可执行文件，用于 GDB 下载与调试 |
| `RF_Uart.hex` / `RF_UartDongle.hex` / `RF_TEST.hex` | Intel HEX，用于烧写 |
| `RF_Uart.map` / `RF_UartDongle.map` / `RF_TEST.map` | 内存映射，检查 Flash/RAM 占用 |
| `RF_Uart.lst` / `RF_UartDongle.lst` / `RF_TEST.lst` | 反汇编列表 |

构建日志末尾会打印内存占用（`--print-memory-usage`）。**注意 RAM 只有 12 KB、可用代码区只有 236 KB**（Flash 末尾 4 KB 被绑定信息占用），加功能时盯着点。

当前源码的实测参考值（同版本 GCC12、`-Os`）：

| 工程 | FLASH | RAM |
|---|---|---|
| `RF_Uart`（从机） | 12.1 KB / 240 KB（4.9%） | 11.7 KB / 12 KB（**95.4%**） |
| `RF_UartDongle`（主机） | 21.3 KB / 240 KB（8.7%） | 9.9 KB / 12 KB（80.7%） |
| `RF_TEST`（射频测试） | 8.9 KB / 240 KB（3.6%） | 4.6 KB / 12 KB（37.7%） |

从机的 RAM 余量**只剩约 0.5 KB**（3 KB 串口环形缓冲区 + 在 RAM 里执行的 `.highcode` 段占了大头）。
加全局变量 / 加大缓冲、或往时序敏感路径加代码前，先看 `build/*.map`：实测 RAM 到 96% 出头就会"编得出但跑不稳"。

---

## 六、烧录

两颗芯片都是 **CH570Q**，用 **WCH-Link** 通过两线调试口烧写。**两个固件都要烧，别烧错**：主机烧 `RF_UartDongle`，从机烧 `RF_Uart`。

> **预编译固件**：不想自己编译的话，直接到 [Releases](https://github.com/SeaHi-Mo/Seahi-Serial-AirTTL/releases) 下载 —— `RF_Uart_vX.Y.Z.hex`（从机）、`RF_UartDongle_vX.Y.Z.hex`（主机）；每个版本"相对上一版改了什么"都写在 Release 说明里。
>
> ⚠️ 用 **ISP 串口工具（Windows `WchIspStudio` 等）**烧写时，**不要勾「全片擦除」** —— 会清掉 Flash 末尾 `0x3B000` 的绑定信息（解绑计数正依赖它）。另外 ISP 接线是「**同名相接**」：CH570 的 TXD 接 TTL 的 TXD、RXD 接 RXD，**仅烧录时如此**（正常透传仍是交叉接法）。

### 方式一：MounRiver Studio 图形界面（推荐）

1. 打开 MounRiver Studio（Linux 版），`File → Import` 导入 `RF_Uart` / `RF_UartDongle` 工程；
2. WCH-Link 接上目标板的两线调试口（SWCLK / SWDIO / GND，必要时接 3V3）；
3. 选中工程 → 工具栏 **Download**（MRS 内部就是 OpenOCD + GDB，配置见工程里的 `.launch` 文件）。

### 方式二：命令行 OpenOCD + GDB

与 MRS 的下载流程等价（复位 → `load` → 运行），适合脚本化 / CI：

```bash
export MRS_HOME="$HOME/MounRiver_Studio2"          # MounRiver Studio 安装根目录（OpenOCD 在里面），按实际修改
export OPENOCD_BIN="$MRS_HOME/toolchain/OpenOCD/bin"
# GDB 直接用仓库子模块里的，与编译用的是同一版工具链
export WCH_GDB="$(pwd)/tools/toolchain/bin/riscv-wch-elf-gdb"

# 1) 起 GDB Server（后台）
"$OPENOCD_BIN/openocd" -f "$OPENOCD_BIN/wch-dual-core.cfg" &

# 2) 用 GDB 把 ELF 写进 Flash 并复位运行
cd RF_Uart
"$WCH_GDB" build/RF_Uart.elf \
  -ex "set architecture riscv:rv32" \
  -ex "set mem inaccessible-by-default off" \
  -ex "target extended-remote localhost:3333" \
  -ex "load" \
  -ex "monitor reset halt" \
  -ex "detach" \
  -ex "quit"
```

要点：

- 端口与 `.launch` 保持一致：GDB `3333`、Telnet `4444`、OpenOCD Tcl `6666`；
- 若你的 MRS 版本里 GDB 可执行文件名是 `riscv-none-elf-gdb`，换成对应名字即可；
- 从机固件运行时会**关闭两线调试功能**（腾出引脚给串口），所以下载失败时先给板子**断电重上电**、或让 WCH-Link 先复位再连；
- 图形化替代品：沁恒官方的 WCH-LinkUtility / WCHISPTool 是 Windows 版，Linux 下建议直接用上面的 MRS 或 OpenOCD。

---

## 七、使用

### 1. 接线

```
被调试设备 TX ──▶ 从机 PA1(RXD)
被调试设备 RX ◀── 从机 PA0(TXD)
被调试设备 GND ── 从机 GND
（需要一键下载时：设备 RESET → 从机 PA2，设备 BOOT0 → 从机 PA3）
```

### 2. 上电与配对

1. 从机接好线并供电，主机插到电脑 USB 口 —— **此时 PC 上还看不到串口**，主机要等配对成功才枚举 USB；
2. **首次配对请把主机和从机贴近**（RSSI > -58 dBm，几厘米内），配对成功后两端 LED 熄灭、主机调试口打印 `bound success.`；
3. 之后双方自动回连：**两侧各自把绑定信息存在 Flash 里**，掉电重启也能回连；
4. 换主机不需要刷固件，按下一节「解绑」操作即可。

> **严格绑定**：主机记得自己绑的是哪台从机，从机也记得自己绑的是哪台主机。非首次连接只有 `serverData` 完全一致才接受，拒绝时打印 `reject.. local=xxxx remote=xxxx`。
> 因此**升级固件时建议两颗一起烧** —— 混用版本可能因绑定值不匹配而一直 `reject`（真遇到了，先按「解绑」给从机解绑再重新配对）。

### 3. 解绑（更换主机）

绑定信息**两侧各自存一份**，而换主机要解的是**从机**那一份 —— 不必再刷固件，用「连续快速重启」即可解绑：

1. 从机**上电后在 15 秒内断电**，如此**连续 5 次**；
2. 第 5 次上电时执行解绑：LED **常亮 2 秒**表示已解绑，随后进入未绑定状态（LED 快闪）；
3. 这一次上电从机会**拒绝所有配对**（避免被旁边还开着的旧主机立刻绑回去、看起来像没解绑），
   **再断电上电一次**，靠近新主机即可重新配对。

判读与注意：

- 每次上电 LED 会**短闪 N 次**（N = 当前连续快速重启次数），据此确认计数是否在累加；
- 只要某次上电后**连续运行超过 15 秒**（正常使用），计数就会被清零，需要重新连续重启 5 次；
- 解绑只对**已绑定**的从机生效：未绑定时不计数、也不写 Flash；
- 上电后马上断电（还没跑完启动）可能来不及写入计数，LED 短闪次数没有增加就是没记上。

### 4. 电脑端使用

```bash
# 看是否识别出虚拟串口
dmesg | tail
ls -l /dev/ttyUSB*

# 用哪个工具都行
minicom -D /dev/ttyUSB0 -b 115200
# 或
screen /dev/ttyUSB0 115200
```

- **配对成功前看不到 `/dev/ttyUSB*` 是正常现象**：主机未与从机连接时不枚举 USB；连上才出现，断开（需约 3 秒去抖确认）后立即收回。
- Linux 内核自带 `ch341` 驱动，插上即用；Windows 需要装 `CH341SER` 驱动。
- **在串口工具里改波特率/数据位/停止位/校验位，从机会自动跟随**（无线下发），无需重新烧写。支持范围受目标设备限制；从机在 400 kbps ~ 1 Mbps 区间会自动切到 100 MHz 主频。
- 若上位机是带「一键下载」的烧写工具（Flash Loader、STM32CubeProgrammer 的 UART 模式等），握手字节 `0x7F` 会触发从机的 `RESET`/`BOOT` 时序，把目标 MCU 拽进 Bootloader，**省掉手动按 BOOT 键**。

### 5. 用电脑端的 DTR/RTS 控制目标板

`DTR_RTS_FUNC` 默认是 `TRUE`：电脑端串口工具的 **DTR / RTS** 会经无线下发到从机，直接驱动 **PA3（DTR）/ PA2（RTS）**。而这两个脚同时也是 ST 一键下载的 **BOOT0 / RESET**，所以只要在上位机里切 DTR/RTS，就等于去拉目标板的 BOOT0 / RESET（例如远程复位目标、或先进 Bootloader 再让上位机下载）。

| 电脑端 | 从机输出 | 常接的目标脚 |
|---|---|---|
| 断言 **DTR** | **PA3 输出低** | 目标 **BOOT0** |
| 断言 **RTS** | **PA2 输出低** | 目标 **RESET**（低有效） |
| 未断言 / 未配对 / 没开串口 | 两脚都输出**高** | 目标正常运行 |

要点：

- **极性**：断言 = 低（标准 TTL 低有效）；从机刚连上时收到的是"两者都未断言"，不会一配对就把目标按住；
- **时效**：DTR/RTS 变化要等从机下一次轮询（约 **10 ms**）才下发，且**只保留最终值** —— 微秒级的连续翻转会丢中间态，esptool 那类几十~几百毫秒的节奏够用；
- **一键下载照旧可用**：下行单字节 `0x7F` 会触发下载时序临时接管这两个脚（做完保持"目标停在 Bootloader"的电平，下次改 DTR/RTS 或重连时会同步回来）；
- 厂商模式（默认）与 CDC 模式都已支持；PC 改线码（波特率等）时也会顺带把当前 DTR/RTS 一起下发；
- 想关掉直控、让这两个脚只做一键下载：把从机 `APP/include/uart.h` 的 `DTR_RTS_FUNC` 改成 `FALSE` 重编译（改完 0x7F 时序仍在）。

### 6. 射频测试固件（`RF_TEST`）

`RF_TEST/` 是**独立的测试固件**（不参与正常通信流程），把板子置于**定频发射**状态，配合
频谱仪 / 综测仪测载波频率（频偏）、发射功率、调制质量与谐波杂散。

- **烧录**：和另外两个固件一样，把 `RF_TEST/build/RF_TEST.hex` 烧到任意一块 CH570Q 板子即可
  （它会覆盖板上的从机/主机固件，**测完记得把正式固件烧回去**）；
- **接线**：`PA0` = TXD、`PA1` = RXD 接 USB-TTL 到电脑，**115200-8-N-1**；`PA7` LED 常亮 = 正在发射；
- **上电后不会自动发射**，必须敲命令（以 CR/LF 结束）：

| 命令 | 说明 |
|---|---|
| `?` / `h` | 打印帮助 |
| `s` | 查看状态（TX 开关 / 信道 / 频率 / 功率） |
| `t` | 开始定频发射（单信道测试模式） |
| `e` | 停止测试模式 |
| `c <0-39>` | 设信道：**f = 2402 + 2×ch MHz**（如 `c 19` → 2440 MHz） |
| `p <-25..7>` | 按 dBm 设发射功率（就近取档） |

> ⚠️ **定频接口的信道编号与正常通信用的是两套**：`RFIP_SingleChannel(ch)` 按 **BLE 信道号 0~39**
> （2 MHz 步进，`f = 2402 + 2×ch`），而 `rf.h` 里的 `DEF_FREQUENCY` / `CH_HOP_TBL`（`{74,76,78}`）
> 是 `f = 2400 + ch` 的另一套编号。**别把 74/76/78 直接喂给 `c`** —— 2478 MHz 在定频接口里是 `c 38`。
>
> 固件同样按已标定的 `HSECap_6p` 配置 32 MHz 晶振负载电容（见「三、晶振与负载电容」），
> 这样测出来的频偏才代表正式固件的工作状态。详细用法见 [`RF_TEST/README.md`](RF_TEST/README.md)。

---

## 八、常见问题

| 现象 | 排查方向 |
|---|---|
| 没有 `/dev/ttyUSB*` | **先确认主机与从机已配对**（未配对时主机有意不枚举 USB）；再 `dmesg \| tail` 看有无 `ch341` 枚举记录；确认插的是主机（`RF_UartDongle`）而不是从机；换数据线/换 USB 口 |
| 串口打印 `reject.. rssi=-xx` | 首次配对距离太远，把主机与从机靠近后重新上电；或从机 Flash 里已有旧绑定信息，先按「七、3 解绑」解绑再配对 |
| 连续重启 5 次也不解绑 | 每次上电必须**在 15 秒内断电**（跑满 15 秒即清零计数）；LED 短闪 N 次就是当前计数，一直只闪 1 次说明每次都被清零了；解绑只对**已绑定**的从机生效 |
| `reject.. local=xxxx remote=xxxx` | 严格绑定生效：主机记录的绑定值与从机带来的不一致（常见于两侧固件版本混用、或换过主机 / 从机）。先按「七、3 解绑」给从机解绑，再贴近重新配对 |
| 配对成功前 PC 上看不到串口 | **有意设计**：主机未与从机连接时不枚举 USB，配对成功才出现虚拟串口，断开立即收回 |
| 一直连不上 | 两个固件的无线参数是否一致（频点/PHY/接入地址常量）、是否同批固件；确认从机与主机都刷新过 |
| 怀疑频偏导致连不上 / 距离短 | 先按「三、晶振与负载电容」测 32M 晶振频率 —— 负载电容配错会偏几十 ppm。片内档位在 `main()` 的 `HSECFG_Capacitance()`，**两个固件要同值** |
| 波特率不对 / 乱码 | 电脑端串口工具的设置会下发到从机，检查是否被上层工具改过；目标设备的实际线码要与之一致 |
| 一配对目标就被按住 / 一直复位 | 电脑端工具**打开串口时通常会自动拉 DTR/RTS**（断言 = 从机 PA2/PA3 输出低）。把目标 RESET/BOOT0 接在这两个脚上时，请在上位机里显式把 DTR/RTS 置为"未断言"，否则见「七、5」 |
| 编译报找不到编译器 | 先确认子模块已拉取：`git submodule update --init --recursive`，`tools/toolchain/bin/riscv-wch-elf-gcc` 应存在；也可用 `-DTOOLCHAIN_FOLDER` 指向别的含 `bin/riscv-wch-elf-gcc` 的目录（别用发行版 / xPack 工具链） |
| `-march` 报错 / `unrecognized opcode 'mcpy'` | 用错工具链了：必须用沁恒定制的 `riscv-wch-elf-` GCC（即 `tools/toolchain`）。xPack 的 `riscv-none-elf-gcc` 虽然接受 `-march=..._xw0p1` 这种写法，但**并不实现** `mcpy` 等 xw 指令 |
| 内存不够 / 链接报错 | Flash 可用区仅 236 KB（末尾 4 KB 存绑定信息）、RAM 12 KB，按 `build/*.map` 精简代码 |
| 下载失败、WCH-Link 连不上 | 固件运行中关闭了两线调试，给目标板断电重上电后再下载 |
| Windows 下 CMake 配置报 `is not a full path to an existing compiler tool` | 本仓库的构建流程**只针对 Linux**，而且 `tools/toolchain` 子模块提供的是 **Linux x64** 工具链，Windows 请在 WSL2 / 虚拟机里编译 |

---

## 九、协议速览

自定义包格式（小端），头部 4 字节：

```c
typedef struct {
    uint8_t type;    // 包类型
    uint8_t length;  // 数据长度
    uint8_t seq;     // 序号（用于 ACK 匹配）
    uint8_t resv;    // 保留
} rfPackage_t;
```

| 常量 | 值 | 含义 |
|---|---|---|
| `PKT_CMD_BOUND_REQ` | `0x01` | 从机 → 主机，绑定请求 |
| `PKT_CMD_BOUND_RSP` | `0x81` | 主机 → 从机，绑定应答（含接入地址/信道/PHY/间隔/超时） |
| `PKT_CMD_GET_STATUS` | `0x02` | 从机 → 主机，轮询状态（顺便取下行数据） |
| `PKT_CMD_RSP_STATUS` | `0x82` | 主机 → 从机，状态应答（`OPCODE_BSP` 线码 / `OPCODE_DATA` 数据 / `OPCODE_ACK` 空） |
| `PKT_DATA_FLAG` | `0x7E` | 从机 → 主机，上行串口数据 |
| `PKT_DATA_RSP_ACK` | `0xFE` | 主机 → 从机，数据确认（可捎带下行数据） |

`OPCODE_DATA = 0x00`（串口数据）、`OPCODE_BSP = 0x01`（线码设置，`BaudRate/StopBits/ParityType/DataBits/ioStaus`）、`OPCODE_ACK = 0xF0`。

---

## 十、许可证

本项目采用 **MIT License**，详见 [LICENSE](LICENSE)。

> 注意：`StdPeriphDriver/`、`LIB/`、`RVMSIS/`、`Startup/` 下的沁恒官方外设库、启动文件与静态库（`libCH57xRF.a`、`libISP572.a`）版权归**南京沁恒微电子股份有限公司**所有，遵循其原始许可与声明，不在本项目的 MIT 授权范围内；商用前请自行确认沁恒的授权条款。

<p align="center">Made with ❤️ for 立创电赛 · 无线串口调试器</p>
