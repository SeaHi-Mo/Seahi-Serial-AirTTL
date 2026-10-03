#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
单次突发容量测试：每轮先 close/open 恢复链路，然后只发「一次」突发，看对端收到多少。
用来判定：链路是「空闲就断」还是「收一包后就再也不收」。
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
import sys
BAUD = int(sys.argv[1]) if len(sys.argv) > 1 else 115200
BURSTS = [1, 2, 3, 5, 8, 10, 15, 20, 30, 50]

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


def call(name, args, quiet=True):
    gap = time.time() - _last[0]
    if gap < 1.1:
        time.sleep(1.1 - gap)
    _last[0] = time.time()
    res = post({"jsonrpc": "2.0", "method": "tools/call",
                "params": {"name": name, "arguments": args}})
    if not quiet:
        print("   ", name, json.dumps(res.get("result", {}), ensure_ascii=False)[:160])
    if res.get("result", {}).get("isError"):
        print("   ⚠", name, "错误:", json.dumps(res["result"], ensure_ascii=False)[:160])
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


def apply_baud(baud):
    for p in (SENDER_PANE, RECV_PANE):
        call("serial_close", {"pane": p})
    for p in (SENDER_PANE, RECV_PANE):
        call("serial_set_baud", {"pane": p, "baud": baud})
    for p in (SENDER_PANE, RECV_PANE):
        call("serial_open", {"pane": p})


post({"jsonrpc": "2.0", "method": "initialize",
      "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                 "clientInfo": {"name": "burst", "version": "1.0"}}})
post({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}}, notify=True)
print(f"MCP 会话: {_sid}")
print("每轮：close/open 恢复链路 -> 只发一次突发 -> 数对端收到多少\n")

for idx, n in enumerate(BURSTS):
    apply_baud(BAUD)
    time.sleep(1.2)
    stx, srx = snap(SENDER_PORT), snap(RECV_PORT)
    seqs = [1000 + idx * 1000 + i for i in range(1, n + 1)]
    data = "".join("@%04d\n" % s for s in seqs)
    call("serial_send", {"pane": SENDER_PANE, "data": data,
                         "mode": "text", "lineEnding": "none"}, quiet=False)
    time.sleep(n * 5 * 10 / BAUD + 2.5)
    got = set(int(m) for m in re.findall(r"@(\d{4})", newer(RECV_PORT, srx)))
    sent = set(int(m) for m in re.findall(r"@(\d{4})", newer(SENDER_PORT, stx)))
    hit = set(seqs) & got
    print("突发 %3d 帧 (%4d 字节): 发出记录 %3d, 从机实收 %3d%s\n" %
          (n, n * 5, len(set(seqs) & sent), len(hit),
           "" if len(hit) == n else "   ← 丢 %d" % (n - len(hit))))
