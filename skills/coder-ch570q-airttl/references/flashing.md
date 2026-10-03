# 烧录与调试指南

> 两颗芯片都是 **CH570Q**，通过 **WCH-Link / WCH-LinkE** 用 **SDI 单线调试接口**（WCH 私有两线/SDI）烧写。
> **主机烧 `RF_UartDongle`，从机烧 `RF_Uart`，别烧错。** 详细对照表见本文第七节。

---

## 一、硬件准备

| 项 | 说明 |
|---|---|
| 调试器 | **WCH-Link** 或 **WCH-LinkE**（推荐 E，OpenOCD 用 `wlinke` 驱动） |
| 接口 | **SDI**（WCH Single-wire Debug Interface）。WCH-Link 上对应 `SWCLK`/`SWDIO`（部分丝印为 `DIO`/`CLK`）+ `GND`，必要时接 `3V3` |
| 目标板 | CH570Q 从机板 / Dongle。**GND 必须共地** |
| 供电 | 目标板可自供电，也可由 WCH-Link 的 3V3 供电（注意电流余量） |

> 本项目**没有**引出独立的调试排针，需要焊线或飞线到芯片的两线调试脚；从机的 PA2/PA3 已被"一键下载"功能占用（见下节）。

### 1.1 ⚠️ WSL 用户：先把 WCH-LinkE 映射进来

如果**编译与烧录都在 WSL2 里做、而 WCH-LinkE 插在 Windows 上**，直接跑 OpenOCD 会报 `Error: open failed`（`libusb` 在 WSL 里根本看不到这个设备）——必须先经 **usbipd** 把设备转发进 WSL：

```bash
# WSL 侧：加载虚拟 USB 主机控制器（没有 /sys/bus/usb 就是没加载，每次 WSL 重启后都要重做）
sudo modprobe vhci-hcd
```

```powershell
# Windows 侧：bind 需要管理员；attach 把设备转发给 WSL
usbipd list                                   # 认准 WCH-LinkE 的 BUSID，例：9-1  1a86:8010  WCH-LinkRV…
usbipd bind --busid 9-1
usbipd attach --wsl --busid 9-1
```

验证（WSL 内）：`lsusb` 应出现 `1a86:8010 QinHeng Electronics WCH-Link`，并多出 `/dev/ttyACM0`。

> **完整流程见 [wsl-usbip.md](./wsl-usbip.md)** —— 含 `usbip` 与 WSL 内核版本不匹配的坑、无 sudo 密码时的免密提权、
> `LIBUSB_ERROR_ACCESS` 的 udev 权限修复、以及 WSL 重启后的自动化。

---

## 二、⚠️ 两个最容易卡住的前提（先读这段）

### 1. 两个固件运行起来后都会**关闭仿真调试接口**

```c
/* RF_Uart/APP/uart.c:299  （从机 UART_Init） */
R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;

/* RF_UartDongle/APP/usb_uart.c:1975（主机 USB_Init） */
R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;
```

**为什么非关不可**（数据手册 §1.2 注 3 原文）：

> 系统上电或复位后**默认调试接口引脚功能开启**……仿真调试接口启用后，**PA0 和 PA1 仅用作 SWDIO 和 SWCLK**，不再用于 GPIO 或外设复用功能引脚。

而本项目从机恰恰要用 **PA0 = UART TXD、PA1 = UART RXD**——不关，这两个脚就当不了串口。

后果是：**固件一跑起来，WCH-Link 就连不上了**（更多引脚约束见 [chip-spec.md](./chip-spec.md) 第五节）。

**对策（按推荐顺序）**：

1. **让 WCH-Link 在复位瞬间抓住芯片**：`init` 后紧跟 `halt`（本文所有命令都是这个顺序）
2. 失败就**给目标板断电、重新上电**，趁固件还没跑起来（或复用窗口）立刻执行烧录
3. 仍失败：先执行"只复位不烧录"（`scripts/flash.sh --reset`）把芯片停住，再烧

### 2. 从机的 PA2/PA3 是"一键下载"输出脚

