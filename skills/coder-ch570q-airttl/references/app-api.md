# 本项目 APP 层 API 参考

> 这里收录的是 `APP/` 目录下**本项目自己的接口**（`rf.*` / `buf.*` / `uart.*` / `rf_uart_tx.*` / `rf_uart_rx.*` / `usb_uart.*` / `log.h`）。
> 沁恒官方库的接口另见 [wch-stdperiph-api.md](./wch-stdperiph-api.md)（标准外设库）与 [rf-stack-api.md](./rf-stack-api.md)（2.4G 协议栈 + 内核）。
>
> **两个工程的同名文件内容基本对称**（`rf.c` / `rf.h` / `buf.c` / `buf.h` / `my_printf.c` / `log.h` 完全一致），差异只在业务层：从机是 `uart.c` + `rf_uart_tx.c`，主机是 `usb_uart.c` + `rf_uart_rx.c`。

---

## 1. `rf.h` — 2.4G 底层封装（两端一致）

**文件**：`APP/rf.c`、`APP/include/rf.h`，依赖 `LIB/CH572rf.h`（预编译协议栈）与 `StdPeriphDriver`。

### 1.1 数据结构

| 类型 | 定义 | 说明 |
|---|---|---|
| `rfPackage_t` | `{ uint8_t type; uint8_t length; uint8_t seq; uint8_t resv; }` | **4 字节无线包头**。`type` 包类型、`length` 数据长度、`seq` 序号（ACK 匹配）、`resv` 保留 |
| `rfTxBuf_t` | `{ uint8_t TxBuf[BUF_LEN_TX]; typeBufSize len; uint8_t status; uint8_t resendCount; }` | 发送缓冲 + 状态（`STA_IDLE`/`STA_BUSY`/`STA_RESEND`）+ 剩余重传次数 |
| `bound_req_t` | `__packed { uint16_t interval; uint16_t severData; }` | **绑定请求**载荷：广播间隔(ms)、上次的识别信息（首次为 0） |
| `bound_rsp_t` | `__packed { uint32_t accessaddr; uint8_t channel; uint8_t phy; uint16_t severData; uint16_t interval; uint16_t timeout; }` | **绑定应答**载荷：接入地址、通信频点、PHY、识别信息、连接间隔(ms)、断开时间(×10ms) |
| `rfRsp_t` | `__packed { uint8_t opcode; union { struct{ uint32_t BaudRate; uint8_t StopBits; uint8_t ParityType; uint8_t DataBits; uint8_t ioStaus; } buad_t; struct{ uint8_t rspData[BUF_LEN_TX]; } other; }; }` | **状态应答/数据承载**：首字节 opcode，后接线码结构或裸数据 |
| `rfStatusCBs_t` | `{ pfnRfRxCB_t pfnRxCB; pfnRfTxCB_t pfnTxCB; pfnRfRxCrcCB_t pfnCrcErrCB; pfnRfTimeoutCB_t pfnTimeoutCB; }` | 四个回调：收包 / 发送完成 / CRC 错误 / 超时 |
| `rfBoundInfo_t` | `{ uint16_t head; uint16_t serverData; }` | 从机 Flash 里保存的绑定信息（`head == 0x55AA` 才有效） |

**回调原型**：

```c
typedef void (*pfnRfRxCB_t)( rfPackage_t *pPkt );   /* 收到一包 */
typedef void (*pfnRfTxCB_t)( void );                /* 发送完成 */
typedef void (*pfnRfRxCrcCB_t)( void  );            /* 收到但 CRC 错 */
typedef void (*pfnRfTimeoutCB_t)( void );           /* 接收超时 */
```

### 1.2 枚举

| 枚举 | 取值 | 用途 |
|---|---|---|
| `enum data_status` | `DATA_STATUS_IDLE` / `START` / `RCV` / `TIMEOUT` | 主机侧"一帧下行数据"的接收状态 |
| `enum bound_status` | `BOUND_STATUS_IDLE` / `WAIT` / `EST` | 绑定状态：未绑定 / 已发请求待确认 / 已建立 |
| `enum rf_status` | `RF_STATUS_IDLE` / `REQ` / `GETS` / `WAIT` / `RX` / `TX` / `WAITRSP` / `TXRSP` / `TXACK` / `RETX` / `REWAIT` | 射频状态机（发送后等待应答、重传等待等） |

