# InduLogger 设计文档

- **日期**: 2026-10-27（2026-10-29 修订：加入证据导向技术定位；2026-10-30 修订：工时重定基线——M1 顺延为 W1–W2，v1 出货点 W4→W5）
- **状态**: 已批准（含 v2 OTA 合并决策 + 三大可量测主张）
- **作者**: Eli Peng
- **性质**: 接案作品集旗舰项目

---

## 1. 背景与定位

本项目是面向国际接案平台（Upwork / Fiverr）的嵌入式作品集旗舰项目。

**市场依据（2026-10 调研）**：
- Upwork 嵌入式顶级接案者时薪 $40–60，主流定位「FreeRTOS or bare-metal on STM32, ESP32」
- 真实职缺实例：*STM32 Firmware Engineer – RTOS and Secure FOTA*（要求处理中断更新、断电、固件损坏、恢复/回滚）；*Fix ESP32 Sensor Device – PCB & Firmware Debugging*；*Custom CANopen to Modbus RTU/TCP Gateway*
- Upwork 官方提示：客户重视「在真实硬件上运行的固件」，而非纯仿真

**定位思路**：个人开发者做不出整体上超越公司的产品，也不需要——本项目的目的是**接案**，作品集的任务是**消除客户恐惧**。因此以「可验证的工程证据」替代「产品卖点」：口号无效，数据有效。拥有示波器/逻辑分析仪并能拿出实测证据的接案者是少数，这是护城河。

**一句话定位**：产品级 STM32 + FreeRTOS 工业数据采集平台，双 MCU 架构（ESP32 通讯协处理器），v1 交付完整 IoT 功能，v2 加入安全 FOTA（含断电恢复与自动回滚），平台分层设计可延伸至仪控、医疗场景——**核心卖点是三大可量测工程主张（见第 3 节）**。

## 2. 目标与成功标准

| 维度 | 标准 |
|---|---|
| 技术展示 | FreeRTOS 多任务架构、外设驱动（I2C/SPI/UART+DMA）、Modbus RTU、自订双 MCU 协定、FATFS、Bootloader/FOTA+回滚 |
| 证据展示 | 三大主张全部有实测量化证据（示波器截图、延迟直方图、断电实测视频、浸泡测试报告、CI 绿灯） |
| 作品集形态 | 英文 README（架构图+接线图+quickstart+「Measured, not claimed」数据表）、协议规格书、平台扩展指南、90 秒演示视频、断电实测视频 |
| 求职效应 | 技能清单与真实职缺要求高度重合：STM32 · FreeRTOS · Bootloader · FOTA · Modbus · MQTT · BLE |
| 时间假设 | 每周 10–15 小时，总计约 8 周（W5 出 v1 即可开始投标；2026-10-30 按 M1 任务级工时估算重定基线） |

## 3. 技术定位：三大可量测主张

| 客户恐惧 | 对应主张 | 证据形态 |
|---|---|---|
| 「装置跑一跑就挂？」 | 主张 2：可靠性 | 断电酷刑视频 + 故障测试报告 |
| 「系统慢、卡？」 | 主张 1：即时性 | 示波器截图 + 延迟直方图 |
| 「韧体是黑箱，出问题怎么办？」 | 主张 3：可观测性 | 诊断暂存器文档 + CI 绿灯 |
| 「代码是意大利面？」 | 主张 3（工程流程） | 单元测试 + CI + 分层架构 |

### 主张 1：即时性——「最坏情况也不抖」
- **量测手段**：Cortex-M DWT cycle counter（硬件级精确量测，零成本）；预留 GPIO benchmark 脚供示波器捕捉
- **量测项**：中断→响应延迟、任务唤醒 jitter、Modbus 请求→响应延迟、感测→上云 端到端延迟
- **方法论**：在压测模式（所有任务满载、队列打满）下取**最坏值 / p99**；禁止只报平均值
- **证据**：示波器截图、延迟直方图、README「Measured, not claimed」数据表

### 主张 2：可靠性——「怎么杀都杀不死」
- **断电酷刑测试**：运行中 / OTA 中随机断电，展示自动恢复；RCC reset-cause 旗标区分看门狗/断电/软件重置并记录（视频一放，观众立刻看懂）
- **I2C bus lockup 自动恢复**：toggle SCL 直至 SDA 释放，加恢复计数器
- **7 天浸泡测试**：错误计数、stack watermark、heap 碎片全程上报，验证零劣化
- **证据**：断电实测视频（作品集最强记忆点）、故障矩阵测试报告

