#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
上行接收侧探测（走 MCP 直接读，不依赖 log-cache 文件）

目的：分清上行丢包是「射频零散丢」还是「主机接收缓冲满了、干净地丢尾部」。
方法：每档重连 -> 一次连续发 N 帧 -> 用 serial_get_output 按 sinceSeq 增量取回
      COM9（主机数据口）实际吐出的内容，统计 @NNNNN 帧与「最大连续前缀」。

判据：
  · 收到的是**连续前缀**（1..k 全中，k 之后全无）→ 接收侧缓冲/取数停摆；
  · 零散缺失 → 射频/空口丢包。
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import loss_test as L

# main = COM9（主机数据口，接收侧）；extra-1 = COM10（从机 UART，发送侧）
L.SENDER_PANE, L.SENDER_PORT = "extra-1", "COM10"
L.RECV_PANE, L.RECV_PORT = "main", "COM9"


def out_text(pane, since=None, limit=2000):
    """按 sinceSeq 增量取 serial_get_output；回 (文本, nextSinceSeq)"""
    args = {"pane": pane, "format": "text", "lines": limit}
    if since is not None:
        args["sinceSeq"] = since
    res = L.call_tool("serial_get_output", args)
    sc = res.get("result", {}).get("structuredContent") or {}
    content = res.get("result", {}).get("content") or []
    text = ""
    if content and isinstance(content, list):
        text = content[0].get("text", "")
    return text, sc.get("nextSinceSeq") or sc.get("seqTo")


def frames5(text):
    return set(int(m) for m in re.findall(r"@(\d{5,6})", text))


def burst(seq, size):
    head = "@%05d" % seq
    pad = max(0, size - len(head) - 1)
    return head + ("." * pad) + "\n"


def run_case(baud, n, size, seq_base):
    """一档：重连 -> 连续发 n 帧 -> 读回 COM9 增量"""
    L.apply_baud(baud)
    time.sleep(1.0)

    seqs = list(range(seq_base + 1, seq_base + n + 1))
    data = "".join(burst(s, size) for s in seqs)

    # 发送前先取一次 seq 基线（并排空旧数据）
    _, since = out_text(L.RECV_PANE)

    t0 = time.time()
    L.call_tool("serial_send", {"pane": L.SENDER_PANE, "data": data,
                                "mode": "text", "lineEnding": "lf"})
    wire = n * size * 10.0 / baud
    time.sleep(wire + 3.0)

    text, _ = out_text(L.RECV_PANE, since)
    got = frames5(text) & set(seqs)

    # 最大连续前缀长度（从第一帧数起）
    prefix = 0
    for i, s in enumerate(seqs, 1):
        if s in got:
            prefix = i
        else:
            break
    lost = n - len(got)
    return dict(baud=baud, n=n, size=size, got=len(got), lost=lost,
                prefix=prefix, rate=100.0 * lost / n, t=time.time() - t0)


def main():
    cases = []
    if len(sys.argv) > 1:                      # 包长,波特率:帧数[,波特率:帧数...]
        parts = sys.argv[1].split(",")
        size = int(parts[0])
        for tok in parts[1:]:
            if ":" in tok:
                b, nn = tok.split(":")
                cases.append((int(b), int(nn), size))
            else:
                cases.append((115200, int(tok), size))
    else:
        for nn in (5, 10, 20, 40, 80, 160, 300):
            cases.append((115200, nn, 20))
    L._post({"jsonrpc": "2.0", "method": "initialize",
             "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                        "clientInfo": {"name": "rxprobe", "version": "1.0"}}})
    L._post({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
            notify=True)
    print(f"MCP 会话: {L._sid}")
    print("上行 COM10 -> COM9，判据 = 收到帧数 / 最大连续前缀\n")
    print("%-9s %-5s %-6s %-5s %-5s %-8s %-6s" %
          ("波特率", "帧数", "包长B", "收到", "丢", "丢包率", "前缀"))
    print("-" * 56)

    seq_base = 400000
    rows = []
    try:
        for baud, n, size in cases:
            r = run_case(baud, n, size, seq_base)
            seq_base += 10000
            rows.append(r)
            print("%-9d %-5d %-6d %-5d %-5d %7.2f%% %-6d" %
                  (r["baud"], r["n"], r["size"], r["got"], r["lost"],
                   r["rate"], r["prefix"]), flush=True)
    finally:
        try:
            L.apply_baud(115200)
            print("\n已恢复两端 115200")
        except Exception as e:
            print("恢复失败:", e)


if __name__ == "__main__":
    main()
