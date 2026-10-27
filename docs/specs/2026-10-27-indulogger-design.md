# InduLogger 设计文档

- **日期**: 2026-10-27
- **状态**: 已批准（经讨论确认，含 v2 OTA 合并决策）
- **作者**: Eli Peng
- **性质**: 接案作品集旗舰项目

---

## 1. 背景与定位

本项目是面向国际接案平台（Upwork / Fiverr）的嵌入式作品集旗舰项目。

**市场依据（2026-10 调研）**：
- Upwork 嵌入式顶级接案者时薪 $40–60，主流定位「FreeRTOS or bare-metal on STM32, ESP32」
- 真实职缺实例：*STM32 Firmware Engineer – RTOS and Secure FOTA*（要求处理中断更新、断电、固件损坏、恢复/回滚）；*Fix ESP32 Sensor Device – PCB & Firmware Debugging*；*Custom CANopen to Modbus RTU/TCP Gateway*
- Upwork 官方提示：客户重视「在真实硬件上运行的固件」，而非纯仿真

**一句话定位**：产品级 STM32 + FreeRTOS 工业数据采集平台，双 MCU 架构（ESP32 通讯协处理器），v1 交付完整 IoT 功能，v2 加入安全 FOTA（含断电恢复与自动回滚），平台分层设计可延伸至仪控、医疗场景。

## 2. 目标与成功标准

| 维度 | 标准 |
|---|---|
| 技术展示 | FreeRTOS 多任务架构、外设驱动（I2C/SPI/UART+DMA）、Modbus RTU、自订双 MCU 协定、FATFS、Bootloader/FOTA+回滚 |
| 作品集形态 | 英文 README（架构图+接线图+quickstart）、协议规格书、平台扩展指南、90 秒演示视频、断电实测视频 |
| 求职效应 | 技能清单与真实职缺要求高度重合：STM32 · FreeRTOS · Bootloader · FOTA · Modbus · MQTT · BLE |
| 时间假设 | 每周 10–15 小时，总计约 7 周（W4 出 v1 即可开始投标） |

## 3. 范围

### v1（W1–W4）——工业数据记录器
- STM32F411 + FreeRTOS：感测取样、SD 卡记录、Modbus RTU slave
- ESP32-S3 协处理器：WiFi + MQTT 上云、BLE 配网
- 自订 UART 帧协定（帧格式与 OTA 指令集在本阶段定案）
- Node-RED 仪表板演示

### v2（W5–W7）——安全 FOTA
- STM32 Bootloader + A/B 双槽 + 断电恢复 + 自动回滚
- ESP32 侧 HTTP 韧体下载 + 串流推送
- Node-RED 一键触发升级演示

### 明确不做（记入 repo roadmap）
- OTA 以外的 ESP32 自我更新机制、以太网路
- 第二垂直场景（仪控/医疗）的实作（仅写扩展文档）
- 低功耗（属项目 1：ESP32 低功耗感测节点的卖点）
- 固件安全签名（v2 用 SHA256 完整性验证，签名列入 roadmap）

## 4. 系统架构

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
└───────────────────────────┬───────────────────────────┘
                            │ UART + 自订帧协议 (CRC16, ACK/重传)
┌───────────────────────────┴───────────────────────────┐
│  STM32F411（主控，FreeRTOS）                            │
│  ┌─ 应用层：工业数据记录器（v1 实作）                    │
│  ├─ 平台服务层：配置存储、日志管理、时间同步、健康监控     │
│  ├─ 中间层：Modbus RTU slave、双MCU协议栈、FATFS         │
│  └─ BSP/驱动层：I2C 感测器、UART+DMA、RS485、SD 卡       │
└───────────────────────────┬───────────────────────────┘
        │ I2C                │ UART                 │ SPI
   BME280 温湿度          MAX485 (RS485)        MicroSD 记录
   MPU6050 振动           (Modbus RTU slave)
