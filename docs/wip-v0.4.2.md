# WIP 记录：v0.4.2 的两笔待验证改动

> 状态：**两笔改动都已编译、已放到桌面待验证，尚未提交到 master**。
> 本文件随 `wip/v0.4.2-rts-led` 分支一起保存；master 仍等于已发布的 **v0.4.1**。
> 记录时间：2026-10-13 晚。

## 一、待验证的两笔改动

| # | 板 | 文件 | 行为变化 | 资源 | 桌面固件 MD5 |
|---|---|---|---|---|---|
| 1 | 从机 | `RF_Uart/APP/include/uart.h`（新增 `RTS_MIN_LOW_MS 500`）、`RF_Uart/APP/rf_uart_tx.c`（`rfFlushDtrRts()`） | **RTS 断言后，PA2 至少压低 500 ms** 才允许回高（上位机提前释放也不理会，由从机把差额补齐） | FLASH 12796 B（5.21%）、RAM 11500 B（93.59%） | `591662395c960365beaf8153f26f3f0f` |
| 2 | 主机 | `RF_UartDongle/APP/rf_uart_rx.c`（`rf_bound()` / 新增 `rfBoundConfirm()` / `rfProcessRx()` 已绑定分支入口） | **"已连接"改由"在新参数上收到从机第一包"认定**（不再在 BOUND_RSP 发完那一刻单向假定）：LED 熄灭、USB 枚举、`serverData` 落盘都挪到那时 | FLASH 21772 B（8.86%）、RAM 10272 B（83.59%） | `506c865aebd3ba76979645f0dada0d61` |

对照：v0.4.1 的从机固件 = `2900527a…`，主机固件 = `e0868a85…`。

### 1. RTS 最小低电平（从机）

**为什么**：Ai-WB2/BL602 经本桥进 ROM ISP 时，**RST 低电平要求 ≥ 500 ms**；而 `bflb` ISP 工具的复位脉冲
**在 115200 下够宽、921600 下只有约 234 ms** → 921600 烧录时工具永远停在 `Please Press Reset Key!`、
等不到 `shake hand success`（极易误判成"无线桥不能烧录"）。

**怎么改**：`rfFlushDtrRts()` 里记录断言时刻；释放沿按"还差多少"决定保持时长
（`max(RTS_RELEASE_HOLD_MS, RTS_MIN_LOW_MS − 已低时长)`）。**闭环在从机侧** —— 保证的是 PA2 引脚上的
物理宽度，与 PC 给多宽、RF 投递抖动无关。DTR/PA3（BOOT0）仍是纯跟随；`0x7F` 一键下载时序走另一条路，
不受影响；`RTS_MIN_LOW_MS 0` 可回到老行为（只保留 30 ms 释放保持）。

**怎么验**：
1. 烧桌面 `RF_Uart.hex`，主机不换也能测；
2. **工具设置全部保持原样**跑 `make flash b=921600` → 应能进 ISP、开始写 flash；
3. 想眼见为实：`python3 tools/loss-test/dtr_rts_test.py --mode monitor` 盯 PA2，跑一次烧录，
   应看到**一次 ≥500 ms 的低电平**（改之前约 234 ms）。

### 2. "已连接"判定（主机）

**为什么**：原来 `rfProcessTx()` 里一旦 `gRfStatus == RF_STATUS_TXRSP`（BOUND_RSP 发完）就调用
`rf_bound()` → `RF_bound_Flag = 1` —— **单向假定**。从机若没收到那包（空口丢包 / 还在旧参数上重试），
主机就会"LED 熄灭 + 枚举 USB + 守着新参数等"，而从机还在旧参数上广播 → **灯说连上了、数据却不通**，
直到主机超时断开。

**怎么改**：`rf_bound()` 只保留"切 sync word/频点/PHY + `gBoundStatus = WAIT`"（切参数必须留在 TX 完成，
不切就听不到新参数）；`RF_bound_Flag = 1`、`PRINT("bound success (link up).")`、`rfSaveServerData()`
挪到新增的 `rfBoundConfirm()`，由 `rfProcessRx()` 的已绑定分支在**收到第一包**时调用。
因为接入地址变了，**只有从机真的采纳了新参数才会在新 AA 上发包** → "收到新参数首包"是可靠的双向证明。

调试口现在能区分两句：`bound rsp sent.AA CH`（已发应答、已切参数）→ `bound success (link up).`（真连上）。

**怎么验**：
1. 烧桌面 `RF_UartDongle.hex`：上电应**一直快闪**（未连接）→ 从机靠近配对 → **收到首包那一刻熄灭**；
2. 从机断电/拿远 → 去抖后应回到**快闪**；
3. 刻意制造"BOUND_RSP 丢失"（配对瞬间把从机拿远再拿回）→ 新逻辑下**不该出现"熄着但数据不通"**，
   调试口应停在 `bound rsp sent` 而没有 `bound success`。

## 二、已经发布的部分（对照用）

- **v0.4.1 已推送**（tag `v0.4.1` → 提交 `80c3595`，远端已确认）：主干是**下行静默丢包根因修复**
  （从机 UART 环满整包拒收却照样 `gTxDataSeq++` → 主机以为送达不再重发 → 每包静默丢一次；
  实测 921600 有效率 **11~17% → 100%**，逐字节一致）。另含按档挑主频（921600 → 60 MHz，−3.12% → +1.72%）、
  2 Mbps 精确档开关（默认关）、以及 `bulk_test.py` / `dtr_rts_test.py` 两个测量脚本。
- 细节见 `README.md`「按档挑主频 / 波特率上限 / 故障排查」、`skills/coder-ch570q-airttl/`、
  `.github/release-notes/v0.4.1.md`。

## 三、待办

- [ ] 验证第 1 笔（921600 ISP 烧录）→ 通过则保留，不通过则 `git checkout -- RF_Uart/APP/...` 回退重编
- [ ] 验证第 2 笔（主机 LED / 真连接判定）
- [ ] 给 `tools/loss-test/dtr_rts_test.py` 补**窄脉冲回归用例**：注入 5 ms 窄脉冲 → PA2 必须仍给出 ≥500 ms
- [ ] 两笔都过了再提交 + 发 **v0.4.2**（v0.4.1 已发布，不再往里塞东西）
- [ ] 可选优化：下行"环水位预判"（环快满时提前让主机停发，减少环满期间的无效重发）

## 四、复现命令

```bash
# 编译两份
cd RF_Uart        && cmake --build build -j"$(nproc)" && cp build/RF_Uart.hex ../RF_Uart-rts.hex
cd ../RF_UartDongle && cmake --build build -j"$(nproc)"

# 回退某笔改动（源码即记录，WIP 分支另有提交）
git checkout -- RF_Uart/APP/rf_uart_tx.c RF_Uart/APP/include/uart.h   # 回退 RTS 那笔
git checkout -- RF_UartDongle/APP/rf_uart_rx.c                        # 回退主机那笔

# 链路健康度复核（应报「两个流逐字节一致」）
python3 tools/loss-test/bulk_test.py --tx /dev/ttyUSB0 --rx /dev/ttyACM0 --baud 921600 --mb 0.5 --rate 60
```