### 1.3 函数

| 原型 | 作用 | 注意 |
|---|---|---|
| `void RFRole_Init(void)` | 射频应用层初始化：注册中断回调、配置 PHY/功率/CRC/频点/接入地址、使能 `BLEB_IRQn`/`BLEL_IRQn` | **必须在注册业务回调前后按需调用**；两端参数必须一致 |
| `void RFRole_RegisterStatusCbs(rfStatusCBs_t *p)` | 注册四个状态回调 | 业务层在 `RF_UartTxInit()`/`RF_UartRxInit()` 里调用 |
| `void rf_tx_start(void *pBuf, uint16_t rfon_us)` | **发送一包**：`pBuf` 指向完整 `rfPackage_t`，`rfon_us` 是发前等待时间(µs) | 项目里都传 `60`（内部 `waitTime = µs*2`）；发完触发 `pfnTxCB` |
| `void rf_rx_set_sync_word(uint32_t sync_word)` | 设置接收接入地址 | 未绑定用 `AA`，绑定后用 `bound_rsp_t.accessaddr` |
| `void rf_rx_set_frequency(uint32_t f)` | 设置接收频点 | 未绑定 `DEF_FREQUENCY(17)`，绑定后用 `channel` |
| `void rf_rx_set_phy_type(uint8_t phy)` | 设置接收 PHY | 绑定后设为 `CONN_PHY_TYPE(1 = 2M)` |
| `void rf_rx_start(uint32_t rx_us)` | **开启接收窗口**，`rx_us` 为超时(µs) | 从机 `150`；主机 `gIntervalTimer`（由连接间隔换算） |
| `void rf_tx_set_sync_word(uint32_t sync_word)` | 设置发送接入地址 | 与 rx 侧成对设置 |
| `void rf_tx_set_frequency(uint32_t f)` | 设置发送频点 | 同上 |
| `void rf_tx_set_phy_type(uint8_t phy)` | 设置发送 PHY | 同上 |

> 发送/接收的**接入地址、频点、PHY 必须成对修改**：项目里的 `rf_disconnect()` 和 `rf_bound()` 都是 6 行连着写 `rf_tx_set_*` + `rf_rx_set_*`，照抄这个模式最安全。

### 1.4 关键宏

| 宏 | 值 | 说明 |
|---|---|---|
| `PKT_HEAD_LEN` | `sizeof(rfPackage_t)` = 4 | 包头长度 |
| `PKT_DATA_OFFSET` | `PKT_HEAD_LEN-2` = **2** | ⚠️ 不是 4！`length` 语义含 `type`+`length` 两个字节 |
| `DATA_LEN_MAX_TX` | `251` | 单包最大**载荷**字节数 |
| `BUF_LEN_TX` | `DATA_LEN_MAX_TX + PKT_HEAD_LEN` = 255 | 完整包缓冲大小 |
| `DATA_LEN_MAX_RX` | `BUF_LEN_TX` = 255 | 接收缓冲上限 |
| `PKT_CMD_BOUND_REQ` | `0x01` | 从→主 绑定请求 |
| `PKT_CMD_GET_STATUS` | `0x02` | 从→主 轮询状态 |
| `PKT_DATA_FLAG` | `0x7E` | 从→主 上行数据 |
| `PKT_CMD_BOUND_RSP` | `0x80\|0x01 = 0x81` | 主→从 绑定应答 |
| `PKT_CMD_RSP_STATUS` | `0x80\|0x02 = 0x82` | 主→从 状态应答 |
| `PKT_DATA_RSP_ACK` | `0x80\|0x7E = 0xFE` | 主→从 数据确认 |
| `OPCODE_DATA` | `0x00` | 载荷是串口数据 |
| `OPCODE_BSP` | `0x01` | 载荷是线码设置 |
| `OPCODE_ACK` | `0xF0` | 空应答 |
| `BOUND_EST_COUNT` | `6` | 绑定确认阶段允许的超时次数 |
| `BOUND_INFO_HEAD` | `0x55AA` | Flash 绑定信息的有效标识 |
| `DEF_FREQUENCY` | `17` | 未绑定广播频点 |
| `AA` | `0x57250425`（2M PHY 分支） | 未绑定接入地址；**其他 PHY 分支取值不同** |
| `CRC_INIT` / `CRC_POLY` | `0x555555` / `0x80032d` | 2M PHY 分支 |
| `TEST_PHY_MODE` | `PHY_MODE_PHY_2M` | 当前选用的 PHY 模式 |

