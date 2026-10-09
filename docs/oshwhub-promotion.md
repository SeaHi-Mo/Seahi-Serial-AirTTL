<h1 align="center">📡 SeaHi-Serial-AirTTL</h1>

<h3 align="center">基于沁恒 CH570Q 的 2.4G 无线串口调试器</h3>

<p align="center"><b>把一根串口线拆成两半 —— 主机插在电脑上，从机放在设备旁，中间 2.4G 无线，对上位机完全透明</b></p>

<p align="center"><i>🔁 双向透传　·　🔌 免驱虚拟串口　·　📡 线码无线同步　·　🎯 一键下载　·　🔗 严格绑定　·　🔓 一键解绑　·　⚡ 丢包 0.00%</i></p>

<hr>

<!-- ======================= 数据卡片 ======================= -->
<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="32%" align="center" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:18px 8px">
<h1 align="center">🔁</h1>
<h3 align="center">双向透传</h3>
<p align="center">单包 <b>251 字节</b></p>
</td>
<td width="2%"></td>
<td width="32%" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:18px 8px">
<h1 align="center">⚡</h1>
<h3 align="center">≤ 1.5 Mbps</h3>
<p align="center">可用波特率上限</p>
</td>
<td width="2%"></td>
<td width="32%" align="center" bgcolor="#dafbe1" style="border:1px solid #1a7f37;border-radius:10px;padding:18px 8px">
<h1 align="center">✅</h1>
<h3 align="center">0.00%</h3>
<p align="center">轻载 <b>零丢包</b></p>
</td>
</tr>
</table>

<hr>

<!-- ======================= 一、这是什么 ======================= -->
<h2 align="center">📖 一、这是什么</h2>

<p align="center"><b>一句话：一根"剪断"了的串口线。</b></p>

<div align="center">
<pre>
PC  ⇄  主机 RF_UartDongle  ⇄  2.4G  ⇄  从机 RF_Uart  ⇄  被调试设备
            USB 插入电脑          无线          杜邦线接目标板
上行 / 下行同时透传，对 PC 来说就是一个普通串口
</pre>
</div>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="49%" valign="top" bgcolor="#fff1f0" style="border:1px solid #cf222e;border-radius:10px;padding:16px 18px">
<h3 align="center">🔧 传统做法</h3>
拖一根长 USB 转串口线过去：<br>
❌ 线不够长、走线难看<br>
❌ 机柜门关不上<br>
❌ 电机 / 变频器旁边干扰大<br>
❌ 每次换设备都要重新拉线
</td>
<td width="2%"></td>
<td width="49%" valign="top" bgcolor="#dafbe1" style="border:1px solid #1a7f37;border-radius:10px;padding:16px 18px">
<h3 align="center">✨ 本项目</h3>
把线"一分为二"，中间走 2.4G：<br>
✅ <b>主机</b>插电脑 USB，配对成功后就是一个虚拟串口<br>
✅ <b>从机</b>放设备旁，杜邦线接目标板 TTL 串口<br>
✅ 对串口助手来说<b>就是一根普通串口线</b>
</td>
</tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="5" bgcolor="#0969da">&nbsp;</td>
<td bgcolor="#ddf4ff" style="padding:12px 16px">
🎯 <b>它解决的是很具体的痛点</b>：设备在机柜里 / 另一张桌子上 / 另一层楼，而你要坐在电脑前调它的串口。
</td>
</tr>
</table>

<hr>

<!-- ======================= 二、基础功能 ======================= -->
<h2 align="center">🧩 二、基础功能</h2>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>🔁 双向透传</b><br>
上行与下行<b>同时</b>工作，单包最大 <b>251 字节</b>；对上层软件完全透明。
</td>
<td width="2%"></td>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>🔌 免驱虚拟串口</b><br>
默认枚举为 <b>CH341 兼容设备</b>（VID <code>0x1A86</code> / PID <code>0x7523</code>）：Linux 内核自带 <code>ch341</code> 驱动，插上即出 <code>/dev/ttyUSB*</code>；Windows 装 <code>CH341SER</code> 即可。也可一键切换为标准 <b>CDC-ACM</b>。
</td>
</tr>
<tr><td colspan="3" height="10"></td></tr>
<tr>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>📡 串口线码无线同步</b><br>
在电脑端串口工具里改<b>波特率 / 数据位 / 停止位 / 校验位</b>，会通过无线<b>实时下发到从机</b>，从机 UART 自动跟随 —— <b>不需要重新烧写固件</b>。<br>
<b>实测可用范围 9600 bps ~ 1.5 Mbps</b>（逐档做过<b>内容校验</b>，超过 1.5 Mbps 的请求会被固件夹到 1.5 Mbps）；<br>
⚠️ <b>2 Mbps 不可用</b>：从机在 24 MHz 下分频只能给出 1.5 Mbps，请求 2 Mbps 会码率不匹配（实测收到乱码）；<br>
⚠️ <b>4 Mbps 不可用</b>：端口打不开（工具/驱动层就拒绝了）。
</td>
<td width="2%"></td>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>📶 从机自适应主频</b><br>
波特率落在 <b>400 kbps ~ 1 Mbps</b> 时，从机自动把系统时钟切到 <b>100 MHz</b>，否则用 24 MHz —— 兼顾高速与功耗。<br>🚧 <b>波特率上限 1.5 Mbps</b>：从机串口是<b>整数分频</b>（<code>DL = round(Fsys/8/baud)</code>），格点离散 —— 请求 2 Mbps 只能生成 1.5 Mbps（差 25%）→ 与目标设备必然对不上，所以固件把上限直接夹在 <b>1.5 Mbps</b>。
</td>
</tr>
<tr><td colspan="3" height="10"></td></tr>
<tr>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>🎯 ST 一键下载</b><br>
电脑端发单字节 <code>0x7F</code>，从机自动拉 <code>BOOT</code> / <code>RESET</code> 时序把目标 MCU 拽进 Bootloader —— 配合 Flash Loader / STM32CubeProgrammer 的 UART 模式即可 <b>免按 BOOT 键下载</b>。
</td>
<td width="2%"></td>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>💡 LED 状态指示</b><br>
未连接<b>快闪</b>（200 ms 周期）/ 连接成功<b>熄灭</b> / 收发数据亮 80 ms；解绑、固件擦写失败各有独立闪烁模式。
</td>
</tr>
<tr><td colspan="3" height="10"></td></tr>
<tr>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>🔗 上电即用</b><br>
首次配对后把绑定信息写进 Flash，<b>掉电不丢</b>；之后两边上电自动回连，不需要任何操作。
</td>
<td width="2%"></td>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>🧰 开箱可改</b><br>
CMake 工程 + 随仓库提供的定制工具链，一条命令编译；<code>skills/</code> 里有字节级协议文档与 API 说明。
</td>
</tr>
</table>

