# RF_TEST —— CH570Q 射频测试固件

把 CH570Q 板子置于**定频发射**（单信道测试模式），配合频谱仪 / 综测仪测：
载波频率（频偏）、发射功率、调制质量、谐波杂散等。

> 这是一个**独立测试固件**，不参与 `RF_Uart`（从机）/ `RF_UartDongle`（主机）的正常通信流程。
> 烧到任意一块 CH570Q 板子上都能跑；烧完测完记得把正式固件烧回去。

---

## 一、编译

```bash
cd RF_TEST
cmake -B build -G "Unix Makefiles"
cmake --build build -j"$(nproc)"
```

产物：`build/RF_TEST.hex`（烧录用）、`RF_TEST.elf` / `RF_TEST.map` / `RF_TEST.lst`。

工具链用仓库内的 `tools/toolchain` 子模块（与另外两个工程一致；首次先
`git submodule update --init --recursive`）。**必须是沁恒定制的 `riscv-wch-elf-` GCC**，
不能用发行版 / xPack 的工具链。

实测占用：**FLASH 8.9 KB / 240 KB（3.6%）、RAM 4.6 KB / 12 KB（37.7%）**。

---

## 二、接线与参数

| 引脚 | 用途 |
|---|---|
| `PA0` | 调试串口 **TXD** → 接 USB-TTL 的 **RXD** |
| `PA1` | 调试串口 **RXD** ← 接 USB-TTL 的 **TXD** |
| `PA7` | LED：**常亮 = 正在定频发射**，熄灭 = 未发射 |
| `GND` | 与 USB-TTL 共地（必须） |

串口参数 **115200-8-N-1**。

---

## 三、使用

上电后**不会自动发射**，需要敲命令（以 CR/LF 结束）：

| 命令 | 说明 |
|---|---|
| `?` / `h` | 打印帮助 |
| `s` | 查看状态（TX 开关 / 信道 / 频率 / 功率） |
| `t` | 开始定频发射 |
| `e` | 停止测试模式 |
| `c <0-39>` | 设信道，**f = 2402 + 2×ch MHz** |
| `p <-25..7>` | 按 dBm 设发射功率（就近取档） |

典型流程：

```text
c 19        # 2440 MHz
p 7         # +7 dBm（最高档）
t           # 开始发射，LED 常亮
s           # 回读确认
e           # 测完停止
```

---

## 四、⚠️ 两个容易踩的点

1. **信道编号是"另一套"**
   定频接口 `RFIP_SingleChannel(ch)` 按 **BLE 信道号 0~39**（2 MHz 步进，`f = 2402 + 2×ch`）；
   而正式固件 `APP/include/rf.h` 里的 `DEF_FREQUENCY` / `CH_HOP_TBL`（`{74,76,78}`）是
   `f = 2400 + ch` 的**另一套编号**。**别把 74/76/78 直接喂给 `c`**
   （`c` 只收 0~39；2478 MHz 对应的是 `c 38`）。
2. **负载电容档位与正式固件一致**
   本工程按标定值用 `HSECFG_Capacitance(HSECap_6p)`（外部电容 4.7 pF × 2，
   实测 31.999954 MHz / −1.4 ppm）。如果这里用了别的档位，测出来的频偏就**不代表**
   正式固件的工作状态了（片内每档约 21 ppm）。

---

## 五、实现要点（主要 API）

| 调用 | 作用 |
|---|---|
| `RFRole_BasicInit(&conf)` | 射频基础初始化（`RFTestInit()` 里调一次） |
| `RFIP_SetTxPower(val)` | 设发射功率档（参数取 `LL_TX_POWEER_*`） |
| `RFIP_SingleChannel(ch)` | 进入定频发射；`0` = 成功，`1` = 射频忙（固件会先 `RFRole_Stop()` 再重试一次） |
| `RFIP_TestEnd()` | 退出定频（必须与上者成对使用，否则回不到正常收发） |

源码结构：

- `APP/rf_test.c` / `include/rf_test.h` —— 频点、功率、测试模式的状态与调用封装
- `APP/uart_cmd.c` / `include/uart_cmd.h` —— 串口命令行（收字符、攒行、发字符串）
- `APP/main.c` —— 时钟/串口初始化与命令解析

> 串口输出刻意只用 ASCII（避免终端编码问题），帮助与状态都是英文。
