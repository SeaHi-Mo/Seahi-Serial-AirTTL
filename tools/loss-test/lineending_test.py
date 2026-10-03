#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
隔离实验：定位「行尾方式」为何影响丢包。

已知（用户实测，间隔 2s、每帧独立发送）：
  · data 内嵌 \n + lineEnding=none   -> 1/30（96.7% 丢）
  · data 不带换行 + lineEnding=crlf  -> 30/30（0% 丢）

本脚本固定「间隔 2s、每模式 10 帧」，对比四种组合。
关键：none 与 lf 发出去的字节完全相同（都是 "@001\n"）——
      若两者结果不同，说明决定因素是工具发送路径而非字节内容。

用法：python3 lineending_test.py [间隔秒] [每模式帧数]
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import loss_test as L

INTERVAL = float(sys.argv[1]) if len(sys.argv) > 1 else 2.0
N = int(sys.argv[2]) if len(sys.argv) > 2 else 10

MODES = [
    ("raw",  "@%03d",   "none", "4B，完全无行尾"),
    ("none", "@%03d\n", "none", "5B，data 内嵌 \\n（原做法）"),
    ("lf",   "@%03d",   "lf",   "5B，工具补 \\n（与 none 字节相同！）"),
    ("crlf", "@%03d",   "crlf", "6B，工具补 \\r\\n"),
]

L._post({"jsonrpc": "2.0", "method": "initialize",
         "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                    "clientInfo": {"name": "lineending-test", "version": "1.0"}}})
L._post({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
        notify=True)
print(f"MCP 会话: {L._sid}")
print(f"间隔 {INTERVAL}s，每模式 {N} 帧\n")

for name, fmt, le, note in MODES:
    L.apply_baud(115200)          # 每模式前重连，保证起点一致
    time.sleep(1.0)
    snap_rx = L.snapshot(L.RECV_PORT)
    snap_tx = L.snapshot(L.SENDER_PORT)
    marks = []
    for i in range(1, N + 1):
        L.call_tool("serial_send", {"pane": L.SENDER_PANE, "data": fmt % i,
                                    "mode": "text", "lineEnding": le})
        time.sleep(INTERVAL)
    recv = L.frames_in(L.collect(L.RECV_PORT, snap_rx)) & set(range(1, N + 1))
    sent = L.frames_in(L.collect(L.SENDER_PORT, snap_tx)) & set(range(1, N + 1))
    print("%-5s lineEnding=%-4s %-38s 发 %2d/收 %2d/%d（丢 %.0f%%）" %
          (name, le, note, len(sent), len(recv), N,
           100.0 * (N - len(recv)) / N), flush=True)
