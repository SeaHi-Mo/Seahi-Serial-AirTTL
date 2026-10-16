<h1 align="center">Seahi-Serial-AirTTL</h1>

<p align="center">
  <b>基于沁恒 CH570Q 的 2.4G 无线串口调试器</b><br>
  一根 USB 接电脑，一台设备放现场，远程调试串口设备
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

## 项目简介

把一根串口线拆成两半，中间走 2.4G 私有无线。对上层软件是透明的：电脑上的串口工具照常用，改串口参数也能同步到被调试设备那一侧。

```
上行：被调试设备 ──UART──▶ 从机 RF_Uart ──2.4G──▶ 主机 RF_UartDongle ──USB──▶ PC
下行：PC ──USB──▶ 主机 ──2.4G──▶ 从机 ──UART──▶ 被调试设备
```

| 固件 | 角色 | 位置 |
|---|---|---|
| `RF_Uart` | 从机 | 被调试设备旁，UART 接目标设备 |
| `RF_UartDongle` | 主机 | 插电脑 USB，枚举成虚拟串口 |
| `RF_TEST` | 射频测试固件（定频发射，配频谱仪用） | 任意一块 CH570Q |

## 特性

| 特性 | 说明 |
|---|---|
| USB 免驱 | 枚举为 CH341 兼容设备，Linux 自带 `ch341` 驱动，Windows 装 `CH341SER`。未与从机配对时不枚举，PC 上看不到串口；配对成功才出现，断开后收回 |
| 串口参数无线同步 | 在电脑端串口工具里改波特率、数据位、停止位、校验位，从机自动跟随，不用重新烧写 |
| 双向透传 | 上下行同时工作，单包最大 251 字节，实测吞吐 27~50 KB/s |
| ST 一键下载 | 电脑端发单字节 `0x7F`，从机自动拉 BOOT/RESET 时序把目标 MCU 拽进 Bootloader，配合 Flash Loader、STM32CubeProgrammer 可以免按 BOOT 键 |
| DTR/RTS 无线直控 | 电脑端的 DTR/RTS 经无线驱动从机 PA3/PA2，也就是目标的 BOOT0/RESET |
| DTR 极性可切换 | PA3 的默认电平可以整条反相，适配 BOOT/EN 高有效的板子（发 4 字节 `A5 5A 01 00` 或 `01`，掉电保存）。切换时链路不断，PC 上的串口也不会消失 |
| 严格绑定与自动回连 | 首次配对要求贴近（RSSI 优于 -58 dBm）；两端各存一份绑定信息在 Flash，掉电不丢，只有两侧绑定值一致才互认 |
| 连续重启解绑 | 换主机不用刷固件，从机连续快速重启 5 次即解绑 |
| LED 指示 | 未连接快闪（200 ms 周期），连接成功熄灭，收发数据时亮 80 ms |
| 波特率上限 1.5 Mbps | 从机串口是整数分频，2 Mbps 只能生成 1.5 Mbps（差 25%），必然乱码，所以上限夹在 1.5 Mbps，详见「常见问题」 |

## 硬件与接线

### 从机 `RF_Uart`（接被调试设备）

| 引脚 | 功能 |
|---|---|
| `PA0` | UART TXD，接目标 RX |
| `PA1` | UART RXD，接目标 TX |
| `PA2` | RTS，或 ST 一键下载的 RESET（低有效） |
| `PA3` | DTR，或 ST 一键下载的 BOOT0 |
| `PA7` | LED |

默认 115200-8-N-1，上电后由主机下发的线码覆盖，务必与目标共地。从机运行时会主动关闭两线调试口（把 PA0/PA1 让给串口），所以烧录连不上时先给板子断电重上电。

### 主机 `RF_UartDongle`（插电脑）

USB D+/D− 枚举成虚拟串口，`PA7` 是 LED。`PA2`/`PA3` 是调试串口（本工程带 `-DDEBUG`，115200 打印启动信息与运行日志）。

### 接线

```
被调试设备 TX  ──▶ 从机 PA1(RXD)
被调试设备 RX  ◀── 从机 PA0(TXD)
被调试设备 GND ──  从机 GND

需要一键下载时：设备 RESET → 从机 PA2，设备 BOOT0 → 从机 PA3
```

