# InduLogger — Agent 交接备忘

> 本文件是 iCode 的项目记忆（默认智能体自动加载工作目录中的 AGENTS.md）。
> **每次完成任务或踩到新坑后更新本文件**；任务级勾选进度在 docs/plans/ 里维护，
> 这里只放"新会话快速进入状况"需要的浓缩事实。

**新会话注意**：工作目录必须是 `D:\InduLogger`（WSL 路径 `/mnt/d/InduLogger`）。
`~/InduLogger` 是废弃的空目录，不要用。

## 1. 项目一句话

工业数据记录器（STM32F411RE Nucleo + FreeRTOS + BME280/MPU6050 + SD/FATFS + Modbus），
定位是给 Upwork/Fiverr 客户看的**可验证作品集项目**：有设计 spec、有计划、
每次 push 跑 CI、每个功能在真实板子上取证后才算完成。
**M1 已完成收官（tag `m1-skeleton`、合 main）；M2.1 协议库已实现（coproc_proto 主机 TDD 23/23 绿 + CI 绿）；v1 范围已定方案 A 全收（2026-11-20 缺口盘点，v1 出货点顺延至 W6 初）**——
交接细节见 `~/.opencode/plan/2026-11-20-InduLogger_handover_notes.md`，
缺口盘点裁定见 `~/.opencode/plan/2026-11-20-InduLogger_v1_gap_audit.md`。

## 2. 文档索引

| 文件 | 内容 |
|---|---|
| `docs/specs/indulogger-design.md` | 设计 spec + **实现状态节**（活文档：项目全局视图，每里程碑更新）；原 2026-10-27 文件已改名 |
| `docs/reports/tN-*.md` | 阶段证据报告（按里程碑命名；T6/T8/T9 已建立，此后每阶段一篇） |
| `docs/specs/coproc-protocol-spec.md` | **STM32↔ESP32 链路协议规格书（英文，作品集交付物）**：帧格式/CRC/CMD 表/ACK 语义/会话状态机 + mermaid 图 |
| `docs/plans/2026-10-29-m1-stm32-skeleton.md` | M1 计划，11 个 Task，**checkbox 即进度** |
| `docs/plans/2026-11-20-m2-1-coproc-proto-tdd.md` | M2.1 计划，8 个 Task（协议库主机 TDD），checkbox 全勾 = 已完成 |
| `docs/notes/flash.md` | 烧录/调试/串口取证方法（含脚本） |
| `scripts/flash.sh`、`scripts/serial_capture.sh` | 烧录（OpenOCD 备份）、串口捕获 |
| `AGENTS.md`（本文件） | 环境事实、约定、踩坑 |

## 3. 当前进度（截至 2026-11-23）

| Task | 内容 | 状态 |
|---|---|---|
| T1 | CubeIDE 脚手架 + Makefile 本地构建 | ✓ `122be40` |
| T2 | 烧录上板 + LD2 心跳 1 Hz | ✓ `f6b828d` |
| T3 | console：printf 重定向到 ST-LINK VCP，横幅取证 | ✓ `0474742` |
| T4 | stats：复位原因解码（主机 TDD 3/3）+ IWDG + 错误计数器 | ✓ `7b87824` |
| T5 | CI：host tests + 固件构建 + cppcheck，push 全绿 | ✓ `30649d0`+`e8eba0b` |
| T6 | I2C + BME280 驱动：连通读数（p=1002 hPa）+ 拔线容错已上板取证；压力补偿 ×2.56 bug 被 Bosch oracle 主机 TDD 抓住并修复（`bme280_compensate` 隔离层）；拔线复插不自恢复=从机 FSM 卡死（踩坑 11）→ bus-clear 已补并复注验证自恢复 | ✓ `8f17104`+`35cacfe`+`efc20c6` |
| T7 | MPU6050 驱动 + 合理性检查：主机 TDD 全绿（2 套件）；无传感器错误路径取证；id 兼容 MPU6500（0x70，`ff96b88`）；倾斜轴切换（+X→−Y，1.013g）+ 平放（az=1.06g）+ 拔线容错全部上板取证 | ✓ `6aaa543`+`ff96b88` |
| T8 | SD + FATFS：挂载/写入/回读 + 拔卡自恢复（插回 ≤4s）+ 脏卡（NRST×3 写中复位）每次重挂载，全部上板取证；发现并修复 **ST diskio `is_initialized` 缓存** bug（踩坑 14） | ✓ `815f3ca` |
| T10 | DWT + GPIO 打点可信度：示波器 CH1（A3）实测 277µs vs DWT 277µs 偏差 0%（判据 ±10%）；校准方波先行验探链；发现并修复 diag.c 引脚映射与 .ioc 标签不一致（打点实际在 PA1，修正到 PB0/A3） | ✓ `9c5450f`+diag 修复（报告 docs/reports/t10-dwt-scope-crosscheck.md） |
| T9 | 三任务集成：主机 TDD（7/7）+ 布线 + [mon] 板测 + 挂死注入（5 轮确定性 iwdg 复位）+ 队列压力暴露三层故障链全修复（看门狗优先级 4 / init 路径总线清障 / open-once 锁泄漏 / fallback 文件） | ✓ `e3a7007`（报告 docs/reports/t9-three-task-watchdog.md） |
| T11 | 里程碑收官：README 状态 + `docs/validation/README.md` 证据索引（9 主张全 ✅）+ toolchain 版本记录 + 干净重建零告警 + tag `m1-skeleton` + 合 main | ✓ 本提交 |
| M2.1 | 协议库 coproc_proto/coproc_session 主机 TDD（23 测试）+ 构建接线 + 协议规格书 | ✓ `f7434b9..f604662`+终审修复 |

