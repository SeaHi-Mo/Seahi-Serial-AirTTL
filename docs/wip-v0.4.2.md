# WIP 记录：v0.4.2 的三笔待验证改动

> 状态：**三笔改动已合并进 master**（2026-10-13 晚），但**尚未打 tag、未发版** —— CI 只在推 `v*` tag 时出 Release。
> 桌面上的 `RF_Uart.hex` / `RF_UartDongle.hex` 就是这三笔的构建（md5 见下表）。
> 记录时间：2026-10-13 晚。

## 一、待验证的三笔改动

| # | 板 | 文件 | 行为变化 | 资源 | 桌面固件 MD5 |
|---|---|---|---|---|---|
| 1 | 从机 | `RF_Uart/APP/include/uart.h`（`RTS_PULSE_DELAY_MS 500` / `RTS_PULSE_LOW_MS 200`）、`RF_Uart/APP/rf_uart_tx.c`（`rfFlushDtrRts()` 复位脉冲状态机） | **RTS 断言沿生成复位**：先等 500 ms（此间 PA2 保持高）→ 再压低；**上位机仍按着就一直保持低**，早释放则低满 200 ms 回高。两值都置 0 = 恢复纯跟随 | FLASH 12772 B（5.20%）、RAM 11492 B（93.52%） | `166d3902ee5acd8f82c578dc045f8c50`（含改动 1+3） |
| 2 | 主机 | `RF_UartDongle/APP/rf_uart_rx.c`（`rf_bound()` / 新增 `rfBoundConfirm()` / `rfProcessRx()` 已绑定分支入口） | **"已连接"改由"在新参数上收到从机第一包"认定**（不再在 BOUND_RSP 发完那一刻单向假定）：LED 熄灭、USB 枚举、`serverData` 落盘都挪到那时 | FLASH 21772 B（8.86%）、RAM 10272 B（83.59%） | `506c865aebd3ba76979645f0dada0d61` |
| 3 | 从机 | `RF_Uart/APP/rf_uart_tx.c`（`PKT_DATA_RSP_ACK` 分支） | **空 ACK / 保活包不再驱动 LED**：`LedDataPulse()` 移进"确实带下行数据"的判断里 | 与改动 1 同一份固件 | `166d3902ee5acd8f82c578dc045f8c50` |

对照：v0.4.1 的从机固件 = `2900527a…`，主机固件 = `e0868a85…`。

### 1. RTS = 复位脉冲发生器（从机）

**为什么**：Ai-WB2/BL602 经本桥进 ROM ISP 时，`bflb` ISP 工具的复位脉冲在 **115200 下够宽、能进 ISP**，
而 **921600 下只有约 234 ms 且来得太早** → 工具永远停在 `Please Press Reset Key!`、等不到
`shake hand success`（极易误判成"无线桥不能烧录"）。

**怎么改**（演化过程：先做过"保证 ≥500 ms 低电平"，后按实测改成**固定形状的脉冲发生器**）：
从机不再"跟随"上位机的脉宽，而是在 **RTS 断言沿**启动一次固定序列：

```
断言沿 --[先等 RTS_PULSE_DELAY_MS = 500ms，此间 PA2 保持高]--> 压低 PA2
     |-- 上位机仍按着 RTS ------> 一直保持低，等释放后低满 RTS_PULSE_LOW_MS = 200ms 才回高
     |-- 上位机已释放 RTS ------> 低满 200ms 回高（固定脉冲）
```
（2026-10-14 补：此前只做"固定脉冲"，Ai-WB2-SDK 的 hello world 烧录出现"无法识别复位"——
上位机按"先按住 RESET，再操作，最后释放"的流程时，固定脉冲跑完就回高，工具再也看不到它期待的复位。
故补上"按住即保持低"这一支，两条流程都兼容。）

前 500 ms 延迟是留给上位机把 **BOOT0（DTR/PA3）** 等线摆好、并开始发同步字节（工具通常是
"先设 BOOT、再拉 RESET、然后发同步"）；脉冲形状与上位机按多久完全解耦。两个值都置 0 可恢复
"PA2 纯跟随 RTS"的老行为。实现：`gRtsPulseStage`（0 空闲 / 1 延迟中 / 2 脉冲中）在
`rfFlushDtrRts()` 里由主循环推进，零阻塞。

**怎么验**：`python3 tools/loss-test/dtr_rts_test.py --mode monitor` 盯 PA2 —— 上位机断言 RTS 后应看到
**先保持高约 500 ms，再出现一次约 200 ms 的低电平**，然后回高（与上位机按多久无关）。

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

### 3. LED：保活 / 空 ACK 不再闪（从机）

