# 无线透传协议详解（字节级）

> 本文是 `rf.h` 里那套私有 2.4G 协议的完整说明：帧结构、每个命令的字节布局、时序参数、状态机与超时规则。
> **改协议时必须两端同时改**（`RF_Uart/APP/rf_uart_tx.c` 与 `RF_UartDongle/APP/rf_uart_rx.c`），且 `rf.h` 里的常量保持一致。

---

## 一、帧结构

所有包都以 4 字节头开始，**小端**：

```
 0        1        2        3        4 ...
+--------+--------+--------+--------+-------------------+
| type   | length | seq    | resv   | 载荷(opcode + data)|
+--------+--------+--------+--------+-------------------+
|<---- PKT_HEAD_LEN = 4 ---->|<---- length - 2 ---->|
```

### `length` 字段的真实语义（最容易踩的坑）

```c
#define  PKT_HEAD_LEN      sizeof(rfPackage_t)   // 4
#define  PKT_DATA_OFFSET   (PKT_HEAD_LEN-2)      // 2   ← 注意是 2，不是 4
```

- `length` = **2 + 载荷字节数**（即"从 `type` 字节数到包尾"）
- 包的**物理总长** = 4 + 载荷字节数 = `length + 2`
- 所以代码里到处是这两个式子：
  - 组包：`pPkt->length = payload_len + PKT_DATA_OFFSET;`
  - 解包：`payload_len = pPkt->length - PKT_DATA_OFFSET - 1;`（那个 `-1` 是 opcode 占的字节）

**举例**：从机把串口收到的 100 字节发出去

```
type=0x7E, length=100+2=102, seq=N, resv=0, [100 字节串口数据]
物理总长 = 106，length 字段 = 102
```

> 写新代码时**不要用 `PKT_HEAD_LEN` 去算载荷长度**，那会多算 2 字节。判据永远是 `PKT_DATA_OFFSET`。

---

## 二、命令总表

| type | 名称 | 方向 | 载荷 | `length` |
|---|---|---|---|---|
| `0x01` | `PKT_CMD_BOUND_REQ` | 从 → 主 | `bound_req_t`（4B） | `2+4 = 6` |
| `0x81` | `PKT_CMD_BOUND_RSP` | 主 → 从 | `bound_rsp_t`（12B） | `2+12 = 14` |
| `0x02` | `PKT_CMD_GET_STATUS` | 从 → 主 | 无 | `2` |
| `0x82` | `PKT_CMD_RSP_STATUS` | 主 → 从 | `opcode` + 线码(8B) 或 数据(nB) 或 空 | `2+1+8` / `2+1+n` / `2` |
| `0x7E` | `PKT_DATA_FLAG` | 从 → 主 | 串口数据（≤251B） | `2+n` |
| `0xFE` | `PKT_DATA_RSP_ACK` | 主 → 从 | `opcode` + 下行数据 或 空 | `2+1+n` / `2` |

应答载荷首字节 `opcode`：`OPCODE_DATA(0x00)` / `OPCODE_BSP(0x01)` / `OPCODE_ACK(0xF0)`。

---

## 三、各命令的字节布局

### 3.1 绑定请求 `0x01`（从机 → 主机）

```c
typedef struct __attribute__((packed)) {
    uint16_t interval;    /* 广播/确认期间发送间隔，单位 ms，从机填 ADV_INTERVAL=20 */
    uint16_t severData;   /* 上次绑定的识别信息；首次连接为 0 */
} bound_req_t;
```

```
[0]=0x01 [1]=0x06 [2]=seq [3]=0 [4..5]=interval(20) [6..7]=severData
```

主机校验：`pPkt->length == sizeof(bound_req_t) + 2` 即 `== 6`，否则忽略。

> 主机还从**包尾之后**读取 RSSI：`rssi = *(int8_t*)((uint8_t*)pPkt + pPkt->length + 4)`——即依赖"协议栈把 RSSI 附在包数据后面"这一行为。

### 3.2 绑定应答 `0x81`（主机 → 从机）

