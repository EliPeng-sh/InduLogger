# InduLogger Milestone 1 (W1): STM32 Skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** STM32F411 + FreeRTOS skeleton that samples BME280/MPU6050, logs CSV to SD card, prints live diagnostics over the debug UART, runs under IWDG watchdog, with DWT/GPIO instrumentation and CI (firmware build + host unit tests) green on every push.

**Architecture:** CubeMX generates the F411 project (100 MHz, CMake toolchain output) into `stm32/`; application code lives in `app/`, `platform/`, `drivers/` per the spec's layered structure. Platform-independent logic (reset-cause decode, CSV formatting, sensor plausibility) is TDD'd on the host with Unity; hardware interaction is verified by on-target smoke steps with expected console output. CI builds firmware with arm-none-eabi-gcc and runs host tests on every push.

**Tech Stack:** C11 · STM32F411RE (Nucleo) · FreeRTOS (raw API, heap_4) · STM32CubeMX (CMake output) · arm-none-eabi-gcc 14.2.1 · CMake 4.4 + Ninja · OpenOCD (flash via ST-Link/usbipd-win) · Unity (host tests) · GitHub Actions

**Spec:** `docs/specs/2026-10-27-indulogger-design.md` (Milestone = spec §14 row W1; evidence infra = spec §3/§4 v1 "证据基建" items; watchdog/health = §8)

## Global Constraints

- Language C11; compile with `-Wall -Wextra`; zero warnings before commit.
- No `malloc()` in drivers or app code; only the FreeRTOS heap (heap_4, 30 KB).
- No blocking busy-wait loops inside tasks; use queues, task notifications, or `vTaskDelay`.
- Every failure path increments a stats error counter (`stats_error(ERR_x)`) — evidence infra rule from spec §3.
- Do not edit CubeMX-generated code outside `/* USER CODE BEGIN ... */` sections.
- Comments/docs in English; commit messages in Conventional Commits; one commit per task.
- Recorded toolchain versions: arm-none-eabi-gcc 14.2.1 (apt 15:14.2.rel1-1), CMake 4.4.0, Ninja, OpenOCD (apt), Unity v2.6.0 — update `docs/notes/toolchain.md` if anything changes.
- Repo root: `~/InduLogger`. Plans live in `docs/plans/` (repo convention, overriding skill default).

## Review Focus

1. **I2C sensor hangs (SDA stuck / NACK)** — a disconnected or wedged sensor must not stall a task: every I2C transaction bounded by ~100 ms timeout, failure increments `ERR_BME_TIMEOUT`/`ERR_MPU_TIMEOUT`, task keeps running. Pinned in Task 6 (disconnect test).
2. **SD card removed mid-run** — FATFS write errors must be caught: LoggerTask logs `ERR_SD_WRITE`, retries mount on interval, never crashes. Pinned in Task 8 (pull-card test).
3. **Implausible sensor values** (e.g., BME280 temp outside −40…85 °C, accel magnitude outside 0.3…2.0 g) — must be rejected by `sensor_plausible_*()`, counted via `ERR_SENSOR_RANGE`, last-good values cached. Pinned in Task 7 (host unit tests).
4. **Watchdog reset in the middle of an SD write** — after reboot the card must mount cleanly and logging resumes (dirty-card tolerance). Pinned in Task 8 (force-reset-during-write step).
5. **Queue overflow under stress** — `sensorQ` full must drop newest + increment `ERR_Q_DROP`, never deadlock the sensor task. Pinned in Task 9 (stress-mode check).

---

### Task 1: CubeMX project scaffold + local firmware build

**Files:**
- Create: `stm32/` (CubeMX-generated CMake project: `InduLogger.ioc`, `CMakeLists.txt`, `Core/`, `Drivers/`, `Middlewares/`)
- Create: `docs/notes/toolchain.md`
- Modify: `.gitignore` (ignore `build/` trees)

**Interfaces:**
- Produces: `stm32/build/InduLogger.elf` build path used by Task 2 (flash) and CI (Task 5); FreeRTOS enabled with raw API; peripherals: USART2 (debug, 115200 8N1), I2C1 (400 kHz, PB8/PB9), SPI1 (PB3/PB4/PB5) + GPIO PA4 `SD_CS`, USART1 (PA9/PA10, 115200, for W2 co-processor link), IWDG (~1 s), GPIO outputs PA5 (onboard LED `LED_HEARTBEAT`), PB0 `BENCH_GPIO1`, PB1 `BENCH_GPIO2`; HAL timebase on TIM6.