### 主张 3：可观测性——「装置透明如仪表板」
- **诊断模块（一级公民，非附属）**：每任务 stack watermark、heap free/最大块、各外设错误计数、uptime、reset-cause、DWT 延迟统计
- **暴露通道**：Modbus 诊断暂存器块 + 协议 `DIAG_REPORT` 帧 → ESP32 → MQTT status topic
- **工程流程**：协议解析器写成平台无关纯 C 库 → PC 端单元测试（Unity）→ GitHub Actions CI（每次 push 自动构建双固件 + 跑测试 + cppcheck 静态分析）
- **证据**：CI 截图、测试覆盖说明、诊断暂存器表文档

### 诚实原则
所有数字必须真实、可复现、附测试方法；数据平平也如实公布——「诚实 + 可验证」本身就是差异化，过度包装反而毁掉信任。

## 4. 范围

### v1（W1–W5）——工业数据记录器
- STM32F411 + FreeRTOS：感测取样、SD 卡记录、Modbus RTU slave
- ESP32-S3 协处理器：WiFi + MQTT 上云、BLE 配网
- 自订 UART 帧协定（帧格式与 OTA 指令集在本阶段定案）
- **证据基建（从第一天埋点）**：诊断/统计模块、DWT 埋点 + benchmark GPIO、压测模式、I2C bus 恢复、协议库 PC 单元测试 + CI
- Node-RED 仪表板演示

### v2（W6–W8）——安全 FOTA
- STM32 Bootloader + A/B 双槽 + 断电恢复 + 自动回滚
- ESP32 侧 HTTP 韧体下载 + 串流推送
- Node-RED 一键触发升级演示
- **证据采集**：断电酷刑测试 + 视频、7 天浸泡测试

### 明确不做（记入 repo roadmap）
- OTA 以外的 ESP32 自我更新机制、以太网路
- 第二垂直场景（仪控/医疗）的实作（仅写扩展文档）
- 低功耗（属项目 1：ESP32 低功耗感测节点的卖点）
- 固件安全签名（v2 用 SHA256 完整性验证，签名列入 roadmap）
- 为追求极限性能数字而牺牲代码可读性——量测 + 诚实呈现即可

## 5. 系统架构

```
                 ┌────────────── 云端 ──────────────┐
                 │  MQTT Broker + Node-RED 看板     │
                 └──────────▲───────────┘
                            │ WiFi (MQTT)
┌───────────────────────────┴───────────────────────────┐
│  ESP32-S3（通讯协处理器）                                │
│  • WiFi 连线管理 + MQTT client                          │
│  • BLE：装置配网（写入 WiFi 帐密、读取状态）              │
│  • 协议 slave 端（UART，同款帧格式）                      │
│  • v2: HTTP 固件下载 + 串流推送                          │
└───────────────────────────┴───────────────────────────┘
                            │ UART + 自订帧协议 (CRC16, ACK/重传)
┌───────────────────────────┴───────────────────────────┐
│  STM32F411（主控，FreeRTOS）                            │
│  ┌─ 应用层：工业数据记录器（v1 实作）                    │
│  ├─ 平台服务层：配置存储、日志管理、时间同步、            │
│  │              健康监控/诊断（一级公民）                │
│  ├─ 中间层：Modbus RTU slave、双MCU协议栈、FATFS         │
│  └─ BSP/驱动层：I2C 感测器（含 bus 恢复）、UART+DMA、    │
│                 RS485、SD 卡、DWT/GPIO 埋点             │
└───────────────────────────┬───────────────────────────┘
        │ I2C                │ UART                 │ SPI
   BME280 温湿度          MAX485 (RS485)        MicroSD 记录
   MPU6050 振动           (Modbus RTU slave)
```

**职责划分**：ESP32 = 通讯专家（联网、配网、OTA 下载源）；STM32 = 应用大脑（感测、记录、工业协议、装置逻辑）。

## 6. 硬件

### BOM（约 $55–70）