- 下一步顺序：见 §10（M2.2 UART 链路 + 等板窗口任务池）
- WSL `/tmp` 目前 100% 满（opencode 的 `oc_readonly.db` 占 4 GB，用户自行处理）→ 编译
  命令加 `TMPDIR=/home/eli_peng/tmp` 前缀即可绕过

分支 `m2-coproc`（M2 系列里程碑分支，自 `m1-skeleton` 切出），里程碑完成时合入 main 并打 tag。

## 4. 环境与常用命令

```bash
cd /mnt/d/InduLogger/stm32 && make -j4     # 构建 → build/InduLogger.elf/.bin
make flash                                  # CubeProgrammer CLI v2.22.0（走 WSL interop，勿需 usbipd）
cd /mnt/d/InduLogger && scripts/serial_capture.sh 30 &   # 后台串口监听 30s
cmake -S host -B host/build -G Ninja && cmake --build host/build && ctest --test-dir host/build
```

- 串口取证流程：**先**起 `serial_capture.sh`（后台）→ `make flash` → **等窗口结束**再读
  `/mnt/c/temp/serial_cap.txt`（文件在监听窗口结束时才落盘；跑之前脚本会自动删旧文件）
- **WSL `/tmp` 满时**：构建加 `TMPDIR=/home/eli_peng/tmp` 前缀（apt 也会被 /tmp 卡死）
- **本地 cppcheck 2.13（=CI 同版本，在 /home 下常驻）**：
  `LD_LIBRARY_PATH=/home/eli_peng/cpx/root/usr/lib/x86_64-linux-gnu
  /home/eli_peng/cpx/root/usr/bin/cppcheck …`（参数同 ci.yml；FILESDIR 已补丁）
- CubeIDE 只用于 `.ioc` 图形化代码生成和图形调试；**自动构建保持关闭**
  （managed build 会生成含 UNC 绝对路径的 makefile——这就是项目搬到 D 盘的原因）
- arm-none-eabi-gcc 14.2.1（apt）；CMake 4.4 + Ninja；Unity v2.6.0 **vendored** 在
  `host/third_party/unity/`（不要用 FetchContent，GitHub 直连不稳）

## 5. 固件/硬件关键事实

- Nucleo-F411RE，100 MHz（HSE bypass 8M → PLL M4 N100 P2），APB1 50 / APB2 100，
  FLASH_LATENCY_3
- HAL 时基 = **TIM11**（F411 无 TIM6/7）；IWDG ≈ **2 s**（32/1999），T9 的 Monitor
  任务负责 1 s 喂狗
- FreeRTOS 接口 = **CMSIS_V1**（CubeMX ≥6.11 已无 raw-API 选项）；app 代码直接用
  原生 FreeRTOS API
- FATFS：`_FS_TINY=0`、`_USE_LFN=2`（STACK 缓冲，`_MAX_LFN=255`）、`_MAX_SS=4096`
- CubeMX 6.12 生成形态：所有 `MX_*` 初始化与任务函数**内联在 main.c**（freertos.c
  只有 hooks；没有 usart.h 对文件 → `extern UART_HandleTypeDef huart2`）
