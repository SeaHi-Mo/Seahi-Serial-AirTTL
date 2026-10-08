#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
下行延迟实测（端到端，绕开上位机工具自身的显示轮询）

为什么必须单独测：
  SeaHi Serial 的接收是**轮询显示**的 —— 可见面板 25ms 拉一次 read_data、隐藏面板 500ms
  （见 app 的 MON_READ_MS_VISIBLE / MON_READ_MS_HIDDEN），而屏幕上那行时间戳是在**渲染时**
  （new Date()）打的。所以"主机发送面板的时间戳 − 从机接收面板的时间戳"本身就带
  0~25ms（面板隐藏时 0~500ms）的误差，不能直接当链路延迟用。

本脚本在 Windows 上直接开两个串口，用 pyserial 的 read 返回时刻打时间戳：
    t0 = 往主机数据口 write() 之前
    t1 = 从机 UART 口收到该帧**最后一个字节**的时刻
    延迟 = t1 - t0      （误差主要来自 USB 帧 1ms + OS 调度，量级 <1ms）

用法（Windows 上跑，端口/波特率按实际改；从机 UART 那一侧就是"目标设备"看到的数据）：
    pip install pyserial
    python latency_test.py --tx COM9 --rx COM10 --baud 1500000
    python latency_test.py --tx COM9 --rx COM10 --baud 115200 --size 1,10,100,250 --trials 30

判读：
  · 固定部分 ≈ 从机轮询周期（正常 10ms，取平均 5ms）+ 空口往返（1~2ms）+ 主机 USB（~1ms）
  · 随 size 线性增长的部分 = 串口线时间 = size × 10 / baud
    → 用 --size 扫几个点作图/看回归，就能把"固件/协议"和"串口物理时间"分开
  · 若固定部分 ≈ 40ms，说明轮询定时器又跑偏了（历史上主频切到 24MHz 时曾被 100MHz
    算出的计数拖成 41.7ms/tick）
"""
import argparse
import statistics
import sys
import time

try:
    import serial  # pyserial
except ImportError:
    sys.exit("需要 pyserial：pip install pyserial")


def run_one(tx, rx, size, seq, timeout=2.0):
    """发一帧 size 字节，返回 (写入前时刻, 收到最后一字节时刻, 收到字节数)。"""
    head = bytes([0xA5, 0x5A, seq & 0xFF, (seq >> 8) & 0xFF])
    filler = bytes(((seq + i) & 0xFF) for i in range(max(0, size - len(head))))
    frame = (head + filler)[:size]

    rx.reset_input_buffer()
    t0 = time.perf_counter()
    tx.write(frame)
    tx.flush()

    got = bytearray()
    t1 = None
    while len(got) < size:
        chunk = rx.read(size - len(got))
        if not chunk:
            if time.perf_counter() - t0 > timeout:
                break
            continue
        got += chunk
        t1 = time.perf_counter()
    return t0, (t1 if t1 is not None else t0), len(got)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tx", required=True, help="主机数据口（CH340/CH341 兼容），如 COM9")
    ap.add_argument("--rx", required=True, help="从机 UART 口（外置 CH343），如 COM10")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--size", default="10,100,250", help="帧长扫描，逗号分隔")
    ap.add_argument("--trials", type=int, default=20, help="每个帧长测几次（丢的会单列）")
    ap.add_argument("--gap", type=float, default=0.3, help="两次之间静置秒数")
    ap.add_argument("--dtr-rts", action="store_true",
                    help="保留 DTR/RTS 默认高（默认会**拉低**：主机会把 DTR/RTS 直控从机 PA3/PA2，"
                         "即目标板的 BOOT/RESET，别误复位目标）")
    args = ap.parse_args()

    sizes = [int(x) for x in args.size.split(",") if x.strip()]
    tx = serial.Serial(args.tx, args.baud, timeout=0.05)
    rx = serial.Serial(args.rx, args.baud, timeout=0.05)
    if not args.dtr_rts:
        for p in (tx, rx):
            try:
                p.dtr = False
                p.rts = False
            except Exception:
                pass
    print(f"tx={args.tx} rx={args.rx} baud={args.baud}  "
          f"DTR/RTS={'默认' if args.dtr_rts else '拉低(保护目标板)'}")
    print(f"{'字节':>5} {'成功':>4} {'丢失':>4} {'最小':>8} {'中位':>8} {'p90':>8} {'最大':>8}  "
          f"{'串口理论':>8}")
    time.sleep(0.5)          # 让从机同步线码

    seq = 0
    for size in sizes:
        lat, lost = [], 0
        for i in range(args.trials):
            seq += 1
            t0, t1, n = run_one(tx, rx, size, seq)
            if n < size:
                lost += 1
            elif i > 0:                       # 首次不计（链路刚唤醒，可能多等一个轮询周期）
                lat.append((t1 - t0) * 1000.0)
            time.sleep(args.gap)
        if not lat:
            print(f"{size:>5} {0:>4} {lost:>4}   —— 全丢：检查端口/波特率/是否已配对")
            continue
        lat.sort()
        p90 = lat[min(len(lat) - 1, int(len(lat) * 0.9))]
        line_ms = size * 10.0 / args.baud * 1000.0
        print(f"{size:>5} {len(lat):>4} {lost:>4} {lat[0]:>7.1f}m {statistics.median(lat):>7.1f}m "
              f"{p90:>7.1f}m {lat[-1]:>7.1f}m  {line_ms:>7.2f}m")

    tx.close()
    rx.close()
    print("\n提示：固定部分（与帧长无关的那一段）就是链路+固件延迟；"
          "随帧长线性增长的那一段是串口线时间，属物理限制。")


if __name__ == "__main__":
    main()