<hr>

<!-- ======================= 三、特色功能点 ======================= -->
<h2 align="center">⭐ 三、特色功能点</h2>

<h3 align="center">1️⃣ 严格绑定 + 一键解绑（换主机不用刷固件）</h3>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="5" bgcolor="#8250df">&nbsp;</td>
<td bgcolor="#faf5ff" style="border:1px solid #8250df;padding:14px 16px">
<b>🔒 严格绑定</b><br>
· 首次配对要<b>"贴近"</b>（RSSI &gt; −58 dBm，实测校准过的门槛）：用"物理靠近"人工指定连哪一台<br>
· <b>两端各自把绑定信息写进 Flash</b>：主机记得绑的是哪台从机、从机记得绑的是哪台主机，只有 <code>serverData</code> <b>完全一致</b>才互认 —— 杜绝"任何一台新主机都能接走已绑定的从机"
</td>
</tr>
<tr><td colspan="2" height="8"></td></tr>
<tr>
<td width="5" bgcolor="#1a7f37">&nbsp;</td>
<td bgcolor="#dafbe1" style="border:1px solid #1a7f37;padding:14px 16px">
<b>🔓 一键解绑</b><br>
从机<b>上电后 15 秒内断电、连续 5 次</b>即解绑（LED <b>常亮 2 秒</b>提示），再上电一次就能配新主机；<br>
每次上电还会<b>短闪 N 次</b>告诉你是第几次 —— 操作全程有反馈。
</td>
</tr>
<tr><td colspan="2" height="8"></td></tr>
<tr>
<td width="5" bgcolor="#9a6700">&nbsp;</td>
<td bgcolor="#fff8c5" style="border:1px solid #9a6700;padding:14px 16px">
<b>🕳️ 这一条是踩过坑才做对的</b><br>
早先"绑定成功就清零计数"，而换主机时旧主机往往还插在电脑上、从机每次上电都先回连成功 → 计数永远攒不够，表现为"<b>怎么重启都解不了绑</b>"。<br>
现在计数<b>只认运行时长</b>，并且解绑那一轮会拒绝配对，避免被旁边的旧主机立刻绑回去。
</td>
</tr>
</table>

<h3 align="center">2️⃣ 电脑端的 DTR / RTS 无线直控从机引脚</h3>

<table align="center" width="100%" border="1" cellpadding="10" cellspacing="0">
<tr bgcolor="#f6f8fa">
<th align="center" width="34%">电脑端</th><th align="center" width="33%">从机输出</th><th align="center" width="33%">常接的目标脚</th>
</tr>
<tr>
<td align="center">断言 <b>DTR</b></td><td align="center"><b>PA3 输出低</b></td><td align="center">目标 <b>BOOT0</b></td>
</tr>
<tr>
<td align="center">断言 <b>RTS</b></td><td align="center"><b>PA2 输出低</b></td><td align="center">目标 <b>RESET</b>（低有效）</td>
</tr>
<tr>
<td align="center">未断言 / 未配对 / 没开串口</td><td align="center">两脚输出<b>高</b></td><td align="center">目标正常运行</td>
</tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="5" bgcolor="#0969da">&nbsp;</td>
<td bgcolor="#ddf4ff" style="padding:12px 16px">
电脑端串口工具的 <b>DTR / RTS</b> 会经无线下发、直接驱动从机引脚 —— 配合上位机或脚本即可 <b>远程复位目标、远程进 Bootloader</b>；与 <code>0x7F</code> 一键下载 <b>共存</b>（收到 <code>0x7F</code> 时由下载时序临时接管这两个脚）。<b>厂商模式与 CDC 模式都支持。</b>
</td>
</tr>
</table>

<h3 align="center">3️⃣ 下行丢包重发：堵上"丢一包 = 丢一帧"</h3>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="49%" valign="top" bgcolor="#fff1f0" style="border:1px solid #cf222e;border-radius:10px;padding:16px">
<h3 align="center">❌ 修复前</h3>
下行数据是"主机应答从机轮询"<b>捎带</b>发出的。承载数据的那一包若在空中丢了，从机会用同一个序号重发轮询，而原实现只回一个 <b>空 ACK</b> —— 那 100 字节（主机已经从 USB 取走了）就<b>永久消失</b>。<br><br>
更糟的是：主机 CRC 回调不打印、链路也没断，<b>两边都看不见</b>。
</td>
<td width="2%"></td>
<td width="49%" valign="top" bgcolor="#dafbe1" style="border:1px solid #1a7f37;border-radius:10px;padding:16px">
<h3 align="center">✅ v0.3.1 起</h3>
主机把最近一次带数据的应答<b>缓存一份</b>，遇到从机重发同一个序号就<b>原样重发</b>，让从机自己的重试（最多 40 次）真能把数据拿回来。<br><br>
实测丢包率 <b>1.08% → 0.00%</b>（见第五节）
</td>
</tr>
</table>