- main.c USER CODE 区地图：`Includes`；`2` = MX 初始化后（DBGMCU 冻结 + console_init
  + 复位原因打印）；`5` = StartDefaultTask 循环（心跳 + 喂狗 + 500ms）；
  `RTOS_THREADS` 区 = T9 建任务处
- 外设：I2C1 400 kHz PB8/PB9（BME280 地址 0x76/0x77 双探测）；SPI1 慢速 390 kHz
  起步（**GUI 键名 `SPI1.BaudRatePrescaler`**——手写 `SPI1.Prescaler` 会被 CubeMX
  无视）；USART1_RX DMA2_Stream2
- **每次烧录后首次 boot 打印 `reset cause: iwdg` 是正常的**（烧录会话超过 2 s 喂狗
  窗口），详见 docs/notes/flash.md

## 6. 仓库约定

- 分层：`stm32/app/`（垂直场景）｜`platform/`（配置/日志/时间/健康/通讯）｜
  `middleware/`（Modbus 等）｜`bsp/`（手写板级驱动，T6 起）
- **手写驱动层叫 `bsp/` 不叫 `drivers/`**：NTFS 大小写不敏感，`drivers/` 会和
  CubeMX 的 `Drivers/` 变成同一个目录（CI 第一次翻车的根因）。Makefile OUR_SRC、
  ci.yml、计划文档全部已用 bsp
- 手写代码 -Wall -Wextra（Makefile CFLAGS_EXTRA pattern rules）；生成代码不深究
- 主机测试（Unity）：**测试文件要自带 main()**（UNITY_BEGIN/RUN_TEST/UNITY_END）；
  TDD red 阶段用 stub 文件让链接失败（CMake 4 在 generate 时就拒绝不存在的源文件）
- CI：`.github/workflows/ci.yml` 三 job（host-tests / firmware-build /
  static-analysis）；cppcheck 只扫手写目录，目录列表做了存在性过滤

## 7. git / 远端 / 网络

- `origin` = `git@github.com:EliPeng-sh/InduLogger.git`（**公开**，作品集主仓）；
  `gitee` = `git@gitee.com:eli-peng/indu-logger.git`（国内镜像备份；Gitee 不跑
  `.github/workflows`，CI 以 GitHub 为准）
- 提交身份：`Eli Peng <xjmps7114@qq.com>`（历史已用 git-filter-repo 洗掉公司邮箱）
- SSH：`~/.ssh/config` 把 github.com 路由到 `ssh.github.com:443`（直连 22 认证会
  间歇性抖）；推送偶发 GitHub 服务端瞬时错误（`fatal error in commit_refs` /
  `Internal Server Error` / 偶发 publickey）→ **单分支 + 退避重试即可，勿改配置**
- 每任务收尾流程：commit → `git push origin refs/heads/m2-coproc && git push gitee
  refs/heads/m2-coproc`（tag/分支同名坑见踩坑 19，一律显式 refspec）→ API 验 CI：
  `curl -s https://api.github.com/repos/EliPeng-sh/InduLogger/actions/runs?per_page=1`

## 8. 踩坑备忘（新坑请追加）

1. Windows USB 选择性暂停 + 旧 ST-LINK 固件（V2J28M18）→ `DEV_USB_COMM_ERR`：
   已用 powercfg 关闭选择性暂停 + 换线换口；根治可升固件 V2J39
2. CubeIDE 工程放 `\\wsl.localhost` UNC → 生成 makefile 烧死 UNC 绝对路径 →
   工程必须放 Windows 原生盘
3. NTFS 大小写不敏感：`drivers/` ≡ `Drivers/`（见 §6）
4. `git filter-repo --force` 会**丢弃未提交修改**——跑之前必须全部 commit
5. bash 管道（pipefail）验证退出码：`cmd | grep -v ...; echo $?` 拿到的是 grep 的
   退出码，不是 cmd 的——要单独跑或落盘再查
6. api.github.com 未认证查询只有公开仓库可见（私有仓一律 404，会误导排查方向）
7. 本网络到 GitHub 的 SSH/API 偶发抖动，重试解决（见 §7）
8. `FreeRTOSConfig.h` 模板里没有 `INCLUDE_uxTaskGetStackHighWaterMark`（默认未定义 →
   链接报 undefined reference）；解决办法是加到该文件的 **USER CODE Defines 区**
   （regen 安全），不要在生成区硬改