```c
typedef struct __attribute__((packed)) {
    uint32_t accessaddr;  /* 连接态接入地址（随机） */
    uint8_t  channel;     /* 连接态频点 = serverData & 0x3F（0~63） */
    uint8_t  phy;         /* 连接态 PHY，填 CONN_PHY_TYPE = 1（2M） */
    uint16_t severData;   /* 本次生成的识别信息 */
    uint16_t interval;    /* 连接间隔 ms = CONN_INTERVAL = 10 */
    uint16_t timeout;     /* 断开时间 ×10ms = CONN_TIMEOUT = 100 */
} bound_rsp_t;
```

```
[0]=0x81 [1]=0x0E [2]=seq [3]=0 [4..7]=accessaddr [8]=channel
[9]=phy [10..11]=severData [12..13]=interval [14..15]=timeout
```

主机生成规则（`rf_uart_rx.c`）：

```c
gServerData    = rf_rand16(rssi);          /* 线性同余伪随机 */
pRsp->accessaddr = rf_rand_aa(gServerData); /* (serverData<<8) | 0x6E0000B6 */
pRsp->channel    = gServerData & 0x3F;      /* 频点 0~63 */
pRsp->phy        = CONN_PHY_TYPE;           /* 1 = 2M */
```

### 3.3 状态轮询 `0x02`（从机 → 主机）

无载荷，`length = 2`。从机在**已绑定**状态下按 `gInterval` 周期发送，或有下行数据待取时连续探询（`getDataProbe` 机制，见 §5.2）。

### 3.4 状态应答 `0x82`（主机 → 从机）

三种形态，由 `opcode` 区分：

**(a) 线码 `OPCODE_BSP`** —— `length = 2 + 1 + 8 = 11`

```
[0]=0x82 [1]=0x0B [2]=seq [3]=0 [4]=0x01(opcode)
[5..8]=BaudRate(LE) [9]=StopBits [10]=ParityType [11]=DataBits [12]=ioStaus
```

从机收到后立即落地到 UART 寄存器（详见 [SKILL.md 第六节](../SKILL.md)）。

**(b) 下行数据 `OPCODE_DATA`** —— `length = 2 + 1 + n`

```
[0]=0x82 [1]=2+1+n [2]=seq [3]=0 [4]=0x00(opcode) [5..]=数据
```

**(c) 空应答 `OPCODE_ACK`** —— `length = 2`

### 3.5 上行数据 `0x7E`（从机 → 主机）

```
[0]=0x7E [1]=n+2 [2]=seq [3]=0 [4..]=串口数据（n ≤ 251）
```

主机收到后写入 RF 缓冲，并通过 EP2 IN 端点送往 USB。

### 3.6 数据确认 `0xFE`（主机 → 从机）

主机对 `0x7E` 的应答，**可以捎带一段下行数据**（省一次往返）：

- 有下行数据：`length = 2 + 1 + n`，`opcode = OPCODE_DATA`
- 无下行数据：`length = 2`（空包）

从机在 `rfProcessRx()` 里判断 `pPkt->length > PKT_DATA_OFFSET+1` 来决定是否解析捎带数据。

---

## 四、连接建立时序

```
       从机 (RF_Uart)                        主机 (RF_UartDongle)
            |                                        |
            |  未绑定：每 20ms 广播                    |
            |-- 0x01 BOUND_REQ(interval=20,           |
            |      severData=Flash 里的值) --------->|
            |                                        |  判定：
            |                                        |   severData!=0 且匹配 → 放行
            |                                        |   severData==0（首次）→ 要求 RSSI>-58dBm
            |                                        |   否则打印 "reject.." 丢弃
            |<-- 0x81 BOUND_RSP(accessaddr,          |
            |      channel=srv&0x3F, phy=2M,         |
            |      severData, interval=10,           |
            |      timeout=100) ---------------------|
            |                                        |
      切接入地址/频点/PHY                       切接入地址/频点/PHY
      写 Flash {0x55AA, severData}              置 RF_bound_Flag=1
      置 RF_bound_Flag=1                        等 20ms（rf_tx_start 的 waitTime）
            |                                        |
            |  已绑定：每 10ms 轮询                    |
            |-- 0x02 GET_STATUS -------------------->|
            |<-- 0x82 RSP_STATUS(线码/数据/空) -------|
            |                                        |
```

