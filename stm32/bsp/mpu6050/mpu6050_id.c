#include "bsp/mpu6050/mpu6050_id.h"

int mpu_id_accepted(uint8_t who_am_i)
{
  switch (who_am_i) {
  case 0x68u: /* genuine MPU6050 */
  case 0x70u: /* GY-521 bench board (MPU6500 clone die), board-measured */
    return 1;
  default:
    return 0;
  }
}