---

## 2. `buf.h` — 环形缓冲（两端一致）

**文件**：`APP/buf.c`、`APP/include/buf.h`。**本项目所有跨中断数据流都靠它**。

```c
typedef unsigned long typeBufSize;      /* 未定义时默认为此 */

struct simple_buf {
    uint8_t *start;                     /* 缓冲起始 */
    uint8_t *end;                       /* 缓冲结束（start + buf_len） */
    uint8_t *read;                      /* 读指针 */
    uint8_t *write;                     /* 写指针 */
    uint16_t buf_len;                   /* 总容量 */
    uint16_t volatile data_len;         /* 当前已存字节数 */
};
```

| 原型 | 返回 | 行为与陷阱 |
|---|---|---|
| `struct simple_buf *simple_buf_create(struct simple_buf *buf, uint8_t *buf_pool, uint16_t pool_size)` | 缓冲对象指针 | 初始化指针与长度，**不做动态分配**（缓冲由调用方提供静态数组） |
| `typeBufSize write_buf(struct simple_buf *buf, void *src, typeBufSize *len)` | 写入后的 `data_len` | 入参 `*len` 是**想写多少**。**空间不足时：打印 `#ERR`、把 `*len` 置 0、返回当前 `data_len`（数据被丢弃）**。因此调用方常写 `if(!len) { ... }` 判断是否塞进去了 |
| `typeBufSize read_buf(struct simple_buf *buf, void *dst, typeBufSize *len)` | 读取后的剩余 `data_len` | 入参 `*len` 是**最多读多少**，函数会把它改成**实际读到的字节数**（缓冲里不够就少读）。返回 0 表示已读空 |

> 两者内部用 `PFIC_DisableAllIRQ()` / `PFIC_EnableAllIRQ()` 保护指针，并用 `__MCPY()`（xw 扩展指令）做内存拷贝——**所以这两个函数必须留在 `.highcode`（`__HIGH_CODE`）**。
> 支持环形回绕（写/读跨越 `end` 时自动折返到 `start`）。

---

## 3. `uart.h` / `uart.c` — 从机串口驱动

**仅在 `RF_Uart`（从机）工程**。

### 3.1 引脚与容量宏

| 宏 | 值 | 含义 |
|---|---|---|
| `UART_BUF_LEN` | `1024*3`（3 KB） | 串口环形缓冲容量（**从机 RAM 大头**） |
| `UART_FIFO_SIZE` | 硬件 FIFO | 见 `CH57x_uart.h` |
| `TXD_PIN` | `(1<<0)` | PA0 |
| `RXD_PIN` | `(1<<1)` | PA1 |
| `DTR_PIN` / `BOOT_PIN` | `(1<<3)` | PA3 —— 同一对物理引脚的两个用途，两组宏名恒定义；`DTR_RTS_FUNC`（默认 `TRUE`）决定 PC 的 DTR 是否下发到它 |
| `RTS_PIN` / `RESET_PIN` | `(1<<2)` | PA2（同上，PC 的 RTS / 一键下载的 RESET） |
| `LED_PIN` | `(1<<7)` | PA7 |
| `LED_BLINK_MS` | `100` | LED 翻转间隔（ms），闪烁周期 = 2×该值 = **200ms**；基于 SysTick 真实时间标定（主机在 `usb_uart.h`，同名） |
| `LED_DATA_BLINK` | `1` | 1 = 收发数据时额外闪一下 LED（亮 `LED_DATA_PULSE_MS`）；0 = 不闪（连接成功后保持熄灭） |
| `DATA_LEN_UART` | `32` | 一次搬运的阈值；缓冲里攒够这么多就立即置 `RCV_END` 不等超时 |
| `BOUND_GET_PERI` | `10` | 发送绑定请求的周期（`uart.c` 内部） |

