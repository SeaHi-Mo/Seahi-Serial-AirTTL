#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DTR/RTS 无线直控验证（从机 PA3=DTR / PA2=RTS）—— 对应 2026-10-12 那个从机修复

被测的行为（全部来自 RF_Uart/APP/include/uart.h 与 rf_uart_tx.c；改常量要同步改这里）：

  · 断言 = 输出**低**（标准 TTL 低有效）：断言 DTR -> PA3 低、断言 RTS -> PA2 低
  · DTR **纯跟随**（进 Bootloader 要能把 BOOT0 一直按住），释放后 RESP 就回高
  · RTS 释放后**再保持低 RTS_RELEASE_HOLD_MS(30ms)** 才回高（保证目标吃到够宽的复位低电平）；
    保持窗口内主机又断言 -> 取消窗口继续压住；反复发同样的"已释放"状态**不续期**
  · 边沿生效走"ISR 只记最终值 + 时刻 -> 主循环到期搬 GPIO"：**连续翻转只保留最终值，且从最后一次起算**
  · 生效延迟 = DTR_RTS_RESP_MS(10ms)；端到端再加 USB(~1ms) + 空口(~1ms) ≈ 12ms
  · 断链 / 上电：两脚**立即回默认高**（不再保持最后一包电平，即不再把目标按在复位上）

四种模式（按你手上有什么观测手段挑）：

  selfcheck  只检查端口能否开、DTR/RTS 能否切、探头采样周期多少；不动时序。**先跑这个**。
  control    逐个场景驱动并打印时间线（断言/释放/快翻/不续期/窗口取消）。
             没有探头也能跑：配合示波器/逻辑分析仪人肉看波形。
  timing     需要**边沿探头**（--probe-port，见下）。测：边沿时延、抖动、保持时长、快翻合并、
             不续期、窗口取消；`--sweep-bauds` 还能证明"时延不再随波特率档位跳"
             （旧实现把 mDelaymS(10) 跑在 24MHz 档上真实要 41.7ms，所以老固件这一项会随档位变）。
  reset      需要一个**持续有输出**的目标板（--watch，默认就是 --port）。测"**PC 只给极窄脉冲，
             目标仍可靠复位**"—— 这正是 RTS 那 30ms 保持要保证的事：旧固件纯跟随 PC，5ms 脉冲
             可能不够；新固件会把低电平撑到 30ms+，所以 1~5ms 的脉冲也该 100% 复位成功。
  monitor    **被动盯引脚**：你去跑烧录工具，它实时打印 PA2/PA3 的每个边沿与低电平宽度。
             专治"烧录工具控制 RTS、从机不响应"——看是意图没到从机，还是被从机的
             10ms「只保留最终值」窗口吃掉（判读见 mode_monitor 的说明）。
  appopen    **测"打开监控那一瞬间"从机 PA2/PA3 到底被怎么了**（需要探头）。它不走 pyserial，
             而是经 MCP 调 serial_open —— 那一条就是"点界面上的开始监控"：上位机先
             `serialport::open()`（驱动会断言 DTR/RTS），紧接着按界面上 DTR/RTS 复选框的状态
             再写一次（`main.rs` 的 `write_data_terminal_ready/write_request_to_send`）。
             所以真实 CH340 上那个"打开即断言后释放"的脉冲**是上位机制造的，不是主机固件**。
             判读见 mode_appopen 的输出说明。

边沿探头（timing 模式）：**一个** USB-TTL 适配器，两个**输入脚**各接一根：
    PA2(RTS/RESET) -> 适配器 CTS（默认；可用 --probe-rts-bit 换 dsr/dcd/ri）
    PA3(DTR/BOOT0) -> 适配器 DSR（默认；可用 --probe-dtr-bit 换）
  脚本用 TIOCMGET 在一个紧循环里**同时**采这两根线（不重复开同一个口），记录跳变时刻，
  并**先报出实测采样周期**：采样周期 > 1ms 时"抖动 ≤1ms"这条判不准（USB 侧的状态上报
  本身就有 ms 级延迟），那时只能验 10ms/30ms 量级。**没有探头别用 timing 模式**。

  注意：不是所有 USB-TTL 都引出 CTS/DSR（很多只有 TX/RX/VCC/GND）。既然是要看"从机引脚上
  到底什么时候变"，用示波器/逻辑分析仪其实更直接 —— 那时用 --mode control 驱动、仪器看波形。

驱动方式（--drive）：
  port  pyserial 直接开串口设线（WSL 的 /dev/ttyUSB0，或 Windows 上先停掉 SeaHi Serial 对该
        端口的监控）—— **时间戳精确**（紧贴 ioctl），代价是独占端口。
  mcp   走 SeaHi Serial 面板同一条路（serial_set_lines），不抢端口；代价是**时间戳只有
        ±HTTP 往返**的精度（脚本会把它报出来），所以只适合验"发生没发生 / 保持多久"，
        不适合验 ≤1ms 抖动；且每步要等 1.1s 限流间隔（见 loss_test.py），会慢。

用法举例：
    # 0) 先自检（不动时序）
    python3 dtr_rts_test.py --mode selfcheck --port /dev/ttyUSB0

    # 1) 无探头：跑一遍控制场景，配合示波器看
    python3 dtr_rts_test.py --mode control --port /dev/ttyUSB0 --trials 10

    # 2) 有探头：测时延/抖动/保持时长，并证明不随波特率档位跳
    python3 dtr_rts_test.py --mode timing --port /dev/ttyUSB0 \
        --probe-port /dev/ttyUSB1 --trials 30 \
        --sweep-bauds 115200,9600,1500000 --csv dtr_rts.csv

    # 3) 复位可靠性（目标板要一直有输出；用窄脉冲验证 30ms 保持）
    python3 dtr_rts_test.py --mode reset --port /dev/ttyUSB0 --watch /dev/ttyUSB2 \
        --pulse-ms 1,3,5,10 --trials 10