| 项目 | 约价 | 用途 |
|---|---|---|
| Nucleo-F411RE（内建 ST-Link） | $18 | 主控 |
| ESP32-S3 DevKitC | $10 | 通讯协处理器 |
| BME280 + MPU6050 模组 | $6 | 温湿度 + 振动感测（I2C） |
| MicroSD 模组（SPI 介面） | $4 | 数据记录 |
| MAX485 模组 | $2 | RS485 / Modbus |
| 0.96" I2C OLED（选配） | $4 | 状态显示 |
| USB-RS485 转接器 | $8 | Modbus 演示必备 |
| 面包板、杜邦线 | $8 | — |

### 外设分配（引脚在 W1 用 CubeIDE 内嵌的 CubeMX 定案）

| 外设 | 用途 |
|---|---|
| USART2 | 除错主控台（Nucleo 板载 ST-LINK VCP） |
| USART1 或 USART6 + DMA | ESP32 协处理器链路（IDLE line 中断收帧） |
| USART6 或 USART1 + DMA | RS485 Modbus（GPIO 控制收发方向） |
| I2C1 | BME280（0x76）、MPU6050（0x68）、OLED（0x3C） |
| SPI1 | MicroSD（FATFS） |
| IWDG | 看门狗 |
| DWT + 2×GPIO | 延迟量测埋点 + 示波器 benchmark 脚 |

**电平说明**：两颗 MCU IO 均为 3.3V，无需电平转换；共地。USB 5V 供电。

## 7. 双 MCU 通信协议（作品集核心亮点）

### 帧格式

```
[SOF(2B)][LEN(2B)][SEQ(1B)][CMD(1B)][PAYLOAD(0..N)][CRC16(2B)]
```

- SOF 固定双字节；CRC16 覆盖 LEN 起至 PAYLOAD 结束
- SEQ 去重；ACK/逾时重传（重试上限后进入 FAULT 上报）
- 大端序；PAYLOAD 内多字节数值同样大端

### 指令集（v1 定案全表，v2 实作 OTA 组）

| CMD | 方向 | 说明 |
|---|---|---|
| PING / HEARTBEAT | 双向 | 存活检测，超时判定协处理器离线 |
| DATA_REPORT | STM32→ESP32 | 感测数据上云 |
| DIAG_REPORT | STM32→ESP32 | 诊断数据上云（错误计数、stack watermark、heap、reset-cause 等） |
| MQTT_PUB / MQTT_SUB | 双向 | 云端发布/订阅透传 |
| WIFI_CONFIG_SET/GET | STM32→ESP32 | BLE 配网流程中写入凭据 |
| NET_STATUS | ESP32→STM32 | RSSI、IP、MQTT 连线状态 |
| TIME_SET | STM32→ESP32 | NTP 时间同步下发 RTC |
| OTA_BEGIN(size, ver, sha256) | STM32→ESP32 | v2：请求下载固件 |
| OTA_DATA(seq, chunk, crc) | ESP32→STM32 | v2：固件块串流 |
| OTA_END / OTA_STATUS / OTA_ABORT / OTA_APPLY | 双向 | v2：校验、查询、中止、应用重启 |

**交付物**：独立协议规格书（英文），含状态机图与时序图。

## 8. STM32 软件架构

| 任务 | 职责 |
|---|---|
| `SensorTask` | 周期取样 BME280 + MPU6050，推入 sensorQ |
| `LoggerTask` | sensorQ → SD 卡 FATFS（按日轮替档案） |
| `ModbusTask` | RS485 slave，暂存器表暴露最新感测值 + **诊断暂存器块** |
| `CommTask` | 双 MCU 协议状态机（帧解析、ACK、重传、DIAG_REPORT 周期上报） |
| `AppTask` | 主状态机 INIT→CONFIG→RUN→FAULT；聚合诊断、监控各任务健康 |

- 队列：`sensorQ`、`logQ`、`commTxQ`；共享状态用 mutex
- IWDG 看门狗；错误计数器纳入 Modbus 诊断暂存器
- **诊断/统计模块**（platform 层，一级公民）：stack watermark、heap、外设错误计数、uptime、reset-cause（RCC 旗标）、DWT 延迟统计
- **I2C 驱动内建 bus lockup 恢复**（toggle SCL）+ 恢复计数
- **压测模式**（配置开关）：所有任务满载运行，供最坏情况量测
- 逻辑分层强制：`app/`（垂直场景）｜`platform/`（配置存储、日志管理、时间同步、健康监控/诊断、通讯管理）｜`middleware/`（Modbus、协议栈、FATFS）｜`drivers/`
- **协议解析器为平台无关纯 C 库**（`middleware/coproc_proto/`），可在 PC 编译跑单元测试
- 配置不写死：装置 ID、取样率、MQTT topic、压测开关存于 Flash 配置区

