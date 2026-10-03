#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
间歇发送丢包测试 —— 模拟"实际使用"（两次收发之间有空闲），而不是一次突发。

背景：一次突发 500 帧只丢约 1%，但那是 close/open 重连后立刻连续发完的理想情况。
实际使用中两次发送之间通常有间隔，而链路「空闲约 1.5s 就会断（自愈需要数秒）」，
所以真实丢包率会远高于 1%。本脚本按固定间隔逐帧发送并统计实收。

用法：python3 interval_test.py [间隔秒] [帧数]
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import loss_test as L

INTERVAL = float(sys.argv[1]) if len(sys.argv) > 1 else 2.0
N = int(sys.argv[2]) if len(sys.argv) > 2 else 30
# 发送方式：none = data 内自带 \n（原做法）；crlf/lf/cr = 交给工具的「行尾」设置追加
LINE_ENDING = sys.argv[3] if len(sys.argv) > 3 else "crlf"

L._post({"jsonrpc": "2.0", "method": "initialize",
         "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                    "clientInfo": {"name": "interval-test", "version": "1.0"}}})
L._post({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
        notify=True)
print(f"MCP 会话: {L._sid}")

# 先重连一次，让链路处于良好起点（与真实"刚连上"的状态一致）
L.apply_baud(115200)
time.sleep(1.0)
snap_rx = L.snapshot(L.RECV_PORT)
snap_tx = L.snapshot(L.SENDER_PORT)

print(f"间隔 {INTERVAL}s，逐帧发送 {N} 帧，行尾方式 = {LINE_ENDING}"
      f"（预计 {INTERVAL * N:.0f}s）\n")
for i in range(1, N + 1):
    if LINE_ENDING == "none":
        L.send_frames(L.SENDER_PANE, [i])           # data 内自带 \n
    else:
        L.call_tool("serial_send", {"pane": L.SENDER_PANE, "data": "@%03d" % i,
                                    "mode": "text", "lineEnding": LINE_ENDING})
    time.sleep(INTERVAL)
    got = L.frames_in(L.collect(L.RECV_PORT, snap_rx))
    mark = "✓" if i in got else "✗"
    print(f"  #{i:03d} {mark}", end="", flush=True)
    if i % 10 == 0:
        print()

sent = L.frames_in(L.collect(L.SENDER_PORT, snap_tx)) & set(range(1, N + 1))
recv = L.frames_in(L.collect(L.RECV_PORT, snap_rx)) & set(range(1, N + 1))
lost = sorted(set(range(1, N + 1)) - recv)

print(f"\n\n间隔 {INTERVAL}s / 共 {N} 帧")
print(f"发出(记录) {len(sent)}   收到 {len(recv)}   丢 {len(lost)}"
      f"（{100.0 * len(lost) / N:.1f}%）")
print(f"丢失序号: {lost if lost else '无'}")
