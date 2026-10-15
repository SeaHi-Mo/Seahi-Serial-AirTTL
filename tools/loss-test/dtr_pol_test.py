#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DTR 极性（默认电平）切换验证 —— 对应从机 rf_uart_tx.c 的 OPCODE_DTR_POL。

被验证的规格（全部来自 RF_Uart/APP/rf_uart_tx.c 与 RF_UartDongle/APP/usb_uart.c，
改了固件里的常量要同步改这里）：

  · 两载体、同一个效果：① 下行魔数 A5 5A 01 <pol>（主机拦截，这 4 字节**不发往目标设备**）
                        ② 厂商请求 0xA3（wValueL 的 bit0 = 极性；需要 libusb 独占设备）
  · 语义：pol=0 = 默认极性（未断言 -> PA3 高，断言 DTR -> PA3 低）
          pol=1 = 反相    （未断言 -> PA3 低，断言 DTR -> PA3 高）
  · 只翻 PA3(DTR)：PA2(RTS) 与它的复位脉冲、0x7F 一键下载时序都不受影响
  · 掉电保存：写在从机 Flash 的绑定信息扇区（0x3B000），掉电 / 换主机都保持；
              恢复出厂默认 = 再发一次 pol=0，不用刷固件
  · 生效时机：从机收到后**立刻**把 PA3 摆到新极性下的"默认电平"再写 Flash；
              若上位机此刻正断言 DTR，主机同一轮还会补一包线码，
              约 10~20 ms 后 PA3 才到"断言"电平（见 README「七、使用」第 6 节）

四种用法：

  status   什么都不发，只打印规格与两条指令的字节（防手抖发错）
  cmd      只发指令（最常用）：--pol 1 切反相、--pol 0 恢复默认
  check    需要**从机侧 PA0<->PA1 短接**（回环）：发一串含魔数的数据再收回来，
           验证「魔数被吃掉、其余字节逐字节透明」—— 不依赖探头就能确认拦截生效
  drive    配合示波器 / 逻辑分析仪：切完极性按 --dtr 摆 DTR，人肉看 PA3 是否按新极性变

用法举例：

    # 规格速查（不碰硬件）
    python3 dtr_pol_test.py --mode status

    # 切成反相 / 恢复默认（任何串口工具都能用的那条路）
    python3 dtr_pol_test.py --mode cmd --port /dev/ttyUSB0 --pol 1
    python3 dtr_pol_test.py --mode cmd --port /dev/ttyUSB0 --pol 0

    # 回环自检：魔数被吃掉 + 数据透明（从机侧 PA0 与 PA1 短接）
    python3 dtr_pol_test.py --mode check --port /dev/ttyUSB0

    # 配探头看波形：先切成反相，再把 DTR 拉高（反相下 = PA3 输出高）
    python3 dtr_pol_test.py --mode drive --port /dev/ttyUSB0 --pol 1 --dtr high

    # 走厂商请求 0xA3（libusb 独占设备）
    python3 dtr_pol_test.py --mode cmd --via usb --pol 1

注意：
  · `--via usb` 用 libusb **独占**设备，不能和 pyserial 同时开同一个口；
    Linux 下 ch341 驱动占着设备时先 `sudo modprobe -r ch341`（脚本会提示错误码）。
  · 魔数请**一次性单独发**（脚本默认就是一次 write 发完）。
  · `--port` 必须是**主机（dongle）**那个数据口，不是从机的串口，也不是主机的调试口。
