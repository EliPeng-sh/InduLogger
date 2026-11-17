#include "app/industrial_logger/app_logger.h"
#include "app/industrial_logger/app_sensors.h"
#include "app/industrial_logger/logger_format.h"
#include "platform/config/config.h"
#include "platform/stats/stats.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"

static int s_mounted;
static int s_fil_open; /* s_fil holds an open FATFS lock entry (open-once pattern) */
static const char *s_log_path = LOG_PATH; /* active file (fallback switch) */
static int s_fallback_tried;              /* one-shot primary->fallback switch */
static int s_sd_gave_up;                  /* FS lesion: stop SD attempts until reset */

static void csv_header_once(void); /* defined below; used by sdcard_open_file */

/* _FS_TINY=0: each FIL embeds a 512-byte sector buffer. Keep them in BSS, not
 * on the defaultTask stack (2 KB; f_open also takes a 512-byte LFN buffer
 * transiently under _USE_LFN=2). The defaultTask uses these strictly
 * sequentially — boot before the loop, tick inside it, never nested. */
static FIL s_fil;

FRESULT sdcard_mount(void)
{
  /* force=1: run disk_initialize now so failures surface here */
  return f_mount(&USERFatFS, USERPath, 1);
}

/* Open the long-lived row file with a one-shot fallback: when the primary
 * file's FS structures are corrupt (board-evidenced FR_INT_ERR from a corrupted
 * directory chain on the old card — mount and sector reads still work, so the
 * lesion never reaches the glue layer and nothing self-heals), switch once to
 * FALLBACK_LOG_PATH. A third strike (fallback also damaged / root sector bad)
 * sets give-up state: the task keeps heartbeating and samples degrade to
 * ERR_Q_DROP — logging is best-effort, the board stays alive until reset. */
static FRESULT sdcard_open_file(void)
{
  if (s_fil_open) {
    return FR_OK;
  }
  if (s_sd_gave_up) {
    return FR_INVALID_OBJECT; /* caller keeps board alive, no SD spin */
  }

  FRESULT fr = f_open(&s_fil, s_log_path, FA_OPEN_APPEND | FA_WRITE | FA_READ);
  if (fr != FR_OK && !s_fallback_tried &&
      (fr == FR_INT_ERR || fr == FR_DISK_ERR || fr == FR_INVALID_OBJECT)) {
    s_fallback_tried = 1;
    s_log_path = FALLBACK_LOG_PATH;
    printf("sd: open failed (%d, err cnt=%u) - switching to %s\r\n", (int)fr,
           (unsigned)stats_get(ERR_SD_WRITE), FALLBACK_LOG_PATH);
    fr = f_open(&s_fil, FALLBACK_LOG_PATH, FA_OPEN_APPEND | FA_WRITE | FA_READ);
  }
  if (fr != FR_OK) {
    s_sd_gave_up = 1;
    printf("sd: open failed (%d, err cnt=%u) - giving up until reset\r\n",
           (int)fr, (unsigned)stats_get(ERR_SD_WRITE));
    return fr;
  }

  s_fil_open = 1;
  csv_header_once(); /* header if the file is fresh; leaves fptr at end */
  return FR_OK;
}

FRESULT sdcard_append_line(const char *path, const char *line)
{
  UINT n = 0;
  size_t len = strlen(line);
  FRESULT fr;

  (void)path; /* rows append to whichever file sdcard_open_file() holds open */
  if (!s_fil_open) {
    fr = sdcard_open_file();
    if (fr != FR_OK) {
      return fr;
    }
  }

  fr = f_write(&s_fil, line, (UINT)len, &n);
  if (fr == FR_OK && n != (UINT)len) {
    fr = FR_DISK_ERR;
  }
  if (fr == FR_OK) {
    UINT m = 0;
    fr = f_write(&s_fil, "\n", 1u, &m);
    if (fr == FR_OK && m != 1u) {
      fr = FR_DISK_ERR;
    }
  }
  /* f_sync per row so pulled-card / dirty-card tests lose at most the open
   * line; CSV consumers tolerate a truncated last line (a reset mid-write can
   * only truncate the line being written, never corrupt earlier lines). */
  if (f_sync(&s_fil) != FR_OK && fr == FR_OK) {
    fr = FR_DISK_ERR;
  }
  return fr;
}

/* Close the long-lived row file (mount teardown path). Best effort: a failed
 * close (f_sync error path) leaves the lock entry behind, but the caller
 * unmounts right after and f_mount's clear_lock() reclaims it. */
static void sdcard_close_file(void)
{
  if (s_fil_open) {
    (void)f_close(&s_fil);
    s_fil_open = 0;
  }
}

