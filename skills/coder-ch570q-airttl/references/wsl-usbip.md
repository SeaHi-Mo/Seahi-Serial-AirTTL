# WSL 下把 WCH-LinkE 映射进来（usbipd）

> 适用场景：**WCH-LinkE 插在 Windows 上，但编译/烧录/调试都在 WSL2 里做**。
> 不做这一步，WSL 里的 OpenOCD 会直接报 `Error: open failed`（`libusb` 根本看不到设备）。
>
> 本文流程在**本机实测通过**（Ubuntu 20.04 WSL2 + 内核 `6.6.114.1-microsoft-standard-WSL2` + `usbipd-win 5.2.0` + WCH-LinkE RV 2.21）。

---

## 一、原理：为什么需要 usbipd

```
CH570Q 目标板 ──SDI──▶ WCH-LinkE ──USB──▶ Windows ──usbipd-win──▶ [USB/IP] ──▶ WSL2 (vhci_hcd)
```

| 角色 | 位置 | 职责 |
|---|---|---|
| `usbipd.exe` | Windows | `bind` 把设备从 Windows 驱动手里接管/导出；`attach` 把它转发给 WSL |
| `vhci_hcd` | WSL 内核模块 | "虚拟 USB 主机控制器"，把转发来的设备当**本地 USB 设备**枚举 |
| `usbip` | WSL 用户态 | attach 握手的客户端 |

**三者缺一不可**，这也是最容易漏的地方：Windows 侧 bind 成功了，WSL 侧没加载 `vhci_hcd`，照样什么都看不到。

---

## 二、四个步骤

### 步骤 1：WSL 里加载 `vhci_hcd`

```bash
sudo modprobe vhci-hcd
ls /sys/bus/usb/devices        # 能列出来就说明加载成功
```

**判断依据：没有 `/sys/bus/usb` 这个目录 = `vhci_hcd` 没加载。**

模块文件在 `/lib/modules/$(uname -r)/kernel/drivers/usb/usbip/vhci-hcd.ko` —— WSL2 内核自带，
但**不会自动加载**，每次 WSL 重启后都要重新加载（见第五节）。

### 步骤 2：WSL 里让 `usbip` 客户端可用（本机踩过的坑）

Ubuntu 的 `/usr/bin/usbip` 来自 `linux-tools-common`，它是一层 **wrapper**：按 `uname -r` 去找
`/usr/lib/linux-tools/<内核版本>/usbip`。而 WSL 的自定义内核（如 `6.6.114.1-microsoft-standard-WSL2`）
在 apt 源里**没有对应的 linux-tools 包**，于是：

```
WARNING: usbip not found for kernel 6.6.114.1-microsoft
```

**解决**：把一个已存在的 usbip 软链到 PATH 更靠前的位置（usbip 是**用户态协议工具**，2.0 版本不必与内核严格对应）：

```bash
ls /usr/lib/linux-tools*/usbip             # 先看有哪些现成的
sudo ln -sf /usr/lib/linux-tools-5.4.0-77/usbip /usr/local/bin/usbip
hash -r && usbip version                   # 期望输出：usbip (usbip-utils 2.0)
```

### 步骤 3：Windows 侧 bind + attach（bind 需要管理员）

```powershell
usbipd list                                # 找 WCH-LinkE，例：9-1  1a86:8010  WCH-LinkRV, WCH-Link SERIAL (COM5)
usbipd bind --busid 9-1                    # 需管理员 → STATE: Not shared → Shared
usbipd attach --wsl --busid 9-1            # 转发给 WSL → STATE: Shared → Attached
```

- `bind` 是**持久**的（重启 Windows 后仍是 `Shared`）；`attach` 是**会话性**的（拔插 / WSL 重启后要重做）
- ⚠️ **bind 之前先关掉占用它的 Windows 程序**（串口助手、SeaHi Serial 里开着的 COM 口、MRS、WCH-LinkUtility），否则 attach 会失败
- 图形化等价操作：**SeaHi Serial 的 WSL 面板**勾选「映射到 WSL：COM5（WCH-LinkRV…）」= 自动完成 bind + attach
  （会弹 UAC 提权框，**需要点「是」**；若勾了没反应，去 app 日志找 `needs elevation, requesting user approval`）

### 步骤 4：在 WSL 里验证

```bash
lsusb                                        # Bus 001 Device 002: ID 1a86:8010 QinHeng Electronics WCH-Link
ls -l /dev/ttyACM0                           # WCH-LinkE 的串口接口，枚举为 CDC-ACM
cat /sys/devices/platform/vhci_hcd.0/status  # 对应端口 sta=006 即"已挂载"
```

本机实测输出：

```
Bus 001 Device 002: ID 1a86:8010 QinHeng Electronics WCH-Link
crw-rw---- 1 root dialout 166, 0 /dev/ttyACM0
```

---

## 三、⚠️ 映射成功后的两个后果

1. **Windows 侧的 COM5 会消失** —— 设备整体转给了 WSL。此后要用这路串口，就在 **WSL 里操作 `/dev/ttyACM0`**
   （前提：当前用户在 `dialout` 组内，`id` 能看到 `20(dialout)`）。
2. **重启 WSL 后要重做**：`vhci_hcd` 不会自动加载、attach 也失效。自动化见第五节。

---

## 四、OpenOCD 访问 WCH-LinkE 的权限问题（实测）

映射成功后，普通用户跑 OpenOCD 仍可能报：

```
Error: libusb_open() failed with LIBUSB_ERROR_ACCESS
Error: open failed
```

原因：USB 设备节点属主是 root，普通用户没有写权限：