## 9. ESP32 软件架构（ESP-IDF，非 Arduino）

| 模块 | 职责 |
|---|---|
| `proto_slave` | 协议 slave 端（与 STM32 同款帧格式，镜像状态机） |
| `wifi_mgr` | WiFi 连线管理、断线重连 |
| `ble_provision` | BLE GATT 配网（写 WiFi 凭据、读装置状态） |
| `mqtt_client` | 上云数据、诊断 status topic、接收云端指令转协议帧 |
| `ota_downloader` (v2) | HTTP 下载固件至自身 flash 分区、SHA256 校验、串流推送 |

## 10. OTA 设计（v2）

### 记忆体映射（STM32F411RET6，512KB Flash，对齐硬件 sector）

| 区域 | 位址 | 大小 | Sector |
|---|---|---|---|
| Bootloader | 0x08000000 | 16K | S0 |
| OTA Metadata | 0x08004000 | 16K | S1 |
| App Slot A | 0x08008000 | 224K | S2–S5 |
| Staging Slot | 0x08040000 | 256K | S6–S7 |

> Sector 对齐依据 F411 实际布局：S0–S3 各 16K、S4 64K、S5–S7 各 128K。
> 合计 16+16+224+256 = 512K，恰好用满。16K Bootloader 需精简实现（无 printf/libc，目标 <14K）。

Metadata 存版本号、影像 SHA、更新旗标、回滚计数。

### 更新流程

1. Node-RED 发布「升级到 vX.Y.Z」→ MQTT
2. ESP32 从 URL 下载 .bin（GitHub Releases 可作固件源）→ 存自身 flash 分区 → 计算 SHA256
3. `OTA_BEGIN` → `OTA_DATA`（逐块 CRC）→ 写入 Staging → `OTA_END` 整包 SHA256 验证
4. 通过 → Metadata 写入 pending 旗标 → 重启
5. Bootloader 验证 Staging → 启动新影像
6. 新影像完成自我测试后回报 healthy；若 N 次开机内未回报或看门狗重置 → 自动回滚旧版

### 故障矩阵（v2 测试项 = 酷刑测试剧本）

| 故障 | 预期行为 |
|---|---|
| 传输中断电 | Staging 不完整，旧版正常运作 |
| 影像损坏 | SHA256 验证挡下，拒绝写入 pending |
| 新版崩溃 | 回滚计数超限，Bootloader 还原旧版 |
| 重启后断电 | Metadata 旗标设计保证幂等/可恢复 |
| 运行中随机断电（酷刑测试） | reset-cause 记录 + 自动恢复，无人工干预 |

## 11. 模板化与垂直扩展

- v1 只实作 `app/industrial_logger/`，README 附**平台扩展指南**
- 仪控版（设计文件层级）：OLED 显示 + 量测量测 + 简易指令介面，仅替换 app 层
- 医疗版（设计文件层级）：BLE GATT 生命征象节点（如 MAX30102）+ 安全记录，仅替换 app 层
- 扩展指南说明：新增垂直场景所需改动清单 = 新 app 目录 + 配置档案 + （视需要）新驱动

## 12. 云端与演示

- 本机 Mosquitto broker + Node-RED 仪表板（工业界标准工具）
- Topic 设计：`indulogger/<devid>/data`、`.../diag`（诊断）、`.../status`、`.../cmd`（升级指令走 cmd）
- 演示脚本：BLE 配网 → WiFi 上云 → 看板即时数据 → Modbus 轮询（USB-RS485）→ SD 卡内容 →（v2）看板一键升级 + **断电酷刑实测**

## 13. 工具链