## 使用方法

### 1. 编译（Linux）

```bash
git clone --recurse-submodules git@github.com:SeaHi-Mo/Seahi-Serial-AirTTL.git
cd Seahi-Serial-AirTTL

cd RF_Uart          && cmake -B build -G "Unix Makefiles" && cmake --build build -j"$(nproc)"
cd ../RF_UartDongle && cmake -B build -G "Unix Makefiles" && cmake --build build -j"$(nproc)"
```

工具链是仓库自带的 `tools/toolchain` 子模块（沁恒定制的 `riscv-wch-elf-gcc` 12.2.0），不用另外装 MounRiver Studio。`-march` 里的 `xw` 扩展（`mcpy` 指令）只有这套支持，换成发行版或 xPack 的 riscv gcc 会报 `unrecognized opcode 'mcpy'`。

产物在 `build/` 下：`*.hex` 用于烧写，`*.elf` 用于调试，`*.map` 用来看内存占用。

改了头文件要强制重编：CMake 没有正确追踪 `.h` 依赖，直接 build 可能什么都不编。用 `touch APP/*.c` 或 `cmake --build build --clean-first`，编完用 `md5sum build/*.hex` 确认产物真的变了。

不想自己编译的话，直接下现成的。Gitee 和 GitHub 两边的发行版内容一样，国内从 Gitee 下更快：**[Gitee 发行版](https://gitee.com/seahi007/Seahi-Serial-AirTTL/releases)** / [GitHub Releases](https://github.com/SeaHi-Mo/Seahi-Serial-AirTTL/releases)。

每个版本拿这两个就够：`RF_Uart_vX.Y.Z.hex`（从机）、`RF_UartDongle_vX.Y.Z.hex`（主机）。`RF_TEST_*_vX.Y.Z.hex` 是射频测试固件，只有要测载波时才用。每个版本改了什么写在发行说明里。

### 2. 烧录

两颗都是 CH570Q，主机烧 `RF_UartDongle`，从机烧 `RF_Uart`，别烧错。推荐 Windows 上的 `WCHISPStudio`（串口 ISP，不需要 WCH-Link）。三条注意：

- 不要勾「全片擦除」，会清掉 Flash 末尾的配置扇区（绑定信息 + DTR 极性）；
- 串口 ISP 的接线是「同名相接」：CH570 的 TXD 接 TTL 的 TXD、RXD 接 RXD。反直觉，但仅烧录时如此，正常透传仍是交叉接法；
- BOOT 模式是上电瞬间检测，先让工具开始下载，再给板子上电。

装进外壳后 PA0/PA1 摸不到，免拆壳的几种办法（USB 下载、USB 转 TTL 免按键、固件内进 BOOT）见 [`skills/coder-ch570q-airttl/references/flashing.md`](skills/coder-ch570q-airttl/references/flashing.md)。

### 3. 上电与配对

1. 从机接好线供电，主机插电脑。此时 PC 上看不到串口，未配对时主机不枚举 USB。
2. 首次配对把两块板贴近（几厘米，RSSI 优于 -58 dBm）。成功后两端 LED 熄灭，主机调试口打印 `bound success (link up).`。
3. 之后自动回连，两端各存了一份绑定信息在 Flash，掉电重启也能连上。
4. 换主机或要解绑：从机上电后 5 秒内断电，连续 5 次。第 5 次上电时 LED 常亮 2 秒表示已解绑（这一次上电会拒绝配对，属正常），再断电上电一次，靠近新主机重新配对。
   - 每次上电 LED 会短闪 N 次，N 就是当前累计的连续快速重启次数，可以据此确认计数有没有在涨；
   - 某次上电后连续运行超过 5 秒，计数就清零，需要重新连续重启 5 次。

非首次连接只有两侧绑定值完全一致才接受，否则打印 `reject.. local=xxxx remote=xxxx`。所以升级固件建议两颗一起烧，混用了就先按上面解绑再配对。

### 4. 电脑端使用

```bash
ls -l /dev/ttyUSB*                      # 配对成功后才会出现
minicom -D /dev/ttyUSB0 -b 115200       # 或 screen、任意串口工具
```

在串口工具里改波特率等参数，从机自动跟随，上限 1.5 Mbps。

断开后 PC 上串口消失的时机约为 2 秒（射频超时约 1 秒 + 断链去抖 1 秒，去抖可用 `LINK_DEBOUNCE_MS` 调）。

用带一键下载的烧写工具（Flash Loader、STM32CubeProgrammer 的 UART 模式）时，`0x7F` 握手会触发从机的 RESET/BOOT 时序，免按 BOOT 键。

### 5. 用 DTR/RTS 控制目标板

电脑端串口工具的 DTR/RTS 会经无线下发到从机，直接驱动 PA3/PA2，也就是目标板的 BOOT0/RESET：

| 电脑端 | 从机输出 | 常接的目标脚 |
|---|---|---|
| 断言 DTR | PA3 输出低（默认极性） | 目标 BOOT0 |
| 断言 RTS | PA2 输出低 | 目标 RESET（低有效） |
| 未断言 / 未配对 / 没开串口 | 两脚都输出高（默认极性） | 目标正常运行 |

DTR 这一路可以整条反相，适配 BOOT/EN 高有效的板子（或者板上隔了一级反相管）：

| DTR 极性 | 未断言 / 未配对 / 断链 | 上位机断言 DTR | 适用 |
|---|---|---|---|
| `0` 默认（出厂） | 高 | 低 | BOOT0/EN 低有效 |
| `1` 反相 | 低 | 高 | BOOT/EN 高有效，或中间有反相管 |

切换方式是让主机给从机下发一条极性指令，下面两条路等效。

**发 4 个字节**（任何串口工具都能用）：在 PC 那个虚拟串口发，HEX 模式、关掉行尾、一次性发完。

| 想切成 | 发送（HEX） |
|---|---|
| 默认极性 | `A5 5A 01 00` |
| 反相 | `A5 5A 01 01` |

这 4 个字节会被主机拦下来，不会发给目标设备。没生效就再单独发一遍。

**厂商请求 `0xA3`**（自研上位机用）：`bmRequestType=0x40`、`bRequest=0xA3`、`wValue` 低字节 bit0 是极性。标准 `ch341` 驱动占着设备时用户态发不出厂商请求，普通串口工具请走上面那条。参考实现见 [`tools/loss-test/dtr_pol_test.py`](tools/loss-test/dtr_pol_test.py)。

几点说明：

- 极性存在从机 Flash（与绑定信息同扇区），掉电保存。恢复默认再发一次 `A5 5A 01 00` 就行，不用刷固件、不用擦 Flash。
- 切换时从机只改 PA3 电平并写 Flash，不软复位，所以链路不断、PC 上的串口也不会消失。
- 只翻 PA3。RTS 的复位脉冲和 `0x7F` 一键下载时序不受极性影响。
- 验证：万用表或示波器接 PA3 与 GND，切 DTR 看电平是否按上表反过来；主机调试口会打印 `dtr pol cmd:x`。
- 经本桥进 ROM ISP（例如博流 Ai-WB2/BL602）时，复位低电平至少要 500 ms。从机已经在 RTS 断言沿自己生成固定形状的复位脉冲：先延迟 `RTS_PULSE_DELAY_MS`（默认 500 ms）再压低，上位机仍按着就保持低，早释放则低满 `RTS_PULSE_LOW_MS`（默认 200 ms）回高。这样与上位机的脉宽解耦，115200 和 921600 都能过。

## 常见问题

| 现象 | 处理 |
|---|---|
| 配对成功后 PC 上看不到串口，或断开后消失 | 有意设计，未连接时不枚举 USB。先确认两块板已配对，再 `dmesg \| tail` 看有没有 `ch341` 枚举记录 |
| 配不上，一直 `reject..` | 首次配对要贴近（-58 dBm 以内）。`local != remote` 说明两侧绑定值不一致，多因固件混用，从机连续快速重启 5 次解绑再贴近重新配对 |
| 连续重启 5 次也不解绑 | 每次上电必须在 5 秒内断电，跑满 5 秒计数就清零。看 LED 短闪几次就知道计数有没有在涨。解绑只对已绑定的从机生效 |
| 一打开串口目标就被按住，或一直复位 | 串口工具打开端口时通常会自动断言 DTR/RTS（等于 PA2/PA3 输出低）。目标 RESET/BOOT0 接在这两个脚上时，在上位机里把它们显式置为未断言 |
| 波特率不对，满屏乱码 | 从机上限 1.5 Mbps，请求 2 Mbps 只能生成 1.5 Mbps（差 -25%），必然乱码。经本桥接第三方 SDK（如 BL602）时，那边的运行波特率也要 ≤1.5 Mbps，推荐 921600 |
| 从机侧串口看不到任何输出 | 从机的 `PRINT` 受 `-DDEBUG` 控制，默认不开，也就是根本没编进去；而且它走 PA0/PA1，与透传数据是同一对线，所以从机不应该开 DEBUG |
| 改了头文件但行为没变 | CMake 没追踪 `.h` 依赖，见上面「编译」。用 `touch APP/*.c` 或 `--clean-first`，并用 `md5sum` 确认产物变了 |
| 大文件传不过去，下行成段丢 | v0.4.1 修掉的老问题：从机发送环满时整包丢弃却照样回 ACK。烧 v0.4.1 及以后的两端固件即可。复核用 `tools/loss-test/bulk_test.py`，健康时报「两个流逐字节一致」 |
| `unrecognized opcode 'mcpy'` | 用错工具链了，必须用 `tools/toolchain` 里的沁恒定制品，xPack 和发行版都不支持 `xw` 扩展 |
| 内存不够或链接报错 | Flash 可用区 236 KB（末尾 4 KB 存绑定信息），RAM 只有 12 KB，按 `build/*.map` 精简。从机 RAM 已到 94.2%，加功能前先看 map |
| 下载失败，WCH-Link 连不上 | 固件运行时会关闭两线调试口（PA0/PA1 让给串口），给板子断电重上电再试 |

## 开发者

- 协议是 4 字节头 `{type, length, seq, resv}`（小端），无线参数与包类型定义在两端各自的 `APP/include/rf.h`。改协议要两端同步改，`rf.h` 里的频点、PHY、接入地址、CRC 常量必须保持一致。
- 两端共用 `rf.c`、`buf.c`、`my_printf.c`，业务层分别是从机的 `rf_uart_tx.c` + `uart.c`、主机的 `rf_uart_rx.c` + `usb_uart.c`。时序敏感的函数标了 `__HIGH_CODE`（放 RAM 执行），别随手删。
- 更细的文档（协议字节级、芯片规格、外设与射频 API、烧录与调试、WSL 下 usbip 映射）在 [`skills/coder-ch570q-airttl/`](skills/coder-ch570q-airttl/) 里，关键取舍也写在源码注释里。
- 发版：打 `v*` tag 触发 [`.github/workflows/release.yml`](.github/workflows/release.yml) 编译并发布 GitHub Release；发完它再用 [Gitee CLI](https://gitee.com/oschina/gitee-cli) 触发 [`.workflow/sync-release.yml`](.workflow/sync-release.yml)，把同一批固件搬到 Gitee 发行版。**Gitee 侧不编译**，只搬运（[`tools/ci/sync-gitee-release.sh`](tools/ci/sync-gitee-release.sh)），所以不消耗构建额度、也不需要那份 2.2 GB 的工具链。
- 上面那条自动同步需要两个地方都配上 Gitee 私人令牌（勾 `projects` 权限），**没配就整段静默跳过**：GitHub 仓库 Settings → Secrets 里加 `GITEE_TOKEN`；Gitee 仓库的流水线 → 变量里也加一个 `GITEE_TOKEN`。触发时 Gitee 若还没同步到该 tag，同步完到 Gitee 流水线页面手动运行一次 `sync-release` 即可（脚本是幂等的，重跑不会留重复附件）。

## 许可证

MIT，详见 [LICENSE](LICENSE)。