- [ ] **Step 1: Create `.ioc` config in STM32CubeMX (Windows side)**

Install STM32CubeMX ≥ 6.11 (STM32 account required), then:
1. New Project → MCU: **STM32F411RETx**.
2. **RCC**: HSE = *Crystal/Resonator → Bypass Clock Source* (Nucleo feeds 8 MHz from ST-LINK MCO).
3. **SYS**: Debug = *Serial Wire*; Timebase Source = **TIM6** (not SysTick, FreeRTOS owns SysTick).
4. **Clock Configuration**: HSE 8 MHz → PLL M=/4, N=×100, P=/2 → SYSCLK = **100 MHz**; AHB /1 = 100 MHz; APB1 /2 = **50 MHz**; APB2 /1 = 100 MHz.
5. **USART2** (PA2/PA3): Asynchronous, 115200, 8N1 (ST-LINK VCP).
6. **I2C1**: I2C mode, Fast Mode 400 kHz (PB8=SCL, PB9=SDA).
7. **SPI1**: Full-Duplex Master, Hardware NSS off; pins PB3=SCK, PB4=MISO, PB5=MOSI; PA4 → `GPIO_Output`, label `SD_CS`.
8. **USART1** (PA9/PA10): Asynchronous, 115200; NVIC: USART1 global interrupt enabled; DMA: USART1_RX (Normal). (Used in W2; pin/NVIC only now.)
9. **IWDG**: Prescaler 32, Counter Reload 999 (LSI 32 kHz / 32 = 1 kHz tick → ~1 s).
10. **GPIO outputs**: PA5 label `LED_HEARTBEAT` (onboard LD2, output low), PB0 label `BENCH_GPIO1`, PB1 label `BENCH_GPIO2`.
11. **Middleware**: FreeRTOS — Interface: *CMSIS-FreeRTOS unchecked* (raw API); Heap: **heap_4**; TOTAL_HEAP_SIZE = **30720**; TICK_RATE 1000 Hz.
12. **FATFS**: check "User-defined" (SD via SPI arrives in Task 8; enabling now avoids re-gen surprises). Set `_FS_TINY = 0`, `_USE_LFN = 1`.
13. Project Manager → Project Name `InduLogger`, Location: `\\wsl.localhost\<distro>\home\eli_peng\InduLogger\stm32`. If CubeMX mishandles the UNC path, generate to `C:\temp\InduLogger\stm32` then copy the whole folder into `~/InduLogger/stm32`.
14. Toolchain/IDE = **CMake**. Generate code.

- [ ] **Step 2: Verify generated tree + ignore build dirs**

Run: `ls stm32` → expect `CMakeLists.txt Core Drivers Middlewares InduLogger.ioc CMakePresets.json`.
Append to `.gitignore`:
```
build/
stm32/build/
host/build/
```

- [ ] **Step 3: Write `docs/notes/toolchain.md`**

```markdown
# Toolchain

- Host: WSL2 Ubuntu 26.04
- arm-none-eabi-gcc 14.2.1 (apt 15:14.2.rel1-1)
- CMake 4.4.0, Ninja
- OpenOCD (apt) — flash via ST-Link over usbipd-win
- STM32CubeMX: >= 6.11, CMake output
- Unity v2.6.0 via CMake FetchContent (host tests)
- FreeRTOS: CubeMX-bundled, raw API, heap_4 (30 KB)
```

- [ ] **Step 4: Add heartbeat toggle to the default task**

In `stm32/Core/Src/freertos.c`, inside the default task loop (`USER CODE` region):
```c
/* USER CODE BEGIN DefaultTask */
for (;;) {
  HAL_GPIO_TogglePin(LED_HEARTBEAT_GPIO_Port, LED_HEARTBEAT_Pin);
  vTaskDelay(pdMS_TO_TICKS(500));
}
/* USER CODE END DefaultTask */
```

- [ ] **Step 5: Configure + build locally**

