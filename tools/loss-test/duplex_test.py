#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
双向（近似并发）收发丢包率测试。

MCP 调用是串行的，没法真正同时发两路，所以用「同一轮里先下行、紧接着上行」来近似
双向业务并发，专门观察两个方向是否互相影响（缓冲/重传/ACK 争用）。

方法学与 rate9600_test 一致：**每个模式前重连 + 统计前排空 + 序号全局唯一**。

用法：python3 duplex_test.py [波特率,逗号分隔] [轮数] [每轮每方向批量]
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import loss_test as L

BAUDS = [int(x) for x in (sys.argv[1].split(",") if len(sys.argv) > 1 else ["9600", "115200"])]
ROUNDS = int(sys.argv[2]) if len(sys.argv) > 2 else 10
BATCH = int(sys.argv[3]) if len(sys.argv) > 3 else 5
INTERVAL = 1.2
DRAIN_SEC = 4.0


def frames4(text):
    return set(int(m) for m in re.findall(r"@(\d{4})", text))


def send(pane, seqs):
    data = "".join("@%04d\n" % s for s in seqs)
    L.call_tool("serial_send", {"pane": pane, "data": data,
                                "mode": "text", "lineEnding": "lf"})


L._post({"jsonrpc": "2.0", "method": "initialize",
         "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                    "clientInfo": {"name": "duplex", "version": "1.0"}}})
L._post({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
        notify=True)
print(f"MCP 会话: {L._sid}")
print(f"波特率 {BAUDS}，每档 {ROUNDS} 轮，每方向每轮 {BATCH} 帧\n")

seq_base = 1000
rows = []

for baud in BAUDS:
    L.apply_baud(baud)                 # 每档前重连，清掉上一档积压
    time.sleep(1.5)

    snap_dn = L.snapshot(L.RECV_PORT)   # 下行接收侧 COM8
    snap_up = L.snapshot(L.SENDER_PORT)  # 上行接收侧 COM9
    dn_all, up_all = [], []

    for r in range(ROUNDS):
        seq_base += 100
        dn = list(range(seq_base + 1, seq_base + BATCH + 1))
        seq_base += 100
        up = list(range(seq_base + 1, seq_base + BATCH + 1))
        dn_all += dn
        up_all += up
        send(L.SENDER_PANE, dn)          # 下行 COM9 -> COM8
        send(L.RECV_PANE, up)            # 上行 COM8 -> COM9（紧接着）
        time.sleep(INTERVAL)

    time.sleep(DRAIN_SEC)
    dn_rx = len(frames4(L.collect(L.RECV_PORT, snap_dn)) & set(dn_all))
    up_rx = len(frames4(L.collect(L.SENDER_PORT, snap_up)) & set(up_all))
    dn_n, up_n = len(dn_all), len(up_all)
    rows.append((baud, dn_n, dn_rx, up_n, up_rx))
    print("%-8d 下行 发%3d/收%3d 丢%.1f%%  |  上行 发%3d/收%3d 丢%.1f%%" %
          (baud, dn_n, dn_rx, 100.0 * (dn_n - dn_rx) / dn_n,
           up_n, up_rx, 100.0 * (up_n - up_rx) / up_n), flush=True)

print("\n" + "=" * 72)
print("%-8s %-22s %-22s" % ("波特率", "下行 COM9->COM8", "上行 COM8->COM9"))
print("-" * 72)
for baud, dn_n, dn_rx, up_n, up_rx in rows:
    print("%-8d 发%3d 收%3d 丢%5.1f%%      发%3d 收%3d 丢%5.1f%%" %
          (baud, dn_n, dn_rx, 100.0 * (dn_n - dn_rx) / dn_n,
           up_n, up_rx, 100.0 * (up_n - up_rx) / up_n))
print("=" * 72)

L.apply_baud(115200)
print("\n已恢复两端 115200")