9. bash 里 `cd repo && cmd &` 会把整个复合命令后台化——后续命令仍在**原** cwd；后台
   起命令后配绝对路径（`make -C /mnt/d/InduLogger/stm32 flash`）
10. `.ioc` 的 `FREERTOS.Tasks01`（第三字段=栈词数）可以直接编辑，与生成代码的
    `osThreadDef(..., stacksz)` 同步改；下次 CubeIDE GUI 重新生成时一致
11. **I2C 从机 FSM 卡死**：传输中段拔线 → 芯片拉死 SDA（实测 0.02 V），HAL 无总线
    清障能力，插回线**不会**自恢复，只能断电重启芯片。**已修复**：连续 3 次读失败 →
    GPIO 模式 9×SCL+STOP（AN4235）→ 清 inited → tick 重 init（`efc20c6`）；
    复注拔线故障：插回 ≤4 s 自恢复、全程零复位、LED 不断
12. **补偿公式必须以官方 driver 为 oracle 逐行 diff**（Bosch BME280_driver v3.5.1）：
    自写 float 版漏掉 Q24.8 /256 + P7 项 → 压力 ×2.56；HAL-free 补偿模块
    （`bme280_compensate.{h,c}`）是主机 TDD 能抓住这类 bug 的前提
13. 板断电（USB 拔插）后 ST-LINK VCP 重枚举，`serial_capture.sh` 可能瞬时找不到
    COM——重试即可；PowerShell 命令外层用**单引号**（双引号内 `$_` 会被 bash 展开）
14. **ST diskio.c 的 `disk.is_initialized[]` 缓存**：ST 给 `disk_initialize` 包了层
    缓存，FatFs 永远不清它 → 重插卡（=上电卡，需完整 CMD0/ACMD41 重 init）永远
    不会被重新初始化，re-mount 从缓存"成功"后 CMD17 打在未初始化卡上 → FR_DISK_ERR
    死循环。修复：I/O 失败时同时清 glue 标志 + `disk.is_initialized[0]`；且
    `sd_spi_init` 入口强制回 400kHz（上次 init 的快速分频会残留毁掉重 init）。
    排查此类"烧录成功却旧行为"用 **CubeProgrammer `-u` 读回 flash 与 build/bin
    做 `cmp` 字节比对**（参数顺序：地址 大小 文件；路径用 `C:\temp`，WSL 路径不通）
15. **蜂鸣档 DC 导通 ≠ 时钟信号完整性**：SD 卡"完全沉默"排查中，杜邦线蜂鸣全通、
    卡座有 3.3V、重插卡都无效，最后万用表笔一碰就好——边际接触能过 DC 却圆掉
    400kHz 时钟沿，CMD0 永远无应答。教训：DC 工具排除不了信号完整性问题，
    排查顺序应尽早做"物理重插"对照实验
16. **boot 横幅 `__DATE__/__TIME__` 是按编译单元固化的**：增量构建不重编 console.c
    → 横幅时间戳陈旧，**横幅时间戳单独不能当固件身份证据**；确认身份用构建尺寸
    比对 + CubeProgrammer `-u` 读回 cmp，或 `touch console.c` 后重建
17. **代码内引脚映射与 .ioc 标签打架时以运行时代码为准**：T10 示波器探 A3（.ioc
    BENCH_GPIO1=PB0）无波形，串口却证明打点在翻转——根因是 diag.c 把
    `set(1,...)` 映射到 PA1（与旧注释自洽但违背 .ioc）。教训：三层（.ioc 标签 /
    main.h 宏 / 手写代码映射）要核对一致；**"仪器探不到"的物理证据比 grep 更可靠**，
    先板证后改码。修复后 diag.c/diag.h 与 .ioc 对齐（PB0/A3、PB1/A4）
18. **台式机 USB 供电 + 示波器测量会耦合 50Hz 工频**：探棒长鳄鱼夹地线环路拾取
    市磁场。对策：接地弹簧/最短地线、边沿触发+Normal、光标卡 50% 交叉点量宽度；
    50Hz 只影响垂直轴不影响时间轴，脉宽判定不受威胁