Run (from repo root):
`cmake -S stm32 -B stm32/build --preset Debug` (if presets missing: `cmake -S stm32 -B stm32/build -G Ninja -DCMAKE_TOOLCHAIN_FILE=stm32/cmake/gcc-arm-none-eabi.cmake`)
then `cmake --build stm32/build`
Expected: `InduLogger.elf` produced, zero warnings.

- [ ] **Step 6: Commit**

```bash
git add stm32 .gitignore docs/notes/toolchain.md
git commit -m "feat(stm32): CubeMX scaffold - 100MHz F411, FreeRTOS raw API, heartbeat"
```

### Task 2: USB flashing path (usbipd-win) + first flash on target

**Files:**
- Create: `docs/notes/flash.md` (flash procedure reference)

**Interfaces:**
- Consumes: `stm32/build/InduLogger.elf` (Task 1)
- Produces: documented one-command flash (`scripts/flash.sh` used by later tasks)

- [ ] **Step 1: Attach ST-Link to WSL via usbipd-win**

On Windows PowerShell **(Admin)**:
```powershell
usbipd list                      # find ST-Link, e.g. 2-3 (VID:PID 0483:3748)
usbipd bind --hardware-id 0483:3748
usbipd attach --wsl --hardware-id 0483:3748
```
In WSL: `lsusb` → expect `0483:3748 STMicroelectronics ST-LINK/V2-1`.

- [ ] **Step 2: Verify OpenOCD sees the target**

Run: `openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "init; targets; exit"`
Expected: `stm32f4x.cpu ... halted` reported.

- [ ] **Step 3: Create `scripts/flash.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail
ELF="${1:-stm32/build/InduLogger.elf}"
exec openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c "program $ELF verify reset exit"
```
Then `chmod +x scripts/flash.sh`.

- [ ] **Step 4: Flash + confirm LED heartbeat**

Run: `./scripts/flash.sh`
Expected: console shows verify OK; **LD2 blinks at 1 Hz on the board** (physical observation).

- [ ] **Step 5: Commit**

```bash
git add scripts/flash.sh docs/notes/flash.md
git commit -m "feat: flashing via OpenOCD+ST-Link (usbipd-win), LED heartbeat verified"
```

### Task 3: Debug console — printf retarget + banner

**Files:**
- Create: `stm32/platform/console/console.h`, `stm32/platform/console/console.c`
- Modify: `stm32/Core/Src/main.c` (call init in USER CODE)

**Interfaces:**
- Produces: `void console_init(void);` + usable `printf()` over USART2 @115200 (all later tasks print through this)

- [ ] **Step 1: Implement retarget**

`console.c`:
```c
#include "usart.h"
#include <unistd.h>
#include <stdio.h>

void console_init(void) {
  /* USART2 already configured by CubeMX (huart2); nothing extra needed. */
  printf("\r\nInduLogger boot v0.1.0-m1\r\n");
}

int _write(int fd, char *ptr, int len) {
  (void)fd;
  HAL_UART_Transmit(&huart2, (uint8_t *)ptr, (uint16_t)len, 100);
  return len;
}
```
(`console.h`: prototype for `console_init`.)

- [ ] **Step 2: Call from main + verify**

In `main.c` USER CODE 2: `console_init();`
Build + flash: `cmake --build stm32/build && ./scripts/flash.sh`
Expected on serial (115200, e.g. `picocom -b 115200 /dev/ttyACM0`): `InduLogger boot v0.1.0-m1`.

- [ ] **Step 3: Commit**

```bash
git add stm32/platform stm32/Core/Src/main.c
git commit -m "feat(console): printf retarget over ST-LINK VCP, boot banner"
```

### Task 4: Stats module — reset-cause decode (TDD) + IWDG watchdog live

**Files:**
- Create: `host/tests/test_reset_cause.c`, `host/CMakeLists.txt`
- Create: `stm32/platform/stats/stats.h`, `stm32/platform/stats/stats.c`
- Modify: `stm32/Core/Src/main.c` (capture + feed + banner print)