**判定细节**（`rf_uart_rx.c` 的 `rfProcessRx()` → `BOUND_STATUS_IDLE` 分支）：

| 收到的 `severData` | 主机行为 |
|---|---|
| `!= 0` 且 `gServerData == 0` | 放行（**主机刚上电，不记得上次的 serverData**） |
| `!= 0` 且 与 `gServerData` 相等 | 放行（回连） |
| `!= 0` 且 不相等 | 拒绝（不是配对的从机） |
| `== 0` | 首次连接，`rssi > -35` 才放行 |

> 从机的配对要求"贴近"（RSSI > -58 dBm）只在**首次**生效；之后靠随机 `serverData` 匹配，与距离无关。

---

## 五、数据透传时序

### 5.1 上行（目标设备 → PC）

```
目标设备 --UART--> 从机
  串口中断把字节写入 uart_buf(3KB)
  攒够 32 字节 或 100bit 超时 → RCV_END
从机主循环 RF_StatusQuery():
  UART_RxQuery() 取出数据，打包 0x7E 发出（rf_tx_start）
主机收到 0x7E:
  写入 pRfBuf(512B)，回 0xFE（若能取到下行数据则捎带）
  USB_StatusQuery() → RF_RxQuery() → 填 Ep2Buffer[64] → EP2 IN 上传
PC 侧串口工具显示
```

### 5.2 下行（PC → 目标设备）

```
PC 写串口 → USB OUT(EP2) 中断 → 写入 usb_buf(512B)
USB_RxQuery() 返回 0（有数据）或 0x80（线码变了）
主机在收到 0x02 GET_STATUS 时：
  0x80 → 回 0x82 + OPCODE_BSP(线码)
  0    → 回 0x82 + OPCODE_DATA(数据)
  都没有 → 回 0x82 + OPCODE_ACK(空)
从机收到 0x82：
  写 pRfBuf(512B) 并置 gRfRxFlag
  UART 中断 default 分支把 pRfBuf 搬进 R8_UART_THR 发给目标设备
```

**`getDataProbe` 机制**：从机一旦收到数据（`OPCODE_DATA`）或 ACK，就把 `getDataProbe = 6`，表示接下来 6 次主循环**立刻再发一次 `GET_STATUS`**（不必等 10ms 定时），用于把连续到达的数据尽快取完，降低时延。

---

## 六、关键时序参数

| 参数 | 值 | 定义处 | 作用 |
|---|---|---|---|
| `ADV_INTERVAL` | 20 ms | `rf_uart_tx.h` | 未绑定时广播/绑定请求间隔 |
| `CONN_INTERVAL` | 10 ms | `rf_uart_rx.h` | 已绑定后从机轮询间隔 |
| `CONN_TIMEOUT` | 100（×10ms=1s） | `rf_uart_rx.h` | 链路断开判定 |
| 从机接收窗口 | `rf_rx_start(150)` → 150 µs | `rf_uart_tx.c` | 发完等应答的窗口 |
| 从机发送前等待 | `rf_tx_start(..., 60)` → `waitTime=120` | `rf_uart_tx.c` | 切频点/信道后稳定时间 |
| 主机发送前等待 | `rf_tx_start(pPkt_t, 20)` → `waitTime=40` | `rf_uart_rx.c` | 同上 |
| 主机接收窗口 | `rf_rx_start(gIntervalTimer)` = `CONN_INTERVAL*1000` µs = 10 ms | `rf_uart_rx.c` | 一次接收窗口 |
| `BOUND_EST_COUNT` | 6 | `rf.h` | 绑定确认阶段允许的超时次数 |
| `RESEND_COUNT` | 40 | `rf_uart_tx.h` | 单包最大重传次数，超过**丢弃** |
| 串口组包阈值 | 32 字节 / 100 bit 超时 | `uart.c` | 决定一帧何时上无线 |