"""
import argparse
import sys
import time

# ---- 固件规格（改固件要同步这里）------------------------------------------
MAGIC = bytes([0xA5, 0x5A, 0x01])      # 前 3 字节固定，第 4 字节 = 极性
POL_DEFAULT = 0                          # 未断言 = 高、断言 = 低
POL_INVERT = 1                           # 未断言 = 低、断言 = 高
VEN_DTR_POL = 0xA3                       # 厂商请求码（本项目扩展）
DONGLE_VID = 0x1A86
DONGLE_VENDOR_PID = 0x7523               # 厂商模式（默认）：CH341 兼容
DONGLE_CDC_PID = 0x8040                  # CDC 模式


def cmd_bytes(pol):
    return MAGIC + bytes([pol & 1])


def describe(pol):
    pol &= 1
    if pol == POL_DEFAULT:
        return "默认极性：未断言/未配对/断链 -> PA3 高；断言 DTR -> PA3 低"
    return "反相：未断言/未配对/断链 -> PA3 低；断言 DTR -> PA3 高"


# --------------------------------------------------------------------------- #
# 载体 ①：下行魔数（pyserial）
# --------------------------------------------------------------------------- #
def open_port(port, baud):
    try:
        import serial
    except ImportError:
        sys.exit("需要 pyserial：pip install pyserial")
    try:
        ser = serial.Serial(port, baud, timeout=0.05)
    except Exception as e:
        sys.exit("打开 %s 失败：%s\n"
                 "（确认这是**主机**的数据口，且链路已连接 —— 未配对时主机不枚举 USB）"
                 % (port, e))
    time.sleep(0.3)
    # 开端口常会自动断言 DTR/RTS —— 立刻释放，别把目标按住（与 dtr_rts_test.py 一致）
    ser.dtr = False
    ser.rts = False
    return ser


def send_magic(ser, pol):
    """一次性把 4 个字节写出去（拆开发会认不出来，见 README）。"""
    ser.reset_input_buffer()
    ser.write(cmd_bytes(pol))
    ser.flush()
    return time.perf_counter()


def mode_cmd(ser, pol):
    t = send_magic(ser, pol)
    print("已发送魔数 %s（一次性 4 字节）" % cmd_bytes(pol).hex(" ").upper())
    print("  切换为 %s" % describe(pol))
    print("  从机：立刻摆 PA3 到该极性的默认电平，随后写 Flash（几毫秒）")
    print("  若此刻正断言 DTR，约 10~20 ms 后 PA3 才到「断言」电平")
    print("  掉电不丢；恢复默认 = 再发一次 %s" % cmd_bytes(POL_DEFAULT).hex(" ").upper())
    return t


def mode_check(ser, pol, timeout=1.5):
    """回环自检：从机侧 PA0<->PA1 短接时，发什么就收什么（魔数除外）。"""
    payload = b"<POL-CHK>" + cmd_bytes(pol) + b"<END>" + bytes(range(0x30, 0x3A))
    expect = b"<POL-CHK>" + b"<END>" + bytes(range(0x30, 0x3A))   # 魔数那 4 字节应被吃掉
    ser.reset_input_buffer()
    ser.write(payload)
    ser.flush()
    got = b""
    deadline = time.time() + timeout
    while time.time() < deadline and len(got) < len(expect):
        got += ser.read(len(expect) - len(got))
    print("发送 %d 字节：%s" % (len(payload), payload.hex(" ").upper()))
    print("期望回环 %d 字节（魔数被吃掉）：%s" % (len(expect), expect.hex(" ").upper()))
    print("实际收到 %d 字节：%s" % (len(got), got.hex(" ").upper()))
    if got == expect:
        print("✓ 拦截生效且数据逐字节透明（魔数没有透传给目标设备）")
        return True
    if got == payload:
        print("✗ 魔数被原样透传了 —— 指令没生效。可能：链路没连上 / 主机固件未更新 / "
              "4 字节被拆开发送（重发一次试试）")
    else:
        print("✗ 与期望不一致。请确认从机侧 PA0 与 PA1 确实短接、线码一致（%d 8N1）" % ser.baudrate)
    return False


def mode_drive(ser, pol, dtr):
    send_magic(ser, pol)
    print("已切为 %s" % describe(pol))
    time.sleep(0.2)          # 留出从机写 Flash 与主机补发线码的时间
    if dtr is None:
        print("未指定 --dtr，保持当前 DTR 不动（只是把极性切了）")
        return
    lvl = (dtr == "high")
    ser.dtr = lvl
    print("DTR = %s -> 期望 PA3 %s（%s）"
          % (dtr, "高" if (lvl != bool(pol)) else "低", describe(pol)))
    print("用示波器 / 逻辑分析仪看 PA3(GND 共地) 是否在约 10~20 ms 内到该电平")


# --------------------------------------------------------------------------- #
# 载体 ②：厂商请求 0xA3（libusb）
# --------------------------------------------------------------------------- #
def mode_usb(pol, vid, pid, detach):
    try:
        import usb.core
    except ImportError:
        sys.exit("需要 pyusb：pip install pyusb")
    dev = usb.core.find(idVendor=vid, idProduct=pid)
    if dev is None:
        sys.exit("没找到 %04X:%04X —— 主机必须**已与从机连接**（未配对时不枚举 USB），"
                 "或者驱动没装 / 被别的进程占用" % (vid, pid))
    if detach:
        try:
            dev.detach_kernel_driver(0)
            print("已从内核驱动解绑（libusb 独占）")
        except Exception as e:
            print("解绑内核驱动：%s（多数是本来没绑定，可忽略）" % e)
    try:
        # bmRequestType=0x40 (Vendor/Device/OUT), bRequest=0xA3, wValue=极性, wIndex=0, 无数据
        dev.ctrl_transfer(0x40, VEN_DTR_POL, pol & 1, 0, None, timeout=1000)
    except Exception as e:
        sys.exit("0xA3 厂商请求失败：%s\n"
                 "Linux 上若是 'Resource busy'，说明 ch341 驱动占着设备 —— "
                 "sudo modprobe -r ch341 后重试（用完 modprobe ch341 加回来）" % e)
    print("已发送厂商请求 0xA3（wValueL bit0 = %d）" % (pol & 1))
    print("  切换为 %s" % describe(pol))


def mode_status():
    print("== DTR 极性指令规格 ==")
    print("  载体① 下行魔数        : %s <pol>（<pol> = 00 默认 / 01 反相）"
          % cmd_bytes(POL_DEFAULT)[:3].hex(" ").upper())
    print("    默认极性（出厂）    : %s   %s" % (cmd_bytes(POL_DEFAULT).hex(" ").upper(),
                                                describe(POL_DEFAULT)))
    print("    反相                : %s   %s" % (cmd_bytes(POL_INVERT).hex(" ").upper(),
                                                describe(POL_INVERT)))
    print("  载体② 厂商请求        : bmRequestType=0x40 bRequest=0x%02X wValueL.bit0=极性 "
          "wIndex=0 wLength=0" % VEN_DTR_POL)
    print("  链路层 opcode         : OPCODE_DTR_POL = 0x02（载荷 1 字节：0 默认 / 1 反相）")
    print("  作用范围              : 只翻 PA3(DTR)；RTS/PA2 复位脉冲与 0x7F 一键下载时序不变")
    print("  掉电                  : 保存于从机 Flash 绑定信息扇区（0x3B000），不需重发；"
          "恢复默认 = 重发 pol=0")
    print("  已知边界              : 魔数被 USB 读取边界切开且之后没有数据时认不出来"
          "（那 4 字节会当数据透传）—— 单独、一次性发即可")


def main():
    ap = argparse.ArgumentParser(description="DTR 极性（默认电平）切换 / 验证")
    ap.add_argument("--mode", choices=("status", "cmd", "check", "drive"), default="status")
    ap.add_argument("--port", help="主机（dongle）的数据口，如 /dev/ttyUSB0 或 COM10")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--pol", type=int, choices=(0, 1), default=1,
                    help="0 = 默认极性（未断言高/断言低），1 = 反相（默认 1）")
    ap.add_argument("--dtr", choices=("high", "low"), help="drive 模式：切完极性后把 DTR 摆到哪")
    ap.add_argument("--via", choices=("magic", "usb"), default="magic")
    ap.add_argument("--vid", type=lambda s: int(s, 0), default=DONGLE_VID)
    ap.add_argument("--pid", type=lambda s: int(s, 0), default=None,
                    help="默认按厂商模式 0x7523 找，找不到再试 CDC 0x8040")
    ap.add_argument("--no-detach", action="store_true", help="usb 模式：不解绑内核驱动")
    args = ap.parse_args()

    if args.mode == "status":
        mode_status()
        return 0

    if args.via == "usb":
        pids = [args.pid] if args.pid else [DONGLE_VENDOR_PID, DONGLE_CDC_PID]
        err = None
        for pid in pids:
            try:
                mode_usb(args.pol, args.vid, pid, not args.no_detach)
                return 0
            except SystemExit as e:
                err = e
        raise err

    if not args.port:
        sys.exit("--via magic 需要 --port（主机那个数据口）")
    ser = open_port(args.port, args.baud)
    try:
        if args.mode == "cmd":
            mode_cmd(ser, args.pol)
            return 0
        if args.mode == "check":
            return 0 if mode_check(ser, args.pol) else 1
        mode_drive(ser, args.pol, args.dtr)
        return 0
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