```
crw-rw-r-- 1 root root 189, 1 /dev/bus/usb/001/002
```

**以 root 运行即可证明"链路本身是通的"**（本机实测）：

```
Info : WCH-LinkE  mode:RV version 2.21
Error:  WCH-Link failed to connect with riscvchip
```

> 看到第一行 `WCH-LinkE mode:RV version 2.21` 就说明 **映射 + wlinke 驱动 + 调试器通信全部正常**。
> 后面那句 `failed to connect with riscvchip` 是**目标芯片侧**的问题（固件关闭了调试口），
> 与 WSL 映射无关 —— 见 [flashing.md](./flashing.md) 第二节。

放开权限三选一：

```bash
# A. 临时（每次 attach 后做一次；设备号可能变，用 lsusb 确认）
sudo chmod a+rw /dev/bus/usb/001/002

# B. 持久：udev 规则（本机 systemd 已启用：/etc/wsl.conf 有 systemd=true，systemd-udevd 在跑）
sudo tee /etc/udev/rules.d/99-wch-link.rules >/dev/null <<'EOF'
# WCH-LinkE (1a86:8010)：放开权限，让普通用户能直接跑 OpenOCD
SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="8010", MODE="0666"
EOF
sudo udevadm control --reload-rules && sudo udevadm trigger

# C. 直接以 root 跑烧录脚本
sudo skills/coder-ch570q-airttl/scripts/flash.sh -m reset
```

> 方案 B 按 **VID:PID 匹配**，不依赖 busid 与设备号，重新 attach 后依然有效，**推荐**。

---

## 五、让它在 WSL 重启后自动就绪

`vhci_hcd` 的加载可以交给 `/etc/wsl.conf`（本机 systemd 已启用）：

```ini
# /etc/wsl.conf
[boot]
systemd=true
command=/sbin/modprobe vhci-hcd
```

或者用 systemd 服务：

```ini
# /etc/systemd/system/wch-link-vhci.service
[Unit]
Description=Load vhci_hcd for WCH-LinkE USB/IP passthrough
[Service]
Type=oneshot
ExecStart=/sbin/modprobe vhci-hcd
[Install]
WantedBy=multi-user.target
```

> **`attach` 无法自动完成** —— 它由 Windows 侧的 usbipd 决定。WSL 每次重启后，仍需在 Windows 执行一次
> `usbipd attach --wsl --busid 9-1`（或在 SeaHi Serial 的 WSL 面板重新勾一次「映射到 WSL」）。

---

## 六、没有 sudo 密码时的免密提权（本机实测可用）

WSL 里 `sudo` 要密码、而你又能操作 Windows 时，可以从 Windows 侧以 root 进入**同一个发行版**：

```bash
# 在 WSL 内部执行（或在 Windows PowerShell 里直接写 wsl -u root ...）
wsl.exe -u root --exec /sbin/modprobe vhci-hcd
wsl.exe -u root --exec ln -sf /usr/lib/linux-tools-5.4.0-77/usbip /usr/local/bin/usbip
```

Windows 侧的 bind 提权（会弹 UAC，需本人在机器上点「是」）：

```powershell
Start-Process usbipd -Verb RunAs -ArgumentList 'bind','--busid','9-1'
```

---

## 七、排错对照

| 现象 | 原因 / 解决 |
|---|---|
| `usbip not found for kernel <ver>` | wrapper 找不到该内核的工具 → 软链一份现成的 usbip（步骤 2） |
| `/sys/bus/usb` 不存在 | `vhci_hcd` 未加载 → `sudo modprobe vhci-hcd`（步骤 1） |
| `usbipd bind` 报权限 / 被拒 | 需要管理员 → 管理员 PowerShell，或 `Start-Process -Verb RunAs` |
| attach 卡住或失败 | 设备被 Windows 程序占用（串口助手 / MRS / WCH-LinkUtility）→ 全部关掉再试 |
| attach 成功但 WSL 看不到设备 | 步骤 2 的 usbip 客户端不可用；或 `dmesg \| tail` 看内核有没有枚举出 `1a86:8010` |
| `libusb_open() failed with LIBUSB_ERROR_ACCESS` | USB 节点权限 → 第四节 A / B / C |
| `WCH-Link failed to connect with riscvchip` | **映射已成功**，问题在目标芯片：固件关了调试口 → 断电重上电；或 SDI 接线 / GND → 见 [flashing.md](./flashing.md) 第二节 |
| Windows 里 COM5 不见了 | 正常：设备已转给 WSL，改用 WSL 的 `/dev/ttyACM0` |
| SeaHi Serial 勾了映射却没反应 | UAC 提权框没点「是」；app 日志会写 `needs elevation, requesting user approval` |

---

## 附：本机实测环境与结论

| 项 | 值 |
|---|---|
| WSL 发行版 | Ubuntu 20.04.6 LTS，内核 `6.6.114.1-microsoft-standard-WSL2`，**systemd 已启用** |
| Windows 侧 | `usbipd-win` **5.2.0** |
| 设备 | `9-1  1a86:8010  WCH-LinkRV, WCH-Link SERIAL (COM5)` |
| 映射结果 | ✅ WSL `lsusb` 见 `1a86:8010 QinHeng Electronics WCH-Link`；`/dev/ttyACM0` 出现；vhci 端口 `sta=006` |
| OpenOCD | ✅ `tools/openocd/bin/openocd` 认到 `WCH-LinkE mode:RV version 2.21` |
| 待办 | ⚠️ 普通用户跑 OpenOCD 报 `LIBUSB_ERROR_ACCESS`（以 root 正常）→ 需第四节的 udev 规则；目标芯片尚未连上（属预期） |