<h3 align="center">4️⃣ 晶振频偏标定</h3>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="49%" align="center" valign="middle" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:16px">
<h4 align="center">标定前</h4>
<h2 align="center">−19.9 ppm</h2>
<p align="center"><code>HSECap_18p</code></p>
</td>
<td width="2%" align="center" valign="middle"><h1>➜</h1></td>
<td width="49%" valign="middle" bgcolor="#dafbe1" style="border:1px solid #1a7f37;border-radius:10px;padding:16px">
<h4 align="center">标定后</h4>
<h2 align="center">−2.06 ppm</h2>
<p align="center"><code>HSECap_6p</code> + 外部 4.7 pF</p>
</td>
</tr>
</table>

<p>实测频率 <b>31.999364 MHz → 31.999934 MHz</b>，两端固件同步修改 —— 频偏直接决定 2.4G 载波精度与链路裕量。</p>

<h3 align="center">5️⃣ 频点主动避让 WiFi</h3>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="49%" valign="top" bgcolor="#fff1f0" style="border:1px solid #cf222e;border-radius:10px;padding:16px">
<h4 align="center">❌ 旧：随机低 6 位</h4>
<p align="center">会随机撞上 WiFi 的 ch1 / ch6 / ch11</p>
</td>
<td width="2%"></td>
<td width="49%" valign="top" bgcolor="#dafbe1" style="border:1px solid #1a7f37;border-radius:10px;padding:16px">
<h4 align="center">✅ 新：候选信道表</h4>
<p><b>{74, 76, 78} = 2474 / 2476 / 2478 MHz</b>（WiFi 之上）<br>配合 <b>+7 dBm</b> 档（芯片最高档，实测传导 <b>+6.48 dBm</b>）</p>
</td>
</tr>
</table>

<h3 align="center">6️⃣ 工程完整度（不是"能跑就行"的 demo）</h3>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="32%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px">
<h3 align="center">🧱 3</h3>
<b>个固件工程</b><br>
从机 <code>RF_Uart</code><br>主机 <code>RF_UartDongle</code><br>测试 <code>RF_TEST</code>
</td>
<td width="2%"></td>
<td width="32%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px">
<h3 align="center">📚 8</h3>
<b>篇开发文档</b><br>
协议字节级详解<br>外设 / 协议栈 API<br>烧写调试 · WSL 踩坑
</td>
<td width="2%"></td>
<td width="32%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px">
<h3 align="center">🤖 5</h3>
<b>个固件自动发布</b><br>
打 tag 即编译<br>Release 直接下载<br>6 个版本迭代
</td>
</tr>
</table>

<hr>

<!-- ======================= 四、RF 性能 ======================= -->
<h2 align="center">📊 四、RF 性能实测</h2>

<p>完整报告：<a href="docs/rf-test-report.md"><code>docs/rf-test-report.md</code></a>（<b>以频谱仪截图为原始记录</b>）</p>

<p><i>测试条件：CH570Q + <code>RF_TEST</code> 定频固件（上电即发射）｜+7 dBm 档｜外部 32 MHz 晶振<br>仪器：Keysight 频谱仪（频率用耦合、功率用焊接同轴传导）</i></p>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="32%" bgcolor="#dafbe1" style="border:1px solid #1a7f37;border-radius:10px;padding:16px 8px">
<h3 align="center">−2.06 ppm</h3>
<p align="center">晶振频偏（基准）<br>31.999934 MHz</p>
</td>
<td width="2%"></td>
<td width="32%" bgcolor="#ddf4ff" style="border:1px solid #0969da;border-radius:10px;padding:16px 8px">
<h3 align="center">+6.48 dBm</h3>
<p align="center">发射功率（2474 MHz）<br>三点最大差 <b>0.09 dB</b></p>
</td>
<td width="2%"></td>
<td width="32%" bgcolor="#dafbe1" style="border:1px solid #1a7f37;border-radius:10px;padding:16px 8px">
<h3 align="center">−5.7 ppm</h3>
<p align="center">最差载波偏差<br>远优于 ±10 ppm 目标</p>
</td>
</tr>
</table>

<h3 align="center">4.1 晶振与载波频率</h3>

<table align="center" width="100%" border="1" cellpadding="10" cellspacing="0">
<tr bgcolor="#f6f8fa">
<th align="center">项目</th><th align="center">标称</th><th align="center">实测</th><th align="center">偏差</th>
</tr>
<tr><td align="center">32 MHz 晶振（基准）</td><td align="center">32.000000 MHz</td><td align="center"><b>31.999934 MHz</b></td><td align="center"><b>−2.06 ppm</b></td></tr>
<tr><td align="center">信道 74</td><td align="center">2474 MHz</td><td align="center"><b>2473.986 MHz</b></td><td align="center">−14 kHz（<b>−5.7 ppm</b>）</td></tr>
<tr><td align="center">信道 76</td><td align="center">2476 MHz</td><td align="center"><b>2475.992 MHz</b></td><td align="center">−8 kHz（<b>−3.2 ppm</b>）</td></tr>
<tr><td align="center">信道 78</td><td align="center">2478 MHz</td><td align="center"><b>2477.990 MHz</b></td><td align="center">−10 kHz（<b>−4.0 ppm</b>）</td></tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr><td bgcolor="#dafbe1" style="border:1px solid #1a7f37;padding:12px 16px">
✅ 三点全部满足 <b>±10 ppm</b> 目标，最大绝对偏差 14 kHz，<b>仅占 2 MHz 信道宽度的 0.7%</b><br>
✅ 主机与从机同板同固件、频偏<b>同向</b> → <b>相对频差接近 0</b><br>
✅ 2478 MHz 离 2.4G 上限（2483.5 MHz）只剩 5.5 MHz，<b>频率合成无异常</b>
</td></tr>
</table>

<h3 align="center">4.2 发射功率（焊接传导，峰值功率）</h3>