`DTR_RTS_FUNC == FALSE`（默认）时，PA2 = `RESET_PIN`、PA3 = `BOOT_PIN`，固件会在这两个脚上产生电平跳变。若你同时用它们接目标板的 RESET/BOOT，**烧写从机前先断开这两根线**，避免复位时序打架。

---

## 三、方法一：OpenOCD 命令行（Linux）

> ⚠️ **实测提醒**：本项目两块板启动后都会关闭两线调试口（PA0/PA1），
> 只剩"上电瞬间"的极窄窗口，而 OpenOCD 每次运行都要重新枚举 + 握手，**基本抓不到**。
> 建议优先用 **[第十一节：ISP 烧录（串口）](#十一方法五isp-烧录串口推荐)**
> 或 **[第五节：MounRiver Studio](#五方法三mounriver-studio图形界面最省事)**。

### 3.0 先确认手上有一份能用的 openocd

必须是**含 `wlinke` 驱动的 WCH 定制版 OpenOCD** —— 发行版仓库里的 `openocd` **没有这个驱动**，装了也用不了。

**这是"用现成工具"，不需要自己编译。** ⚠️ **本项目已不再自带 OpenOCD**（原先的 `tools/openocd` 子模块已移除，以缩减仓库体积），
需要你自行准备一份，`scripts/flash.sh` 会按下面的优先级自动挑选：

| 优先级 | 来源 | 路径 |
|---|---|---|
| **①（推荐）** | **独立仓库下载** | [SeaHi-Mo/wch-openocd-linux-x64](https://github.com/SeaHi-Mo/wch-openocd-linux-x64)，clone 后用 `OPENOCD_BIN` 指定 |
| ② | PATH 里的 `openocd` | 会**校验它是否真带 `wlinke`**，发行版自带的（0.10）不合格会被自动跳过 |
| ③ | 系统已装的 WCH 定制版 | `/usr/local/bin/openocd` |
| ④ | 安信可 FlashKey 编译的 Linux 版 | 随其 `flashkey-mcp` 包分发，示例：`~/.local/venvs/flashkey-mcp/lib/python3.11/site-packages/flashkey_mcp/openocd/bin/linux-x64/openocd` |
| ⑤ | MounRiver Studio 自带的 | `$MRS_HOME/toolchain/OpenOCD/bin/openocd` |

**获取方式**：

```bash
git clone https://github.com/SeaHi-Mo/wch-openocd-linux-x64.git
export OPENOCD_BIN="$PWD/wch-openocd-linux-x64/bin/openocd"
```

> **这份 Linux 版的来历（已核实）**：由安信可 **FlashKey** 项目编译（WCH OpenOCD v1.6 / OpenOCD 0.11.0 分支，含 `wlinke` 驱动与 SDI 传输），托管于
> [`SeaHi-Mo/wch-openocd-linux-x64`](https://github.com/SeaHi-Mo/wch-openocd-linux-x64)，并附 `NOTICE-OPENOCD.md` 做 GPL-2.0 合规声明。
> **该仓库就是原先 `tools/openocd` 子模块的上游** —— 子模块移除后从这里取，内容完全一样。
>
> 沁恒与社区的同类仓库（`openwch/openocd_wch`、`cjacker/wch-openocd`、`fxsheep/openocd_wchlink-rv`）**都只有源码**、无 Linux 预编译包，
> 所以上游把构建产物单独托管。

**怎么判断能不能用**：能加载 `wch-riscv.cfg` 而**不报** `unknown adapter`（即已编入 `wlinke` 驱动）。`scripts/flash.sh` 会自动做这个校验。

target 配置内容就是：

```tcl
adapter driver wlinke          # WCH-Link / WCH-LinkE
adapter speed 6000
transport select sdi           # WCH 的 SDI 单线调试接口
wlink_set_address 0x00000000
# …wch_riscv target + flash bank（地址 0x00000000）
```

`scripts/flash.sh` 会**自动查找** `openocd` 与 `wch-riscv.cfg`，也支持用环境变量 `OPENOCD_BIN` / `OPENOCD_CFG` 或 `-c` 指定。

### 3.1 一条命令烧录

```bash
cd Seahi-Serial-AirTTL

# 烧从机
openocd -f /usr/local/share/openocd/scripts/target/wch-riscv.cfg \
  -c "init" -c "halt" \
  -c "program RF_Uart/build/RF_Uart.hex verify" \
  -c "reset" -c "exit"

# 烧主机
openocd -f /usr/local/share/openocd/scripts/target/wch-riscv.cfg \
  -c "init" -c "halt" \
  -c "program RF_UartDongle/build/RF_UartDongle.hex verify" \
  -c "reset" -c "exit"
```

或用本 skill 附带的脚本（自动找配置、支持多种模式）：

```bash
skills/coder-ch570q-airttl/scripts/flash.sh RF_Uart/build/RF_Uart.hex          # 烧 + 校验 + 复位
skills/coder-ch570q-airttl/scripts/flash.sh --erase-program RF_Uart/build/RF_Uart.hex
skills/coder-ch570q-airttl/scripts/flash.sh --unlock-program RF_Uart/build/RF_Uart.hex
skills/coder-ch570q-airttl/scripts/flash.sh --reset                            # 只复位，不烧
```

### 3.2 各模式的区别

| 模式 | OpenOCD 命令 | 何时用 |
|---|---|---|
| **烧录+校验**（默认） | `program <hex> verify` | 日常烧写 |
| 只校验 | `verify_image <hex>` | 确认上次烧写是否成功 |
| 擦除后烧写 | `flash write_image erase <hex>` + `verify_image <hex>` | `program` 失败时 |
| **解除读保护后烧写** | `flash erase_address unlock 0x00000000 0x10000` + `flash write_image <hex>` + `flash verify_image <hex>` | 芯片被写保护 / 报 "flash protected" |
| 只复位 | `init` + `halt`（或 + `reset`） | 连不上时"停住"芯片 |
| 全片擦除 | `flash erase_sector 0 0 last` | 需要彻底清空（**会连绑定信息一起清掉**） |

> `reset` 让芯片复位后**继续运行**；`reset halt` 是复位后**停在入口等 GDB**。做纯烧录用 `reset`。

### 3.3 本项目**特有**的一件事：别把绑定信息擦掉

从机把配对信息存在 **Flash 偏移 `1024*236`（0xF0000）** 的 4 KB 扇区里（`BOUND_INFO_FLASH_ADDR`）。

- 常规 `program` 只写固件区，**不会**碰它 → 重新烧固件后仍能自动回连
- 但 `flash erase_sector` 全片擦除会把它清掉 → 需要**重新贴近配对**（首次配对要求 RSSI > −35 dBm）

需要"清空绑定、重新配对"时，就是故意全片擦除。

### 3.4 验证状态（诚实说明）

| ✅ 已验证 | ❌ 未验证 |
|---|---|
| WCH 定制版 openocd 能加载 `wch-riscv.cfg`，输出 `Ready for Remote Connections` | **目标芯片的实际擦写与校验**：`program` / `verify` 的真机结果 |
| **接上 WCH-LinkE 后能认出调试器**：`Info : WCH-LinkE mode:RV version 2.21` | 目标板上的烧录结果（调试口被固件关闭，需趁复位窗口抢） |
| 无设备 / 无权限时停在 `Error: open failed` —— 说明 `wlinke` 驱动加载正常 | |
| **WSL 下经 usbipd 映射后同样可用**（见 [wsl-usbip.md](./wsl-usbip.md)）；普通用户需先处理 USB 节点权限 | |

也就是说：**本文的命令与参数取自 MRS 工程配置（`RF_Uart.launch`）与已验证可用的 FlashKey 烧录脚本；"OpenOCD ↔ WCH-LinkE 的通信"已在本机（含 WSL 映射后）实测跑通，但"芯片真的写进去了"仍取决于接线与是否抢到复位窗口，需你实测确认。**

---

## 四、方法二：OpenOCD + GDB（要调试时用）

### 4.1 起 GDB Server（前台/后台）

```bash
openocd -f /usr/local/share/openocd/scripts/target/wch-riscv.cfg
# 端口：GDB 3333 / Telnet 4444 / Tcl 6666（与工程 .launch 一致）
```

### 4.2 用 GDB 加载并运行

```bash
tools/toolchain/bin/riscv-wch-elf-gdb RF_Uart/build/RF_Uart.elf \
  -ex "set architecture riscv:rv32" \
  -ex "set mem inaccessible-by-default off" \
  -ex "target extended-remote localhost:3333" \
  -ex "load" \
  -ex "monitor reset halt" \
  -ex "detach" -ex "quit"
```

这几个 `-ex` 参数与工程自带的 `RF_Uart.launch` 一致（MRS 用的也是同样端口和命令），照抄即可。反汇编若要正确显示 `mcpy` 等 xw 指令，加 `-ex "set disassembler-options xw"`。

---

## 五、方法三：MounRiver Studio（图形界面，最省事）

1. 打开 MRS，`File → Import` 导入 `RF_Uart` / `RF_UartDongle` 工程
2. WCH-Link 接上目标板，选中工程 → 工具栏 **Download**
3. MRS 内部就是 OpenOCD + GDB（配置见 `RF_Uart.launch`：`wch-dual-core.cfg`，端口 3333/4444/6666）

> `.launch` 里引用的 `wch-dual-core.cfg` 是 MRS 自带的；命令行方式用 `wch-riscv.cfg` 等效（都是 `wlinke` + `sdi`）。

---

## 六、方法四：Windows 官方工具（备选）

| 工具 | 用途 |
|---|---|
| **WCH-LinkUtility** | 图形化烧写/读保护设置，配 WCH-Link 使用 |
| **WCHISPTool** | USB/串口 ISP 下载（把芯片置于 BOOT 模式后经 USB 或串口烧写），不依赖 WCH-Link |

两者均在沁恒官网下载中心获取（见 [resources.md](./resources.md)）。本项目日常开发用方法一/三即可。

---

## 七、烧哪个固件？

| 产物 | 角色 | 烧到哪块板 | 烧错的表现 |
|---|---|---|---|
| `RF_Uart/build/RF_Uart.hex` | **从机**（接被调试设备） | 从机板（UART 侧） | 板子上没有 `/dev/ttyUSB*`，主机 USB 不枚举 |
| `RF_UartDongle/build/RF_UartDongle.hex` | **主机**（插电脑） | Dongle（USB 侧） | 从机不广播、电脑端没有虚拟串口 |

两者都烧好后才可能配对成功。发版页 `/releases` 上的文件名就是 `RF_Uart_<版本>.hex` / `RF_UartDongle_<版本>.hex`。

---

## 八、怎么确认烧录成功

| 手段 | 从机 | 主机 |
|---|---|---|
| OpenOCD 输出 | 出现 `** Verified OK **` | 同 |
| 串口/调试口 | 复位后打印 `start.` + RF 库版本（从机默认 `DEBUG` 关，需打开才有输出） | 带 `-DDEBUG`，PA3/PA2 会打印 `start.` 与库版本 |
| LED（PA7） | 未绑定：常灭/微弱；绑定后长亮闪烁 | 同理 |
| 功能验证 | 电脑端出现 `/dev/ttyUSB*`，串口工具收发正常即链路通 | — |

只校验不烧录：

```bash
openocd -f /usr/local/share/openocd/scripts/target/wch-riscv.cfg \
  -c "init" -c "halt" -c "verify_image RF_Uart/build/RF_Uart.hex" -c "exit"
```

---

## 九、常见问题

| 现象 | 原因 / 解决 |
|---|---|
| `Error: open failed` | 没检测到 WCH-Link：检查 USB、驱动；要装 `libusb`/udev 规则（Linux 下建议把 WCH-Link 的 USB 权限放开） |
| `Ready for Remote Connections` 后连不上芯片 | **固件已关闭两线调试**（第二节）——断电重上电，或先 `init`+`halt` 抢在固件运行前；多次重试 |
| `flash protected` / 写不进去 | 芯片处于读/写保护：用 `--unlock-program`（`flash erase_address unlock`）模式 |
| `program` 报校验失败 | 先 `flash write_image erase` 再 `verify_image`；仍失败考虑全片擦除后重烧 |
| 烧完不运行 | 命令里加了 `reset halt` 会停在入口，改成 `reset`（或 GDB 里 `continue`/`detach`） |
| 烧从机后无法再连 WCH-Link | 正常现象（调试口被复用）。断电重上电后再烧 |
| 两个固件都新烧了却连不上 | 检查两端 `rf.h` 的频点/PHY/`AA`/`CRC_*` 是否同批；首次配对要贴近（RSSI > −35 dBm） |
| Windows 上 MRS 能烧、命令行不行 | 命令行需用 WCH 定制版 OpenOCD（带 `wlinke` 驱动），发行版 `openocd` 不含该驱动 |
| WSL 里 `Error: open failed` | WCH-LinkE 还没映射进 WSL（或 `vhci_hcd` 未加载、`usbip` 客户端缺失）→ 见 [wsl-usbip.md](./wsl-usbip.md) |
| WSL 里 `libusb_open() failed with LIBUSB_ERROR_ACCESS` | USB 设备节点属主是 root → 加 udev 规则，或 `sudo` 跑 → 见 [wsl-usbip.md](./wsl-usbip.md) 第四节 |

---

## 十、快速对照：最小可用流程

```bash
# 1) 编译（工具链随仓库提供）
cd RF_Uart        && cmake -B build -G "Unix Makefiles" && cmake --build build -j$(nproc)
cd ../RF_UartDongle && cmake -B build -G "Unix Makefiles" && cmake --build build -j$(nproc)

# 2) 接 WCH-Link 到目标板（SDI + GND）

# 3) 烧录（分别对两块板执行）
skills/coder-ch570q-airttl/scripts/flash.sh RF_Uart/build/RF_Uart.hex
skills/coder-ch570q-airttl/scripts/flash.sh RF_UartDongle/build/RF_UartDongle.hex

# 4) 首次配对：把两块板贴近，上电；从机串口出现 "bound success." 即成功
```


---

## 十一、方法五：ISP 烧录（串口，**推荐**）

> **为什么推荐**：CH570Q 的两线调试口就是 **PA0/PA1**，而两个固件启动后都会执行
> `R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN` 把它关掉（从机是为了把 PA0/PA1 让给 UART）。
> 于是固件一跑起来 WCH-Link 就连不上，只剩"上电瞬间"的极窄窗口；OpenOCD 每次运行
> 都要重新枚举 + 握手，实测基本抓不到。**ISP 走片内 ROM bootloader
> （`BOOT_LOAD_ADDR = 0x3C000`，8KB），完全不碰调试口**，所以稳定得多。

### 11.1 关键：BOOT 模式是「上电瞬间检测」

CH570 的 ROM 在**上电/复位那一刻**检测 ISP 握手数据，因此：

- **工具必须先开始发数据，MCU 后上电** —— 顺序反了就进不去 bootloader
- `WCHISPTool_CMD` 只枚举一次就退出（源码里 `WCH55x_EnumDevices()` 没有等待循环），
  所以要用脚本的 **`-w` 等待模式**：脚本反复调用工具，你在这段时间给 MCU 上电
- **不需要跳线或按键**（这点和多数 MCU 的"BOOT 引脚"不同）

### 11.2 准备

| 项 | 说明 |
|---|---|
| 串口连接 | USB 转串口接到 CH570 的 ISP 串口，**电平 3.3V** |
| 映射进 WSL | 见 [wsl-usbip.md](./wsl-usbip.md)。**注意 CH343 会枚举成 `/dev/ttyACM0`**（CDC-ACM 类），不是 `/dev/ttyUSB*`（那是 CH340/CP210x 之类）；用 `ls /dev/ttyUSB* /dev/ttyACM*` 确认 |
| `Config.ini` | **已随 skill 提供**：[`scripts/Config.ini`](../scripts/Config.ini)（CH570Q/CH570，`IsEraseAllCFlash=0`，因此**不会擦掉从机 `0xF0000` 的绑定信息**）。里面的 `swzUserFile1` 只是占位——**实际固件由命令行的 `-f` 指定**，所以从机/主机通用，不必各改一份。要自行重生成则用 Windows 版 `WchIspStudio.exe` 的「文件→保存配置」（[下载](https://www.wch.cn/downloads/WCHISPTool_Setup_exe.html)） |
| 工具 | 本机已有编译好的 **`~/.local/bin/WCHISPTool_CMD`（V3.70，实测 `-v tool` 正常）**，`isp-flash.sh` 会优先找它。子模块里的 `tools/wchisptool/bin/x64/WCHISPTool_CMD` 是官方预编译版，**要求 glibc ≥ 2.33，Ubuntu 20.04 跑不了**（报 `GLIBC_2.33 not found`），必要时按下方命令自行编译 |

```bash
# 编译出本机可用的 WCHISPTool_CMD（源码 + 静态库都在子模块里）
cd tools/wchisptool
g++ src/IspCmdTool.cpp -I lib/x64/dynamic \
    -o ~/.local/bin/WCHISPTool_CMD \
    lib/x64/static/libwch55xisp-4.0.0.a -lpthread
```

### 11.3 烧录

```bash
# Config.ini 用 skill 自带的那份（固件由 -f 指定，从机/主机通用）
CFG=skills/coder-ch570q-airttl/scripts/Config.ini

# 脚本方式（推荐）：-w 10 = 等待 10 秒，期间给 MCU 上电
skills/coder-ch570q-airttl/scripts/isp-flash.sh -c "$CFG" -f RF_Uart/build/RF_Uart.hex -w 10

# 只校验
skills/coder-ch570q-airttl/scripts/isp-flash.sh -c "$CFG" -f RF_Uart/build/RF_Uart.hex -o verify -w 10
```

等价的手工命令：

```bash
sudo ln -sfn /dev/ttyUSB0 /dev/ttyISP0        # 工具要求设备名叫 ttyISPx
sudo ~/.local/bin/WCHISPTool_CMD -p /dev/ttyISP0 -b 115200 \
     -c Config.ini -o program -f RF_Uart/build/RF_Uart.hex
```

### 11.4 实测要点（本机验证）

- **`Config.ini` 是必需品**：连 `-v boot` 这种只读查询都会因缺配置而失败，报
  `read configuration file or set isp option to device error`
- **`-v` 必须带参数**：`-v tool`（打印工具版本，不需要设备）或 `-v boot`（需 MCU 在 BOOT 模式）
- ⚠️ **失败时退出码也可能是 0**（实测 `-v boot` 失败仍返回 0）—— 判断成败要看它打印的 JSON：
  `{"Device":"...","Status":"Fail","Code":N,"Message":"..."}`，而不是看返回码
- CH343 在 WSL 里枚举为 `/dev/ttyACM0`；脚本会把它软链成工具要求的 `/dev/ttyISP0`
- 本机实测：工具版本 V3.70，`-v tool` 正常 → 工具与串口打开均通

### 11.5 参数与状态码

| 参数 | 含义 |
|---|---|
| `-p` | 设备：USB 用 `/dev/ch37x`；串口用 `/dev/ttyISPx` |
| `-b` | 串口波特率（115200 / 230400 …） |
| `-v` | 打印 boot / 工具版本 |
| `-c` | `Config.ini` |
| `-o` | `program` 下载 / `verify` 校验 |
| `-f` | `xxx.hex` 或 `xxx.bin` |
| `-r` | 下载前先解除代码保护 |

| 状态码 | 含义 |
|---|---|
| 0 | 成功 |
| 4 | 串口名称无效 |
| 5 | **未枚举到设备** —— 多半是没在等待窗口内上电 |
| 6 | 芯片类型与 `Config.ini` 不符 |
| 13 / 14 | 下载失败 / 校验失败 |

> **WSL 里只有串口方式可行**：USB 方式（`/dev/ch37x`）需要 `ch37x.ko` 内核模块，
> 而 WSL2 是定制内核、没有 `/lib/modules/$(uname -r)/build`，编不出模块。
> Windows 侧则两种都行（官方驱动现成）。
