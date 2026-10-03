#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
探测「一次性能安全传送多少帧」——阶梯递增，找出下行捎带的实际容量。
不改变任何端口设置，只往 main(COM9) 发、从 COM8 的会话日志看收到多少。
"""
import glob
import json
import os
import re
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

SENDER_PANE, RECV_PANE = "main", "extra-4"
SENDER_PORT, RECV_PORT = "COM9", "COM8"
SIZES = [1, 2, 3, 5, 10, 20, 30, 40, 50]

# 可选方向：down = COM9->COM8（下行），up = COM8->COM9（上行）
import sys as _sys
if len(_sys.argv) > 1 and _sys.argv[1] == "up":
    SENDER_PANE, RECV_PANE = "extra-4", "main"
    SENDER_PORT, RECV_PORT = "COM8", "COM9"

_sid = None
_id = 0
_last = [0.0]


def post(body, notify=False):
    global _sid, _id
    _id += 1
    if not notify:
        body["id"] = _id
    hdr = {"Content-Type": "application/json",
           "Accept": "application/json, text/event-stream"}
    if _sid:
        hdr["mcp-session-id"] = _sid
    req = urllib.request.Request(URL, data=json.dumps(body).encode(), headers=hdr)
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
    return json.loads(raw)


def call(name, args):
    gap = time.time() - _last[0]
    if gap < 1.1:
        time.sleep(1.1 - gap)
    _last[0] = time.time()
    res = post({"jsonrpc": "2.0", "method": "tools/call",
                "params": {"name": name, "arguments": args}})
    if res.get("result", {}).get("isError"):
        print("  ⚠ 工具错误:", json.dumps(res["result"], ensure_ascii=False)[:150])
    return res


def files(port):
    return glob.glob(os.path.join(LC, f"*-{port}.log"))


def snap(port):
    d = {}
    for f in files(port):
        try:
            d[f] = os.path.getsize(f)
        except OSError:
            pass
    return d


def newer(port, s):
    out = []
    for f in files(port):
        try:
            with open(f, "rb") as fh:
                fh.seek(s.get(f, 0))
                out.append(fh.read().decode("utf-8", "ignore"))
        except OSError:
            pass
    return "".join(out)


def send(pane, seqs):
    data = "".join("@%04d\n" % s for s in seqs)
    call("serial_send", {"pane": pane, "data": data,
                         "mode": "text", "lineEnding": "none"})


post({"jsonrpc": "2.0", "method": "initialize",
      "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                 "clientInfo": {"name": "probe-size", "version": "1.0"}}})
post({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}}, notify=True)
print(f"MCP 会话: {_sid}")
print("每帧 5 字节（@NNNN + LF），发到 main(COM9)，看 COM8 收到几帧\n")

for idx, n in enumerate(SIZES):
    seqs = [1000 + idx * 100 + i for i in range(1, n + 1)]
    stx, srx = snap(SENDER_PORT), snap(RECV_PORT)
    send(SENDER_PANE, seqs)
    time.sleep(n * 5 * 10 / 115200.0 + 1.2)
    got = set(int(m) for m in re.findall(r"@(\d{4})", newer(RECV_PORT, srx)))
    sent_ok = set(int(m) for m in re.findall(r"@(\d{4})", newer(SENDER_PORT, stx)))
    hit = set(seqs) & got
    print("%3d 帧 (%4d 字节) -> 主机记录发出 %3d, 从机收到 %3d%s" %
          (n, n * 5, len(set(seqs) & sent_ok), len(hit),
           "" if len(hit) == n else f"   ← 丢 {n - len(hit)}"))