<table align="center" width="100%" border="1" cellpadding="10" cellspacing="0">
<tr bgcolor="#f6f8fa">
<th align="center">频点</th><th align="center">峰值功率</th><th align="center">相对 2474 MHz</th>
</tr>
<tr><td align="center">2474 MHz</td><td align="center"><b>+6.48 dBm</b></td><td align="center">基准</td></tr>
<tr><td align="center">2476 MHz</td><td align="center"><b>+6.44 dBm</b></td><td align="center">−0.04 dB</td></tr>
<tr><td align="center">2478 MHz</td><td align="center"><b>+6.39 dBm</b></td><td align="center"><b>−0.09 dB</b></td></tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr><td bgcolor="#dafbe1" style="border:1px solid #1a7f37;padding:12px 16px">
✅ 三点<b>最大差异仅 0.09 dB</b>，一致性极好<br>
✅ <b>边缘频点 2478 MHz 几乎无滚降</b>（仅 −0.09 dB），匹配与滤波在整段应用频段内表现一致<br>
✅ 绝对值 +6.4 dBm 量级与 <code>+7 dBm</code> 档吻合（差值来自电缆损耗与档位公差）
</td></tr>
</table>

<h3 align="center">4.3 结论</h3>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="5" bgcolor="#1a7f37">&nbsp;</td>
<td bgcolor="#dafbe1" style="padding:14px 16px">
<p><b>应用频段 2474 / 2476 / 2478 MHz 发射性能正常，三个候选频点都可以使用，<code>CH_HOP_TBL</code> 无需调整。</b></p>
</td>
</tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="5" bgcolor="#9a6700">&nbsp;</td>
<td bgcolor="#fff8c5" style="padding:12px 16px">
⚠️ 报告里当时<b>只测了发射</b>（定频固件只有发射），所以留了一条"接收 / 丢包率未测"。<br>
<b>这条缺口在下面第五节补上了 —— 而且正是这次补测，抓出了那个 1% 的协议级丢包。</b>
</td>
</tr>
</table>

<h3 align="center">4.4 仪器截图（原始记录）</h3>

<p align="center"><b>晶振 32 MHz 频偏（基准 −2.06 ppm）</b></p>

<p align="center"><img alt="晶振 32MHz 频偏" src="docs/images/xtal-32MHz-frequency-deviation.png" width="80%"></p>

<p align="center"><b>三个频点的载波频率与发射功率</b></p>

<table align="center" width="100%" border="1" cellpadding="6" cellspacing="0">
<tr bgcolor="#f6f8fa">
<td align="center" width="33%"><b>2474 MHz</b></td>
<td align="center" width="33%"><b>2476 MHz</b></td>
<td align="center" width="33%"><b>2478 MHz</b></td>
</tr>
<tr>
<td align="center"><img alt="2474 频率" src="docs/images/2474MHz-frequency-offset.png" width="100%"></td>
<td align="center"><img alt="2476 频率" src="docs/images/2476MHz-frequency-offset.png" width="100%"></td>
<td align="center"><img alt="2478 频率" src="docs/images/2478MHz-frequency-offset.png" width="100%"></td>
</tr>
<tr>
<td align="center"><img alt="2474 功率" src="docs/images/2474MHz-transmission-power.png" width="100%"></td>
<td align="center"><img alt="2476 功率" src="docs/images/2476MHz-transmission-power.png" width="100%"></td>
<td align="center"><img alt="2478 功率" src="docs/images/2478MHz-transmission-power.png" width="100%"></td>
</tr>
</table>

<p><i>截图在仓库 <code>docs/images/</code> 下。发布到立创开源广场时请把这几张图上传到平台，再把上面的图片链接替换成平台地址。</i></p>

<hr>

<!-- ======================= 五、丢包测试 ======================= -->
<h2 align="center">🧪 五、丢包测试</h2>

<h3 align="center">5.1 怎么测的（可复现）</h3>

<div align="center">
<pre>
COM9(主机数据口) ──▶ 主机 ──2.4G──▶ 从机 ──▶ COM10(从机 UART)
</pre>
</div>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>🧾 帧格式</b><br>
每帧<b>线上恰好 100 字节</b>（99 字节 ASCII 内容 + 行尾 <code>LF</code>）<br>
形如 <code>@NNN…填充…#NNN</code>，<b>首尾都带序号</b> → 能区分"丢帧"与"截断"
</td>
<td width="2%"></td>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>⚖️ 判据</b><br>
以从机侧 COM10 的<b>日志条目字节数</b>折算收到帧数，并用分栏累计字节数交叉校验（两口径全程一致）<br>
脚本：<code>tools/loss-test/baud100_test.py</code>
</td>
</tr>
</table>

<h3 align="center">5.2 主结果：1.5 Mbps、每帧 100 字节、1.1 s 一帧（3 轮 × 300 帧）</h3>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="49%" valign="middle" bgcolor="#fff1f0" style="border:1px solid #cf222e;border-radius:10px;padding:18px">
<h4 align="center">修复前 v0.3.0</h4>
<h1 align="center">1.08%</h1>
<p align="center">890 / 900 帧<br>10 帧丢失</p>
</td>
<td width="2%" align="center" valign="middle"><h1>➜</h1></td>
<td width="49%" valign="middle" bgcolor="#dafbe1" style="border:1px solid #1a7f37;border-radius:10px;padding:18px">
<h4 align="center">修复后 v0.3.1</h4>
<h1 align="center">0.00%</h1>
<p align="center"><b>900 / 900 帧</b><br>零缺失序号</p>
</td>
</tr>
</table>