void app_logger_boot(void)
{
  FRESULT fr = sdcard_mount();
  if (fr != FR_OK) {
    printf("sd: mount failed (%d, err cnt=%u)\r\n", (int)fr,
           (unsigned)stats_get(ERR_SD_WRITE));
    return;
  }
  s_mounted = 1;

  fr = sdcard_open_file(); /* open-once + header if fresh file + fallback switch */
  if (fr != FR_OK) {
    /* sdcard_open_file already printed the transition (switch/give-up) */
    s_mounted = 0;
    (void)f_mount(NULL, USERPath, 0);
    return;
  }

  fr = sdcard_append_line(LOG_PATH, "boot,ok");
  if (fr != FR_OK) {
    printf("sd: append failed (%d, err cnt=%u)\r\n", (int)fr,
           (unsigned)stats_get(ERR_SD_WRITE));
    s_mounted = 0;
    sdcard_close_file();
    (void)f_mount(NULL, USERPath, 0);
    return;
  }

  /* readback evidence: print the first line of LOG_PATH, then park the file
   * pointer back at end-of-file so rows continue appending */
  if (f_lseek(&s_fil, 0) == FR_OK) {
    char line[24];
    if (f_gets(line, (int)sizeof line, &s_fil) != NULL) {
      size_t L = strlen(line);
      if (L > 0u && (line[L - 1u] == '\n' || line[L - 1u] == '\r')) {
        line[L - 1u] = '\0';
      }
      printf("sd: mounted, readback: %s\r\n", line);
    } else {
      printf("sd: mounted, readback: <empty>\r\n");
    }
  } else {
    printf("sd: mounted, readback: <seek failed>\r\n");
  }
  (void)f_lseek(&s_fil, f_size(&s_fil));
}

/* Write the CSV header if the file is empty. Called right after the row file
 * is opened on a fresh mount (open-once pattern — see sdcard_append_line).
 * Reuses the long-lived s_fil (same task, strictly sequential, never nested). */
static void csv_header_once(void)
{
  static const char hdr[] = "ts_ms,temp_c,hum_pct,press_hpa,ax_g,ay_g,az_g\n";

  if (f_size(&s_fil) == 0u) {
    UINT n = 0;
    (void)f_write(&s_fil, hdr, (UINT)(sizeof hdr - 1u), &n);
    (void)f_sync(&s_fil);
    (void)f_lseek(&s_fil, f_size(&s_fil)); /* rows append after the header */
  }
}

void app_logger_task(void *argument)
{
  (void)argument;
  app_logger_boot(); /* mount + append + readback evidence (once, T8) */
  csv_header_once();

  for (;;) {
    sensor_sample_t s;
    /* Unmounted: poll the card once a second (mount retry cadence).
     * Mounted: drain rows as they arrive (250 ms cap so the heartbeat
     * continues even with an idle queue). */
    TickType_t wait = s_mounted ? pdMS_TO_TICKS(250) : pdMS_TO_TICKS(1000);
    BaseType_t got = xQueueReceive(sensorQ, &s, wait);

    if (s_mounted) {
      if (got == pdTRUE) {
        /* 96 B fits the longest row (ts 10 + 6x7 + 6 commas + LF = 59):
         * logger_format_csv truncation (-1) is unreachable here. */
        char line[96];
        int n = logger_format_csv(line, sizeof line, s.ts_ms, s.t, s.h, s.p,
                                  s.ax, s.ay, s.az);
        if (n >= 0) {
          FRESULT fr = sdcard_append_line(LOG_PATH, line);
          if (fr != FR_OK) {
            printf("sd: append failed (%d, err cnt=%u)\r\n", (int)fr,
                   (unsigned)stats_get(ERR_SD_WRITE));
            s_mounted = 0;
            sdcard_close_file(); /* release the lock entry (best effort) */
            (void)f_mount(NULL, USERPath, 0); /* force fresh init; clear_lock
                                                 reclaims any leaked entry */
          }
        }
      }
    } else if (!s_sd_gave_up && sdcard_mount() == FR_OK) {
      s_mounted = 1;
      /* transition print: recovery from a pulled/failed card is observable */
      printf("sd: re-mounted after failure (err cnt=%u)\r\n",
             (unsigned)stats_get(ERR_SD_WRITE));
      if (sdcard_open_file() != FR_OK) {
        s_mounted = 0; /* open failed (or gave up): no spin, heartbeats continue */
        (void)f_mount(NULL, USERPath, 0);
      }
    }

    wd_heartbeat(WD_HB_LOGGER); /* every iteration, append or not */
  }
}