19. **tag 与分支同名后，一切 push 都必须用显式 refspec**：`git push origin
    m1-skeleton` 报 "src refspec matches more than one"（源同时命中分支和 tag）。
    分支用 `git push origin refs/heads/m1-skeleton`，tag 用
    `git push origin refs/tags/m1-skeleton`（M1 收官 tag 之后此坑永久存在，别想删 tag 绕开）
20. **`uint32_t` 毫秒计数器的超时比较必须用减法**：`now_ms >= deadline_ms`（deadline 用
    加法算）在 `now_ms` 每 49.7 天回绕一次的那个窗口内会**立即判超时**，导致提前重传、
    4 次 tick 内 retries 耗尽 → 误报 FAULT + link 误置 DOWN（终审在
    `cp_session_tick` 实证发现，修法 `(int32_t)(now_ms - deadline_ms) >= 0`；周期类比较
    `now - last >= PERIOD` 天然安全）。回归测试 21 钉住：`now=0xFFFFFF00` 发帧后
    `tick(0xFFFFFF10)` 必须无输出。凡是写"未来截止时间"的代码，一律减法比较 + 跨回绕用例
21. **推 CI 前跑"和 CI 一字不差"的 cppcheck 命令**：M2.1 收尾首轮 CI 挂在
    `coproc_proto.c:141 style: Variable 'hdr4' can be declared as const array`——本地此前
    只对 `stm32/middleware` 跑了 MISRA、没跑 CI 那条 `--enable=warning,style` 的**全目录**
    命令。教训：门禁命令要整条复制执行（含 `--error-exitcode=1` 和目录列表），不要只跑子集

## 9. 会话工作法（沿用既有节奏）

- 每任务：读 plan 对应节 → 实现 → **板上验证/串口取证** → 勾 checkbox → 更新本文件
  → commit + push 双仓 → CI 绿
- 涉及硬件操作的（接线/拔插/换卡）提前一轮告诉用户，等确认后再动手
- 给用户的说明用中文；代码、commit message、文档正文用英文
- 证据纪律：一个功能说"完成"前必须有板上证据（串口输出/CI 绿/测试通过）

## 10. 下一步清单（M2 启动，2026-11-20 裁定后）

1. **M2.1 协议库主机 TDD（零硬件，计划已落）**：执行计划 `docs/plans/2026-11-20-m2-1-coproc-proto-tdd.md`（8 Task：脚手架/CRC/encode/parse/会话核心/会话鲁棒/协议规格书/收尾，计划 20 例 + 终审补 3 例 = 23 例，分支 `m2-coproc`）—— ✅ 已完成（见 §3 M2.1 行；协议参数冻结见第 6 条；英文协议规格书 `docs/specs/coproc-protocol-spec.md`；终审 2 个 Important 已修：回绕安全比较 + stale-ACK 回归测试）
2. **等板窗口任务池（零 ESP32 依赖，按序）**：ADC(+DMA) → HardFault 诊断链+栈溢出钩子 → Flash 配置区 EEPROM 仿真 → RTC 基础 init；若板先到则窗口池顺延至 M2.5 后
3. **M2.2–M2.5**：M2.2 UART 链路+CommTask（`.ioc` USART1 只有基础配置，**需补 IDLE+DMA 收帧**）→ M2.3 WiFi/MQTT（mbedTLS 注记 + TIME_SET→RTC 接线）→ M2.4 BLE 配网 → M2.5 Node-RED 看板
4. **Modbus RTU（M2.5 后、v1 收官前）**：需提前购买 MAX485 + USB-RS485（约 $10）
5. **v1 收官（W6 初）**：文档打磨/压测/验证报告/协议规格书/演示视频；MISRA cppcheck addon 已接线（M2.1 收尾：CI 新增一步只对 `stm32/middleware` 跑 `--addon=misra`，本地 0 findings；M1 legacy 目录有 44 条 `misra-config` 假报（FatFs 枚举识别不到），要在 v1 收官前补 addon 的 rule-text/config 才能扩到全仓）；CLI 可选
6. **协议定案参数（冻结）**：SOF=0xA5 0x5A、CRC16=CCITT-False（check=0x29B1）、停等 500ms×3 重试、HEARTBEAT 5s/15s 离线、帧总长 8+N、TIME_SET 方向=ESP32→STM32（spec §7 已修正）；黄金向量 `A5 5A 00 00 00 02 A4 82`
