#include "app/industrial_logger/app_logger.h"
#include "platform/config/config.h"
#include "platform/stats/stats.h"

#include <stdio.h>
#include <string.h>

static int s_mounted;
static uint32_t s_seq;

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

FRESULT sdcard_append_line(const char *path, const char *line)
{
  UINT n = 0;
  size_t len = strlen(line);

  FRESULT fr = f_open(&s_fil, path, FA_OPEN_APPEND | FA_WRITE);
  if (fr != FR_OK) {
    return fr;
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
  /* f_sync so pulled-card / dirty-card tests lose at most the open line;
   * CSV consumers tolerate a truncated last line (a reset mid-write can
   * only truncate the line being written, never corrupt earlier lines). */
  if (f_sync(&s_fil) != FR_OK && fr == FR_OK) {
    fr = FR_DISK_ERR;
  }
  (void)f_close(&s_fil);
  return fr;
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

  fr = sdcard_append_line(LOG_PATH, "boot,ok");
  if (fr != FR_OK) {
    printf("sd: append failed (%d, err cnt=%u)\r\n", (int)fr,
           (unsigned)stats_get(ERR_SD_WRITE));
    s_mounted = 0;
    return;
  }

  /* readback evidence: print the first line of LOG_PATH */
  fr = f_open(&s_fil, LOG_PATH, FA_READ);
  if (fr != FR_OK) {
    printf("sd: readback open failed (%d)\r\n", (int)fr);
    return;
  }
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
  (void)f_close(&s_fil);
}

void app_logger_tick(void)
{
  if (!s_mounted) {
    FRESULT mfr = sdcard_mount();
    if (mfr != FR_OK) {
      return; /* no/failed card: retry next tick (SPI layer counts) */
    }
    s_mounted = 1;
    /* transition print: recovery from a pulled/failed card is observable */
    printf("sd: re-mounted after failure (err cnt=%u)\r\n",
           (unsigned)stats_get(ERR_SD_WRITE));
  }

  char line[24];
  (void)snprintf(line, (int)sizeof line, "tick,%lu", (unsigned long)++s_seq);

  FRESULT fr = sdcard_append_line(LOG_PATH, line);
  if (fr != FR_OK) {
    printf("sd: append failed (%d, err cnt=%u)\r\n", (int)fr,
           (unsigned)stats_get(ERR_SD_WRITE));
    s_mounted = 0;
    (void)f_mount(NULL, USERPath, 0); /* force a fresh init next tick */
  }
}