<table align="center" width="100%" border="1" cellpadding="10" cellspacing="0">
<tr bgcolor="#f6f8fa">
<th align="center">版本</th><th align="center">第 1 轮</th><th align="center">第 2 轮</th><th align="center">第 3 轮</th><th align="center">合计</th>
</tr>
<tr>
<td align="center">修复前 <b>v0.3.0</b></td>
<td align="center">296 / 300</td><td align="center">296 / 300</td><td align="center">298 / 300</td>
<td align="center"><b>890 / 900 = 1.08%</b> ❌</td>
</tr>
<tr bgcolor="#dafbe1">
<td align="center"><b>修复后 v0.3.1</b></td>
<td align="center"><b>300 / 300</b></td><td align="center"><b>300 / 300</b></td><td align="center"><b>300 / 300</b></td>
<td align="center"><b>900 / 900 = 0.00%</b> ✅</td>
</tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="5" bgcolor="#0969da">&nbsp;</td>
<td bgcolor="#ddf4ff" style="padding:12px 16px">
· 修复前丢失的 10 帧序号<b>在时间上很分散</b>（不是开头突发），且每轮都有 → 是持续存在的偶发丢帧<br>
· 修复后三轮<b>零丢帧、零缺失序号</b>，帧到达间隔中位 1100 ms（与发送节奏一致）<br>
· 补上主机 CRC 打印后，同一 17 分钟窗口记录到 <b>338 次 <code>crc err</code></b>（≈20 次/分钟）—— 说明空中确实有 <b>0.3%（小包）～1%（100 字节数据包）的单包错误率</b>，但都已被"射频层重传 + 本次的载荷重发"兜住，<b>上层看到的是 0 丢包</b>
</td>
</tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="5" bgcolor="#1a7f37">&nbsp;</td>
<td bgcolor="#dafbe1" style="padding:14px 16px">
<p>💡 <b>结论：这 1% 不是射频质量问题，是协议层"丢了包不重传"。</b><br>修掉之后，在 1.5 Mbps 轻载下本链路达到 <b>零丢包</b>。</p>
</td>
</tr>
</table>

<h3 align="center">5.3 边界与已知限制（写清楚才算完整）</h3>

<table align="center" width="100%" border="1" cellpadding="10" cellspacing="0">
<tr bgcolor="#f6f8fa">
<th align="center" width="30%">负载</th><th align="center" width="22%">丢包率</th><th align="center">说明</th>
</tr>
<tr bgcolor="#dafbe1">
<td align="center">轻载逐帧<br>（100 B / 帧，≥1.1 s 间隔）</td>
<td align="center"><b>0.00%</b> ✅</td>
<td>上表：1.5 Mbps 下 3 轮 900 帧全到</td>
</tr>
<tr>
<td>突发负载<br>（每组 100 帧 = 10 KB 连发，平均 ≈6.7 KB/s）</td>
<td>115200 ~ 921600 bps：<b>0.5% ~ 1.5%</b><br>1.5 Mbps：<b>4.3%</b></td>
<td>突发会撞主机 <b>512 B RF 缓冲</b> + USB 写背压 → 建议上位机分帧发送</td>
</tr>
<tr bgcolor="#fff8c5">
<td align="center">同上，9600 ~ 57600 bps</td>
<td align="center">40% ~ 80%</td>
<td><b>不是无线丢包</b>：串口本身只有 960 B/s ~ 5.76 KB/s 的吞吐，灌 6.7 KB/s 必然积压溢出。<br>低波特率下请按"下行 ≤50 B、上行 ≤15 B 每轮"的实测安全线分帧</td>
</tr>
</table>

<h3 align="center">5.4 实际吞吐与可用波特率（实测）</h3>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="32%" bgcolor="#ddf4ff" style="border:1px solid #0969da;border-radius:10px;padding:16px 8px">
<h3 align="center">27.6 KB/s</h3>
<p align="center">单批 58 KB 完整到达<br>（耗时 2.10 s，零丢包）</p>
</td>
<td width="2%"></td>
<td width="32%" bgcolor="#ddf4ff" style="border:1px solid #0969da;border-radius:10px;padding:16px 8px">
<h3 align="center">50 KB/s</h3>
<p align="center">连续 10 批共 580 KB<br>（耗时 11.6 s，<b>零丢包</b>）</p>
</td>
<td width="2%"></td>
<td width="32%" bgcolor="#fff8c5" style="border:1px solid #9a6700;border-radius:10px;padding:16px 8px">
<h3 align="center">1.5 Mbps</h3>
<p align="center">官方支持上限<br>（&gt;1.5M 由固件夹到 1.5M）</p>
</td>
</tr>
</table>

<table align="center" width="100%" border="1" cellpadding="10" cellspacing="0">
<tr bgcolor="#f6f8fa">
<th align="center">波特率</th><th align="center">内容校验</th><th align="center">结论</th>
</tr>
<tr><td align="center">9600 ~ 921600</td><td align="center">9 档扫频，帧序号完整</td><td align="center">✓ 可用</td></tr>
<tr bgcolor="#dafbe1"><td align="center">1.5 Mbps</td><td>30/30 帧、2970 字节完整；3 轮 × 300 帧<b>零丢包</b></td><td align="center"><b>✓ 可用</b></td></tr>
<tr bgcolor="#fff1f0"><td align="center">2 Mbps</td><td align="center"><b>0/30 合法帧</b>，收到 5089 字节乱码</td><td align="center"><b>✗ 不可用</b>（码率不匹配）</td></tr>
<tr bgcolor="#fff8c5"><td align="center">3 Mbps</td><td align="center">30/30 帧、字节数精确</td><td>⚠️ 精确但<b>无收益</b>（吞吐受 2.4G 限制仅 27~50 KB/s）→ <b>现已夹到 1.5 Mbps</b></td></tr>
<tr bgcolor="#fff1f0"><td align="center">4 Mbps</td><td align="center">端口打不开</td><td align="center"><b>✗ 不可用</b></td></tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="5" bgcolor="#9a6700">&nbsp;</td>
<td bgcolor="#fff8c5" style="padding:12px 16px">
⚠️ <b>吞吐口径说明（不许夸大）</b>：上面 27.6 ~ 50 KB/s 是<b>测试工具能灌进去的上限</b>带来的结果（MCP 接口 60 次/分，每次最多 64 KB），
所以这两个数是<b>实测下限</b>，<b>不是链路上限</b> —— 链路上限需要旁路测试工具、直接灌数据才能测到，<b>本项目暂未测</b>。<br>
另：1.5 Mbps 与 3 Mbps 两档实测吞吐<b>完全一样</b> → 瓶颈在 <b>2.4G 链路 / 协议轮询</b>，不在串口波特率；<br>这也正是把上限收到 1.5 Mbps 的依据 —— 更高波特率不会更快，只会引入量化误差。
</td>
</tr>
</table>

