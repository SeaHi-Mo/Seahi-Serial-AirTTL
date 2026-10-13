#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AirTTL 真实吞吐上限（旁路 MCP，pyserial 直灌）

为什么必须另写一个：现有 throughput_test.py 走 MCP，而 MCP 有 **60 次/分、每次 ≤64 KB**
的限流 → 喂料上限约 55 KB/s。文档里"27.6 ~ 50 KB/s"就是**这个喂料上限的结果**，是实测
**下限而非链路上限**（见 docs/oshwhub-promotion.md 的"吞吐口径说明"）。本脚本直接用
pyserial 连续灌，喂料能力是链路的几十倍，所以量到的才是链路自身的上限。

协议端的天花板可以算出来（供对照）：单包 ≤251 B、从机每 1 ms 轮询一次 → **≈250 KB/s**；
一轮"轮询+应答"只占空口约 0.2 ms。实测若远低于它，瓶颈就在每轮固定开销/反压路径上。

测法（两条方向各测一次）：
    下行：PC --USB--> dongle --2.4G--> 从机 --UART--> PC        (--tx = dongle 口)
    上行：PC --UART--> 从机 --2.4G--> dongle --USB--> PC        (--tx = 从机 UART 口)
判据：
    · write() 阻塞占比高 → 链路在反压发送侧，**发送侧不是瓶颈**（这才是我们要的状态）；
    · 接收侧丢帧/坏帧 ≈ 0 → 收到的 KB/s 就是**真实上限**；
    · 有丢帧 → 喂料超过了链路吸收能力，用 --rate 逐档降，找到"零丢包的最高档"＝可用上限。

关键前提：
    1) 两个口都要能独占打开 —— **先在 SeaHi Serial 里把这两个分栏"停止监控"**（否则被占用）；
    2) tx 是 dongle 口时要先释放 DTR/RTS（脚本已做：打开后立刻释放），否则目标被按住；
    3) 两口的波特率要一致（= 从机 UART 的线码；从机能给的上限见 README 的波特率章节）；
    4) 烧录工具的 ISP 速率应低于这里量出的上限，否则必然超时。

用法：
    python3 bulk_test.py --tx /dev/ttyUSB0 --rx /dev/ttyUSB1 --baud 1500000 --mb 2
    python3 bulk_test.py --tx /dev/ttyUSB0 --rx /dev/ttyUSB1 --baud 115200  --rate 20
    # 限速扫描（找零丢包上限）：--rate 用逗号分隔，逐个跑一遍
    python3 bulk_test.py --tx /dev/ttyUSB0 --rx /dev/ttyUSB1 --baud 1500000 --rate 40,80,120,200
"""
import argparse
import os
import statistics
import sys
import threading
import time

try:
    import serial
except ImportError:
    sys.exit("需要 pyserial：pip install pyserial")

FRAME = 250          # 帧总长（含结尾 \n）：6 + 237 + 6 + 1
FILLER = FRAME - 6 - 6 - 1
CTRL_C = False


# 【填充必须避开 0x0A】接收侧是按 \n 切帧的，填充若含 0x0A 就会把帧切碎
# （实测：用 (seq+i)&0xFF 时有效帧只剩 2.87%，全是"坏帧"）。所以用可打印字母表。
FILLER_ALPHABET = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"


def _fill(seq):
    """与序号绑定的确定性填充：**伪随机**（LCG），保证 24 字节锚点不会周期性重现。
    （用 62 字符表循环 `(seq+i)%62` 时，任何 24 字节锚点都会在 +62 处重现 —— 自测里
     因此把"单字节翻转"误判成"插入 62 B"。）
    取值落在 33..122（可打印、绝不含 \n/\r），且与序号绑定 → 错位/丢字节都能查出来。"""
    x = (seq * 2654435761 + 1) & 0xFFFFFFFF
    out = bytearray(FILLER)
    for i in range(FILLER):
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        out[i] = 33 + (x % 90)
    return bytes(out)


def make_frame(seq):
    """定长帧：@NNNNN + 确定性填充 + #NNNNN + \\n。"""
    head = b"@%05d" % (seq % 100000)
    tail = b"#%05d" % (seq % 100000)
    return head + _fill(seq) + tail + b"\n"