**Interfaces:**
- Produces (used by Tasks 6–9, 11):
```c
typedef enum {
  RESET_CAUSE_UNKNOWN, RESET_CAUSE_BOR, RESET_CAUSE_PIN, RESET_CAUSE_POR,
  RESET_CAUSE_SOFT, RESET_CAUSE_IWDG, RESET_CAUSE_WWDG, RESET_CAUSE_LPWR
} reset_cause_t;
reset_cause_t reset_cause_capture(void);      // reads RCC->CSR, clears flags
const char *reset_cause_name(reset_cause_t c); // pure, host-tested
void stats_error(uint32_t id);                 // atomic counter increment
uint32_t stats_get(uint32_t id);
extern const uint32_t ERR_BME_TIMEOUT, ERR_MPU_TIMEOUT, ERR_SD_WRITE,
                      ERR_Q_DROP, ERR_SENSOR_RANGE, ERR_I2C_RECOVERY;
void iwdg_feed(void);                          // wraps HAL_IWDG_Refresh
```

- [ ] **Step 1: Write failing host test**

`host/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20)
project(indulogger_host_tests C)
include(FetchContent)
FetchContent_Declare(unity GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git GIT_TAG v2.6.0)
FetchContent_MakeAvailable(unity)
enable_testing()
file(GLOB TESTS tests/test_*.c)
foreach(T ${TESTS})
  get_filename_component(N ${T} NAME_WE)
  add_executable(${N} ${T} ../stm32/platform/stats/stats.c)
  target_include_directories(${N} PRIVATE ../stm32/platform/stats)
  target_compile_options(${N} PRIVATE -Wall -Wextra)
  add_test(NAME ${N} COMMAND ${N})
endforeach()
```
`host/tests/test_reset_cause.c`:
```c
#include "unity.h"
#include "stats.h"
void setUp(void) {} void tearDown(void) {}
void test_bor_flag(void)  { TEST_ASSERT_EQUAL_STRING("brownout", reset_cause_name(RESET_CAUSE_BOR)); }
void test_iwdg_flag(void) { TEST_ASSERT_EQUAL_STRING("iwdg", reset_cause_name(RESET_CAUSE_IWDG)); }
void test_unknown(void)   { TEST_ASSERT_EQUAL_STRING("unknown", reset_cause_name(RESET_CAUSE_UNKNOWN)); }
```

- [ ] **Step 2: Run, verify fail**

Run: `cmake -S host -B host/build -G Ninja && cmake --build host/build && ctest --test-dir host/build --output-on-failure`
Expected: FAIL — `stats.h` / `stats.c` do not exist yet.

- [ ] **Step 3: Implement `stats.c` decode part**