> `rf_tx_start()` / `rf_rx_start()` 的入参单位是 µs，**函数内部会乘以 2** 再写寄存器（`waitTime = rfon_us*2`）。
>
> ⚠️ **`waitTime` 字段是 `uint8_t`**（见 `LIB/CH572rf.h` 的 `rfipTx_t`）：`waitTime = rfon_us * 2`，因此**入参 `rfon_us` 不得超过 127**，否则 8 位回绕（例如 `rf_tx_start(buf, 128)` → `waitTime = 0`，等于放弃了 PLL 稳定等待）。项目当前用 `60`（→120）是安全的，但**余量很小**；`RFRole_Init()` 里预置的 `80*2 = 160` 也在范围内。改这个值时务必先核对。

---

## 七、超时与重传规则

### 从机（`rfProcessTimeout()`）

| 当前状态 | 条件 | 动作 |
|---|---|---|
| `RF_STATUS_WAITRSP`（发完等应答超时） | — | `resendCount = RESEND_COUNT(40)`，置 `STA_RESEND` 触发重传 |
| `BOUND_STATUS_WAIT`（绑定确认中） | `++gTimeout > BOUND_EST_COUNT(6)` | `rf_disconnect()`：回落到广播频点/`AA`，清 `RF_bound_Flag` |
| `BOUND_STATUS_EST`（已连接） | `++gTimeout > gTimeoutMax` | 同上，断开重连 |

重传耗尽（`resendCount` 减到 0）：打印 `resend fail.N` 并**丢弃该包**（`STA_IDLE`）。

### 主机（`rfProcessTimeout()` / `RF_RxQuery()`）

- `++gTimeout > gTimeoutMax` → `gRxDataStatus = DATA_STATUS_TIMEOUT`，打印 `connect timeout.`
- `RF_RxQuery()` 看到 `DATA_STATUS_TIMEOUT` 时调 `rf_disconnect()`：`RFRole_Shut()` 关射频、回广播参数、`gServerData = 0`

> `gTimeoutMax` 的算法两端不同：主机 `CONN_TIMEOUT*10/CONN_INTERVAL = 100`；从机用**主机下发的值** `rsp->timeout*10/rsp->interval`。

---

## 八、状态机

### 从机（`gRfStatus` + `gBoundStatus` + `gTxBuf.status`）

```
 gTxBuf.status:  STA_IDLE ──(有数据/要轮询)──▶ STA_BUSY ──(收到应答)──▶ STA_IDLE
                     ▲                                          │
                     └────────(重传)── STA_RESEND ◀──(超时)─────┘

 gBoundStatus:   IDLE ──(发出 BOUND_REQ)──▶ WAIT ──(收到 RSP_STATUS)──▶ EST
                   ▲                         │                          │
                   └────(超时/断开)──────────┴──────────────────────────┘
```

### 主机（`gBoundStatus` + `gRxDataStatus`）

```
 gBoundStatus:   IDLE ──(接受 BOUND_REQ，回 BOUND_RSP)──▶ WAIT ──(收到任意有效包)──▶ EST

 gRxDataStatus:  IDLE/START ──(收到 0x7E)──▶ RCV ──(RF_RxQuery 读空)──▶ START
                                              │
                                              └──(超时)──▶ TIMEOUT ──▶ rf_disconnect()
```

---

## 九、改协议时的检查清单

- [ ] **两端 `rf.h` 同步**：包类型、opcode、`AA`、`CRC_INIT`/`CRC_POLY`、`DATA_LEN_MAX_TX`
- [ ] **`length` 用 `PKT_DATA_OFFSET(=2)` 计算**，别用 `PKT_HEAD_LEN(=4)`
- [ ] **`seq` 匹配**：两端都靠 `pPkt->seq == gTxDataSeq/gDataSeq` 判定有效，加新包类型时别打乱序号推进
- [ ] **`bound_rsp_t` 字段顺序**：它决定对端切到哪个接入地址/频点/PHY，改字段必须两端一起改
- [ ] **单包不超 251 字节**（`DATA_LEN_MAX_TX`），否则接收缓冲溢出
- [ ] 改完**两个工程都要编译**，并确认 RAM 没超（从机余量很小）
- [ ] 若改了 PHY，确认 `rf.h` 对应分支的 `AA`/`CRC` 取值（不同分支不一样）