def check_frame(line, expect_seq=None):
    """回 (seq, ok)。空/长度不对/头尾不匹配/填充不对 → ok=False。"""
    if len(line) != FRAME - 1 or not line.startswith(b"@"):
        return None, False
    try:
        seq = int(line[1:6])
    except ValueError:
        return None, False
    if line[6 + FILLER:6 + FILLER + 6] != b"#%05d" % seq:
        return None, False
    if line[6:6 + FILLER] != _fill(seq):
        return None, False
    if expect_seq is not None and seq != expect_seq:
        return seq, False
    return seq, True


# --------------------------------------------------------------------------- #
# 对齐分析：把"发送流"与"接收流"做**字节级**对齐，给出每个事件的**确切字节数**
#
# 为什么必须字节级、不能按帧号差算：主机的下行包长是**可变**的（`USB_RxQuery` 有多大取
# 多大，最多 250 B），所以"丢一个包"会让后面的字节流整体错位 —— 按帧号差算会把
# "少 2 个字节"也报成"少 250 字节"，正好把要区分的两种情形搞反（自测里踩过）。
#
# 判读（这才是能定位的判据）：
#     · 删除 ≈ 几十~250 B（一个包长） → 整包丢失：空口丢 / 从机侧整包丢弃
#     · 删除/插入 1~几 B               → 字节级损伤：UART 那一跳（线速/桥/线材）
#     · 插入（同一段内容重复）          → 重传被重复投递
# --------------------------------------------------------------------------- #
ANCHOR = 24         # 重新对上所需的"锚"长度（24 字节偶然相同的概率可忽略）
MAXK = 8192         # 单次事件最多容忍的跳过量（字节）


def _common(a, i, b, j, limit):
    """a[i:] 与 b[j:] 从第几个字节开始不同（最多比 limit 个）。块比对 + 块内逐字节。"""
    n = 0
    while n < limit:
        sz = min(4096, limit - n)
        sa = a[i + n:i + n + sz]
        sb = b[j + n:j + n + sz]
        if sa == sb:
            n += sz
            continue
        for k in range(sz):
            if sa[k] != sb[k]:
                return n + k
        n += sz
    return n


def align_report(expected, got):
    """字节级对齐。回 [(exp_off, 类型, 字节数)]；类型 = 删除/插入/无法重同步。"""
    ev = []
    exp_pos = pos = 0
    le, lg = len(expected), len(got)
    while pos < lg and exp_pos < le:
        # 不要把上限设小：命中上限会被误当成"分歧"（自测里每 64 KB 报一次假事件）
        same = _common(expected, exp_pos, got, pos, min(le - exp_pos, lg - pos))
        exp_pos += same
        pos += same
        if pos >= lg or exp_pos >= le:
            break
        if exp_pos + ANCHOR > le or pos + ANCHOR > lg:
            ev.append((exp_pos, "尾部截断", le - exp_pos))
            break
        a_got = got[pos:pos + ANCHOR]
        a_exp = expected[exp_pos:exp_pos + ANCHOR]
        # 先试"替换"：两边各跳同样多的字节后能重新对上（单字节翻转就属这类）
        m_sub = 0
        for m in range(1, 65):
            if exp_pos + m + ANCHOR > le or pos + m + ANCHOR > lg:
                break
            if expected[exp_pos + m:exp_pos + m + ANCHOR] == got[pos + m:pos + m + ANCHOR]:
                m_sub = m
                break
        if m_sub:
            ev.append((exp_pos, "替换", m_sub))
            exp_pos += m_sub
            pos += m_sub
            continue
        i_del = expected.find(a_got, exp_pos + 1, exp_pos + MAXK)   # expected 少了 k 字节
        i_ins = got.find(a_exp, pos + 1, pos + MAXK)                # got 多了 k 字节
        k_del = (i_del - exp_pos) if i_del >= 0 else None
        k_ins = (i_ins - pos) if i_ins >= 0 else None
        if k_del is None and k_ins is None:
            ev.append((exp_pos, "无法重同步", le - exp_pos))
            break
        if k_del is not None and (k_ins is None or k_del <= k_ins):
            ev.append((exp_pos, "删除", k_del))
            exp_pos = i_del
        else:
            ev.append((exp_pos, "插入", k_ins))
            pos = i_ins
    if pos < lg and exp_pos >= le:
        ev.append((exp_pos, "尾部多余", lg - pos))
    return ev


