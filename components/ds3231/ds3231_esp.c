#include "ds3231.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "ds3231";

#define DS3231_I2C_HZ      100000   /* 로직 분석기로 보기 쉬운 100kHz (DS3231은 400kHz까지 지원) */
#define DS3231_TIMEOUT_MS  100

struct ds3231 {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
};

static struct ds3231 s_dev;   /* 시계에는 RTC가 하나뿐이다 */

esp_err_t ds3231_init(int sda_gpio, int scl_gpio, ds3231_t **out)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,   /* 모듈에도 풀업이 있지만 배선이 빠졌을 때를 대비 */
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_dev.bus), TAG, "i2c bus");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_I2C_ADDR,
        .scl_speed_hz = DS3231_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_dev.bus, &dev_cfg, &s_dev.dev), TAG, "i2c device");

    *out = &s_dev;
    return ESP_OK;
}

/* 레지스터 주소 1바이트를 쓰고(반복 START) n바이트를 읽는다 */
static esp_err_t read_regs(ds3231_t *d, uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(d->dev, &reg, 1, buf, n, DS3231_TIMEOUT_MS);
}

esp_err_t ds3231_read_utc(ds3231_t *d, int64_t *utc_s)
{
    uint8_t status = 0;
    ESP_RETURN_ON_ERROR(read_regs(d, DS3231_REG_STATUS, &status, 1), TAG, "read status");
    if (!ds3231_status_time_valid(status)) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t regs[7];
    ESP_RETURN_ON_ERROR(read_regs(d, DS3231_REG_TIME, regs, sizeof(regs)), TAG, "read time");
    if (!ds3231_decode(regs, utc_s)) {
        ESP_LOGW(TAG, "bad registers %02x %02x %02x %02x %02x %02x %02x",
                 regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6]);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t ds3231_write_utc(ds3231_t *d, int64_t utc_s)
{
    uint8_t buf[8];
    buf[0] = DS3231_REG_TIME;
    if (!ds3231_encode(utc_s, &buf[1])) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(i2c_master_transmit(d->dev, buf, sizeof(buf), DS3231_TIMEOUT_MS), TAG, "write time");

    /* 시각을 새로 썼으니 "발진이 멈춘 적 있음" 표시를 지운다(다른 비트는 유지) */
    uint8_t status = 0;
    ESP_RETURN_ON_ERROR(read_regs(d, DS3231_REG_STATUS, &status, 1), TAG, "read status");
    uint8_t wr[2] = { DS3231_REG_STATUS, (uint8_t)(status & ~DS3231_STATUS_OSF) };
    return i2c_master_transmit(d->dev, wr, sizeof(wr), DS3231_TIMEOUT_MS);
}