**为什么**：实测反馈"**没有任何数据在传，LED 却偶尔闪一下**"。查下来主机侧两处（`rf_uart_rx.c:836`
无线侧、`usb_uart.c:2016` USB 侧）**都**有数据守卫（`gRxDataStatus == RCV` / `gEnd2DataLen`），不会空闪；
问题在从机 `rf_uart_tx.c` 的 `PKT_DATA_RSP_ACK` 分支 —— 那句 `LedDataPulse()` 原本写在
"有没有下行数据"判断**之前**，而主机对**上次上行数据的纯 ACK**（`length == PKT_DATA_OFFSET`）
也是这个类型，且它在重传时会被原样重发 → 于是出现"无数据的空闪"。

**怎么改**：把 `LedDataPulse()` 移进 `if( pPkt->length > PKT_DATA_OFFSET+1 )` 内。
从机 LED 现在三处触发点**全是数据驱动**：① `PKT_DATA_RSP_ACK` 带下行数据（`:507`）、
② `OPCODE_DATA` 下行数据（`:617`）、③ 发出上行数据 `s == 0`（`:950`）—— 语义不变，空包不再闪。

**怎么验**：PC 口不打开、目标不发东西，静置观察 → LED 不应再出现零星单闪；一旦有真实数据仍会闪一下。
若静置时还会闪，说明确实有东西在发（目标定时上报 / 噪声），或空口在丢包重传 ——
按本文档上面那条"三种来源"表（PC 口有没有字节 / 主机调试口 `ulost` 与 `crc` 是否在涨）区分。

### 4. 经 AirTTL 用 Ai-WB2（博流 BL602）SDK 工程：**运行输出**的波特率也要 <= 1.5 Mbps

**这不是烧录失败的原因，别混淆**（我第一版写错过，2026-10-14 更正）。

**现象**：SDK 的 `applications/get-started/helloworld` 默认 `CONFIG_UART_OTA_FAST_BAUD := 2000000`
（该组件**启动即把控制台切到这个速率**）。而从机对 2 Mbps 只能生成 **1.5 Mbps**（整数分频，差 −25%，
见 README「波特率上限 1.5 Mbps」）-> 于是**应用跑起来以后，PC 端串口看到的就是乱码**。
把它改成 **921600**（= 成功的 `AiPi-Clock-Mini` 的取值）后，打印才正常可读。

**要点**：
1. 应用侧「启动即切控制台」的旋钮（如 `CONFIG_UART_OTA_FAST_BAUD`）必须 **<= 1500000**，推荐 **921600**；
2. 改完之后 PC 端串口工具也要用同一个速率，否则一样是乱码；
3. 与本桥固件无关 —— 从机只是**忠实夹到 1.5 Mbps**。

**仍然未解决（与本条无关）**：helloworld 用 `make flash` 走本桥**烧录仍然失败** ——
桥侧 `shake hand success` 之后在**载入 helper**（`eflash_loader_40m.bin`，37184 B）时出错：
进度到 `4080/37184` 就报不认识回应、`BFLB LOAD HELP BIN FAIL`。
已知对照：**同一套工具、同一块桥烧 `AiPi-Clock-Mini` 成功**。
待查方向（按优先级）：① 成功那次用的**速率**（有一份 Clock-Mini 尝试的日志显示 `com speed: 115200`，
而失败的 helloworld 用的是 921600）-> 先试 `make flash b=115200`；
② 两个工程用的**烧录工具版本**是否不同（Clock-Mini 的 `flash.sh` 用的是它自带 `sdk/` 里那套）；
③ 从机 0x7F 一键下载是否在 ISP 会话中途抢了 BOOT/RESET（诊断件 `RF_Uart-noSTISP.hex`）。

## 二、已经发布的部分（对照用）

- **v0.4.1 已推送**（tag `v0.4.1` → 提交 `80c3595`，远端已确认）：主干是**下行静默丢包根因修复**
  （从机 UART 环满整包拒收却照样 `gTxDataSeq++` → 主机以为送达不再重发 → 每包静默丢一次；
  实测 921600 有效率 **11~17% → 100%**，逐字节一致）。另含按档挑主频（921600 → 60 MHz，−3.12% → +1.72%）、
  2 Mbps 精确档开关（默认关）、以及 `bulk_test.py` / `dtr_rts_test.py` 两个测量脚本。
- 细节见 `README.md`「按档挑主频 / 波特率上限 / 故障排查」、`skills/coder-ch570q-airttl/`、
  `.github/release-notes/v0.4.1.md`。

## 三、待办

- [ ] 验证第 1 笔（921600 ISP 烧录）→ 通过则保留，不通过则 `git checkout -- RF_Uart/APP/...` 回退重编
- [ ] 验证第 2 笔（主机 LED / 真连接判定）与第 3 笔（保活包不再闪 LED）
- [ ] 给 `tools/loss-test/dtr_rts_test.py` 补**窄脉冲回归用例**：注入 5 ms 窄脉冲 → PA2 必须仍给出 ≥500 ms
- [ ] 三笔都过了再提交 + 发 **v0.4.2**（v0.4.1 已发布，不再往里塞东西）
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