### 3.2 `enum uart_status`

`UART_STATUS_IDLE` → `START` → （`RCVING` → `RCV_END`）或（`SENDING` → `SEND`），由 `TMR_IRQHandler` 与串口中断共同推进。

### 3.3 函数

| 原型 | 作用 | 返回值 / 注意 |
|---|---|---|
| `void UART_Init(void)` | 初始化：LED/DTR-RTS 或 RESET-BOOT 引脚、**关闭仿真调试接口**（`R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN`——手册 §1.2 规定 PA0/PA1 上电默认是 SWDIO/SWCLK，不关就用不了串口）、`UART_Remap(PA0/PA1)`、默认 115200、4 字节触发、开接收中断、启动定时器 | 无 |
| `uint8_t UART_RxQuery(void *buf, typeBufSize *len)` | **从机业务层取"要发去无线的数据"** | `0` = 有数据（`*len` 为长度）；`0x80` = 只有"事件"（`UART_STATUS_SEND`，用于触发发状态查询）；`0xFF` = 无事件 |
| `void UART_SetBuad(uint32_t buad)` | 按当前 `gSysClock` 重算分频写 `R16_UART_DL` | 与上次相同则直接返回；会打印 `bsp = N` |
| `void UART_SetTimer(uint16_t ms)` | 设置定时器周期（按 `gSysClock/2000*ms`） | 用于绑定广播间隔 / 连接轮询间隔 |
| `void UART_Send(char *data, uint16_t size)` | 阻塞式写 `R8_UART_THR` | 遇 FIFO 满会等待（`while(R8_UART_TFC == UART_FIFO_SIZE)`） |

### 3.4 中断行为（`UART_IRQHandler`）

| 触发 | 处理 |
|---|---|
| `UART_II_RECV_RDY`（达到 4 字节触发点） | 把 FIFO 里的字节搬进环形缓冲；**攒够 `DATA_LEN_UART(32)` 立即置 `RCV_END`**，否则置 `RCVING` 并启动"100 bit 超时"（`uart_rx_timeout()`） |
| `UART_II_RECV_TOUT`（接收超时） | 把 FIFO 剩余字节搬进缓冲，置 `RCV_END`（**一帧结束**） |
| `UART_II_LINE_STAT` | 读掉线路状态寄存器并忽略 |
| `default`（发送保持寄存器空） | 若 `gRfRxFlag`，把无线收到的数据从 `pRfBuf` 搬进 `R8_UART_THR` 发出去 |

> **`default` 分支就是"无线→串口"的落地路径**：业务层收到无线数据后写入 `pRfBuf` 并置 `gRfRxFlag`，再由这个中断把数据推给目标板。

---

## 4. `rf_uart_tx.h` / `rf_uart_tx.c` — 从机业务

| 项 | 内容 |
|---|---|
| `ADV_INTERVAL` | `20` — 广播/绑定请求间隔(ms) |
| `RESEND_COUNT` | `40` — 单包最大重传次数，**超过即丢弃该包** |
| `BOUND_INFO_FLASH_ADDR` | `1024*236`（=0xF0000）— 绑定信息 Flash 偏移（4 KB 扇区） |
| `STA_IDLE` / `STA_BUSY` / `STA_RESEND` | 发送缓冲状态：空闲 / 已发出待应答 / 需重传 |
| `extern uint32_t gRfRxFlag` | 非 0 表示 `pRfBuf` 里有待发往串口的数据 |
| `extern struct simple_buf *pRfBuf` | 无线→串口方向的环形缓冲（512 B） |

| 原型 | 作用 |
|---|---|
| `void RF_UartTxInit(void)` | 从机业务初始化：建 RF 缓冲、从 Flash 读回 `gServerData`（`head==0x55AA` 才认）、注册回调 |
| `void RF_StatusQuery(void)` | **从机主循环体**（`main.c` 里 `while(1)` 调用）。负责：LED 指示、把串口数据打包成 `PKT_DATA_FLAG` 发出去、未绑定时发 `PKT_CMD_BOUND_REQ`、已绑定时周期发 `PKT_CMD_GET_STATUS`、以及失败重传 |

