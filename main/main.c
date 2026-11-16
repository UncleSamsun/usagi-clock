#include "esp_log.h"

#include "alarm_core.h"
#include "button_fsm.h"

static const char *TAG = "usagi";

void app_main(void)
{
    ESP_LOGI(TAG, "boot (max alarms: %d)", ALARM_MAX);
}