`stats.c`:
```c
#include "stats.h"
const char *reset_cause_name(reset_cause_t c) {
  switch (c) {
  case RESET_CAUSE_BOR:   return "brownout";
  case RESET_CAUSE_PIN:   return "external-pin";
  case RESET_CAUSE_POR:   return "power-on";
  case RESET_CAUSE_SOFT:  return "software";
  case RESET_CAUSE_IWDG:  return "iwdg";
  case RESET_CAUSE_WWDG:  return "wwdg";
  case RESET_CAUSE_LPWR:  return "low-power";
  default:                return "unknown";
  }
}
```
(On-target parts — `reset_cause_capture` reading/stripping `RCC->CSR` flags per RM0383, atomic `stats_error` via `__atomic_fetch_add`, `iwdg_feed` — are added in the same file, guarded so they don't compile on host: wrap target code in `#ifndef HOST_TEST`.)

- [ ] **Step 4: Run, verify pass**

Same command as Step 2. Expected: 3/3 PASS.

- [ ] **Step 5: Wire into firmware + force-watchdog verification**

`main.c` USER CODE 2 (after console_init):
```c
reset_cause_t rc = reset_cause_capture();
printf("reset cause: %s\r\n", reset_cause_name(rc));
HAL_IWDG_Init(&hiwdg); /* CubeMX-generated handle */
```
Default task loop: `iwdg_feed();`
Build + flash → expect `reset cause: external-pin` (first flash asserts NRST).
Then temporarily comment `iwdg_feed()`, rebuild+flash, wait 2 s → LED stops, board resets; restore feed, flash again → expect `reset cause: iwdg` on next boot console output. Restore the feed.

- [ ] **Step 6: Commit**

```bash
git add host stm32/platform/stats stm32/Core/Src/main.c
git commit -m "feat(stats): reset-cause decode (host-TDD), IWDG live, error counters"
```

### Task 5: CI — firmware build + host tests on every push

**Files:**
- Create: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `host/CMakeLists.txt` (Task 4), `stm32/` CMake project (Task 1)
- Produces: green CI badge; artifact `InduLogger.elf` on every push

- [ ] **Step 1: Write workflow**

`.github/workflows/ci.yml`:
```yaml
name: ci
on: [push, pull_request]
jobs:
  host-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y gcc cmake ninja-build
      - run: cmake -S host -B host/build -G Ninja
      - run: cmake --build host/build
      - run: ctest --test-dir host/build --output-on-failure
  firmware-build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y gcc-arm-none-eabi cmake ninja-build
      - run: cmake -S stm32 -B stm32/build -G Ninja
      - run: cmake --build stm32/build
      - uses: actions/upload-artifact@v4
        with:
          name: firmware
          path: stm32/build/InduLogger.elf
```

- [ ] **Step 2: Push to GitHub, verify green**

Create empty repo `InduLogger` on GitHub (no README), then:
`git remote add origin git@github.com:<user>/InduLogger.git && git push -u origin main`
Expected: both jobs green on the Actions tab.

- [ ] **Step 3: Commit**

```bash
git add .github
git commit -m "ci: host unit tests + cross firmware build on push"
```

### Task 6: I2C bring-up + BME280 driver (bounded timeouts, error counters)

**Files:**
- Create: `stm32/drivers/bme280/bme280.h`, `stm32/drivers/bme280/bme280.c`
- Create: `stm32/app/industrial_logger/app_sensors.c` (driver smoke harness, expanded in Task 9)

**Interfaces:**
- Consumes: `stats_error()` (Task 4), `hiadc?` no — CubeMX `hi2c1` handle
- Produces:
```c
// returns 0 on success, -1 on timeout/NACK (increments ERR_BME_TIMEOUT)
int bme280_init(I2C_HandleTypeDef *hi2c);
int bme280_read(float *temp_c, float *hum_pct, float *press_hpa); // blocking ≤100ms
```

- [ ] **Step 1: Implement driver (I2C bounded + compensation)**

Key content of `bme280.c` (compact float compensation, normal mode):
- init: verify chip id reg `0xD0 == 0x60`; soft reset `0xE0=0xB6`; write trim reads `0x88..0xA1` (T1..P9, 18B) + `0xE1..0xE7` (H4/H5/H6); config `0xF5=0x1C` (standby 1000ms, IIR ×16); `0xF2=0x01` (hum ×1); `0xF4=0x57` (T ×2, P ×16, normal mode).
- read: burst `0xF7..0xFE` (8B) with `HAL_I2C_Mem_Read(..., 100)`; per datasheet 3.3.3 float formulas compute `temp_c`, `hum_pct`, `press_hpa` (=P/256/100).
- Any `HAL_TIMEOUT`/`HAL_ERROR` → `stats_error(ERR_BME_TIMEOUT)`, return -1.
All I2C calls use 100 ms timeout — never infinite.

- [ ] **Step 2: Smoke harness prints reading**

`app_sensors.c` (called from default task every 2 s, temporary until Task 9):
```c
float t, h, p;
if (bme280_read(&t, &h, &p) == 0)
  printf("BME280 t=%.2fC rh=%.2f%% p=%.1fhPa\r\n", t, h, p);
else
  printf("BME280 read failed (err cnt=%u)\r\n", stats_get(ERR_BME_TIMEOUT));
```

- [ ] **Step 3: On-target verification (two physical cases)**

Build + flash. Serial output:
1. Sensor connected: `t≈room temp ±2°C`, `rh` sane, `p≈1013±30 hPa`.
2. **Disconnect SDA wire** → within ~2 s: `BME280 read failed (err cnt=N)` repeating; board keeps running (LED blinks) — Review Focus #1 evidence.

- [ ] **Step 4: Commit**

```bash
git add stm32/drivers/bme280 stm32/app
git commit -m "feat(bme280): I2C driver with 100ms bound, error counters, smoke verified"
```

### Task 7: MPU6050 driver + sensor plausibility checks (host TDD)

**Files:**
- Create: `stm32/drivers/mpu6050/mpu6050.h`, `stm32/drivers/mpu6050/mpu6050.c`
- Create: `stm32/platform/stats/sensor_check.h`, `stm32/platform/stats/sensor_check.c`
- Create: `host/tests/test_sensor_check.c`
- Modify: `host/CMakeLists.txt` (add sensor_check.c to test target sources), `stm32/app/industrial_logger/app_sensors.c`

**Interfaces:**
- Consumes: `stats_error()` (Task 4)
- Produces:
```c
int mpu6050_init(I2C_HandleTypeDef *hi2c);                       // WHO_AM_I==0x68
int mpu6050_read(float *ax_g, float *ay_g, float *az_g);         // ±4g range, ≤100ms
bool sensor_plausible_bme(float t, float h, float p);  // -40..85C, 0..100%, 300..1100hPa
bool sensor_plausible_mpu(float ax, float ay, float az);         // 0.3..2.0 g magnitude
```

- [ ] **Step 1: Write failing host test**

`host/tests/test_sensor_check.c`:
```c
#include "unity.h"
#include "sensor_check.h"
void setUp(void) {} void tearDown(void) {}
void test_bme_valid(void)    { TEST_ASSERT_TRUE(sensor_plausible_bme(23.5f, 45.0f, 1013.0f)); }
void test_bme_temp_range(void){ TEST_ASSERT_FALSE(sensor_plausible_bme(-41.0f, 45.0f, 1013.0f)); }
void test_bme_hum_range(void){ TEST_ASSERT_FALSE(sensor_plausible_bme(25.0f, 101.0f, 1013.0f)); }
void test_mpu_rest(void)     { TEST_ASSERT_TRUE(sensor_plausible_mpu(0.10f, -0.05f, 0.98f)); }
void test_mpu_flat(void)     { TEST_ASSERT_FALSE(sensor_plausible_mpu(5.0f, 0.0f, 0.0f)); }
```

- [ ] **Step 2: Run, verify fail** (same ctest command as Task 4). Expected: FAIL (sensor_check.c missing).

- [ ] **Step 3: Implement `sensor_check.c` + wire into test build** — straightforward range/magnitude checks; add `sensor_check.c` to `host/CMakeLists.txt` sources list. Re-run ctest: 6/6 PASS (reset-cause + sensor-check tests).

- [ ] **Step 4: Implement MPU6050 driver**

init: `WHO_AM_I` (0x75) == 0x68; wake `PWR_MGMT_1=0x00`; `ACCEL_CONFIG=0x08` (±4g), `GYRO_CONFIG=0x08` (±500 dps); read: burst 14B from 0x3B, scale accel `raw/8192.0f`. Timeouts 100 ms → `stats_error(ERR_MPU_TIMEOUT)`. Any out-of-range reading → `stats_error(ERR_SENSOR_RANGE)`, return last-good.

- [ ] **Step 5: On-target verification**

Extend smoke print with MPU line (`ax,ay,az`); board flat on desk → magnitude ≈ 1.0 g ±0.1. Tilt board 90° → az→0, ax→±1. Disconnect sensor → error counter increments, task survives.

- [ ] **Step 6: Commit**

```bash
git add host stm32/drivers/mpu6050 stm32/platform/stats stm32/app
git commit -m "feat(mpu6050): driver + host-tested plausibility checks, last-good cache"
```

### Task 8: SD card + FATFS (mount/write/readback, dirty-card tolerant)

**Files:**
- Create: `stm32/drivers/sdcard/sd_spi.c`, `stm32/drivers/sdcard/sd_spi.h`, `stm32/drivers/sdcard/user_diskio_glue.c`
- Create: `stm32/app/industrial_logger/app_logger.c` (initial), `stm32/platform/config/config.h` (compile-time defaults)
- Modify: `stm32/app/industrial_logger/app_sensors.c` (remove smoke, real task arrives here/Task 9)

**Interfaces:**
- Consumes: CubeMX FATFS (`fatfs.h`), SPI1, PA4 `SD_CS`; `stats_error(ERR_SD_WRITE)` (Task 4)
- Produces:
```c
FRESULT sdcard_mount(void);                        // f_mount + retry-tolerant
FRESULT sdcard_append_line(const char *path, const char *line); // opens FA_OPEN_APPEND, writes, f_sync
```
Config defaults (`config.h`): `LOG_PATH "0:indulog.csv"`, `SAMPLE_PERIOD_MS 500`, `SENSOR_Q_LEN 16`.

- [ ] **Step 1: Implement SPI-SD driver + diskio glue**

`sd_spi.c`: standard SPI-mode SD init (74 dummy clocks CS-high → CMD0 until idle → CMD8 0x1AA → ACMD41(HCS) → CMD58 OCR check) and single-block read(CMD17)/write(CMD24) over SPI1 at 400 kHz init then ≤ 21 MHz; CRC off after CMD0 (CMD59=0). Implement the FATFS glue (`disk_initialize/disk_read/disk_write/disk_ioctl`) in `user_diskio_glue.c` mapping to these functions (this fills the CubeMX `user_diskio.c` stubs — do it via the glue file so CubeMX regen-safe).

- [ ] **Step 2: Mount + write + readback test**

Temporary test in default task (once, on boot):
```c
if (sdcard_mount() == FR_OK) {
  sdcard_append_line(LOG_PATH, "boot,ok");
  /* readback: f_open FA_READ, print first line */
}
```
Build + flash → serial: `sd: mounted, readback: boot,ok`.

- [ ] **Step 3: Pull-card fault injection (Review Focus #2)**

With periodic appends running: physically pull the card → expect `ERR_SD_WRITE` counter increments and **board keeps running** (LED alive); re-insert → next mount attempt succeeds and appends resume.

- [ ] **Step 4: Dirty-card tolerance (Review Focus #4)**

Start an append loop, then **press NRST mid-write** repeatedly (simulating watchdog reset during flash/FATFS activity). After each reset: card mounts, file appends continue (partial last line acceptable — document in code comment that CSV consumers tolerate truncated last line).

- [ ] **Step 5: Commit**

```bash
git add stm32/drivers/sdcard stm32/app stm32/platform/config
git commit -m "feat(sdcard): SPI-SD + FATFS, pull-card and dirty-card fault-injection verified"
```

### Task 9: Three-task integration — Sensor→Queue→Logger + Monitor diagnostics

**Files:**
- Create: `stm32/app/industrial_logger/logger_format.h`, `stm32/app/industrial_logger/logger_format.c`
- Create: `host/tests/test_csv_format.c`
- Create: `stm32/app/industrial_logger/app_monitor.c`
- Modify: `host/CMakeLists.txt` (add logger_format.c to sources), `stm32/app/industrial_logger/app_sensors.c`, `app_logger.c`, `stm32/Core/Src/freertos.c` (task creation in USER CODE)

**Interfaces:**
- Consumes: Tasks 4/6/7/8 APIs
- Produces (end-state for M1):
```c
int logger_format_csv(char *buf, size_t len, uint32_t ts_ms,
                      float t, float h, float p, float ax, float ay, float az);
// writes "ts,temp,hum,press,ax,ay,az\n" (2 decimals), returns chars written
extern QueueHandle_t sensorQ; // struct sensor_sample {uint32_t ts_ms; float t,h,p,ax,ay,az;}
// freertos.c USER CODE: xTaskCreate(SensorTask, "sensor", 512, NULL, 3, NULL);
// xTaskCreate(LoggerTask, "logger", 1024, NULL, 2, NULL);
// xTaskCreate(MonitorTask, "monitor", 512, NULL, 1, NULL);
```
Monitor prints every 5 s: uptime, reset cause, per-task stack high-water (via `uxTaskGetStackHighWaterMark`), heap free/largest (`xPortGetFreeHeapSize`/`xPortGetMinimumEverFreeHeapSize`), all error counters.

- [ ] **Step 1: Write failing CSV formatter test**

`host/tests/test_csv_format.c`:
```c
#include "unity.h"
#include "logger_format.h"
void setUp(void) {} void tearDown(void) {}
void test_row_format(void) {
  char buf[128];
  logger_format_csv(buf, sizeof buf, 1234, 23.45f, 40.12f, 1013.25f, 0.12f, -0.98f, 0.03f);
  TEST_ASSERT_EQUAL_STRING("1234,23.45,40.12,1013.25,0.12,-0.98,0.03\n", buf);
}
```
Run ctest (Task 4 command) → Expected: FAIL.

- [ ] **Step 2: Implement `logger_format.c`** (`snprintf` with `"%" PRIu32 ",%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n"`), add to host build, run ctest → PASS (now 4/4 test executables incl. csv).

- [ ] **Step 3: Wire tasks + queue (drop-newest policy on full: `xQueueSend` with 0 tick timeout → `stats_error(ERR_Q_DROP)`); LoggerTask writes header line once when file is empty (`f_lseek` end==0 → `"ts_ms,temp_c,hum_pct,press_hpa,ax_g,ay_g,az_g\n"`)**

- [ ] **Step 4: On-target functional check**

Serial: 2 Hz samples land in `indulog.csv` (verify by re-inserting card in PC reader); Monitor block prints every 5 s with sane values; stack watermarks ≥ 40 words reported (if any task < 40, increase its stack by 256 words and re-verify).

- [ ] **Step 5: Queue-overflow stress check (Review Focus #5)**

Temporarily set `SAMPLE_PERIOD_MS 10` (logger disk latency > sample period) → expect `ERR_Q_DROP` increments in Monitor output, sensor task never blocks; restore 500.

- [ ] **Step 6: Commit**

```bash
git add host stm32/app stm32/Core
git commit -m "feat(app): csv format (host-TDD), sensor->queue->logger pipeline, monitor task"
```

### Task 10: DWT + GPIO instrumentation (evidence infra per spec §3)

**Files:**
- Create: `stm32/platform/diag/diag.h`, `stm32/platform/diag/diag.c`
- Modify: `app_sensors.c` (measure I2C read duration), `Core/Src/stm32f4xx_it.c` USER CODE (SysTick? no — measure in task context)

**Interfaces:**
- Produces:
```c
void diag_dwt_init(void);                    // DEMCR TRCENA, CYCCNT=0, enable
static inline uint32_t diag_cycles(void);    // DWT->CYCCNT
#define DIAG_PULSE(gpio, pin) /* high->low 8-cycle marker for scope */
void diag_bench_gpio_set(int n, bool on);    // BENCH_GPIO1/2
```

- [ ] **Step 1: Implement + sample usage**

Measure `bme280_read` duration: `uint32_t t0=diag_cycles(); ...; uint32_t us=(diag_cycles()-t0)/100;` (100 MHz → 100 cycles/µs) printed by Monitor: `i2c read: 312 us (p50/p99 histogram deferred to M4)`.

- [ ] **Step 2: Scope verification (physical)**

CH1 on `BENCH_GPIO1` around an I2C transaction: visible pulse framing each read; cycle-count printed value within ±10% of scope-measured pulse width.

- [ ] **Step 3: Commit**

```bash
git add stm32/platform/diag stm32/app
git commit -m "feat(diag): DWT cycle counter + benchmark GPIOs, I2C latency measured"
```

### Task 11: Milestone close-out — docs, tag, CI green

**Files:**
- Modify: `README.md` (status line: M1 done), `docs/notes/toolchain.md` (final versions), create `docs/validation/README.md` (evidence index, placeholder page for W4 — lists planned artifacts, no fake data)

**Interfaces:** none (docs only)

- [ ] **Step 1: Update docs** — README status: "M1 skeleton verified on hardware"; validation index lists: I2C-bounded failures (Task 6), plausibility tests (Task 7), pull-card/dirty-card (Task 8), queue stress (Task 9), DWT latency (Task 10) — each with one-line result and how it was produced.

- [ ] **Step 2: Full clean rebuild + ctest locally, then push** — `cmake --build stm32/build --clean-first && ctest --test-dir host/build` → all pass; `git push` → CI green (Actions tab).

- [ ] **Step 3: Tag milestone** — `git tag -a m1-skeleton -m "STM32 skeleton: sensors+SD+diag+watchdog, CI green" && git push --tags`

- [ ] **Step 4: Commit**

```bash
git add README.md docs
git commit -m "docs: M1 close-out - validation index, toolchain versions, status"
```

---

## M1 Exit Criteria (maps to spec §14 W1 row)

- [ ] Firmware boots at 100 MHz, LED heartbeat, banner over VCP
- [ ] BME280 + MPU6050 sampled 2 Hz via I2C (bounded 100 ms, errors counted)
- [ ] CSV rows land in `0:indulog.csv` on SD (FATFS), tolerant to pull-card & dirty reset
- [ ] Monitor prints uptime/reset-cause/stack-watermarks/heap/counters every 5 s
- [ ] IWDG live (force-reset produces `reset cause: iwdg`)
- [ ] DWT latency + BENCH GPIO verified with scope
- [ ] Host tests (reset-cause, plausibility, CSV) + cross build green in CI
- [ ] All 5 Review Focus failure modes exercised with recorded evidence