**关键全局变量**：`gTxBuf`（发送缓冲）、`gTxDataSeq`（序号）、`gRfStatus`（射频状态机）、`gBoundStatus`、`gInterval`/`gTimeout`/`gTimeoutMax`、`gServerData`、`getDataProbe`（收到数据后继续探询的次数）、`RF_bound_Flag`。

---

## 5. `rf_uart_rx.h` / `rf_uart_rx.c` — 主机业务

| 项 | 内容 |
|---|---|
| `RF_BUF_LEN` | `512` — 无线→USB 方向的环形缓冲 |
| `CONN_INTERVAL` | `10` — 连接间隔(ms) |
| `CONN_TIMEOUT` | `100` — 断连超时（×10ms = 1s） |
| `CONN_PHY_TYPE` | `1` — 连接态 PHY（2M） |

| 原型 | 作用 | 返回值 |
|---|---|---|
| `void RF_UartRxInit(void)` | 主机业务初始化：建 RF 缓冲、清零序号/绑定状态、注册回调 | 无 |
| `uint8_t RF_RxQuery(void *buf, typeBufSize *len)` | **主机主循环取数据**：把无线收到的数据交给调用方（`USB_StatusQuery` 用它填 EP2 缓冲） | 返回 `*len`；`0` = 当前没数据。内部还会在空闲时重新 `rf_rx_start()`，并在 `DATA_STATUS_TIMEOUT` 时调 `rf_disconnect()` |

**关键全局变量**：`gTxBuf`、`gDataSeq`、`gRfStatus`、`gBoundStatus`、`gRxDataStatus`、`gServerData`（主机侧**不存 Flash**）、`gIntervalTimer`、`gTimeout`/`gTimeoutMax`、`gBaudRate`、`gSysClock`。

**主机侧独有的工具函数**（`static`，仅供理解）：

| 函数 | 作用 |
|---|---|
| `rf_rand16(uint32_t seed)` | 线性同余伪随机数，用来生成 `serverData` |
| `rf_rand_aa(uint16_t rand)` | 由随机数派生出接入地址（`(rand<<8) \| 0x6E0000B6`） |
| `rf_bound(bound_rsp_t *rsp)` | 切换射频参数并置 `RF_bound_Flag` |
| `rf_disconnect(void)` | 关射频（`RFRole_Shut()`）、复位到广播参数、清 `gServerData` |

---

## 6. `usb_uart.h` / `usb_uart.c` — 主机 USB（仅 `RF_UartDongle`）

### 6.1 数据结构

```c
typedef struct __PACKED _LINE_CODE {
    uint32_t BaudRate;    /* 波特率 */
    uint8_t  StopBits;    /* 0:1位 1:1.5位 2:2位 */
    uint8_t  ParityType;  /* 0:None 1:Odd 2:Even 3:Mark 4:Space */
    uint8_t  DataBits;    /* 5/6/7/8/16 */
    uint8_t  ioStaus;     /* DTR/RTS 等 IO 状态位 */
} LINE_CODE, *PLINE_CODE;

extern LINE_CODE Uart0Para;   /* 电脑端当前的串口线码 */
```

### 6.2 函数

| 原型 | 作用 | 返回值 / 注意 |
|---|---|---|
| `void USB_Init(void)` | 关闭仿真调试接口（同 `UART_Init`，解除 PA0/PA1 的调试占用）、初始化 USB 设备参数与端点、使能 `USB_IRQn`、建 USB 环形缓冲 | 无 |
| `void USB_StatusQuery(void)` | **主机主循环体**（`main.c` 里 `while(1)` 调用）。处理 USB 中断标志；当 EP2 IN 空闲时调 `RF_RxQuery()` 把无线数据放进 `Ep2Buffer[64]` 并 ACK 上传 | 无 |
| `uint8_t USB_RxQuery(void *buf, typeBufSize *len)` | **主机业务取"要发去无线的数据"** | `0` = 有数据；`0x80` = **线码有变化**（`UART_Status`，业务层据此发 `OPCODE_BSP`）；`0xFF` = 无事件 |

### 6.3 内部关键符号（改 USB 逻辑时会碰到）

