#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SeaHi-Serial-AirTTL 2.4G 透传链路丢包测试 —— 不同波特率，每档 500 帧

路径：PC COM9(主机数据口) --USB--> 主机 --2.4G--> 从机 --UART--> PC COM8(从机 UART)

关键实测结论（决定了本脚本的形态）：
  · 链路「空闲就断」：两次发送之间留 1.5s 以上就再也不通了。所以每档必须
    「close/open 重连 -> 尽快一次连续发完 500 帧」，中间不能有停顿；
  · 改波特率必须「关闭端口 -> 设置 -> 重新打开」，只改输入框不会下发到 CH341/主机；
  · 每次 close/open 都新建 log-cache 会话文件，统计必须按「文件快照+增量」做。

统计手段：会话日志实时落盘；单方向发送时，接收侧文件里出现的帧即为实收。

用法：python3 loss_test.py [波特率,逗号分隔]
"""
import glob
import json
import os
import re
import sys
import time
import urllib.request

def _mcp_url():
    """MCP 地址：优先环境变量 SEAHI_MCP_URL，其次读 SeaHi Serial 的 mcp-endpoint.json。
    注意：不要把 token 写死在源码里（会进 git 历史）。"""
    import json
    url = os.environ.get("SEAHI_MCP_URL")
    if url:
        return url
    for p in (os.path.join(os.environ.get("APPDATA", ""), "seahi-serial", "mcp-endpoint.json"),
              "/mnt/d/Users/Seahi/AppData/Roaming/seahi-serial/mcp-endpoint.json"):
        try:
            with open(p, encoding="utf-8") as f:
                d = json.load(f)
            if d.get("urlStreamable"):
                return d["urlStreamable"]
        except OSError:
            pass
    raise SystemExit("未找到 MCP 地址：请设置环境变量 SEAHI_MCP_URL")
LC = "/mnt/d/Users/Seahi/AppData/Roaming/seahi-serial/log-cache"
URL = _mcp_url()

SENDER_PANE = "main"      # COM9 —— 主机数据口
RECV_PANE = "extra-4"     # COM8 —— 从机 UART
SENDER_PORT = "COM9"
RECV_PORT = "COM8"

N_FRAMES = 500
BAUDS = [9600, 115200, 460800, 921600, 1500000]
MIN_CALL_GAP = 1.1        # MCP 限流 60 次/分

_sid = None
_id = 0
_last_call = [0.0]


def _post(body, notify=False):
    global _sid, _id
    _id += 1
    if not notify:
        body["id"] = _id
    data = json.dumps(body).encode("utf-8")
    hdr = {"Content-Type": "application/json",
           "Accept": "application/json, text/event-stream"}
    if _sid:
        hdr["mcp-session-id"] = _sid
    req = urllib.request.Request(URL, data=data, headers=hdr)
    with urllib.request.urlopen(req, timeout=60) as r:
        if not _sid:
            _sid = r.headers.get("mcp-session-id")
        raw = r.read().decode("utf-8", "ignore")
    if notify:
        return None
    if raw.lstrip().startswith("event:"):
        for line in raw.splitlines():
            if line.startswith("data:"):
                return json.loads(line[5:].strip())
        raise ValueError("SSE 响应里没有 data 行")
    return json.loads(raw)


def call_tool(name, args):
    gap = time.time() - _last_call[0]
    if gap < MIN_CALL_GAP:
        time.sleep(MIN_CALL_GAP - gap)
    _last_call[0] = time.time()
    res = _post({"jsonrpc": "2.0", "method": "tools/call",
                 "params": {"name": name, "arguments": args}})
    if "error" in res:
        raise RuntimeError(f"{name} 失败: {res['error']}")
    if res.get("result", {}).get("isError"):
        print(f"   ⚠ {name}: {json.dumps(res['result'], ensure_ascii=False)[:160]}",
              flush=True)
    return res


def _files(port):
    return glob.glob(os.path.join(LC, f"*-{port}.log"))


def snapshot(port):
    d = {}
    for f in _files(port):
        try:
            d[f] = os.path.getsize(f)
        except OSError:
            pass
    return d


def collect(port, snap):
    out = []
    for f in _files(port):
        try:
            with open(f, "rb") as fh:
                fh.seek(snap.get(f, 0))
                out.append(fh.read().decode("utf-8", "ignore"))
        except OSError:
            pass
    return "".join(out)


def frames_in(text):
    return set(int(m) for m in re.findall(r"@(\d{3})", text))


def send_frames(pane, seqs):
    data = "".join("@%03d\n" % s for s in seqs)
    # 【关键】必须让工具的 lineEnding 非 none：实测同样的字节，lineEnding=none
    # 时逐帧发送会丢约 90%，而 lf/crlf 时为 0 丢 —— 差别在工具的发送路径而非字节内容。
    call_tool("serial_send", {"pane": pane, "data": data,
                              "mode": "text", "lineEnding": "lf"})


def apply_baud(baud):
    """改波特率：关闭端口 -> 选对端口 -> 设置 -> 重新打开。

    同时也是链路从「空闲断开」中恢复的手段。
    注意：拔插 USB / 烧录之后，SeaHi Serial 的端口选择可能回落到别的设备
    （实测回落成 COM4 / COM3），此时 open 打开的是错设备 → 全丢。所以每次
    都要先把端口选回来。
    """
    for p in (SENDER_PANE, RECV_PANE):
        call_tool("serial_close", {"pane": p})
    call_tool("serial_select_port", {"pane": SENDER_PANE, "port": SENDER_PORT})
    call_tool("serial_select_port", {"pane": RECV_PANE, "port": RECV_PORT})
    for p in (SENDER_PANE, RECV_PANE):
        call_tool("serial_set_baud", {"pane": p, "baud": baud})
    for p in (SENDER_PANE, RECV_PANE):
        call_tool("serial_open", {"pane": p})


def tx_time(baud, nbytes):
    return nbytes * 10.0 / baud


def run_baud(baud, results):
    print(f"\n=== {baud} bps ===", flush=True)
    seqs = list(range(1, N_FRAMES + 1))
    sent = set()
    recv = set()
    for attempt in (1, 2):
        apply_baud(baud)
        time.sleep(1.2)                        # 够从机同步线码，又不至于空闲断开
        snap_tx = snapshot(SENDER_PORT)
        snap_rx = snapshot(RECV_PORT)
        t0 = time.time()
        send_frames(SENDER_PANE, seqs)         # ★ 一次连续发完，中途不停顿
        time.sleep(tx_time(baud, N_FRAMES * 5) + 2.5)
        sent = frames_in(collect(SENDER_PORT, snap_tx)) & set(seqs)
        recv = frames_in(collect(RECV_PORT, snap_rx)) & set(seqs)
        print(f"  第 {attempt} 次突发：发出 {len(sent)}/500，收到 {len(recv)}/500"
              f"（耗时 {time.time() - t0:.1f}s）", flush=True)
        if recv or attempt == 2:
            break
        print("  零接收，重连后重试…", flush=True)

    lost = sorted(set(seqs) - recv)
    lost_tx = sorted(set(seqs) - sent)
    rate = 100.0 * len(lost) / N_FRAMES
    results.append((baud, len(sent), len(recv), len(lost), rate, lost))
    print(f"  → 丢包 {len(lost)}（{rate:.2f}%）", flush=True)
    if lost_tx:
        print(f"  ⚠ 发送侧缺失 {len(lost_tx)} 帧: {lost_tx[:20]}")
    if lost:
        print(f"  丢失序号(前40): {lost[:40]}")


def main():
    bauds = BAUDS
    if len(sys.argv) > 1:
        bauds = [int(x) for x in sys.argv[1].split(",")]

    _post({"jsonrpc": "2.0", "method": "initialize",
           "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                      "clientInfo": {"name": "loss-test", "version": "2.0"}}})
    _post({"jsonrpc": "2.0", "method": "notifications/initialized",
           "params": {}}, notify=True)
    print(f"MCP 会话: {_sid}")
    print(f"每档 {N_FRAMES} 帧（{'@NNN' + chr(10)}，共 {N_FRAMES * 5} 字节），"
          f"一次连续发完；方向：COM9 -> COM8")

    results = []
    try:
        for b in bauds:
            run_baud(b, results)
    finally:
        try:
            apply_baud(115200)
            print("\n已恢复两端 115200")
        except Exception as e:
            print("恢复波特率失败:", e)

    print("\n" + "=" * 78)
    print("%-10s %-6s %-6s %-6s %-9s %s" %
          ("波特率", "发出", "收到", "丢包", "丢包率", "丢失序号"))
    print("-" * 78)
    for baud, s, r, l, rate, lost in results:
        brief = ",".join(str(x) for x in lost[:8]) + ("…" if len(lost) > 8 else "")
        print("%-10d %-6d %-6d %-6d %-8.2f%% %s" % (baud, s, r, l, rate, brief or "-"))
    print("=" * 78)


if __name__ == "__main__":
    main()