def report_align(expected, got, max_detail=6):
    ev = align_report(expected, got)
    print("\n--- 对齐分析（发送流 vs 接收流，字节级）---")
    dels = [e[2] for e in ev if e[1] == "删除"]
    inss = [e[2] for e in ev if e[1] == "插入"]
    if not ev:
        print("  ✅ 两个流逐字节一致：这次传输是**字节完好**的")
        return
    if dels:
        b = {"1~9B": 0, "10~49B": 0, "50~199B": 0, "≥200B(≈整包)": 0}
        for x in dels:
            b["1~9B" if x <= 9 else "10~49B" if x <= 49 else "50~199B" if x < 200
              else "≥200B(≈整包)"] += 1
        print(f"  删除 {len(dels)} 段（共 {sum(dels)} B）：" +
              "、".join(f"{k} {v} 次" for k, v in b.items() if v))
        if b["≥200B(≈整包)"] >= b["1~9B"] + b["10~49B"]:
            print("  判读：**以整包删除为主** → 整包丢失（空口丢包 / 从机侧整包丢弃），"
                  "不是字节级损伤")
        elif b["1~9B"] > b["≥200B(≈整包)"]:
            print("  判读：**以小段删除为主（1~9B）** → 字节级损伤（UART 那一跳："
                  "线速/桥/线材）")
        else:
            print("  判读：整包删除与小段损伤**并存** —— 两层都有问题")
    subs = [e[2] for e in ev if e[1] == "替换"]
    if subs:
        print(f"  替换 {len(subs)} 处（共 {sum(subs)} B；多为 1~几字节 = 单/双字节翻转）："
              f"最大 {max(subs)} B")
        if not dels:
            print("  判读：只有替换、没有整段删除 → 字节级损伤（UART 那一跳：线速/桥/线材）")
    if inss:
        print(f"  插入 {len(inss)} 段（共 {sum(inss)} B）→ 同一段内容重复投递（重传未去重）")
    for off, kind, size in [e for e in ev if e[1] not in ("删除", "插入")][:3]:
        print(f"  ⚠ offset={off} {kind}（{size} B）→ 断点之后基本没收上来")
    print(f"  首断点：发送流第 {ev[0][0]} 字节处（{ev[0][1]} {ev[0][2]} B）")
    print(f"  前 {max_detail} 个事件：")
    for off, kind, size in ev[:max_detail]:
        print(f"    exp_offset={off:<9} {kind} {size} B")


class Sender(threading.Thread):
    """连续灌数据；记录写入字节数、阻塞在 write() 里的时间。"""

    def __init__(self, ser, total_bytes, rate_kbps):
        super().__init__()
        self.ser = ser
        self.total = total_bytes
        self.rate = rate_kbps            # 0 = 不限速
        self.sent = 0
        self.blocked = 0.0
        self.t0 = None
        self.t1 = None
        self.err = None
        self._stop_req = False

    def run(self):
        seq = 0
        buf = bytearray()
        self.t0 = time.perf_counter()
        try:
            while self.sent < self.total and not self._stop_req:
                buf.clear()
                for _ in range(16):                     # 一次凑 4 KB 再写，减少调用次数
                    buf += make_frame(seq)
                    seq += 1
                t = time.perf_counter()
                n = self.ser.write(bytes(buf))
                self.blocked += time.perf_counter() - t
                self.sent += n
                if self.rate:
                    # 限速：按累计应发时长回压
                    due = self.t0 + self.sent / (self.rate * 1000.0)
                    now = time.perf_counter()
                    if due > now:
                        time.sleep(due - now)
        except Exception as e:                          # 端口掉了之类
            self.err = e
        self.t1 = time.perf_counter()

    def stop(self):
        self._stop_req = True


