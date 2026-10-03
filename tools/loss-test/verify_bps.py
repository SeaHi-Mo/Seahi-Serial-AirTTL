#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
验证「PC 侧接收口波特率该用标称值还是主机反算值」。

背景：主机 usb_uart.c 用 CH341 分频寄存器反算波特率（9600->9615、115200->115384），
并把该值下发给从机（rf_uart_rx.c 的 OPCODE_BSP），所以从机 UART 实际跑 9615。
若 PC 侧 COM8 用标称 9600，两端就差 0.16%，连续长流会大量误码。

本脚本固定 COM9=9600（从机 UART 随之变成主机反算值），只改 COM8 的波特率，
各发 500 帧对比实收。
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import loss_test as L

BAUD = 9600
N = 500
RX_TRIALS = [9600, 9615, 9648]

L._post({"jsonrpc": "2.0", "method": "initialize",
         "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                    "clientInfo": {"name": "verify-bps", "version": "1.0"}}})
L._post({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
        notify=True)
print(f"MCP 会话: {L._sid}")
print(f"COM9 固定 {BAUD}（从机 UART = 主机反算值），改 COM8 波特率对比，各发 {N} 帧\n")

for rx in RX_TRIALS:
    # 1) 两端先都设标称值（触发一次完整的线码同步）
    L.apply_baud(BAUD)
    time.sleep(0.8)
    # 2) 把 PC 侧接收口改成待验证的值
    L.call_tool("serial_close", {"pane": L.RECV_PANE})
    L.call_tool("serial_set_baud", {"pane": L.RECV_PANE, "baud": rx})
    L.call_tool("serial_open", {"pane": L.RECV_PANE})
    # 3) 重开发送口重新激活链路（链路空闲就会断，必须在发送前一刻做）
    L.call_tool("serial_close", {"pane": L.SENDER_PANE})
    L.call_tool("serial_set_baud", {"pane": L.SENDER_PANE, "baud": BAUD})
    L.call_tool("serial_open", {"pane": L.SENDER_PANE})
    time.sleep(0.8)

    snap_tx = L.snapshot(L.SENDER_PORT)
    snap_rx = L.snapshot(L.RECV_PORT)
    seqs = list(range(1, N + 1))
    L.send_frames(L.SENDER_PANE, seqs)
    time.sleep(N * 5 * 10.0 / BAUD + 2.5)

    recv = L.frames_in(L.collect(L.RECV_PORT, snap_rx)) & set(seqs)
    lost = len(set(seqs) - recv)
    print("COM8=%-6d  实收 %3d/%d   丢 %3d (%.1f%%)" %
          (rx, len(recv), N, lost, 100.0 * lost / N), flush=True)
    time.sleep(0.5)

# 恢复
L.apply_baud(115200)
print("\n已恢复两端 115200")
