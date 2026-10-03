#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
连续流压力测试 —— 模拟"快速连续发送"（目标：每 10ms 一帧量级）。

为什么这样发：MCP 有 60 次/分钟限流，无法用软件定时做到每 10ms 一次调用。
改为「一次调用连续发 N 帧」，数据在 UART 上是硬件连续发送，帧间隔只由波特率决定：
  · 9600   : 5 字节/帧 → 约 5.2 ms/帧（比每 10ms 一帧更密）
  · 115200 : 约 0.43 ms/帧
所以本测试是「比目标更激进」的连续流。

方法学沿用修正版：每档前重连、统计前排空、序号全局唯一、按方向过滤。

用法：python3 stream_test.py [帧数] [波特率,逗号分隔] [方向 down|up|both]
默认：2000 帧，9600,115200，both
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import loss_test as L

N = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
BAUDS = [int(x) for x in (sys.argv[2].split(",") if len(sys.argv) > 2 else ["9600", "115200"])]
DIRS = sys.argv[3] if len(sys.argv) > 3 else "both"
DRAIN_BASE = 6.0


def frames4(text):
    """本脚本序号为 5 位（@NNNNN），必须匹配 4~6 位，别只抓 4 位。"""
    return set(int(m) for m in re.findall(r"@(\d{4,6})", text))


def send(pane, seqs):
    data = "".join("@%04d\n" % s for s in seqs)
    L.call_tool("serial_send", {"pane": pane, "data": data,
                                "mode": "text", "lineEnding": "lf"})


L._post({"jsonrpc": "2.0", "method": "initialize",
         "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                    "clientInfo": {"name": "stream", "version": "1.0"}}})
L._post({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
        notify=True)
print(f"MCP 会话: {L._sid}")
print(f"连续流: 每次 {N} 帧（{'@NNNN'+chr(10)}，共 {N*6} 字节），方向 {DIRS}\n")

cases = []
if DIRS in ("down", "both"):
    cases.append(("下行 COM9->COM8", L.SENDER_PANE, L.SENDER_PORT, L.RECV_PANE, L.RECV_PORT))
if DIRS in ("up", "both"):
    cases.append(("上行 COM8->COM9", L.RECV_PANE, L.RECV_PORT, L.SENDER_PANE, L.SENDER_PORT))

seq_base = 1000
for baud in BAUDS:
    for label, sp, sport, rp, rport in cases:
        L.apply_baud(baud)                 # 每档前重连
        time.sleep(1.5)
        seq_base += 10000
        seqs = list(range(seq_base + 1, seq_base + N + 1))
        snap_tx = L.snapshot(sport)
        snap_rx = L.snapshot(rport)

        t0 = time.time()
        send(sp, seqs)                     # ★ 一次调用，硬件连续发送
        tx_elapsed = time.time() - t0
        time.sleep(N * 6 * 10.0 / baud + DRAIN_BASE)   # 按波特率估算发送耗时 + 排空

        sent = frames4(L.collect(sport, snap_tx)) & set(seqs)
        recv = frames4(L.collect(rport, snap_rx)) & set(seqs)
        # 自检：接收数不可能超过发送数（口径错了立刻暴露，而不是静默给出假结论）
        assert len(recv) <= len(sent), \
            "统计口径异常：接收 %d > 发送 %d（检查序号位数/方向过滤）" % (len(recv), len(sent))
        lost = sorted(set(seqs) - recv)
        print("%-14s %-7d 发 %4d 收 %4d 丢 %4d (%5.2f%%)  连续流约 %.1fs  "
              "丢失序号(前10): %s" %
              (label, baud, len(sent), len(recv), len(lost),
               100.0 * len(lost) / N, N * 6 * 10.0 / baud,
               ",".join(str(x) for x in lost[:10]) or "-"), flush=True)

L.apply_baud(115200)
print("\n已恢复两端 115200")
