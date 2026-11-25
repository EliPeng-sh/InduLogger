// InduLogger ESP32-S3 side — M2.2 bring-up smoke: identity + N16R8 size proof.
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"

#include "link_task.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "InduLogger ESP M2.2 boot (idf %s)", IDF_VER);

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "chip: model=%s revision=%d.%d cores=%d",
             (chip.model == CHIP_ESP32S3) ? "ESP32-S3" : "other",
             chip.revision % 100, chip.revision / 100, chip.cores);

    uint32_t flash_sz = 0;
    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &flash_sz));
    ESP_LOGI(TAG, "flash: %" PRIu32 " MB", flash_sz / 1024 / 1024);

    size_t psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "psram: %u KB total", (unsigned)(psram / 1024));

    link_task_start();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
