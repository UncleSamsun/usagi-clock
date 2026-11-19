#include "esp_log.h"

#include "alarm_core.h"
#include "board_pins.h"
#include "button_fsm.h"

static const char *TAG = "usagi";

static void log_pins(void)
{
    ESP_LOGI(TAG, "pins: LCD sclk=%d mosi=%d miso=%d cs=%d dc=%d rst=%d bl=%d",
             PIN_LCD_SCLK, PIN_LCD_MOSI, PIN_LCD_MISO, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST, PIN_LCD_BL);
    ESP_LOGI(TAG, "pins: SD sck=%d mosi=%d miso=%d cs=%d", PIN_SD_SCK, PIN_SD_MOSI, PIN_SD_MISO, PIN_SD_CS);
    ESP_LOGI(TAG, "pins: I2C sda=%d scl=%d", PIN_I2C_SDA, PIN_I2C_SCL);
    ESP_LOGI(TAG, "pins: I2S bclk=%d ws=%d dout=%d", PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT);
    ESP_LOGI(TAG, "pins: button=%d", PIN_BUTTON);
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot (max alarms: %d)", ALARM_MAX);
    log_pins();
}
