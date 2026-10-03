#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
长稳（soak）测试：持续双向收发，每分钟统计一次并写 CSV。

关注：
  · 丢包率是否随时间上升（缓冲/状态缓慢劣化）；
  · 是否出现整段静默（seq 失步那类"永久卡死"，高危风险 #1）；
  · 每隔 INTERVAL 的空闲是否会导致链路失效（真实存在的话会体现在丢包率突增）。

用法：python3 soak_test.py [时长分钟] [波特率] [批量] [间隔秒]
默认：60 分钟 / 115200 / 每批 10 帧 / 每 5 秒一批
CSV 输出到本目录 soak-<时间戳>.csv
"""
import csv
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import loss_test as L

DURATION_MIN = float(sys.argv[1]) if len(sys.argv) > 1 else 60.0
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
BATCH = int(sys.argv[3]) if len(sys.argv) > 3 else 10
INTERVAL = float(sys.argv[4]) if len(sys.argv) > 4 else 5.0
DRAIN_SEC = 4.0

CSV_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "soak-%s.csv" % time.strftime("%Y%m%d-%H%M%S"))


def frames4(text):
    """本脚本序号 4 位起（@NNNN），长跑后 base 会到 5 位，所以匹配 4~6 位。"""
    return set(int(m) for m in re.findall(r"@(\d{4,6})", text))


def send(pane, seqs):
    data = "".join("@%04d\n" % s for s in seqs)
    L.call_tool("serial_send", {"pane": pane, "data": data,
                                "mode": "text", "lineEnding": "lf"})


L._post({"jsonrpc": "2.0", "method": "initialize",
         "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                    "clientInfo": {"name": "soak", "version": "1.0"}}})
L._post({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
        notify=True)
print(f"MCP 会话: {L._sid}")
print(f"长稳测试: {DURATION_MIN} 分钟, {BAUD} bps, 每批 {BATCH} 帧 / 每 {INTERVAL}s, "
      f"CSV -> {CSV_PATH}", flush=True)

L.apply_baud(BAUD)
time.sleep(1.5)

seq = 1000
t0 = time.time()
snap_dn = L.snapshot(L.RECV_PORT)
snap_up = L.snapshot(L.SENDER_PORT)
dn_all, up_all = [], []          # ★ 必须记住本方向发过的序号，统计时按它过滤
dn_sent = up_sent = 0
last_report = t0

with open(CSV_PATH, "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["elapsed_min", "dn_tx", "dn_rx", "dn_loss_pct",
                "up_tx", "up_rx", "up_loss_pct", "silent_rounds"])
    silent = 0

    while (time.time() - t0) < DURATION_MIN * 60:
        # 双向各发一批（近似并发）
        seq += 100
        dn = list(range(seq + 1, seq + BATCH + 1))
        seq += 100
        up = list(range(seq + 1, seq + BATCH + 1))
        send(L.SENDER_PANE, dn)
        send(L.RECV_PANE, up)
        dn_all += dn
        up_all += up
        dn_sent += len(dn)
        up_sent += len(up)
        time.sleep(INTERVAL)

        # 每分钟落一行
        now = time.time()
        if now - last_report >= 60:
            last_report = now
            dn_rx = len(frames4(L.collect(L.RECV_PORT, snap_dn)) & set(dn_all))
            up_rx = len(frames4(L.collect(L.SENDER_PORT, snap_up)) & set(up_all))
            dn_lost = dn_sent - dn_rx
            up_lost = up_sent - up_rx
            # 本分钟是否"整段静默"（这一分钟里一个都没收到）
            silent = 1 if (dn_rx == 0 and up_rx == 0) else 0
            w.writerow(["%.1f" % ((now - t0) / 60), dn_sent, dn_rx,
                        "%.2f" % (100.0 * dn_lost / max(dn_sent, 1)),
                        up_sent, up_rx,
                        "%.2f" % (100.0 * up_lost / max(up_sent, 1)), silent])
            f.flush()
            print("[%6.1f min] 下行 发%5d 收%5d 丢%5.2f%%  |  上行 发%5d 收%5d 丢%5.2f%%" %
                  ((now - t0) / 60, dn_sent, dn_rx, 100.0 * dn_lost / max(dn_sent, 1),
                   up_sent, up_rx, 100.0 * up_lost / max(up_sent, 1)), flush=True)

# 收尾：排空后写最后一行
time.sleep(DRAIN_SEC)
dn_rx = len(frames4(L.collect(L.RECV_PORT, snap_dn)) & set(dn_all))
up_rx = len(frames4(L.collect(L.SENDER_PORT, snap_up)) & set(up_all))
with open(CSV_PATH, "a", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["FINAL", dn_sent, dn_rx,
                "%.2f" % (100.0 * (dn_sent - dn_rx) / max(dn_sent, 1)),
                up_sent, up_rx,
                "%.2f" % (100.0 * (up_sent - up_rx) / max(up_sent, 1)), ""])
print("\n最终: 下行 发%d 收%d 丢%.2f%% | 上行 发%d 收%d 丢%.2f%%" %
      (dn_sent, dn_rx, 100.0 * (dn_sent - dn_rx) / max(dn_sent, 1),
       up_sent, up_rx, 100.0 * (up_sent - up_rx) / max(up_sent, 1)), flush=True)
print("CSV: %s" % CSV_PATH)

L.apply_baud(115200)
print("已恢复两端 115200")
