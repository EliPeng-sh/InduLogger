# InduLogger — Agent 交接备忘

> 本文件是 iCode 的项目记忆（默认智能体自动加载工作目录中的 AGENTS.md）。
> **每次完成任务或踩到新坑后更新本文件**；任务级勾选进度在 docs/plans/ 里维护，
> 这里只放"新会话快速进入状况"需要的浓缩事实。

**新会话注意**：工作目录必须是 `D:\InduLogger`（WSL 路径 `/mnt/d/InduLogger`）。
`~/InduLogger` 是废弃的空目录，不要用。

## 1. 项目一句话

工业数据记录器（STM32F411RE Nucleo + FreeRTOS + BME280/MPU6050 + SD/FATFS + Modbus），
定位是给 Upwork/Fiverr 客户看的**可验证作品集项目**：有设计 spec、有计划、
每次 push 跑 CI、每个功能在真实板子上取证后才算完成。当前执行到 **M1 骨架里程碑**。

## 2. 文档索引

| 文件 | 内容 |
|---|---|
| `docs/specs/2026-10-27-indulogger-design.md` | 设计 spec（分层架构、功能边界、验收主张） |
| `docs/plans/2026-10-29-m1-stm32-skeleton.md` | M1 计划，11 个 Task，**checkbox 即进度** |
| `docs/notes/flash.md` | 烧录/调试/串口取证方法（含脚本） |
| `scripts/flash.sh`、`scripts/serial_capture.sh` | 烧录（OpenOCD 备份）、串口捕获 |
| `AGENTS.md`（本文件） | 环境事实、约定、踩坑 |

## 3. 当前进度（截至 2026-11-06）

| Task | 内容 | 状态 |
|---|---|---|
| T1 | CubeIDE 脚手架 + Makefile 本地构建 | ✓ `122be40` |
| T2 | 烧录上板 + LD2 心跳 1 Hz | ✓ `f6b828d` |
| T3 | console：printf 重定向到 ST-LINK VCP，横幅取证 | ✓ `0474742` |
| T4 | stats：复位原因解码（主机 TDD 3/3）+ IWDG + 错误计数器 | ✓ `7b87824` |
| T5 | CI：host tests + 固件构建 + cppcheck，push 全绿 | ✓ `30649d0`+`e8eba0b` |
| T6 | I2C + BME280 驱动：连通读数（p=1002 hPa）+ 拔线容错已上板取证；压力补偿 ×2.56 bug 被 Bosch oracle 主机 TDD 抓住并修复（`bme280_compensate` 隔离层）；拔线复插不自恢复=从机 FSM 卡死（踩坑 11，bus-clear 待补） | ✓ `8f17104`+`35cacfe` |
| **T7** | MPU6050 驱动 + 合理性检查：主机 TDD 全绿（2 套件），无传感器错误路径已取证（+1/2s、wm=372）；平坦/倾斜验证待硬件 | ✓（部分） |
| T8 | SD + FATFS（需 SD 模块与卡） | 未开始 |
| **T10** | DWT + GPIO 打点基建：已实测 I2C 失败尝试 51.6 ms/次（稳定、<100ms 限界）；示波器交叉验证待仪器 | ✓（部分） |
| T9、T11 | 三任务集成（需传感器+SD）/ 里程碑收官 | 未开始 |

- 下一步顺序：① bus-clear/重 init 机制（连续 N 次失败→GPIO 9×SCL+STOP 按 AN4235→清
  inited 标志→tick 重 init；主机 TDD 决策逻辑）→ 故障复注验证自恢复 ② MPU6500 id 兼容
  （GY-521 实测 WHO_AM_I=0x70 非 0x68，寄存器基本兼容）③ T8 SD+FATFS（模块+卡已到货）
- WSL `/tmp` 目前 100% 满（opencode 的 `oc_readonly.db` 占 4 GB，用户自行处理）→ 编译
  命令加 `TMPDIR=/home/eli_peng/tmp` 前缀即可绕过

分支 `m1-skeleton`，里程碑完成时合入 main 并打 tag（T11）。

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
- 每任务收尾流程：commit → `git push origin m1-skeleton && git push gitee
  m1-skeleton` → API 验 CI：
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
    清障能力，插回线**不会**自恢复，只能断电重启芯片；固件缺口 → 待补"连续失败→
    GPIO 9×SCL+STOP→重 init"（见 §3 下一步）
12. **补偿公式必须以官方 driver 为 oracle 逐行 diff**（Bosch BME280_driver v3.5.1）：
    自写 float 版漏掉 Q24.8 /256 + P7 项 → 压力 ×2.56；HAL-free 补偿模块
    （`bme280_compensate.{h,c}`）是主机 TDD 能抓住这类 bug 的前提
13. 板断电（USB 拔插）后 ST-LINK VCP 重枚举，`serial_capture.sh` 可能瞬时找不到
    COM——重试即可；PowerShell 命令外层用**单引号**（双引号内 `$_` 会被 bash 展开）

## 9. 会话工作法（沿用既有节奏）

- 每任务：读 plan 对应节 → 实现 → **板上验证/串口取证** → 勾 checkbox → 更新本文件
  → commit + push 双仓 → CI 绿
- 涉及硬件操作的（接线/拔插/换卡）提前一轮告诉用户，等确认后再动手
- 给用户的说明用中文；代码、commit message、文档正文用英文
- 证据纪律：一个功能说"完成"前必须有板上证据（串口输出/CI 绿/测试通过）

## 10. 下一步清单（T6 完成后）

1. **bus-clear/重 init**（主机 TDD）：连续 N 次 bme280_read 失败 → GPIO 模式 9×SCL
   脉冲 + STOP（AN4235）→ 清 `s_bme_inited` → tick 走重 init；完成后拔线复注验证
   自恢复（当前板上证据：卡死只能断电清）
2. **MPU6500 id 兼容**（主机 TDD）：GY-521 实测 WHO_AM_I=0x70（非 6050 的 0x68），
   寄存器基本兼容 → 驱动接受 {0x68, 0x70} → MPU 插回 wires → T7 平坦/倾斜上板取证
3. **T8 SD+FATFS**：`stm32/bsp/sdcard/{sd_spi.c,sd_spi.h,user_diskio_glue.c}` +
   `app_logger.c`；SPI1 PB3/PB4/PB5 + PA4 CS（丝印 D5/D4/D3/A2）；**先确认 SD 模块
   是 5V+稳压型还是 3.3V 直供型**再接线；init 400kHz→12.5MHz，全部 100ms 限界
4. T9 三任务集成（CSV formatter 可先于 SD 硬件做主机 TDD）→ T11 收官
