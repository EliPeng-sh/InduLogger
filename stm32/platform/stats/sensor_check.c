#include "sensor_check.h"

bool sensor_plausible_bme(float t_c, float h_pct, float p_hpa)
{
  return t_c >= -40.0f && t_c <= 85.0f &&
         h_pct >= 0.0f && h_pct <= 100.0f &&
         p_hpa >= 300.0f && p_hpa <= 1100.0f;
}

bool sensor_plausible_mpu(float ax_g, float ay_g, float az_g)
{
  float mag2 = ax_g * ax_g + ay_g * ay_g + az_g * az_g;
  return mag2 >= 0.09f && mag2 <= 4.0f;   /* magnitude 0.3..2.0 g (squared, no sqrt) */
}