<h3 align="center">5.5 数据来源与未验证项（诚实清单）</h3>

<table align="center" width="100%" border="1" cellpadding="10" cellspacing="0">
<tr bgcolor="#f6f8fa">
<th align="center" width="20%">类别</th><th align="center">内容</th>
</tr>
<tr bgcolor="#dafbe1">
<td align="center"><b>✅ 实测</b><br><sub>有仪器截图或脚本可复现</sub></td>
<td>
· RF 载波频率三点（−3.2 ~ −5.7 ppm）与发射功率三点（+6.48 / +6.44 / +6.39 dBm）—— 频谱仪截图<br>
· 32 MHz 晶振频偏 −2.06 ppm<br>
· 丢包率：1.5 Mbps 逐帧 3 轮 × 300 帧 <b>1.08% → 0.00%</b>；突发负载 9 档扫频<br>
· 可用波特率 <b>9600 bps ~ 1.5 Mbps</b>（<b>内容校验</b>；2 Mbps 因整数分频只能落成 1.5 Mbps 而不可用，4 Mbps 电脑端端口打不开，>1.5 Mbps 现由固件夹到 1.5 Mbps）<br>
· 吞吐 27.6 ~ 50 KB/s（零丢包，受测试工具限流）<br>
· FLASH / RAM 占用（构建日志）
</td>
</tr>
<tr bgcolor="#ddf4ff">
<td align="center"><b>📄 代码定义</b><br><sub>可在源码核对，未单独做破坏性测试</sub></td>
<td>
· 协议单包上限 <b>251 字节</b>（<code>rf.h: DATA_LEN_MAX_TX</code>，本次测试帧长 100 字节）<br>
· 解绑：上电 15 秒内断电 × 5 次（<code>BOOT_UNBIND_TIMES</code>）<br>
· 首次配对门槛 RSSI &gt; −58 dBm；候选频点 <code>{74,76,78}</code>；DTR/RTS → PA3/PA2 映射<br>
· LED 各状态闪烁模式；UART 分频公式（<code>uart.c</code>）
</td>
</tr>
<tr bgcolor="#fff8c5">
<td align="center"><b>⚠️ 未验证</b><br><sub>本项目<b>不做</b>任何宣称</sub></td>
<td>
· 4 Mbps、2 Mbps 之外的其它非标准波特率（如 2.5 Mbps）<br>
· 链路的<b>吞吐上限</b>、传输距离 / 穿墙能力<br>
· <b>CDC-ACM 模式</b>的实机使用（代码支持 <code>USB_WORK_MODE</code> 切换，未实测）<br>
· 251 字节满包长测、<b>110 bps</b> 级极低速、长时间（小时级）稳定性
</td>
</tr>
</table>

<hr>

<!-- ======================= 六、优势与完整度 ======================= -->
<h2 align="center">🏆 六、项目优势与完整度一览</h2>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>🧬 协议自研可控</b><br>
2.4G 私有协议（非 BLE 连接态），帧格式 / 绑定 / 重传 / 线码同步全部源码可见可改；<code>skills/</code> 里有<b>字节级协议文档</b>。
</td>
<td width="2%"></td>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>📐 不是"能跑就行"</b><br>
有 <code>RF_TEST</code> 定频固件 + <b>射频测试报告（仪器截图）</b> + <b>可复现的丢包测试脚本</b> + 实测数据。
</td>
</tr>
<tr><td colspan="3" height="10"></td></tr>
<tr>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>📝 问题都留了记录</b><br>
频偏标定、丢包根因、测量假象（日志截断）、WSL 烧录踩坑……都写进了提交信息、报告与文档。
</td>
<td width="2%"></td>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>🚀 版本化发布</b><br>
<b>6 个版本 tag</b>（v0.1.0 → v0.3.1），CI 自动编译 <b>5 个固件</b>并发布 Release（预编译 hex 下载即烧）。
</td>
</tr>
<tr><td colspan="3" height="10"></td></tr>
<tr>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>🧑‍💻 可继续开发</b><br>
附 <b>8 篇开发文档</b>：协议、外设库 / 协议栈 API、芯片规格、烧写调试、工具链坑位；换频点 / 加命令都有现成路径。
</td>
<td width="2%"></td>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>📦 资源占用（实测）</b><br>
从机 FLASH <b>12.1 KB (4.9%)</b> / RAM <b>11.7 KB (95.5%)</b><br>
主机 FLASH <b>21.5 KB (8.7%)</b> / RAM <b>10.2 KB (83.3%)</b><br>
—— 12 KB RAM 的小芯片上把透传、绑定、重传、线码同步都塞下了。
</td>
</tr>
</table>

<hr>

<!-- ======================= 七、使用方法 ======================= -->
<h2 align="center">🛠️ 七、使用方法</h2>

<h3 align="center">7.1 接线（从机 → 被调试设备）</h3>

<div align="center">
<pre>
被调试设备 TX   ──▶  从机 PA1 (RXD)
被调试设备 RX   ◀──  从机 PA0 (TXD)
被调试设备 GND  ──── 从机 GND            ← 务必共地

（需要一键下载 / 远程复位时）
被调试设备 RESET ── 从机 PA2
被调试设备 BOOT0 ── 从机 PA3
</pre>
</div>

<p><i>从机默认 <b>115200-8-N-1</b>，上电后会被电脑端设置覆盖。</i></p>