| 符号 | 含义 |
|---|---|
| `USB_WORK_MODE` | `USB_VENDOR_MODE`(默认, CH341 兼容) / `USB_CDC_MODE` |
| `Uart0Para` | 线码结构体，被 EP0 请求更新、被 `rf_uart_rx.c` 读出下发 |
| `UART_Status` | 线码/状态变更标志，`USB_RxQuery()` 返回 `0x80` 的依据 |
| `VENSer0ParaChange` / `CDCSer0ParaChange` | USB 复位/挂起后触发端点与缓冲重新初始化 |
| `pUsbBuf` / `usb_buf[USB_BUF_LEN]` | USB OUT 环形缓冲（512 B） |
| `gEnd2DataLen` | EP2 OUT 缓冲里剩余数据量（< 容量-64 时重新 ACK 接收） |
| `Ep2Buffer[2*MAX_PACKET_SIZE]` | EP2 收发缓冲（`MAX_PACKET_SIZE = 64`）；数据实际放在 `Ep2Buffer[64]` 起 |
| `TAB_USB_VEN_DEV_DES` / `TAB_USB_VEN_CFG_DES` | CH341 兼容模式的设备/配置描述符（VID `0x1A86` / PID `0x7523`） |
| `TAB_USB_CDC_DEV_DES` / `TAB_USB_CDC_CFG_DES` | CDC 模式描述符（VID `0x1A86` / PID `0x8040`） |
| `DEF_VEN_DEBUG_WRITE(0x9A)` / `DEF_VEN_UART_INIT(0xA1)` / `DEF_VEN_UART_M_OUT(0xA4)` / `DEF_VEN_BUF_CLEAR(0xB2)` / `DEF_VEN_GET_VER(0x5F)` / `DEF_VEN_DEBUG_READ(0x95)` | CH341 厂商请求码（EP0 里解析） |

---

## 7. `log.h` / `my_printf.c` — 调试打印

| 宏 / 函数 | 说明 |
|---|---|
| `PRINT(...)` | **本项目调试输出主入口**（在 `log.h` 的 `ENABLE_LOG_ROM` 分支里定义为 `{SWU_TX(); log_printf(...);}`）。`DEBUG` 宏关闭时无输出 |
| `dbg_printf(const char *format, ...)` | 格式化输出实现（`my_printf.c`） |
| `my_dump_byte(uint8_t *pData, int dlen)` | 十六进制 dump |
| `__HIGH_CODE_PRINT` | `__attribute__((section(".highcode")))`，把打印函数也放进 RAM 段 |
| `DEBUG_INFO` | 日志等级选择（`1` = 只开 `LOG()`；本项目主要用 `PRINT`） |

> 从机默认**没有** `-DDEBUG`（`CMakeLists.txt` 里从机未定义），主机带了 `-DDEBUG`（PA3/PA2 复用为调试串口）。用 `PRINT` 排查时序问题时记得它本身也耗时。

---

## 8. 全局状态变量速查（跨文件共享）

| 变量 | 所在 | 含义 |
|---|---|---|
| `gTxBuf` | 两端 | 发送缓冲 + 状态（`rfTxBuf_t`） |
| `gRfStatus` | 两端 | 射频状态机（`enum rf_status`） |
| `gBoundStatus` | 两端 | 绑定状态（`enum bound_status`） |
| `gServerData` | 两端 | 绑定识别信息（从机存 Flash，主机仅内存） |
| `gTxDataSeq` / `gDataSeq` | 从机 / 主机 | 包序号，用于 `seq` 匹配与 ACK |
| `gRfRxFlag` | 从机 | 无线→串口有数据待发 |
| `gRxDataStatus` | 主机 | 下行数据接收状态（`enum data_status`） |
| `gBaudRate` / `gSysClock` | 两端 | 当前波特率 / 系统时钟缓存 |
| `gIntervalTimer` | 两端 | 定时器装载值 |
| `gUartRxCount` / `gRfTxCount` | 从机 / 主机 | 收/发字节计数（调试用） |
| `RF_bound_Flag` | 两端 | LED 指示用的绑定标志 |
| `Uart0Para` | 主机 | 电脑端线码 |