⚠️ 打开串口本身常常会自动断言 DTR/RTS（Windows 上一片常见的"一连上就把目标按住"）。
   脚本开完端口会立刻把两者**释放**（= 两脚输出高），所以开始前目标可能已经被复位过一次。
⚠️ --port 上设的是**线码波特率**，跟 DTR/RTS 无关；sweep 时用"关闭 -> 改波特率 -> 重开"
   （只改输入框不会下发，见 loss_test.py 的结论）。
⚠️ 新固件**没有**"从机侧打点"输出，所以"从机什么时候真的把 GPIO 搬了"只能从引脚上量
   （探头/示波器）。若要把从机侧也变成可读数字，需要给从机加编译期 debug 打印 —— 见文末 TODO。
"""
import argparse
import csv
import os
import statistics
import sys
import threading
import time

try:
    import serial
except ImportError:
    sys.exit("需要 pyserial：pip install pyserial")

# ---- 固件规格（RF_Uart/APP/include/uart.h；改了要同步这里）--------------------
SPEC_RESP_MS = 10      # DTR_RTS_RESP_MS：采集延时
SPEC_HOLD_MS = 30      # RTS_RELEASE_HOLD_MS：RTS 释放后的保持低时间

# 探头可用的 modem 输入脚
MODEM_BITS = ("cts", "dsr", "dcd", "ri")


# --------------------------------------------------------------------------- #
# 驱动：把 DTR/RTS 发给主机（dongle）
# --------------------------------------------------------------------------- #
class PortDriver:
    """pyserial 直接设线。时间戳精确（紧贴 ioctl）。"""

    name = "port"

    def __init__(self, port, baud):
        self.port = port
        self.baud = baud
        self.ser = self._open(port, baud)
        self.uncertainty_ms = 0.0
        time.sleep(0.3)
        # 开端口常会自动断言 DTR/RTS —— 立刻释放（= 从机两脚输出高），别把目标按住
        self.set(dtr=False, rts=False)

    @staticmethod
    def _open(port, baud):
        return serial.Serial(port, baud, timeout=0.02)

    def reopen_baud(self, baud):
        """改线码：必须"关闭 -> 改波特率 -> 重开"（只改输入框不会下发）。"""
        self.ser.close()
        time.sleep(0.3)
        self.baud = baud
        self.ser = self._open(self.port, baud)
        time.sleep(0.5)
        self.set(dtr=False, rts=False)      # 重开可能又断言了，先释放

    def set(self, dtr=None, rts=None):
        t = time.perf_counter()
        if dtr is not None:
            self.ser.dtr = dtr
        if rts is not None:
            self.ser.rts = rts
        return t

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass


class McpDriver:
    """走 SeaHi Serial 面板同一条路（serial_set_lines），不独占端口。"""

    name = "mcp"

    def __init__(self, pane):
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        self.L = __import__("loss_test")          # 复用它的 MCP 客户端与限流
        self.pane = pane
        self.uncertainty_ms = 0.0
        self.set(dtr=False, rts=False)

    def set(self, dtr=None, rts=None):
        args = {"pane": self.pane}
        if dtr is not None:
            args["dtr"] = bool(dtr)
        if rts is not None:
            args["rts"] = bool(rts)
        t0 = time.perf_counter()
        self.L.call_tool("serial_set_lines", args)
        t1 = time.perf_counter()
        # 实际生效时刻落在 [t0, t1] 里，无法更准：把往返时间当不确定度报出去
        self.uncertainty_ms = (t1 - t0) * 1000.0
        return t1

    def reopen_baud(self, baud):
        print("   （--drive mcp 不改线码：需要 --drive port 才能验波特率档位那条）")

    def close(self):
        pass


class NullDriver:
    """不驱动任何东西的占位（appopen 模式：串口由 App 打开，脚本只当观察者）。

    刻意什么都不做：那时若再去 pyserial 开同一个口，会和 App 抢端口。"""

    name = "none"
    uncertainty_ms = 0.0

    def set(self, dtr=None, rts=None):
        return time.perf_counter()

    def reopen_baud(self, baud):
        pass

    def close(self):
        pass


def make_driver(args):
    d = McpDriver(args.pane) if args.drive == "mcp" else PortDriver(args.port, args.baud)
    if d.uncertainty_ms > 1.0:
        print(f"⚠ 驱动时间戳不确定度 ≈{d.uncertainty_ms:.1f}ms（--drive mcp 的 HTTP 往返）"
              f"—— 只能验 ms 级以上的差异，别拿它判 ≤1ms 抖动\n")
    return d


# --------------------------------------------------------------------------- #
# 探头：一个端口、一次循环同时采样两路 modem 输入线
# --------------------------------------------------------------------------- #
class ModemProbe(threading.Thread):
    """紧循环 TIOCMGET：一次 ioctl 读回全部状态位，两路各自记跳变。

    这不是"万用表"，是"会报出自己采样周期的时间戳器"——采样周期取决于适配器与驱动
    对 modem 状态的上报方式，脚本会实测出来并据此给出可信度。
    """

    daemon = True

    def __init__(self, port, bit_of):
        """bit_of: {"dtr": "dsr", "rts": "cts"}（键是我们要量的线，值是适配器输入脚）"""
        super().__init__()
        self.port = port
        self.bit_of = bit_of
        self.ser = serial.Serial(port, 115200, timeout=0)
        self.fd = self.ser.fileno()
        self.mask_of = {k: self._mask(v) for k, v in bit_of.items()}
        self.edges = {k: [] for k in bit_of}
        self._last = {k: None for k in bit_of}
        self._stop = False
        self._polls = 0
        self._t0 = time.perf_counter()

    @staticmethod
    def _mask(bit):
        import termios
        return {"cts": termios.TIOCM_CTS, "dsr": termios.TIOCM_DSR,
                "dcd": termios.TIOCM_CAR, "ri": termios.TIOCM_RNG}[bit]

    def _read_raw(self):
        import array
        import fcntl
        import termios
        buf = array.array("i", [0])
        fcntl.ioctl(self.fd, termios.TIOCMGET, buf, True)
        return buf[0]

    def run(self):
        last_t = self._t0
        while not self._stop:
            raw = self._read_raw()
            t = time.perf_counter()
            self._polls += 1
            for k, mask in self.mask_of.items():
                v = bool(raw & mask)
                if self._last[k] is not None and v != self._last[k]:
                    self.edges[k].append((t, v, t - last_t))
                self._last[k] = v
            last_t = t

    def reset(self, key=None):
        if key is None:
            for k in self.edges:
                self.edges[k] = []
        else:
            self.edges[key] = []

    def wait_edge(self, key, timeout=0.5):
        """等 key 这条线上的一条新边沿，返回 (时刻, 电平) 或 None。"""
        n0 = len(self.edges[key])
        t_end = time.perf_counter() + timeout
        while time.perf_counter() < t_end:
            if len(self.edges[key]) > n0:
                t, v, _ = self.edges[key][n0]
                return t, v
            time.sleep(0.0002)
        return None

    def level(self, key):
        return self._last[key]

    def n_edges(self, key):
        return len(self.edges[key])

    def poll_period_ms(self):
        if self._polls < 2:
            return None
        return (time.perf_counter() - self._t0) * 1000.0 / self._polls

    def stop(self):
        self._stop = True
        try:
            self.ser.close()
        except Exception:
            pass


# --------------------------------------------------------------------------- #
# 小工具
# --------------------------------------------------------------------------- #
def hold(ms):
    time.sleep(ms / 1000.0)


def stats(vals):
    if not vals:
        return None
    return {"n": len(vals), "min": min(vals), "med": statistics.median(vals),
            "p90": sorted(vals)[max(0, int(len(vals) * 0.9) - 1)], "max": max(vals)}


def show_stats(label, vals, expect=None, tol=5.0):
    s = stats(vals)
    if not s:
        print(f"  {label:<22} (无样本)")
        return
    line = (f"  {label:<22} n={s['n']:<3} 最小{s['min']:7.2f}  中位{s['med']:7.2f}  "
            f"p90{s['p90']:7.2f}  最大{s['max']:7.2f}  ms")
    if expect is not None:
        ok = abs(s["med"] - expect) <= tol
        extra = "" if s["n"] < 2 else f"  抖动(max-min) {s['max'] - s['min']:.2f}ms"
        line += f"  | 期望≈{expect:g}ms {'✔' if ok else '✘'}{extra}"
    print(line)


class Recorder:
    """把每次试验落 CSV，便于事后画图。"""

    def __init__(self, path):
        self.rows = []
        self.path = path

    def add(self, **kw):
        self.rows.append(kw)

    def flush(self):
        if not self.path:
            return
        keys = ["mode", "scene", "baud", "trial", "drive_ms", "edge_ms",
                "lat_ms", "edge", "note"]
        with open(self.path, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=keys, extrasaction="ignore")
            w.writeheader()
            for r in self.rows:
                w.writerow(r)
        print(f"→ 已写 {self.path}（{len(self.rows)} 行）")


def probe_bits(args):
    if args.probe_dtr_bit == args.probe_rts_bit:
        sys.exit("--probe-dtr-bit 与 --probe-rts-bit 不能是同一个输入脚"
                 "（PA3 和 PA2 要接到两个不同的脚上）")
    return {"dtr": args.probe_dtr_bit, "rts": args.probe_rts_bit}


# --------------------------------------------------------------------------- #
# 模式实现
# --------------------------------------------------------------------------- #
def mode_selfcheck(drv, args):
    print(f"驱动：{drv.name}   探针：{'有' if args.probe_port else '无'}")
    print("  ① 端口可开、DTR/RTS 可切 …… ", end="", flush=True)
    try:
        drv.set(dtr=False, rts=False)
        drv.set(dtr=True, rts=False)
        drv.set(dtr=False, rts=True)
        drv.set(dtr=False, rts=False)
        print("OK（最后停在：两脚都输出高 = 未断言）")
    except Exception as e:
        print(f"失败：{e}")
        return 1
    if args.probe_port:
        bits = probe_bits(args)
        print(f"  ② 探针 {args.probe_port} "
              f"DTR←{bits['dtr']} / RTS←{bits['rts']} …… ", end="", flush=True)
        try:
            p = ModemProbe(args.probe_port, bits)
            p.start()
            hold(300)
            per = p.poll_period_ms()
            print(f"采样周期≈{per:.3f}ms"
                  + ("  ⚠ >1ms：判不了 ≤1ms 抖动" if per and per > 1 else "  （够用）"))
            p.stop()
        except Exception as e:
            print(f"失败：{e}（适配器没引出这个脚 / 驱动不支持 TIOCMGET）")
    print("\n接线（断言 = 低）：")
    print("  · 从机 PA2(RTS/RESET) -> 探头输入脚（默认 cts）")
    print("  · 从机 PA3(DTR/BOOT0) -> 另一个探头输入脚（默认 dsr）")
    print("  · 探头地 —— 必须和从机共地")
    return 0


def mode_control(drv, args, rec):
    """逐个场景驱动；没有探头也能跑（人肉/示波器看波形）。"""
    t = args.trials
    print(f"\n控制场景（{t} 轮，无条件断言 {args.hold_assert_ms:g}ms；"
          f"断言=输出低，释放=输出高）")
    for i in range(t):
        drv.set(dtr=False, rts=False)
        hold(150)
        t0 = drv.set(dtr=True, rts=False)          # PA3 低
        hold(args.hold_assert_ms)
        t1 = drv.set(dtr=False, rts=False)         # PA3 高；PA2 一直高
        hold(150)
        rec.add(mode="control", scene="dtr_follow", trial=i,
                drive_ms=round((t1 - t0) * 1000, 3))
    print(f"  dtr_follow  x{t} 完成（PA3 应：低 -> 高，纯跟随，无自动释放）")

    for i in range(t):
        drv.set(dtr=False, rts=False)
        hold(150)
        t0 = drv.set(dtr=False, rts=True)          # PA2 低
        hold(args.pulse_ms_default)
        t1 = drv.set(dtr=False, rts=False)         # 保持 SPEC_HOLD_MS 后才高
        hold(150)
        rec.add(mode="control", scene="rts_pulse", trial=i,
                drive_ms=round((t1 - t0) * 1000, 3),
                note=f"PC 脉冲{args.pulse_ms_default:g}ms")
    print(f"  rts_pulse   x{t} 完成（PC 只给 {args.pulse_ms_default:g}ms，"
          f"PA2 低电平应被撑到 ≈{SPEC_RESP_MS + SPEC_HOLD_MS:g}ms）")

    # 连续翻转只保留最终值：一串"微秒级"翻转后停在断言
    drv.set(dtr=False, rts=False)
    hold(150)
    t0 = time.perf_counter()
    for k in range(args.burst):
        drv.set(dtr=bool(k % 2))
    t1 = time.perf_counter()
    drv.set(dtr=True)                              # 最终值 = 断言
    hold(300)
    drv.set(dtr=False)
    rec.add(mode="control", scene="burst", trial=0,
            drive_ms=round((t1 - t0) * 1000, 3),
            note=f"{args.burst} 次翻转，{(t1 - t0) * 1000 / args.burst:.3f}ms/次，最后断言")
    print(f"  burst       x1 完成（{args.burst} 次翻转、平均 "
          f"{(t1 - t0) * 1000 / args.burst:.3f}ms/次 —— 从机只应保留最终值=低，"
          f"且从最后一次起算）")
    return 0


def mode_timing(drv, args, rec):
    """需要探头：时延 / 抖动 / 保持时长 / 快翻合并 / 不续期 / 窗口取消（+ 波特率档位扫描）。"""
    probe = ModemProbe(args.probe_port, probe_bits(args))
    probe.start()
    hold(300)
    per = probe.poll_period_ms()
    print(f"\n探头采样周期≈{per:.3f}ms（{'⚠ >1ms：抖动量与 ≤1ms 判据不可信，只能看 10ms/30ms 量级' if per > 1 else '够用'}）")
    dx = drv.uncertainty_ms

    bauds = [int(x) for x in args.sweep_bauds.split(",")] if args.sweep_bauds else [None]
    all_lat = {}
    for baud in bauds:
        if baud is not None:
            drv.reopen_baud(baud)
        tag = f"baud={baud}" if baud else "单档"
        print(f"\n=== {tag}（{args.trials} 轮）===")
        lat_dtr, lat_rts, rise_rts, rise_dtr = [], [], [], []
        miss = 0
        for i in range(args.trials):
            # --- DTR 断言 -> PA3 下降
            drv.set(dtr=False, rts=False)
            hold(120)
            probe.reset("dtr")
            t0 = drv.set(dtr=True)
            e = probe.wait_edge("dtr", args.edge_timeout)
            if not e:
                miss += 1
                rec.add(mode="timing", scene="dtr_assert", baud=baud, trial=i, edge=0)
            else:
                lat = (e[0] - t0) * 1000 - dx
                lat_dtr.append(lat)
                rec.add(mode="timing", scene="dtr_assert", baud=baud, trial=i,
                        lat_ms=round(lat, 3), edge=1)
            hold(120)
            probe.reset("dtr")
            t1 = drv.set(dtr=False)
            e = probe.wait_edge("dtr", args.edge_timeout)
            if e:
                rise_dtr.append((e[0] - t1) * 1000 - dx)
                rec.add(mode="timing", scene="dtr_release_rise", baud=baud, trial=i,
                        lat_ms=round((e[0] - t1) * 1000 - dx, 3), edge=1)

            # --- RTS 断言 -> PA2 下降；释放 -> 保持 SPEC_HOLD_MS 后上升
            drv.set(dtr=False, rts=False)
            hold(120)
            probe.reset("rts")
            t2 = drv.set(rts=True)
            e = probe.wait_edge("rts", args.edge_timeout)
            if not e:
                miss += 1
                rec.add(mode="timing", scene="rts_assert", baud=baud, trial=i, edge=0)
            else:
                lat = (e[0] - t2) * 1000 - dx
                lat_rts.append(lat)
                rec.add(mode="timing", scene="rts_assert", baud=baud, trial=i,
                        lat_ms=round(lat, 3), edge=1)
            hold(120)
            probe.reset("rts")
            t3 = drv.set(rts=False)
            e = probe.wait_edge("rts", args.edge_timeout)
            if e:
                rise_rts.append((e[0] - t3) * 1000 - dx)
                rec.add(mode="timing", scene="rts_release_rise", baud=baud, trial=i,
                        lat_ms=round((e[0] - t3) * 1000 - dx, 3), edge=1)

        show_stats("DTR 断言(从机侧)", lat_dtr, SPEC_RESP_MS + 2, args.tolerance_ms)
        show_stats("RTS 断言(从机侧)", lat_rts, SPEC_RESP_MS + 2, args.tolerance_ms)
        show_stats("DTR 释放->回高", rise_dtr, SPEC_RESP_MS, args.tolerance_ms)
        show_stats("RTS 释放->回高", rise_rts, SPEC_RESP_MS + SPEC_HOLD_MS, args.tolerance_ms)
        if miss:
            print(f"  ⚠ 漏边沿 {miss} 次（断言没生效 —— ISR/主循环竞态那类问题）")
        if baud:
            all_lat[baud] = (stats(lat_dtr), stats(lat_rts))
        if per > 1:
            print("  ⚠ 采样周期 >1ms：上面「抖动」列不要当结论上报")

    if len(all_lat) > 1:
        print("\n档位一致性（旧固件这里会随档位跳：mDelaymS 在 24MHz 档真实 41.7ms）")
        for k, (sd, sr) in all_lat.items():
            f = lambda s: "—" if not s else f"{s['med']:.2f}ms"
            print(f"  baud={k:<8} DTR 断言中位 {f(sd):<10} RTS 断言中位 {f(sr)}")
        print(f"  ⚠ sweep 结束后 --port 停在最后一个档位（{baud}），要复位请手动改回")

    # --- 反复发"已释放"不续期：第二次释放不该把上升沿推后
    drv.set(rts=False)
    hold(150)
    t_a = drv.set(rts=True)
    probe.wait_edge("rts", 0.5)
    hold(120)
    t_b = drv.set(rts=False)              # 第一次释放，窗口开始
    hold(5)
    drv.set(rts=False)                    # 5ms 后重复同样状态 —— 不应续期
    e = probe.wait_edge("rts", 0.5)
    if e:
        d = (e[0] - t_b) * 1000 - dx
        ok = abs(d - (SPEC_RESP_MS + SPEC_HOLD_MS)) <= args.tolerance_ms
        print(f"  不续期：重复发已释放后，上升沿仍在 {d:.2f}ms"
              f"（期望≈{SPEC_RESP_MS + SPEC_HOLD_MS:g}ms）{'✔' if ok else '✘ 被续期了'}")
        rec.add(mode="timing", scene="no_renew", trial=0, lat_ms=round(d, 3), edge=1)

    # --- 保持窗口内又断言：取消窗口，继续压住
    drv.set(rts=False)
    hold(150)
    drv.set(rts=True)
    hold(60)
    t2 = drv.set(rts=False)               # 窗口开始
    hold(15)                              # 15ms < 40ms，仍在窗口里
    t3 = drv.set(rts=True)                # 又断言 -> 取消窗口
    hold(60)
    probe.reset("rts")
    t4 = drv.set(rts=False)               # 重新释放，从这个时刻起算新窗口
    e = probe.wait_edge("rts", 0.5)
    if e:
        d = (e[0] - t4) * 1000 - dx
        early = (e[0] - t2) * 1000 - dx
        ok = abs(d - (SPEC_RESP_MS + SPEC_HOLD_MS)) <= args.tolerance_ms
        print(f"  窗口取消：再次断言后重新释放，上升沿距最后一次释放 {d:.2f}ms"
              f"（期望≈{SPEC_RESP_MS + SPEC_HOLD_MS:g}ms）{'✔' if ok else '✘'}；"
              f"距第一次释放 {early:.2f}ms（应 >{SPEC_RESP_MS + SPEC_HOLD_MS:g}ms，"
              f"说明窗口确实被取消了）")
        rec.add(mode="timing", scene="cancel_window", trial=0, lat_ms=round(d, 3), edge=1)

    # --- 快翻只保留最终值：一串翻转后停在断言，只应看到极少边沿
    drv.set(dtr=False, rts=False)
    hold(150)
    probe.reset("dtr")
    for k in range(args.burst):
        drv.set(dtr=bool(k % 2))
    drv.set(dtr=True)
    hold(200)
    final = probe.level("dtr")
    print(f"  快翻合并：{args.burst} 次翻转只产生 {probe.n_edges('dtr')} 个边沿、"
          f"最终电平={'低(断言)' if final is False else '高(释放)' if final else '?'}"
          f"（期望最终=低）")
    rec.add(mode="timing", scene="burst_coalesce", trial=0, edge=1,
            note=f"{probe.n_edges('dtr')} edges, final={final}")
    drv.set(dtr=False)

    probe.stop()
    return 0


def mode_reset(drv, args, rec):
    """目标板复位可靠性：PC 只给极窄脉冲，新固件的 30ms 保持应保证 100% 复位。

    观测方式（--watch-mode）：
      gap     目标一直有周期性输出，复位时会出现一段静默，随后恢复 -> 记为一次成功复位
      banner  目标启动会打出匹配 --banner 的行 -> 每出现一次记为一次成功复位
    """
    import re

    if args.watch == args.port and isinstance(drv, PortDriver):
        watch = drv.ser                                     # 同一个口：读 + 设线共用
    else:
        watch = serial.Serial(args.watch, args.baud, timeout=0.01)

    def drain(seconds):
        """读 `seconds` 秒，返回 [(时刻, bytes)]。"""
        out = []
        t_end = time.perf_counter() + seconds
        while time.perf_counter() < t_end:
            n = watch.in_waiting
            if n:
                out.append((time.perf_counter(), watch.read(n)))
            else:
                time.sleep(0.002)
        return out

    # --- 先确认目标确实在"持续"输出（gap 模式要求它平时没有 >= gap_ms 的静默）
    pre = drain(1.0)
    tot = sum(len(b) for _, b in pre)
    if tot == 0:
        print(f"⚠ --watch {args.watch} 1 秒内没有数据。reset 模式需要一个**会持续输出**的目标板"
              f"（或先给它发个让它吐数据的命令），或改用 --watch-mode banner。")
        return 2
    arrivals = [t for t, _ in pre]
    base_gap = max((arrivals[i + 1] - arrivals[i]) * 1000
                   for i in range(len(arrivals) - 1)) if len(arrivals) > 1 else 0.0
    print(f"目标有输出（1s 内 {tot} B，平时最大静默 {base_gap:.0f}ms）")
    if args.watch_mode == "gap" and base_gap >= args.gap_ms * 0.8:
        print(f"⚠ 目标本身输出就不连续（正常静默 {base_gap:.0f}ms ≥ 判据 {args.gap_ms:g}ms 的 80%），"
              f"gap 模式会把'它自己没说话'误判成复位 —— 请改用 --watch-mode banner（目标启动横幅）。")
        return 2

    for pulse in [float(x) for x in args.pulse_ms.split(",")]:
        ok = 0
        lat = []
        for i in range(args.trials):
            drv.set(rts=False)
            hold(200)
            drain(0.25)                                   # 排空
            t1 = time.perf_counter()
            drv.set(rts=True)                             # 目标 RESET 拉低
            hold(pulse)                                   # PC 只给这么窄（1~10ms）
            drv.set(rts=False)                            # 从机应把低电平撑到 30ms+
            t_release = time.perf_counter()
            got = _wait_reset(args, watch, t1, t_release)
            if got:
                ok += 1
                lat.append(got)
                rec.add(mode="reset", scene="reset", trial=i,
                        lat_ms=round(got, 3), edge=1, note=f"pulse={pulse:g}ms")
            else:
                rec.add(mode="reset", scene="reset", trial=i, edge=0,
                        note=f"pulse={pulse:g}ms 无复位证据")
        extra = f"  释放->恢复 中位{statistics.median(lat):.1f}ms" if lat else ""
        flag = "✔" if ok == args.trials else "✘"
        print(f"  PC 脉冲 {pulse:>4.1f}ms：复位成功 {ok}/{args.trials} "
              f"({ok * 100.0 / args.trials:.0f}%) {flag}{extra}")
    return 0


def _wait_reset(args, watch, t_assert, t_release):
    """等复位证据，返回"释放->恢复"毫秒数；没等到返回 None。

    gap 模式：从 t_release 起算，等到出现一次 >= gap_ms 的静默之后数据恢复。
    banner 模式：等到匹配 --banner 的行（从 t_release 起算）。
    """
    import re

    pat = re.compile(args.banner.encode()) if (args.watch_mode == "banner" and args.banner) else None
    last_data = time.perf_counter()
    saw_gap = False
    buf = b""
    t_end = time.perf_counter() + args.reset_timeout
    while time.perf_counter() < t_end:
        n = watch.in_waiting
        if n:
            chunk = watch.read(n)
            now = time.perf_counter()
            if pat is not None:
                buf += chunk
                if pat.search(buf):
                    return (now - t_release) * 1000
            else:
                if saw_gap:
                    return (now - t_release) * 1000
                last_data = now
        else:
            if pat is None and not saw_gap and \
                    (time.perf_counter() - last_data) * 1000 >= args.gap_ms:
                saw_gap = True                    # 静默已达标 -> 等数据恢复即是复位证据
            time.sleep(0.002)
    return None


def _fmt_edges(edges, t_ref):
    """把边沿列表格式化成 '低@12.3ms 高@52.4ms' 这种。"""
    out = []
    for t, lv, _ in edges:
        out.append(f"{'低' if lv is False else '高'}@{(t - t_ref) * 1000:.1f}ms")
    return " ".join(out) if out else "（无）"


def _low_width(edges, t_ref, still_low_at_end):
    """从边沿里算"低电平持续了多久"（拿第一个下降沿到随后的上升沿）。"""
    low_t = None
    for t, lv, _ in edges:
        if lv is False and low_t is None:
            low_t = t
        elif lv is True and low_t is not None:
            return (t - low_t) * 1000, (low_t - t_ref) * 1000
    if low_t is not None and still_low_at_end:
        return None, (low_t - t_ref) * 1000      # 还在低 -> 一直按住
    return None, None


def mode_appopen(drv, args, rec):
    """打开监控那一下，从机 PA2/PA3 被怎么了（走 App 自己的打开路径 = MCP serial_open）。

    上位机 `open_port()` 的顺序是：`serialport::open()`（驱动可能先断言一次）→ 紧接着按界面
    DTR/RTS 复选框的状态 `write_data_terminal_ready/write_request_to_send`。所以：
      · 复选框**未勾**（= 未断言）时，若那次断言落在从机 10ms 合并窗口内 -> **什么都不发生**；
        若落在窗口外 -> 出现一次窄低脉冲（RTS 会被撑到 40ms 左右）；
      · 复选框**勾上**时 -> App 明确要求一直断言 -> 从机**一直拉低**，目标被按住（README 那个坑）。
    """
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    L = __import__("loss_test")               # 复用 MCP 客户端

    if not args.probe_port:
        sys.exit("appopen 模式需要 --probe-port：这条必须「看引脚」，没有探头只能靠猜")

    probe = ModemProbe(args.probe_port, probe_bits(args))
    probe.start()
    hold(300)
    per = probe.poll_period_ms()
    print(f"\n探头采样周期≈{per:.3f}ms"
          + ("  ⚠ >1ms：窄脉冲可能被漏采样" if per > 1 else ""))

    # 打开前：App 会把这两个开关的状态写下去，先读出来（决定后面怎么判读）
    st = L.call_tool("serial_get_state", {"pane": args.pane})
    sc = st.get("result", {}).get("structuredContent") or {}
    print(f"App 当前 DTR={sc.get('dtr')} RTS={sc.get('rts')}（打开监控时它会按这两个值写一次）")
    print(f"打开前电平：DTR(PA3)={'低' if probe.level('dtr') is False else '高'}  "
          f"RTS(PA2)={'低' if probe.level('rts') is False else '高'}")

    probe.reset()
    t0 = time.perf_counter()
    L.call_tool("serial_open", {"pane": args.pane})
    t1 = time.perf_counter()
    print(f"\n已开监控（MCP 往返 {1000 * (t1 - t0):.0f}ms）—— 观察 {args.app_open_watch_s:g}s")
    hold(args.app_open_watch_s)
    for key, pin in (("dtr", "PA3"), ("rts", "PA2")):
        edges = probe.edges[key]
        w, at = _low_width(edges, t0, probe.level(key) is False)
        print(f"  {pin}: {_fmt_edges(edges, t0)}")
        if at is None:
            print(f"       → 打开监控**没有**动这根脚（断言被 {SPEC_RESP_MS:g}ms 合并窗口吃掉了）")
            rec.add(mode="appopen", scene=f"{key}_open", trial=0, edge=0)
        elif w is None and probe.level(key) is False:
            print(f"       → ⚠ 打开后一直保持低（从 {at:.1f}ms 起）—— 目标被按住了！"
                  f"（把界面上这颗复选框取消勾选）")
            rec.add(mode="appopen", scene=f"{key}_open", trial=0, edge=1,
                    lat_ms=round(at, 3), note="held low")
        else:
            print(f"       → 打开时给了一次低脉冲：从 {at:.1f}ms 起、宽 {w:.1f}ms")
            rec.add(mode="appopen", scene=f"{key}_open", trial=0, edge=1,
                    lat_ms=round(at, 3), note=f"low {w:.1f}ms")
    print(f"  说明：RTS 的低脉冲若 ≥{SPEC_RESP_MS + SPEC_HOLD_MS:g}ms"
          f"（采集 {SPEC_RESP_MS:g}ms + 释放后保持 {SPEC_HOLD_MS:g}ms）属正常；"
          f"<{SPEC_RESP_MS:g}ms 或干脆没有，就是命中了合并窗口。")

    # 关闭：看两脚是否回到默认高（关端口时驱动会释放）
    probe.reset()
    t2 = time.perf_counter()
    L.call_tool("serial_close", {"pane": args.pane})
    hold(1.0)
    print(f"\n已关监控 —— 关闭后 {args.app_open_watch_s:g}s 内：")
    for key, pin in (("dtr", "PA3"), ("rts", "PA2")):
        print(f"  {pin}: {_fmt_edges(probe.edges[key], t2)}"
              f"   现在={'低' if probe.level(key) is False else '高'}")
    probe.stop()
    return 0


def mode_monitor(drv, args, rec):
    """被动盯引脚：你去跑烧录工具，这里实时打印 PA2/PA3 的每个边沿与低电平宽度。

    这条专治"烧录工具控制 RTS，从机不响应"这类问题 —— 关键是分清丢在哪一层：
      · 工具切了 RTS，但**这里一个边沿都没有** → 意图没到从机：
        主机驱动/平台的请求路径没被固件解析（或脉冲窄到主机那 1 位标志就没了）；
      · 有边沿，但**只看到释放、没看到断言**（或低电平只有几 ms） → 被从机
        `DTR_RTS_RESP_MS`(10ms) 的"只保留最终值"窗口吃掉了；
      · 有边沿且低电平 ≈40ms（10ms 采集 + 30ms 保持）却**还是烧不进** →
        那是低电平太长/时序不匹配把工具的握手窗口顶掉了，要调的是 DTR_RTS_RESP_MS /
        RTS_RELEASE_HOLD_MS，不是"没响应"。
    """
    if not args.probe_port:
        sys.exit("monitor 模式需要 --probe-port（要盯的就是从机那两根脚）")
    probe = ModemProbe(args.probe_port, probe_bits(args))
    probe.start()
    hold(300)
    per = probe.poll_period_ms()
    print(f"\n探头采样周期≈{per:.3f}ms"
          + ("  ⚠ >1ms：比这更窄的脉冲可能漏采，结论要保守" if per > 1 else ""))
    print("接线：从机 PA2(RTS/RESET)→探头 cts、PA3(DTR/BOOT0)→探头 dsr，必须共地")
    lim = f"{args.monitor_s:g}s" if args.monitor_s > 0 else "Ctrl-C"
    print(f"起点电平：PA2={'低' if probe.level('rts') is False else '高'}  "
          f"PA3={'低' if probe.level('dtr') is False else '高'}")
    print(f"现在去跑你的烧录工具 —— 实时打印边沿（{lim}）：\n")
    probe.reset()                      # 只报"开始观察之后"的边沿，时间戳才不会为负
    seen = {k: 0 for k in probe.edges}
    low_from = {}
    lows = {"rts": [], "dtr": []}
    t_start = time.perf_counter()
    try:
        while args.monitor_s <= 0 or (time.perf_counter() - t_start) < args.monitor_s:
            for key, pin in (("rts", "PA2/RESET"), ("dtr", "PA3/BOOT0")):
                edges = probe.edges[key]
                while seen[key] < len(edges):
                    t, lv, _ = edges[seen[key]]
                    seen[key] += 1
                    rel = t - t_start
                    if lv is False:
                        low_from[key] = t
                        print(f"[{rel:9.3f}s] {pin:<11} ↓ 低（断言）", flush=True)
                    else:
                        w = (t - low_from.pop(key)) * 1000 if key in low_from else None
                        if w is not None:
                            lows[key].append(w)
                        print(f"[{rel:9.3f}s] {pin:<11} ↑ 高"
                              + (f"   ← 低电平持续 {w:.1f}ms" if w is not None else ""),
                              flush=True)
            time.sleep(0.002)
    except KeyboardInterrupt:
        print("\n（Ctrl-C 结束）")

    print("\n—— 小结 ——")
    for key, pin in (("rts", "PA2/RESET(RTS)"), ("dtr", "PA3/BOOT0(DTR)")):
        n_low = len(lows[key])
        held = "（结束时仍为低）" if key in low_from else ""
        s = stats(lows[key])
        extra = (f"，低电平 最小{s['min']:.1f} / 中位{s['med']:.1f} / 最大{s['max']:.1f}ms"
                 if s else "")
        print(f"  {pin:<16} 断言 {n_low} 次{extra}{held}")
        rec.add(mode="monitor", scene=key, trial=n_low,
                note=f"lows={','.join('%.1f' % x for x in lows[key][:20])}")
    n_rts = len(lows["rts"])
    if n_rts == 0:
        print("  → 引脚上**一个 RTS 断言都没有**。两种可能，要再分一步：")
        print("     (i)  工具那次控制根本没变成主机固件认识的 modem 请求（平台/驱动路径不通），"
              "或者窄到连主机那 1 位待发标志都盖掉了；")
        print(f"     (ii) 从机收到了，但被「只保留最终值 + 等 {SPEC_RESP_MS:g}ms」的窗口吃掉"
              f"（工具断言后 <{SPEC_RESP_MS:g}ms 就释放）。")
        print(f"     区分办法：把从机 uart.h 的 DTR_RTS_RESP_MS 临时改成 0 重烧再测 —— "
              f"若这次能看到断言，就是 (ii)，改从机；若还是没有，就是 (i)，查主机/平台。")
    elif min(lows["rts"]) + 1.0 < SPEC_HOLD_MS:
        print(f"  → 断言确实落到了引脚，但最短低电平只有 {min(lows['rts']):.1f}ms"
              f"（少于释放后应保持的 {SPEC_HOLD_MS:g}ms）—— 保持窗口没生效，查 RTS 那段窗口逻辑")
    else:
        print(f"  → 断言都落到了引脚、低电平宽度也正常（≥{SPEC_HOLD_MS:g}ms 保持窗口）："
              f"问题不在「有没有响应」，而在低电平宽度/时序是否匹配工具的握手窗口")
    probe.stop()
    return 0


# --------------------------------------------------------------------------- #
def main():
    global SPEC_RESP_MS, SPEC_HOLD_MS
    ap = argparse.ArgumentParser(description="DTR/RTS 无线直控验证（从机 PA3=DTR / PA2=RTS）")
    ap.add_argument("--drive", choices=["port", "mcp"], default="port")
    ap.add_argument("--port", default="/dev/ttyUSB0", help="主机数据口（dongle）")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--pane", default="main", help="--drive mcp 时的分栏")
    ap.add_argument("--mode",
                    choices=["selfcheck", "control", "timing", "reset", "appopen", "monitor"],
                    default="selfcheck")
    ap.add_argument("--trials", type=int, default=10)
    ap.add_argument("--hold-assert-ms", type=float, default=200.0)
    ap.add_argument("--pulse-ms-default", type=float, default=5.0,
                    help="control 模式里 RTS 的 PC 脉冲宽度")
    ap.add_argument("--burst", type=int, default=20, help="连续翻转次数")
    ap.add_argument("--edge-timeout", type=float, default=0.5)
    ap.add_argument("--expect-resp-ms", type=float, default=SPEC_RESP_MS,
                    help="从机 DTR_RTS_RESP_MS（uart.h）")
    ap.add_argument("--expect-hold-ms", type=float, default=SPEC_HOLD_MS,
                    help="从机 RTS_RELEASE_HOLD_MS（uart.h）")
    ap.add_argument("--tolerance-ms", type=float, default=5.0)
    ap.add_argument("--probe-port", help="边沿探头（timing 模式必需）")
    ap.add_argument("--probe-dtr-bit", choices=MODEM_BITS, default="dsr",
                    help="PA3(DTR) 接在探头的哪个输入脚")
    ap.add_argument("--probe-rts-bit", choices=MODEM_BITS, default="cts",
                    help="PA2(RTS) 接在探头的哪个输入脚")
    ap.add_argument("--sweep-bauds", default="", help="timing 模式扫这些档位，如 115200,9600,1500000")
    ap.add_argument("--watch", help="reset 模式观测端口（默认与 --port 同一个）")
    ap.add_argument("--watch-mode", choices=["gap", "banner"], default="gap")
    ap.add_argument("--banner", default="", help="--watch-mode banner 时的正则")
    ap.add_argument("--gap-ms", type=float, default=30.0, help="gap 模式的静默判据")
    ap.add_argument("--reset-timeout", type=float, default=2.0)
    ap.add_argument("--pulse-ms", default="1,3,5,10", help="reset 模式扫的 PC 脉冲宽度")
    ap.add_argument("--app-open-watch-s", type=float, default=2.0,
                    help="appopen 模式：打开/关闭后各观察多久")
    ap.add_argument("--monitor-s", type=float, default=60.0,
                    help="monitor 模式：盯多少秒（0 = 一直盯到 Ctrl-C）")
    ap.add_argument("--csv", default="")
    args = ap.parse_args()

    if args.mode == "timing" and not args.probe_port:
        sys.exit("timing 模式需要 --probe-port（一个 USB-TTL 的输入脚接到从机 PA2/PA3 上）；"
                 "没探头请用 --mode control 或 --mode reset")
    if args.mode == "appopen" and not args.probe_port:
        sys.exit("appopen 模式需要 --probe-port（要看打开监控那一瞬间的引脚电平）")
    if args.mode == "monitor" and not args.probe_port:
        sys.exit("monitor 模式需要 --probe-port（要盯的就是从机那两根脚）")
    if args.mode == "reset" and not args.watch:
        args.watch = args.port
    if args.mode == "reset" and args.watch_mode == "banner" and not args.banner:
        sys.exit("--watch-mode banner 需要 --banner 正则")
    SPEC_RESP_MS = args.expect_resp_ms
    SPEC_HOLD_MS = args.expect_hold_ms

    # appopen 由 App 自己开串口；monitor 是纯观察 —— 两者都不能再去开端口（会和被抓的对象抢）
    drv = (NullDriver() if args.mode in ("appopen", "monitor") else make_driver(args))
    print(f"模式 {args.mode}   驱动 {drv.name}   规格：采集延时{SPEC_RESP_MS:g}ms、"
          f"RTS 保持{SPEC_HOLD_MS:g}ms（断言=输出低）")
    rec = Recorder(args.csv)
    try:
        if args.mode == "selfcheck":
            return mode_selfcheck(drv, args)
        if args.mode == "control":
            return mode_control(drv, args, rec)
        if args.mode == "timing":
            return mode_timing(drv, args, rec)
        if args.mode == "reset":
            return mode_reset(drv, args, rec)
        if args.mode == "appopen":
            return mode_appopen(drv, args, rec)
        if args.mode == "monitor":
            return mode_monitor(drv, args, rec)
    finally:
        drv.set(dtr=False, rts=False)      # 收工：两脚都输出高，别把目标按在复位上
        drv.close()
        rec.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