<h3 align="center">7.2 上电与配对</h3>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="40" align="center" valign="middle" bgcolor="#0969da"><b><font color="#ffffff">1</font></b></td>
<td bgcolor="#f6f8fa" style="border:1px solid #d0d7de;padding:12px 16px">从机接好线并供电，主机插到电脑 USB 口 —— <b>此时 PC 上还看不到串口</b>，主机要等配对成功才枚举 USB</td>
</tr>
<tr><td colspan="2" height="8"></td></tr>
<tr>
<td width="40" align="center" valign="middle" bgcolor="#0969da"><b><font color="#ffffff">2</font></b></td>
<td bgcolor="#f6f8fa" style="border:1px solid #d0d7de;padding:12px 16px"><b>首次配对请把主机和从机贴近</b>（RSSI &gt; −58 dBm，几厘米内）</td>
</tr>
<tr><td colspan="2" height="8"></td></tr>
<tr>
<td width="40" align="center" valign="middle" bgcolor="#0969da"><b><font color="#ffffff">3</font></b></td>
<td bgcolor="#f6f8fa" style="border:1px solid #d0d7de;padding:12px 16px">配对成功后<b>两端 LED 熄灭</b>，电脑上出现虚拟串口（Linux <code>ls /dev/ttyUSB*</code>）</td>
</tr>
<tr><td colspan="2" height="8"></td></tr>
<tr>
<td width="40" align="center" valign="middle" bgcolor="#0969da"><b><font color="#ffffff">4</font></b></td>
<td bgcolor="#f6f8fa" style="border:1px solid #d0d7de;padding:12px 16px">之后双方<b>自动回连</b>，绑定信息存在两侧 Flash 里，掉电重启也不用再靠近</td>
</tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="5" bgcolor="#9a6700">&nbsp;</td>
<td bgcolor="#fff8c5" style="padding:12px 16px">
⚠️ 升级固件时建议<b>两颗一起烧</b>：配对 / 绑定判定两侧都改了，混用版本可能因绑定值不匹配而一直 <code>reject</code>（真遇到了，先按 7.3 给从机解绑再重新配对）。
</td>
</tr>
</table>

<h3 align="center">7.3 解绑（更换主机）</h3>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="40" align="center" valign="middle" bgcolor="#8250df"><b><font color="#ffffff">1</font></b></td>
<td bgcolor="#faf5ff" style="border:1px solid #8250df;padding:12px 16px">从机<b>上电后在 15 秒内断电</b>，如此<b>连续 5 次</b></td>
</tr>
<tr><td colspan="2" height="8"></td></tr>
<tr>
<td width="40" align="center" valign="middle" bgcolor="#8250df"><b><font color="#ffffff">2</font></b></td>
<td bgcolor="#faf5ff" style="border:1px solid #8250df;padding:12px 16px">第 5 次上电时执行解绑：<b>LED 常亮 2 秒</b>表示已解绑，随后进入未绑定状态（LED 快闪）</td>
</tr>
<tr><td colspan="2" height="8"></td></tr>
<tr>
<td width="40" align="center" valign="middle" bgcolor="#8250df"><b><font color="#ffffff">3</font></b></td>
<td bgcolor="#faf5ff" style="border:1px solid #8250df;padding:12px 16px">这一次上电从机会<b>拒绝所有配对</b>（避免被旁边还开着的旧主机立刻绑回去），<b>再断电上电一次</b>，靠近新主机即可重新配对</td>
</tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="5" bgcolor="#0969da">&nbsp;</td>
<td bgcolor="#ddf4ff" style="padding:12px 16px">
🔢 <b>判读技巧</b>：每次上电 LED 会<b>短闪 N 次</b>（N = 当前连续重启次数），据此确认计数在累加；只要某次上电后连续运行超过 15 秒（正常使用），计数就会被清零。
</td>
</tr>
</table>

<h3 align="center">7.4 电脑端使用</h3>

<div align="center">
<pre>
# Linux：确认识别出虚拟串口
dmesg | tail
ls -l /dev/ttyUSB*

# 用哪个串口工具都行
minicom -D /dev/ttyUSB0 -b 115200
# 或
screen /dev/ttyUSB0 115200
</pre>
</div>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>🔁 参数自动跟随</b><br>
<b>在串口工具里改波特率 / 数据位 / 停止位 / 校验位，从机会自动跟随</b>（无线下发），无需重烧固件。
</td>
<td width="2%"></td>
<td width="49%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:14px 16px">
<b>🎯 一键下载</b><br>
若上位机是带"一键下载"的烧写工具（Flash Loader、STM32CubeProgrammer 的 UART 模式等），握手字节 <code>0x7F</code> 会触发从机的 <code>RESET</code> / <code>BOOT</code> 时序，<b>省掉手动按 BOOT 键</b>。
</td>
</tr>
</table>

<p>想用 <b>DTR / RTS 远程控制目标</b>：见第三节第 2 条（DTR → PA3 / BOOT0、RTS → PA2 / RESET，断言 = 输出低）。</p>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="5" bgcolor="#9a6700">&nbsp;</td>
<td bgcolor="#fff8c5" style="padding:12px 16px">
⚠️ 多数串口工具<b>一打开端口就会拉 DTR / RTS</b>，接目标 RESET / BOOT0 时请在上位机里显式设置初始电平。
</td>
</tr>
</table>

<h3 align="center">7.5 获取固件 / 自己编译</h3>

<p align="center"><b>① 直接下载预编译固件（推荐）</b></p>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="5" bgcolor="#1a7f37">&nbsp;</td>
<td bgcolor="#dafbe1" style="padding:14px 16px">
<p>
📦 <a href="https://github.com/SeaHi-Mo/Seahi-Serial-AirTTL/releases"><b>Releases</b></a> —— 每个版本都有 5 个 hex：<br>
<code>RF_Uart_vX.Y.Z.hex</code>（<b>从机</b>）　·　<code>RF_UartDongle_vX.Y.Z.hex</code>（<b>主机</b>）<br>
<code>RF_TEST_247{4,6,8}M_ch3{6,7,8}_vX.Y.Z.hex</code>（射频测试固件，测完记得烧回正常固件）
</p>
</td>
</tr>
</table>