```

**职责划分**：ESP32 = 通讯专家（联网、配网、OTA 下载源）；STM32 = 应用大脑（感测、记录、工业协议、装置逻辑）。

## 5. 硬件

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

### 外设分配（引脚在 W1 用 CubeMX 定案）

| 外设 | 用途 |
|---|---|
| USART2 | 除错主控台（Nucleo 板载 ST-LINK VCP） |
| USART1 或 USART6 + DMA | ESP32 协处理器链路（IDLE line 中断收帧） |
| USART6 或 USART1 + DMA | RS485 Modbus（GPIO 控制收发方向） |
| I2C1 | BME280（0x76）、MPU6050（0x68）、OLED（0x3C） |
| SPI1 | MicroSD（FATFS） |
| IWDG | 看门狗 |

**电平说明**：两颗 MCU IO 均为 3.3V，无需电平转换；共地。USB 5V 供电。

## 6. 双 MCU 通信协议（作品集核心亮点）

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
| MQTT_PUB / MQTT_SUB | 双向 | 云端发布/订阅透传 |
| WIFI_CONFIG_SET/GET | STM32→ESP32 | BLE 配网流程中写入凭据 |
| NET_STATUS | ESP32→STM32 | RSSI、IP、MQTT 连线状态 |
| TIME_SET | STM32→ESP32 | NTP 时间同步下发 RTC |
| OTA_BEGIN(size, ver, sha256) | STM32→ESP32 | v2：请求下载固件 |
| OTA_DATA(seq, chunk, crc) | ESP32→STM32 | v2：固件块串流 |
| OTA_END / OTA_STATUS / OTA_ABORT / OTA_APPLY | 双向 | v2：校验、查询、中止、应用重启 |

**交付物**：独立协议规格书（英文），含状态机图与时序图。

## 7. STM32 软件架构

| 任务 | 职责 |
|---|---|
| `SensorTask` | 周期取样 BME280 + MPU6050，推入 sensorQ |
| `LoggerTask` | sensorQ → SD 卡 FATFS（按日轮替档案） |
| `ModbusTask` | RS485 slave，暂存器表暴露最新感测值+诊断计数 |
| `CommTask` | 双 MCU 协议状态机（帧解析、ACK、重传） |
| `AppTask` | 主状态机 INIT→CONFIG→RUN→FAULT；监控各任务健康 |

- 队列：`sensorQ`、`logQ`、`commTxQ`；共享状态用 mutex
- IWDG 看门狗；错误计数器纳入 Modbus 诊断暂存器
- 逻辑分层强制：`app/`（垂直场景）｜`platform/`（配置存储、日志管理、时间同步、健康监控、通讯管理）｜`middleware/`（Modbus、协议栈、FATFS）｜`drivers/`
- 配置不写死：装置 ID、取样率、MQTT topic 存于 Flash 配置区

## 8. ESP32 软件架构（ESP-IDF，非 Arduino）

| 模块 | 职责 |
|---|---|
| `proto_slave` | 协议 slave 端（与 STM32 同款帧格式，镜像状态机） |
| `wifi_mgr` | WiFi 连线管理、断线重连 |
| `ble_provision` | BLE GATT 配网（写 WiFi 凭据、读装置状态） |
| `mqtt_client` | 上云数据、接收云端指令转协议帧 |
| `ota_downloader` (v2) | HTTP 下载固件至自身 flash 分区、SHA256 校验、串流推送 |

## 9. OTA 设计（v2）

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

### 故障矩阵（v2 测试项）

| 故障 | 预期行为 |
|---|---|
| 传输中断电 | Staging 不完整，旧版正常运作 |
| 影像损坏 | SHA256 验证挡下，拒绝写入 pending |
| 新版崩溃 | 回滚计数超限，Bootloader 还原旧版 |
| 重启后断电 | Metadata 旗标设计保证幂等/可恢复 |

## 10. 模板化与垂直扩展

- v1 只实作 `app/industrial_logger/`，README 附**平台扩展指南**
- 仪控版（设计文件层级）：OLED 显示 + 量测量测 + 简易指令介面，仅替换 app 层
- 医疗版（设计文件层级）：BLE GATT 生命征象节点（如 MAX30102）+ 安全记录，仅替换 app 层
- 扩展指南说明：新增垂直场景所需改动清单 = 新 app 目录 + 配置档案 + （视需要）新驱动

## 11. 云端与演示

- 本机 Mosquitto broker + Node-RED 仪表板（工业界标准工具）
- Topic 设计：`indulogger/<devid>/data`、`.../status`、`.../cmd`（升级指令走 cmd）
- 演示脚本：BLE 配网 → WiFi 上云 → 看板即时数据 → Modbus 轮询（USB-RS485）→ SD 卡内容 →（v2）看板一键升级 + 断电实测

## 12. 工具链

STM32CubeIDE（真实职缺常指定）、ESP-IDF、Node-RED、Git/GitHub

## 13. 里程碑

| 週 | 目标 | 出货点 |
|---|---|---|
| W1 | CubeIDE 工程 + FreeRTOS 任务骨架 + 感测驱动 + SD 记录 | — |
| W2 | Modbus RTU slave + 协议栈（STM32 端）；ESP32 WiFi/MQTT | — |
| W3 | BLE 配网 + 系统整合 + Node-RED 看板 | — |
| W4 | 文档打磨、协议规格书、平台指南、演示视频 | ✅ v1 上架作品集，可开始投标 |
| W5 | Bootloader + 记忆体映射 + OTA 协定（STM32 端） | — |
| W6 | ESP32 下载串流 + Node-RED 触发 + 故障矩阵实测 | — |
| W7 | OTA 文档 + 测试矩阵 + 断电实测视频 | ✅ 旗舰作品完成 |

## 14. 交付物清单

1. GitHub repo：英文 README（架构图、接线图、quickstart）
2. 双 MCU 协议规格书（英文，含状态机/时序图）
3. 平台扩展指南（仪控/医疗怎么套）
4. v1 演示视频（90 秒）
5. v2：OTA 设计文档 + 故障测试矩阵 + **断电实测视频**
6. Upwork portfolio 条目 + 个人定位文案（Firmware Engineer | STM32 · ESP32 · FreeRTOS · FOTA）

## 15. 风险与对策

| 风险 | 对策 |
|---|---|
| 范围蔓延 | 严格按 v1/v2 边界；新想法记入 roadmap 不实作 |
| 硬件相容性（模组品质） | BOM 选主流模组；到货先做单件 smoke test |
| 协议调试耗时 | 先用 USB-TTL + PC 模拟对端（Python 脚本）单元验证，再联机 |
| W4 后求职分心 | v1 即可投标；v2 与投标并行，时间表弹性后移 |
| 公司 email 出现在公开 repo | 推 GitHub 前改用个人 email 的 git identity |
