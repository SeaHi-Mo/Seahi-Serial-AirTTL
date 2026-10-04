#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
不同波特率下「定时发送 100 字节」的丢包率测试

路径：PC COM9(主机数据口) --USB--> 主机 --2.4G--> 从机 --UART--> PC COM10(从机 UART)

每帧线上恰好 100 字节：99 字节 ASCII + 工具追加的 LF
    帧内容 = "@NNN " + 90 字节数字填充 + "#NNN"（首尾都带序号，便于识别截断）

判据（三层，从可靠到仅供参考）：
  1) ★ COM10 的日志（日志中心 channel `serial:<recv-pane>:rx`）里每条记录的 `bytes` 求和。
     这是工具自己记的字节数，不经过文本渲染、也不依赖日志文件落盘，最可靠。
  2) 交叉验证：工具分栏的 `outputBytes`/`outputLines` 累计计数（扣掉每行时间戳前缀）。
  3) 仅供参考：按日志行的文本逐行判定「完整/截断」。低速 + 长帧时**会假丢** ——
     实测工具落盘日志会把长条目截断到 64 字节，而 1)、2) 都显示数据完整。

速率说明（重要）：MCP 限流 60 次/分（`mcp_limits.rateLimitPerMin=60`），
所以"每帧一次调用"最多约 1 帧/秒。要更快只能**打包**：单次 `serial_send` 最多
64K 字符，于是每次调用发 K 帧、调用之间间隔 T —— 等效每帧间隔 T/K。
默认 K=12、T=1.6s → 等效 133ms/帧、≈750 B/s（比逐帧调用快约 9 倍）。

用法：
  python3 baud100_test.py                          # 9 档 × 120 帧，K=12，组间隔 1.6s
  python3 baud100_test.py 9600,115200 240 1600 20  # 波特率 / 帧数 / 组间隔(ms) / 每包帧数
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import loss_test as L

# —— 端口角色（用户确认：COM9=主机数据口，COM10=从机 UART，COM8=主机 log）——
L.SENDER_PANE, L.SENDER_PORT = "main", "COM9"
L.RECV_PANE, L.RECV_PORT = "extra-4", "COM10"
# 2026-10 实测：服务器已无限流（40 次连发 0.12s 全成功），本地节流降到 2ms
L.MIN_CALL_GAP = 1.05      # 限流 60/分 → 调用间隔 ≥1.05s（打包后不影响发送速率）

BAUDS = [9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1500000]
N = 500                    # 每档帧数（5 组 × 100）
K = 100                    # 每次 serial_send 打包的帧数（100 帧 = 10 KB）
INTERVAL_MS = 1500         # 组间隔（ms）→ 平均 ≈6.7 KB/s ≈ 100 字节/15ms
FRAME_LEN = 100           # 线上字节数（含 LF），可由 --len 覆盖
CONTENT = FRAME_LEN - 1   # 99：每帧内容字节
CLEN = 99                 # 帧内容字节数（不含 LF）

CH = "serial:%s:rx" % L.RECV_PANE


def frame_body(i):
    """构造 CLEN 字节帧内容（不含 LF）：@NNN + 填充 + #NNN"""
    head, tail = "@%03d " % i, "#%03d" % i
    pad = (("%03d" % i) * 40)
    mid = max(0, CLEN - len(head) - len(tail))
    return head + pad[:mid] + tail


def send_one(i):
    return send_group([i])


def send_group(idx):
    """一次调用发多帧（打包）—— 绕开 60 次/分的限流瓶颈"""
    data = "".join(frame_body(i) for i in idx)
    return L.call_tool("serial_send", {"pane": L.SENDER_PANE, "data": data,
                                       "mode": "text", "lineEnding": "lf"})


def counters(pane):
    st = L.call_tool("serial_get_state", {"pane": pane})["result"].get("structuredContent") or {}
    return int(st.get("outputBytes") or 0), int(st.get("outputLines") or 0)


def log_items(since_seq, lines=2000):
    """读 COM10 的日志条目（日志中心）"""
    r = L.call_tool("log_tail", {"channel": CH, "format": "json",
                                 "sinceSeq": since_seq, "lines": lines})
    sc = r["result"].get("structuredContent") or {}
    return sc.get("lines") or [], sc


def last_seq():
    sc = L.call_tool("log_tail", {"channel": CH, "format": "json", "lines": 1})["result"] \
           .get("structuredContent") or {}
    ln = sc.get("lines") or []
    return (ln[-1].get("seq") if ln else 0), sc


