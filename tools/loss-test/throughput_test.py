#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
实际端到端吞吐（饱和法，干净版）

上一版的教训：测 2 Mbps 时"收到 > 发出"—— 因为计数器里混进了上一档还在链路/缓冲里
排队的积压。所以这里：
  1) 换档后先**静置排空**，并确认计数器不再增长（否则本次结果作废）；
  2) 然后**供载远大于链路能力**（10 批 × 58 KB，批间隔贴着 MCP 限流 1.05s，中间无空档）；
  3) 轮询计数器直到**连续 2 秒不再增长**，才认定链路吐完；
  4) 吞吐 = 收到字节 /（首次发送 → 最后一次增长）；
  5) 丢包 = 1 − 收到 / 发出（饱和时缓冲溢出会体现为丢包，一起报出来）。

用法：python3 throughput_test.py 1500000,2000000
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import loss_test as L

L.SENDER_PANE, L.SENDER_PORT = "main", "COM9"
L.RECV_PANE, L.RECV_PORT = "extra-4", "COM10"
L.MIN_CALL_GAP = 1.05

CONTENT = 99
K = 600            # 每批 600 帧 = 58 KB（< 64K 字符）
BATCHES = 10
INTERVAL = 1.05    # 贴着限流，无空档 → 连续灌


def payload(k):
    return "".join("@%03d " % i + (("%03d" % i) * 40)[:90] + "#%03d" % i for i in range(1, k + 1))


def counters(pane):
    st = L.call_tool("serial_get_state", {"pane": pane})["result"].get("structuredContent") or {}
    return int(st.get("outputBytes") or 0), int(st.get("outputLines") or 0)


def content_bytes(b, l, b0, l0, prefix):
    return (b - b0) - prefix * (l - l0)


def main():
    bauds = [int(x) for x in sys.argv[1].split(",")] if len(sys.argv) > 1 else [1500000, 2000000]
    prefix = int(os.environ.get("PRESET_PREFIX") or 15)
    L._post({"jsonrpc": "2.0", "method": "initialize",
             "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                        "clientInfo": {"name": "thr2", "version": "1.0"}}})
    L._post({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}}, notify=True)

    data = payload(K)
    results = []
    for baud in bauds:
        print(f"=== {baud} bps ===", flush=True)
        L.apply_baud(baud)

        # 1) 静置排空：直到计数器 2 秒不动
        prev = None
        t_idle = time.time()
        while time.time() - t_idle < 20:
            b, l = counters(L.RECV_PANE)
            if prev == (b, l):
                break
            prev = (b, l)
            time.sleep(1.0)
        b0, l0 = counters(L.RECV_PANE)
        print(f"  排空完成（静置 {time.time()-t_idle:.1f}s）", flush=True)

        # 2) 饱和灌数据
        t_start = time.time()
        for _ in range(BATCHES):
            tick = time.time()
            L.call_tool("serial_send", {"pane": L.SENDER_PANE, "data": data,
                                        "mode": "text", "lineEnding": "lf"})
            dt = INTERVAL - (time.time() - tick)
            if dt > 0:
                time.sleep(dt)

        # 3) 轮询到不再增长（连续 2 秒），记录最后一次增长的时刻
        b_prev, l_prev = counters(L.RECV_PANE)
        t_last = time.time()
        quiet = 0.0
        t_prev = time.time()
        while quiet < 2.0 and time.time() - t_start < 120:
            time.sleep(0.5)
            b, l = counters(L.RECV_PANE)
            if (b, l) != (b_prev, l_prev):
                t_last = time.time()
                quiet = 0.0
                b_prev, l_prev = b, l
            else:
                quiet += time.time() - t_prev
            t_prev = time.time()

        b1, l1 = counters(L.RECV_PANE)
        got = content_bytes(b1, l1, b0, l0, prefix)
        sent = K * CONTENT * BATCHES
        secs = t_last - t_start
        thr = got / secs / 1024 if secs > 0 else 0
        loss = 100.0 * max(0.0, 1.0 - got / sent)
        results.append((baud, got, sent, thr, loss, secs))
        print(f"  收到 {got/1024:.1f} KB / 发出 {sent/1024:.1f} KB，用时 {secs:.1f}s"
              f" → **{thr:.1f} KB/s（{thr*8/1000:.2f} Mbps）**，饱和丢包 {loss:.1f}%", flush=True)

    print("\n============== 汇总（饱和法）==============")
    print(f"{'波特率':>9} {'吞吐 KB/s':>10} {'等效 Mbps':>10} {'饱和丢包':>9}")
    for baud, got, sent, thr, loss, secs in results:
        print(f"{baud:>9} {thr:>10.1f} {thr*8/1000:>10.2f} {loss:>8.1f}%")


if __name__ == "__main__":
    main()
