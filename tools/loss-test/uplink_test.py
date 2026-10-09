#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
上行（从机 UART -> 2.4G -> 主机 USB）丢包与背压测试

背景：接收侧（上行）没有下行的缓存重发保护，而主机 RF 环只有 512 B、
单包最大 251 B → 只能容纳 2 个满包；且 RF_RxQuery() 被 USB 端点状态门控。
这个脚本用来量化「上行到底在什么包长/节奏下开始丢」。

路径：COM10(从机 UART，即"目标设备"那侧) --UART--> 从机 --2.4G--> 主机 --USB--> COM9
判据：COM9 的日志文件里出现的 @NNNNN 帧。

关键前提（沿用既有测试的修正）：
  · 链路重连用 apply_baud（close -> select_port -> set_baud -> open）；
  · 每档前抓「文件快照」，只统计增量，否则会把 100% 算成丢包；
  · 一次调用连续发完，中途不停顿。

用法：python3 uplink_test.py [包长,逗号分隔] [波特率,逗号分隔]
默认：包长 5,15,20,50,100,251,500；波特率 115200,1500000
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import loss_test as L

# ★ 与 loss_test.py 的默认值不同：上行是「从机 UART 发、主机数据口收」。
#   分栏名与端口按当前 SeaHi Serial 配置实测：main=COM9（主机数据口）、
#   extra-1=COM10（从机 UART）。分栏名会随用户增删监视器而变，换环境要重新核对。
L.SENDER_PANE, L.SENDER_PORT = "extra-1", "COM10"
L.RECV_PANE, L.RECV_PORT = "main", "COM9"

SIZES = [int(x) for x in (sys.argv[1].split(",") if len(sys.argv) > 1 else
                          ["5", "15", "20", "50", "100", "251", "500"])]
BAUDS = [int(x) for x in (sys.argv[2].split(",") if len(sys.argv) > 2 else
                          ["115200", "1500000"])]
N = 300                 # 每档帧数
DRAIN_BASE = 6.0
SEQ_BASE = 300000


def frames5(text):
    return set(int(m) for m in re.findall(r"@(\d{5,6})", text))


def burst(seq, size):
    """第 seq 帧、包长 size 字节（含行尾）：@NNNNN + 填充 + \\n"""
    head = "@%05d" % seq
    pad = max(0, size - len(head) - 1)
    return head + ("." * pad) + "\n"


def run_case(baud, size, results):
    """一档：apply_baud 重连 -> 连续发 N 帧 -> 统计 COM9 增量"""
    L.apply_baud(baud)
    time.sleep(1.2)

    global SEQ_BASE
    SEQ_BASE += 10000
    seqs = list(range(SEQ_BASE + 1, SEQ_BASE + N + 1))
    data = "".join(burst(s, size) for s in seqs)

    snap_tx = L.snapshot(L.SENDER_PORT)
    snap_rx = L.snapshot(L.RECV_PORT)
    t0 = time.time()
    L.call_tool("serial_send", {"pane": L.SENDER_PANE, "data": data,
                                "mode": "text", "lineEnding": "lf"})
    tx_elapsed = time.time() - t0
    # 线上时间 = 字节数 × 10bit / 波特率；再留排空时间
    wire = N * size * 10.0 / baud
    time.sleep(wire + DRAIN_BASE)

    sent = frames5(L.collect(L.SENDER_PORT, snap_tx)) & set(seqs)
    recv = frames5(L.collect(L.RECV_PORT, snap_rx)) & set(seqs)
    lost = sorted(set(seqs) - recv)
    rate = 100.0 * len(lost) / N
    results.append((baud, size, len(sent), len(recv), rate, lost))

    print("  %7d bps  %4d B/帧  发 %3d 收 %3d  丢 %3d (%6.2f%%)  "
          "线上 %.2fs / 调用 %.2fs" %
          (baud, size, len(sent), len(recv), len(lost), rate, wire, tx_elapsed),
          flush=True)
    if lost:
        print("        丢失(前12): %s" % ",".join(str(x) for x in lost[:12]), flush=True)


def main():
    L._post({"jsonrpc": "2.0", "method": "initialize",
             "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                        "clientInfo": {"name": "uplink", "version": "1.0"}}})
    L._post({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
            notify=True)
    print(f"MCP 会话: {L._sid}")
    print(f"方向：上行 COM10(从机 UART) -> COM9(主机数据口)，每档 {N} 帧\n")

    results = []
    try:
        for baud in BAUDS:
            print(f"=== {baud} bps ===", flush=True)
            for size in SIZES:
                run_case(baud, size, results)
    finally:
        try:
            L.apply_baud(115200)
            print("\n已恢复两端 115200")
        except Exception as e:
            print("恢复波特率失败:", e)

    print("\n" + "=" * 72)
    print("%-9s %-8s %-6s %-6s %-9s" % ("波特率", "包长B", "发出", "收到", "丢包率"))
    print("-" * 72)
    for baud, size, s, r, rate, _ in results:
        print("%-9d %-8d %-6d %-6d %-8.2f%%" % (baud, size, s, r, rate))
    print("=" * 72)


if __name__ == "__main__":
    main()