def parse_frames(text):
    ok, partial = set(), set()
    for line in text.splitlines():
        heads = set(int(x) for x in re.findall(r"@(\d{3})", line))
        tails = set(int(x) for x in re.findall(r"#(\d{3})", line))
        ok |= (heads & tails)
        partial |= (heads - (heads & tails))
    return ok, partial


def calib_prefix():
    """标定时间戳前缀长度：发 1 帧，Δbytes − 99 即前缀"""
    L.apply_baud(115200)
    time.sleep(0.8)
    b0, _ = counters(L.RECV_PANE)
    send_one(999)
    time.sleep(1.2)
    b1, _ = counters(L.RECV_PANE)
    return (b1 - b0) - CONTENT


def main():
    bauds = [int(x) for x in sys.argv[1].split(",")] if len(sys.argv) > 1 else BAUDS
    n = int(sys.argv[2]) if len(sys.argv) > 2 else N
    interval = (int(sys.argv[3]) if len(sys.argv) > 3 else INTERVAL_MS) / 1000.0
    k = int(sys.argv[4]) if len(sys.argv) > 4 else K
    global CLEN, CONTENT, FRAME_LEN
    if len(sys.argv) > 5:
        CLEN = int(sys.argv[5]); CONTENT = CLEN; FRAME_LEN = CLEN + 1

    L._post({"jsonrpc": "2.0", "method": "initialize",
             "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                        "clientInfo": {"name": "baud100-test", "version": "2.0"}}})
    L._post({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
            notify=True)
    global PREFIX
    if os.environ.get("PRESET_PREFIX"):
        prefix = int(os.environ["PRESET_PREFIX"])
        print(f"MCP 会话: {L._sid}   （跳过标定，沿用前缀 {prefix} 字符）")
    else:
        prefix = calib_prefix(); PREFIX = prefix
    print(f"时间戳前缀 {prefix} 字符")
    print(f"【定时 100 字节丢包测试】{len(bauds)} 档 × {n} 帧，每次打包 {k} 帧、组间隔 "
          f"{interval*1000:.0f}ms → 等效每帧 {interval*1000/k:.0f}ms、"
          f"≈{FRAME_LEN*k/interval/1000:.2f} KB/s"
          f"\n方向 {L.SENDER_PORT} → {L.RECV_PORT}，判据＝COM10 日志的 bytes\n")

    results = []
    for baud in bauds:
        print(f"=== {baud} bps ===", flush=True)
        L.apply_baud(baud)
        time.sleep(0.8)
        seq0, _ = last_seq()
        b0, l0 = counters(L.RECV_PANE)
        snap_tx = L.snapshot(L.SENDER_PORT)
        snap_rx = L.snapshot(L.RECV_PORT)

        t0 = time.time()
        for base in range(1, n + 1, k):
            tick = time.time()
            send_group(range(base, min(base + k, n + 1)))
            dt = interval - (time.time() - tick)
            if dt > 0:
                time.sleep(dt)
            if k == 1 and base % 25 == 0:
                print(f"    …已发 {base}/{n}（{time.time() - t0:.0f}s）", flush=True)
        # 等接收侧把在途数据吐完（按最慢波特率算）
        time.sleep(max(n * FRAME_LEN * 10.0 / baud, 1.5))
        send_ms = (time.time() - t0) * 1000

        items, meta = log_items(seq0)
        rx = [it for it in items if it.get("dir") == "rx"]
        rx_bytes = sum(int(it.get("bytes") or 0) for it in rx)
        bad = sum(1 for it in rx if int(it.get("bytes") or 0) != CONTENT)
        b1, l1 = counters(L.RECV_PANE)
        db, dl = b1 - b0, l1 - l0
        content = db - prefix * dl

        sent = L.frames_in(L.collect(L.SENDER_PORT, snap_tx)) & set(range(1, n + 1))
        recv, partial = parse_frames(L.collect(L.RECV_PORT, snap_rx))
        recv &= set(range(1, n + 1))
        partial &= set(range(1, n + 1))

        exp = CONTENT * n
        # 主判据：工具分栏的累计字节计数 → 折算成"完整收到的帧数"
        got_frames = content / CONTENT if CONTENT else 0
        rate = 100.0 * max(0.0, 1.0 - got_frames / n)
        # 次判据：COM10 日志条目里的 bytes（低速+长条目会被工具截断，仅供对照）
        log_rate = 100.0 * max(0, exp - rx_bytes) / exp
        rx_idx = sorted(int(m.group(1)) for it in rx
                        for m in [re.search(r"@(\d{3})\s", it.get("text", ""))] if m)
        missing = [i for i in range(1, n + 1) if i not in set(rx_idx)]
        results.append(dict(baud=baud, sent=len(sent), n=n, entries=len(rx), missing=missing,
                            t_from=(rx[0].get("t") if rx else None),
                            t_to=(rx[-1].get("t") if rx else None),
                            rx_bytes=rx_bytes, exp=exp, rate=rate, log_rate=log_rate, bad=bad,
                            counters=content, db=db, dl=dl, got=got_frames,
                            dropped=meta.get("dropped"), missed=meta.get("missed"),
                            lines_ok=len(recv), lines_partial=len(partial),
                            secs=time.time() - t0, send_ms=send_ms))
        # 接收端帧到达间隔（只看长度=内容的条目，避开被截断的）
        ts = sorted(int(it["t"]) for it in rx if int(it.get("bytes") or 0) == CONTENT)
        gaps = [b - a for a, b in zip(ts, ts[1:])]
        gap_txt = ""
        if len(gaps) >= 3:
            gaps_sorted = sorted(gaps)
            gap_txt = (f" | 帧到达间隔 中位 {gaps_sorted[len(gaps_sorted)//2]}ms"
                       f" 最小 {gaps_sorted[0]}ms 最大 {gaps_sorted[-1]}ms")
        print(f"  发出 {len(sent)}/{n}（{send_ms:.0f}ms） | 收到 {got_frames:.1f} 帧 / "
              f"{content} 字节（应为 {exp}）→ 丢包 {rate:.2f}%"
              f" | 对照·COM10日志口径 {rx_bytes} 字节 / {len(rx)} 条 → {log_rate:.2f}%"
              f" | Δbytes={db} Δlines={dl} | 行口径 完整{len(recv)}/截断{len(partial)}"
              + gap_txt, flush=True)
        print(f"  日志缺失序号（{len(missing)} 个）: {missing[:40]}", flush=True)

    print("\n======== 汇总（100 字节/帧；主判据＝分栏累计字节数，日志口径仅对照）========")
    print(f"{'波特率':>9} {'发出':>9} {'收到帧':>8} {'收到字节':>9} {'应为':>7} {'丢包率':>8} {'[日志口径]':>10}")
    for r in results:
        print(f"{r['baud']:>9} {str(r['sent']) + '/' + str(r['n']):>9} {r['got']:>8.1f}"
              f" {r['counters']:>9} {r['exp']:>7} {r['rate']:>7.2f}% {r['log_rate']:>9.2f}%")

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "baud100-%s.md" % time.strftime("%Y%m%d-%H%M%S"))
    with open(out, "w", encoding="utf-8") as f:
        f.write("# 不同波特率 · 定时 100 字节 · 丢包率\n\n")
        f.write(f"- 时间：{time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write("- 路径：COM9(主机数据口) → 2.4G → COM10(从机 UART)\n")
        f.write(f"- 每档 {n} 帧，每帧线上 {FRAME_LEN} 字节（{CLEN} 内容 + LF）；"
                f"每次调用打包 {k} 帧、组间隔 {interval*1000:.0f}ms"
                f"（等效每帧 {interval*1000/k:.0f}ms，≈{FRAME_LEN*k/interval/1000:.2f} KB/s）\n")
        f.write("- ⚠️ 组内 k 帧是**连发**的（USB 写入速度，微秒级），不是均匀 10ms —— "
                f"MCP 限流 60 次/分决定了做不到均匀 10ms/帧，只能保证**平均速率**。\n")
        f.write("- 主判据＝分栏累计字节数（`outputBytes/outputLines`，扣掉每行时间戳前缀）折算帧数；"
                f"次要判据＝COM10 日志条目的 bytes —— **低速下工具会把长条目截断**，"
                f"日志口径会显著高估丢包（实测同一批流量：计数器 2376 字节 vs 日志 73 字节）。\n")
        f.write(f"- 判据：**COM10 的日志**（日志中心 channel `{CH}`）里每条记录的 `bytes` 求和\n\n")
        f.write(f"- COM10 日志窗口：t={results[0]['t_from']} → {results[0]['t_to']}\n\n")
        f.write("| 波特率 | 发出 | 日志条数 | 收到字节 | 应为 | 丢包率 | 日志丢弃 | 行口径(完整/截断) |\n")
        f.write("|---|---|---|---|---|---|---|---|\n")
        for r in results:
            f.write(f"| {r['baud']} | {r['sent']}/{r['n']} | {r['entries']} | {r['rx_bytes']} |"
                    f" {r['exp']} | **{r['rate']:.2f}%** | {r['dropped']} |"
                    f" {r['lines_ok']}/{r['lines_partial']} |\n")
    print(f"\n报告已写入：{out}")


if __name__ == "__main__":
    main()
