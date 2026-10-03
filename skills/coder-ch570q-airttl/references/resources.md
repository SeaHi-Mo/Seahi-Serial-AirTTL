# 资源索引

> 本项目开发所需的资料入口。**外部链接均为公开可访问地址，若失效请以沁恒官网为准。**

---

## 一、本仓库内文档（优先看这些）

| 文档 | 内容 |
|---|---|
| [`README.md`](../../../README.md) | 项目总览、接线、编译、烧写、使用与常见问题 |
| [`SKILL.md`](../SKILL.md) | AI 开发指南：架构、机制、二次开发、排错 |
| [`references/app-api.md`](./app-api.md) | **本项目 APP 层 API**（rf / buf / uart / rf_uart_tx / rf_uart_rx / usb_uart / log） |
| [`references/protocol.md`](./protocol.md) | **无线协议字节级详解**：帧结构、命令布局、时序、状态机 |
| [`references/wch-stdperiph-api.md`](./wch-stdperiph-api.md) | 沁恒标准外设库 API（CLK/GPIO/UART/Flash/TMR/PWM/SPI/I2C/USB…） |
| [`references/rf-stack-api.md`](./rf-stack-api.md) | 2.4G 协议栈（`CH572rf.h`）+ RISC-V 内核抽象层（`core_riscv.h`）API |
| [`.github/workflows/release.yml`](../../../.github/workflows/release.yml) | 发版 CI：推 `v*` tag 自动出固件并发布 Release |

### 关键源码文件

| 文件 | 说明 |
|---|---|
| `RF_Uart/APP/rf_uart_tx.c` | 从机业务核心 |
| `RF_UartDongle/APP/rf_uart_rx.c` | 主机业务核心 |
| `RF_UartDongle/APP/usb_uart.c` | USB 设备实现（~1980 行，含 CH341 兼容与 CDC 双模式） |
| `RF_Uart/APP/uart.c` | 从机串口驱动、线码落地、一键下载时序 |
| `RF_Uart/APP/include/rf.h` | **协议与射频参数的唯一定义处（两端必须一致）** |
| `RF_Uart/Ld/Link.ld` | 链接脚本：FLASH 240K / RAM 12K |

---

## 二、芯片资料（沁恒 CH570 / CH572 系列）

| 资源 | 链接 |
|---|---|
| CH572 产品页（系列概览、选型） | https://www.wch.cn/products/CH572.html |
| **CH572 数据手册下载页** | https://www.wch-ic.com/downloads/CH572DS1_PDF.html |
| 沁恒官网（资料/工具总入口） | https://www.wch.cn |
| 沁恒资料下载中心 | https://www.wch-ic.com/downloads |
| 第三方资料汇总（原理图、例程、封装） | https://github.com/SoCXin/CH572 |

**本地副本**（已在开发机上确认存在）：

```
D:\Users\Seahi\Desktop\项目文档\立创电赛\无线串口调试器\CH570Q无线串口调试器\CH570Q无线串口调试器\硬件相关的资料\CH572DS1 .PDF
```

> 已把与开发相关的部分（CH570Q 引脚表、存储与地址映射、外设基址、电气/低功耗参数、2.4G 射频参数、**PA0/PA1 被调试口占用**、复位脚可选 PA7/PA8）摘录成速查表 → [chip-spec.md](./chip-spec.md)，不必每次翻 123 页手册。

> **CH570 与 CH572 是同一系列**（WCH BLE SoC），共用 `CH572DS1` 手册与同一套 SDK；本项目主控为 **CH570Q**。
> 芯片 ID：`ID_CH570 = 0x70`、`ID_CH572 = 0x72`（见 `StdPeriphDriver/inc/CH572SFR.h`）。

---

## 三、工具链

| 用途 | 说明 / 链接 |
|---|---|
| **本项目使用的工具链** | 仓库子模块 `tools/toolchain`（沁恒定制 `riscv-wch-elf` GCC **12.2.0**，Linux x64） |
| 工具链仓库 | https://github.com/SeaHi-Mo/riscv-gun-toolchain-12.2.0-x86_64-linux-riscv-wch-elf |
| 官方 IDE（含工具链） | MounRiver Studio 2 — https://www.mounriver.com |
| 为什么必须是沁恒定制版 | 项目 `-march` 含 `xw` 扩展，`RVMSIS/core_riscv.h` 的 `__MCPY()` 内联了 `mcpy` 指令；xPack / 发行版 GCC 不支持 |

---

## 四、烧写与调试工具

| 工具 | 说明 |
|---|---|
| **WCH-Link** | 两线调试器，烧写两颗 CH570Q 必需 |
| WCH-LinkUtility / WCHISPTool | 沁恒官方 Windows 烧写工具（官网下载中心） |
| **OpenOCD（WCH 定制版）** | **已不再随仓库分发**（原 `tools/openocd` 子模块已移除）。Linux x64 预编译版（含 `wlinke` 驱动 + `wch-riscv.cfg`）从这里取：https://github.com/SeaHi-Mo/wch-openocd-linux-x64 |
| **Windows 版 ISP 工具（推荐烧录方式）** | `WchIspStudio` 图形界面：https://www.wch.cn/downloads/WCHISPTool_Setup_exe.html —— 本项目实际采用 |
| 烧录脚本（OpenOCD） | `skills/coder-ch570q-airttl/scripts/flash.sh` —— 自动挑选可用的 openocd 与 `wch-riscv.cfg`；**实测难连**，见 flashing.md 第十一节 |
| 仓库内 GDB | `tools/toolchain/bin/riscv-wch-elf-gdb` |

> **从机固件运行时会关闭两线调试**（`R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN`，为复用 PA2/PA3 给串口/一键下载）。下载失败先给板子**断电重上电**。

---

## 五、调试环境

| 项 | 说明 |
|---|---|
| 本机串口工具 | Linux 下 `minicom` / `screen` / `picocom`；芯片识别为 CH341 兼容设备，内核自带 `ch341` 驱动 |
| Linux 检查识别 | `dmesg \| tail`、`ls -l /dev/ttyUSB*` |
| Windows 驱动 | CH341SER（仅当使用 CH341 兼容模式且系统未自带时） |
| 主机调试串口 | 本工程已带 `-DDEBUG`，PA3=TXD / PA2=RXD，115200 |

---

## 六、相关标准与规范

| 项 | 说明 |
|---|---|
| RISC-V 指令集 | 本项目使用 `rv32imc_zba_zbb_zbc_zbs_xw`（`xw` 为沁恒自有扩展） |
| 代码风格 | **本项目走沁恒 SDK 风格，不使用 `axk` 前缀规范**（详见 [SKILL.md](../SKILL.md) 开头） |
| 开源许可 | 本项目 MIT；`StdPeriphDriver/`、`LIB/`、`RVMSIS/`、`Startup/` 下沁恒官方库版权归南京沁恒微电子，遵循其原始许可 |