def run_one(args, rate_kbps):
    tx = serial.Serial(args.tx, args.baud, timeout=0.01, write_timeout=None)
    rx = serial.Serial(args.rx, args.baud, timeout=0.01)
    for p in (tx, rx):
        try:
            p.dtr = False                            # 打开端口常会自动断言 —— 先释放
            p.rts = False
        except Exception:
            pass
    time.sleep(0.5)

    rx.reset_input_buffer()
    total = int(args.mb * 1024 * 1024)
    raw = bytearray()                       # 收到的**原始字节流**（供对齐分析/落盘）
    snd = Sender(tx, total, rate_kbps)
    snd.start()

    # 收：按 \n 切帧，边收边统计
    total_bytes = 0
    valid = bad = 0
    seqs = []
    buf = bytearray()
    last_data = time.perf_counter()
    r0 = time.perf_counter()
    while True:
        chunk = rx.read(4096)
        now = time.perf_counter()
        if chunk:
            total_bytes += len(chunk)
            raw += chunk
            buf += chunk
            last_data = now
            while True:
                i = buf.find(b"\n")
                if i < 0:
                    break
                line = bytes(buf[:i])
                del buf[:i + 1]
                seq, ok = check_frame(line)
                if ok:
                    valid += 1
                    seqs.append(seq)
                else:
                    bad += 1
        else:
            if not snd.is_alive() and (now - last_data) > args.drain:
                break
            if (now - r0) > args.timeout:
                print(f"  ⚠ 超过 {args.timeout:g}s 上限，提前收尾", flush=True)
                break
    r1 = time.perf_counter()
    snd.join(timeout=2)
    tx.close()
    rx.close()

    send_s = (snd.t1 or r1) - (snd.t0 or r0)
    recv_s = max(1e-9, r1 - r0)
    # 丢帧：seq 不连续的部分（只用连续段落统计，避免把绕回算成丢）
    lost = 0
    for a, b in zip(seqs, seqs[1:]):
        d = (b - a) % 100000
        if d > 1:
            lost += d - 1
    # 重复帧：同一个 seq 出现多次 → 说明重传件被重复投递（与 rf_uart_tx.c:463 的 seq 门控矛盾）
    cnt = {}
    for q in seqs:
        cnt[q] = cnt.get(q, 0) + 1
    dup = sum(c - 1 for c in cnt.values() if c > 1)

    # 逐字节损伤率（上界）：把"整帧完好率"折算成每字节的等效损坏概率 —— 整帧丢失也压低了
    # 完好率，所以这是上界。250 B 的帧在 0.25% 字节损伤下就只剩 ~53% 完好。
    total_frames = valid + bad + lost
    intact = valid / float(total_frames) if total_frames else 0.0
    byte_dmg = (1.0 - intact ** (1.0 / FRAME)) * 100 if intact > 0 else 0.0

    ok_kbps = valid * FRAME / 1024.0 / recv_s
    tx_kbps = snd.sent / 1024.0 / send_s if send_s > 0 else 0
    blk = 100.0 * snd.blocked / send_s if send_s > 0 else 0

    print(f"\n=== 限速 {rate_kbps or '不限'} KB/s | 波特率 {args.baud} | 灌 {args.mb:g} MB ===")
    print(f"  发送: {snd.sent/1024:.0f} KB / {send_s:.2f}s = {tx_kbps:.1f} KB/s"
          f"（write 阻塞占比 {blk:.0f}%）")
    print(f"  接收: {total_bytes/1024:.0f} KB / {recv_s:.2f}s = {total_bytes/1024/recv_s:.1f} KB/s"
          f"（原始字节）")
    print(f"  帧: 有效 {valid}、坏 {bad}、丢 {lost}、重复 {dup}  → 有效率 "
          f"{100.0*valid/max(1,valid+bad+lost):.2f}%")
    print(f"  逐字节损伤率（上界，由 1-(有效/总帧)^(1/{FRAME}) 反推）: {byte_dmg:.3f}%"
          + "   ← 结论看下面的对齐分析（坏帧多**不能**说明是字节损伤）")
    if dup:
        print(f"  ⚠ 重复帧 {dup} 个：重传件被重复投递 —— 与 rf_uart_tx.c:463 的 seq 门控矛盾，要查")
    print(f"  ★ 端到端有效吞吐（只算校验通过的帧）: {ok_kbps:.1f} KB/s")
    if snd.err:
        print(f"  ⚠ 发送侧异常: {snd.err}")
    if blk < 10:
        print("  ⚠ write 几乎没阻塞 → **喂料还没到链路极限**（这次的数不是上限，加大 --mb 或查端口）")
    # 【判据以对齐分析为准】逐帧校验只能说"坏没坏"，说不了"坏在哪" —— 见 report_align 的说明
    n_sent_frames = snd.sent // FRAME
    expected = b"".join(make_frame(k) for k in range(n_sent_frames))
    if args.raw_out:
        with open(args.raw_out, "wb") as f:
            f.write(bytes(raw))
        print(f"  → 原始接收流已落盘: {args.raw_out}（{len(raw)} B），发送流可用 make_frame() 重建")
    report_align(expected, bytes(raw))
    return {"rate": rate_kbps, "ok_kbps": ok_kbps, "raw_kbps": total_bytes/1024/recv_s,
            "valid": valid, "bad": bad, "lost": lost, "dup": dup,
            "byte_dmg": byte_dmg, "blocked_pct": blk}


