#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "display.h"
#include "status_ui.h"
#include "ble_hid.h"

static const char *TAG = "claude_remote";

using namespace cr;

extern "C" void app_main(void)
{
    /* PWR button (GPIO6) sense — the latch itself is grabbed in displayInit */
    gpio_config_t pwrKey = {};
    pwrKey.mode = GPIO_MODE_INPUT;
    pwrKey.pull_up_en = GPIO_PULLUP_ENABLE;
    pwrKey.pin_bit_mask = 1ULL << GPIO_NUM_6;
    gpio_config(&pwrKey);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    displayInit();
    statusUiCreate(); /* onboarding screen shows first */

    ESP_LOGI(TAG, "== CLAUDE REMOTE == touch + BLE");
    if (!bleHidInit()) {
        statusUiSetEvent("bluetooth init failed");
    }

    bool wasConnected = false;
    uint32_t heldSince = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        bool connected = bleHidConnected();
        if (connected != wasConnected) {
            wasConnected = connected;
            statusUiSetConnected(connected);
            if (connected) statusUiSetStatus(ST_READY);
            statusUiSetEvent(connected ? "connected" : "connection lost - re-pairing");
        }

        /* hold PWR ~3s -> release power latch (battery: full power off) */
        bool pressed = gpio_get_level(GPIO_NUM_6) == 0;
        if (!pressed) {
            heldSince = 0;
        } else if (heldSince == 0) {
            heldSince = now;
        } else if (now - heldSince > 3000) {
            statusUiSetEvent("POWER OFF");
            vTaskDelay(pdMS_TO_TICKS(300));
            gpio_set_level(GPIO_NUM_7, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            heldSince = 0;
        }
    }
}
