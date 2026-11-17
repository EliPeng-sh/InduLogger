#ifndef PLATFORM_CONFIG_H
#define PLATFORM_CONFIG_H

/* Compile-time defaults shared by the app tasks (Task 8/9 of the M1 plan). */

#define LOG_PATH          "0:indulog.csv" /* volume 0 = CubeMX USER diskio driver */
#define FALLBACK_LOG_PATH "0:indulog2.csv" /* used when the primary file's FS
                                           * directory chain is corrupt (board-
                                           * evidenced FR_INT_ERR, T9 Step 6) */
#define SAMPLE_PERIOD_MS  500u           /* sensor sampling period (Task 9) */
#define SENSOR_Q_LEN      16u             /* sensor->logger queue depth (Task 9) */

#endif /* PLATFORM_CONFIG_H */
