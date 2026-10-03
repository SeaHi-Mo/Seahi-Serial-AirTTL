#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
9600（可指定波特率）收发丢包率专项测试 —— 修正版。

相比第一版的修正（第一版结论不可靠）：
  1. **每个模式开始前先 apply_baud 重连**，清掉上一轮积压，避免跨模式串扰
     （第一版出现"1 帧丢 5%、2 帧 0%、3 帧 55%"这种非单调怪象就是串扰）；
  2. **统计前先排空等待 DRAIN_SEC**，并把「排空前/后」两次结果都打出来，
     这样能区分"真丢包"与"只是延迟到达"；
  3. 序号全局唯一（@NNNN），不会跨模式混淆。

用法：python3 rate9600_test.py [波特率] [每模式轮数] [批量,逗号分隔]
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import loss_test as L

BAUD = int(sys.argv[1]) if len(sys.argv) > 1 else 9600
ROUNDS = int(sys.argv[2]) if len(sys.argv) > 2 else 10
BATCHES = [int(x) for x in (sys.argv[3].split(",") if len(sys.argv) > 3
                            else ["1", "3", "10", "20"])]
INTERVAL = 1.2          # 每轮之间的间隔（> MCP 限流 1.1s）
DRAIN_SEC = 4.0         # 统计前的排空等待


def frames4(text):
    """本脚本用 4 位序号（@NNNN）。"""
    return set(int(m) for m in re.findall(r"@(\d{4})", text))


L._post({"jsonrpc": "2.0", "method": "initialize",
         "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                    "clientInfo": {"name": "rate9600-v2", "version": "2.0"}}})
L._post({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
        notify=True)
print(f"MCP 会话: {L._sid}")
print(f"{BAUD} bps，每模式 {ROUNDS} 轮，批量 {BATCHES}，排空等待 {DRAIN_SEC}s\n")

results = []
seq_base = 1000

for direction in ("down", "up"):
    if direction == "down":
        sp, rp, sport, rport = (L.SENDER_PANE, L.RECV_PANE,
                                L.SENDER_PORT, L.RECV_PORT)
        label = "下行 COM9->COM8"
    else:
        sp, rp, sport, rport = (L.RECV_PANE, L.SENDER_PANE,
                                L.RECV_PORT, L.SENDER_PORT)
        label = "上行 COM8->COM9"

    for batch in BATCHES:
        # ① 每个模式前重连，清掉上一模式的积压
        L.apply_baud(BAUD)
        time.sleep(1.5)

        snap_rx = L.snapshot(rport)
        snap_tx = L.snapshot(sport)
        seq_all = []
        t0 = time.time()
        for r in range(ROUNDS):
            seq_base += 100
            seqs = list(range(seq_base + 1, seq_base + batch + 1))
            seq_all += seqs
            data = "".join("@%04d\n" % s for s in seqs)
            L.call_tool("serial_send", {"pane": sp, "data": data,
                                        "mode": "text", "lineEnding": "lf"})
            time.sleep(INTERVAL)

        # ② 排空前统计
        recv_early = frames4(L.collect(rport, snap_rx)) & set(seq_all)
        time.sleep(DRAIN_SEC)
        # ③ 排空后统计
        recv_late = frames4(L.collect(rport, snap_rx)) & set(seq_all)
        sent = frames4(L.collect(sport, snap_tx)) & set(seq_all)

        n = len(seq_all)
        lost = n - len(recv_late)
        results.append((label, batch, n, len(sent), len(recv_early),
                        len(recv_late), lost))
        print("%-14s 批量 %3d 帧/次 x%2d 轮: 发 %4d | 排出前收 %4d | 排空后收 %4d | "
              "丢 %3d (%5.1f%%)  用时 %.0fs" %
              (label, batch, ROUNDS, n, len(recv_early), len(recv_late),
               lost, 100.0 * lost / n, time.time() - t0), flush=True)

print("\n" + "=" * 84)
print("%-16s %-8s %-6s %-8s %-9s %-9s %s" %
      ("方向", "批量", "发", "发送侧", "排出前收", "排空后收", "丢包率"))
print("-" * 84)
for label, batch, n, s, e, l, lost in results:
    print("%-16s %-8d %-6d %-8d %-9d %-9d %.1f%%" %
          (label, batch, n, s, e, l, 100.0 * lost / n))
print("=" * 84)

# 恢复 115200
L.apply_baud(115200)
print("\n已恢复两端 115200")