def main():
    ap = argparse.ArgumentParser(description="AirTTL 真实吞吐上限（旁路 MCP）")
    ap.add_argument("--tx", required=True, help="发送口（下行时=dongle 口；上行时=从机 UART 口）")
    ap.add_argument("--rx", required=True, help="接收口（与 --tx 相反的那一侧）")
    ap.add_argument("--baud", type=int, default=1500000)
    ap.add_argument("--mb", type=float, default=2.0, help="总共灌多少 MB")
    ap.add_argument("--rate", default="0",
                    help="限速 KB/s（0=不限速；逗号分隔可扫多档，如 40,80,120,200）")
    ap.add_argument("--drain", type=float, default=1.5, help="发完后静置多久认定收完（秒）")
    ap.add_argument("--timeout", type=float, default=120.0)
    ap.add_argument("--raw-out", default="", help="把接收到的原始字节流落盘（供离线对齐/取证）")
    ap.add_argument("--csv", default="")
    args = ap.parse_args()

    rates = [float(x) for x in args.rate.split(",") if x.strip()]
    rows = []
    for r in rates:
        rows.append(run_one(args, r))
        time.sleep(0.8)                              # 让链路/缓冲排空，下一档干净

    if len(rows) > 1:
        print("\n=== 汇总（找「零丢包的最高档」＝可用上限）===")
        for x in rows:
            flag = ("干净 ✓" if (x["bad"] == 0 and x["lost"] == 0)
                    else f"坏{x['bad']}/丢{x['lost']}/重复{x['dup']}")
            print(f"  限速 {x['rate'] or '不限':>6} KB/s → 有效 {x['ok_kbps']:7.1f} KB/s"
                  f"  逐字节损伤 {x['byte_dmg']:.3f}%  {flag}")
    if args.csv:
        with open(args.csv, "w", encoding="utf-8") as f:
            f.write("rate_kbps,ok_kbps,raw_kbps,valid,bad,lost,dup,byte_dmg_pct,blocked_pct\n")
            for x in rows:
                f.write(f"{x['rate']},{x['ok_kbps']:.2f},{x['raw_kbps']:.2f},"
                        f"{x['valid']},{x['bad']},{x['lost']},{x['dup']},"
                        f"{x['byte_dmg']:.3f},{x['blocked_pct']:.1f}\n")
        print(f"→ 已写 {args.csv}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