**repo Makefile + arm-none-eabi-gcc（apt）构建固件 + STM32CubeIDE（仅 .ioc 代码生成与图形调试）+ STM32CubeProgrammer CLI 烧录 + Unity（PC 端单元测试）+ GitHub Actions**——选择理由：本地与 CI 共用同一 Makefile（`stm32/Makefile`），构建不依赖 IDE；CubeIDE 托管构建在 WSL/UNC 工程路径下会把 `\\wsl.localhost` 绝对路径烤进 makefile 导致 make 失败（2026-11-04 实测弃用），故 CubeIDE 退居代码生成与调试；烧录经 WSL interop 直调 Windows 的 CubeProgrammer CLI（免 usbipd），OpenOCD 备用。覆盖职缺关键词（STM32/STM32CubeIDE/GCC/FreeRTOS）。ESP-IDF、Node-RED、Git/GitHub、GitHub Actions（CI：固件构建 + 协议库单元测试 + cppcheck）、Unity（PC 端单元测试框架）。

## 14. 里程碑

| 週 | 目标 | 出货点 |
|---|---|---|
| W1–W2 | M1 骨架（任务级计划见 `docs/plans/2026-10-29-m1-stm32-skeleton.md`）：CubeIDE 工程 + FreeRTOS 任务骨架 + 感测驱动 + SD 记录 + **诊断/埋点骨架（DWT、benchmark GPIO、stats 模块、reset-cause）** + CI 上线。估算 20–30 h，主要耗时/风险项：CubeIDE 工程生成、无头 CI 构建、SPI-SD+FATFS | — |
| W3 | Modbus RTU slave + 协议栈（STM32 端）+ **协议库 PC 单元测试 + CI 上线**；ESP32 WiFi/MQTT | — |
| W4 | BLE 配网 + 系统整合 + Node-RED 看板 + **诊断端到端（Modbus 诊断暂存器 + diag topic）** + I2C 恢复 | — |
| W5 | 文档打磨 + **压测模式基准量测 + 验证报告 v1** + 协议规格书、平台指南、演示视频（约多花 2–3 天，总时程不变） | ✅ v1 上架作品集，可开始投标 |
| W6 | Bootloader + 记忆体映射 + OTA 协定（STM32 端） | — |
| W7 | ESP32 下载串流 + Node-RED 触发 + 故障矩阵实测 + **断电酷刑测试 + 视频拍摄** + **启动 7 天浸泡测试（背景运行）** | — |
| W8 | OTA 文档 + 测试矩阵 + 断电实测视频 + **浸泡测试报告**（若时程挤压，浸泡测试可延后发布为文件增补） | ✅ 旗舰作品完成 |

> **2026-10-30 重定基线**：M1 任务级计划落地后实测估算为 20–30 h，压不进 W1（10–15 h/周），整体顺延 1 周、v1 出货点 W4→W5。W3–W5 与 M1 存在同样的估算不确定性——每个里程碑开工前按 M1 模式先出任务级计划、重估后再定基线；若周投入超过 15 h，出货点可相应提前。

## 15. 交付物清单

1. GitHub repo：英文 README（架构图、接线图、quickstart、**「Measured, not claimed」数据表**）
2. 双 MCU 协议规格书（英文，含状态机/时序图）
3. 平台扩展指南（仪控/医疗怎么套）
4. v1 演示视频（90 秒）
5. v2：OTA 设计文档 + 故障测试矩阵 + **断电实测视频（酷刑测试）**
6. **验证报告（英文）**：基准量测数据（延迟/jitter 直方图）+ 故障注入结果 + 浸泡测试结果
7. Upwork portfolio 条目 + 个人定位文案（Firmware Engineer | STM32 · ESP32 · FreeRTOS · FOTA | *measured, not claimed*）

## 16. 风险与对策

| 风险 | 对策 |
|---|---|
| 范围蔓延 | 严格按 v1/v2 边界；新想法记入 roadmap 不实作 |
| 硬件相容性（模组品质） | BOM 选主流模组；到货先做单件 smoke test |
| 协议调试耗时 | 先用 USB-TTL + PC 模拟对端（Python 脚本）单元验证，再联机 |
| W5 后求职分心 | v1 即可投标；v2 与投标并行，时间表弹性后移 |
| 公司 email 出现在公开 repo | 推 GitHub 前改用个人 email 的 git identity |
| 量测数字不如预期 | 如实公布 + 说明方法；诚实可验证本身就是卖点，不造假、不挑数据 |
| 浸泡测试占墙钟时间 | W7 特征冻结即启动背景运行；来不及就作为发布后增补文件 |
