#include "button_fsm.h"

#include <stdatomic.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "button";

#define BUTTON_PERIOD_MS 10

static int s_gpio;
static button_cb_t s_cb;
static void *s_ctx;
static atomic_uint s_long_ms;
static button_fsm_t s_fsm;

static void button_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        uint32_t want = atomic_load(&s_long_ms);
        if (want != s_fsm.long_ms && !s_fsm.pressed) {
            s_fsm.long_ms = want;   /* 누르는 중에는 바꾸지 않는다 */
        }

        bool pressed = gpio_get_level(s_gpio) == 0;
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        button_event_t ev = button_fsm_update(&s_fsm, pressed, now_ms);
        if (ev != BUTTON_EV_NONE) {
            ESP_LOGI(TAG, "%s", ev == BUTTON_EV_SHORT ? "SHORT" : "LONG");
            if (s_cb != NULL) {
                s_cb(ev, s_ctx);
            }
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(BUTTON_PERIOD_MS));
    }
}

bool button_gpio_raw_pressed(int gpio)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&cfg);
    return gpio_get_level(gpio) == 0;
}

void button_gpio_set_long_ms(uint32_t long_ms)
{
    atomic_store(&s_long_ms, long_ms);
}

esp_err_t button_gpio_start(int gpio, uint32_t long_ms, button_cb_t cb, void *ctx)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio");

    s_gpio = gpio;
    s_cb = cb;
    s_ctx = ctx;
    atomic_store(&s_long_ms, long_ms);
    button_fsm_init(&s_fsm, BUTTON_DEBOUNCE_MS, long_ms);

    if (xTaskCreate(button_task, "button", 3072, NULL, 9, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