<p><b>② 烧写</b>：两颗都是 CH570Q，用 <b>WCH-Link</b>（两线调试口）或 <b>ISP 串口工具（Windows <code>WchIspStudio</code>）</b></p>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="40" align="center" valign="middle" bgcolor="#cf222e"><h3>🚫</h3></td>
<td bgcolor="#fff1f0" style="border:1px solid #cf222e;padding:12px 16px"><b>不要勾「全片擦除」</b> —— 会清掉 Flash 末尾 <code>0x3B000</code> 的绑定信息（解绑计数正依赖它）</td>
</tr>
<tr><td colspan="2" height="8"></td></tr>
<tr>
<td width="40" align="center" valign="middle" bgcolor="#9a6700"><h3>🔌</h3></td>
<td bgcolor="#fff8c5" style="border:1px solid #9a6700;padding:12px 16px">从机固件运行时会关闭两线调试功能（腾出 PA0 / PA1 给串口），下载失败时先给板子<b>断电重上电</b></td>
</tr>
<tr><td colspan="2" height="8"></td></tr>
<tr>
<td width="40" align="center" valign="middle" bgcolor="#0969da"><h3>🔁</h3></td>
<td bgcolor="#ddf4ff" style="border:1px solid #0969da;padding:12px 16px">ISP 接线是「<b>同名相接</b>」：CH570 的 TXD 接 TTL 的 TXD、RXD 接 RXD（<b>仅烧录时</b>如此，正常透传仍是交叉接法）</td>
</tr>
</table>

<p align="center"><b>③ 自己编译</b>（Linux，工具链已作为子模块随仓库提供）</p>

<div align="center">
<pre>
git clone --recurse-submodules \
    https://github.com/SeaHi-Mo/Seahi-Serial-AirTTL.git
cd Seahi-Serial-AirTTL

# 从机
cd RF_Uart
cmake -B build -G "Unix Makefiles"
cmake --build build -j$(nproc)

# 主机
cd ../RF_UartDongle
cmake -B build -G "Unix Makefiles"
cmake --build build -j$(nproc)
</pre>
</div>

<p><i><code>tools/toolchain</code> 是沁恒定制的 <code>riscv-wch-elf-gcc</code> 12.2.0（<b>必须用这套</b>，<code>xw</code> 扩展指令发行版 / xPack 工具链不支持）。构建日志末尾会打印 FLASH / RAM 占用。</i></p>

<hr>

<!-- ======================= 八、常见问题 ======================= -->
<h2 align="center">❓ 八、常见问题</h2>

<table align="center" width="100%" border="1" cellpadding="10" cellspacing="0">
<tr bgcolor="#f6f8fa">
<th align="center" width="32%">现象</th><th align="center">原因 / 处理</th>
</tr>
<tr><td align="center">配对成功前 PC 上看不到串口</td><td><b>有意设计</b>：主机未与从机连接时不枚举 USB，配对成功才出现，断开立即收回</td></tr>
<tr><td align="center">没有 <code>/dev/ttyUSB*</code></td><td>先确认主机与从机已配对；再 <code>dmesg | tail</code> 看 <code>ch341</code> 枚举记录</td></tr>
<tr><td align="center"><code>reject.. rssi=-xx</code></td><td>首次配对距离太远（要贴近）；或从机 Flash 里已有旧绑定，先按 7.3 解绑</td></tr>
<tr><td><code>reject.. local=xxxx remote=xxxx</code></td><td>严格绑定生效：两侧绑定值不一致（常见于固件版本混用）→ 解绑后重配</td></tr>
<tr><td align="center">连续重启 5 次也不解绑</td><td>每次上电必须<b>在 15 秒内断电</b>（跑满 15 秒即清零计数）；LED 短闪次数就是当前计数</td></tr>
<tr><td align="center">一配对目标就被按住</td><td>上位机打开端口时自动拉 DTR / RTS（断言 = 从机 PA2 / PA3 输出低），见 7.4</td></tr>
<tr><td align="center">9600 等低波特率丢数据</td><td>串口吞吐瓶颈（9600 只有 960 B/s），请按分帧 / 限速发送，见 5.3</td></tr>
</table>

<hr>

<!-- ======================= 九、开源与致谢 ======================= -->
<h2 align="center">📄 九、开源与致谢</h2>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
<tr>
<td width="32%" valign="top" bgcolor="#f6f8fa" style="border:1px solid #d0d7de;border-radius:10px;padding:16px">
<h3 align="center">📜 MIT</h3>
<p>许可证<br><sub><code>StdPeriphDriver/</code>、<code>LIB/</code>、<code>RVMSIS/</code>、<code>Startup/</code> 版权归南京沁恒微电子</sub></p>
</td>
<td width="2%"></td>
<td width="32%" valign="top" bgcolor="#ddf4ff" style="border:1px solid #0969da;border-radius:10px;padding:16px">
<h3 align="center">💻 源码</h3>
<p><a href="https://github.com/SeaHi-Mo/Seahi-Serial-AirTTL">github.com/SeaHi-Mo/Seahi-Serial-AirTTL</a><br><sub>含协议文档 · 射频报告 · 丢包测试脚本</sub></p>
</td>
<td width="2%"></td>
<td width="32%" valign="top" bgcolor="#dafbe1" style="border:1px solid #1a7f37;border-radius:10px;padding:16px">
<h3 align="center">📦 固件</h3>
<p><a href="https://github.com/SeaHi-Mo/Seahi-Serial-AirTTL/releases">Releases</a><br><sub>v0.3.1 起 CI 自动构建 5 个 hex</sub></p>
</td>
</tr>
</table>

<p><b>⭐ 如果这个项目对你有帮助，欢迎点个 Star、或者把测试数据 / 改进反馈给我！</b></p>

<p>Made with ❤️ for 立创电赛 · 无线串口调试器</p>
