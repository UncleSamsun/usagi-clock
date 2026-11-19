#include "display_ui.h"

#include <stdio.h>
#include <string.h>

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ui_text.h"

static const char *TAG = "display";

#define LCD_HOST        SPI2_HOST
#define LCD_PIXEL_HZ    (40 * 1000 * 1000)
#define LCD_H_RES       320   /* 가로로 눕혀 쓴다 [제안] */
#define LCD_V_RES       240
#define LCD_BUF_LINES   40
#define BL_LEDC_TIMER   LEDC_TIMER_0
#define BL_LEDC_CHANNEL LEDC_CHANNEL_0
#define BL_DUTY_MAX     1023  /* 10비트 */

/* [실물 확인] 패널마다 색 반전·좌우 방향이 다를 수 있다. 실기에서 맞춘 값을 여기에 적는다 */
#define LCD_INVERT_COLOR false
#define LCD_MIRROR_X     false
#define LCD_MIRROR_Y     false

static QueueHandle_t s_state_q;
static uint8_t s_brightness = 0xFF;   /* 아직 설정 전 */

static lv_obj_t *s_time_lbl, *s_ampm_lbl, *s_date_lbl, *s_info_lbl, *s_title_lbl, *s_wifi_lbl;

static esp_err_t backlight_init(int gpio)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BL_LEDC_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer");
    ledc_channel_config_t ch = {
        .gpio_num = gpio,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_LEDC_CHANNEL,
        .timer_sel = BL_LEDC_TIMER,
        .duty = 0,
    };
    return ledc_channel_config(&ch);
}

void display_ui_set_brightness(uint8_t pct)
{
    if (pct > 100) {
        pct = 100;
    }
    if (pct == s_brightness) {
        return;
    }
    s_brightness = pct;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, (uint32_t)pct * BL_DUTY_MAX / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
}

static void build_screen(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_text_color(scr, lv_color_white(), 0);

    s_title_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(s_title_lbl, &lv_font_montserrat_24, 0);
    lv_obj_align(s_title_lbl, LV_ALIGN_TOP_MID, 0, 16);

    s_wifi_lbl = lv_label_create(scr);
    lv_obj_align(s_wifi_lbl, LV_ALIGN_TOP_RIGHT, -8, 8);

    s_time_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(s_time_lbl, &lv_font_montserrat_48, 0);
    lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, -24, -16);

    s_ampm_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(s_ampm_lbl, &lv_font_montserrat_24, 0);

    s_date_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(s_date_lbl, &lv_font_montserrat_24, 0);
    lv_obj_align(s_date_lbl, LV_ALIGN_CENTER, 0, 36);

    s_info_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(s_info_lbl, &lv_font_montserrat_24, 0);
    lv_obj_align(s_info_lbl, LV_ALIGN_BOTTOM_MID, 0, -16);
}

static void fmt_clock_ampm(int64_t local, char *out, size_t n)
{
    char hm[8];
    bool pm = false;
    ui_fmt_clock(local, hm, sizeof(hm), &pm);
    snprintf(out, n, "%s %s", hm, pm ? "PM" : "AM");
}

static void render_wifi(ui_wifi_t w)
{
    switch (w) {
    case UI_WIFI_CONNECTED:  lv_label_set_text(s_wifi_lbl, LV_SYMBOL_WIFI); break;
    case UI_WIFI_CONNECTING: lv_label_set_text(s_wifi_lbl, LV_SYMBOL_WIFI " ..."); break;
    case UI_WIFI_AP:         lv_label_set_text(s_wifi_lbl, "AP"); break;
    case UI_WIFI_NONE:
    default:                 lv_label_set_text(s_wifi_lbl, ""); break;
    }
}

/* LVGL 잠금 안에서만 부른다 */
static void render(const ui_state_t *s)
{
    char buf[96];

    render_wifi(s->wifi);

    if (!s->time_known) {
        lv_label_set_text(s_title_lbl, s->mode == UI_MODE_SETUP ? "SETUP MODE" : "");
        lv_label_set_text(s_time_lbl, "--:--");
        lv_label_set_text(s_ampm_lbl, "");
        lv_label_set_text(s_date_lbl, "SET TIME");
        lv_label_set_text(s_info_lbl, s->mode == UI_MODE_SETUP ? s->line1 : "");
        lv_obj_align_to(s_ampm_lbl, s_time_lbl, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -6);
        return;
    }

    bool pm = false;
    ui_fmt_clock(s->now_local, buf, sizeof(buf), &pm);
    lv_label_set_text(s_time_lbl, buf);
    lv_label_set_text(s_ampm_lbl, pm ? "PM" : "AM");
    lv_obj_align_to(s_ampm_lbl, s_time_lbl, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -6);
    ui_fmt_date(s->now_local, buf, sizeof(buf));
    lv_label_set_text(s_date_lbl, buf);

    char t[16];
    switch (s->mode) {
    case UI_MODE_PRE:
        lv_label_set_text(s_title_lbl, "USAGI!");
        ui_fmt_countdown(s->ring_at - s->now_local, buf, sizeof(buf));
        lv_label_set_text(s_info_lbl, buf);
        break;
    case UI_MODE_RINGING:
        lv_label_set_text(s_title_lbl, s->label[0] != '\0' ? s->label : "ALARM");
        lv_label_set_text(s_info_lbl, "HOLD TO STOP");
        break;
    case UI_MODE_SNOOZED:
        fmt_clock_ampm(s->snooze_until, t, sizeof(t));
        lv_label_set_text(s_title_lbl, "SNOOZE");
        snprintf(buf, sizeof(buf), "UNTIL %s", t);
        lv_label_set_text(s_info_lbl, buf);
        break;
    case UI_MODE_SETUP:
        lv_label_set_text(s_title_lbl, "SETUP MODE");
        snprintf(buf, sizeof(buf), "%s\n%s", s->line1, s->line2);
        lv_label_set_text(s_info_lbl, buf);
        break;
    case UI_MODE_CLOCK:
    default:
        lv_label_set_text(s_title_lbl, "");
        if (s->next_alarm_local >= 0) {
            fmt_clock_ampm(s->next_alarm_local, t, sizeof(t));
            snprintf(buf, sizeof(buf), "NEXT %s", t);
            lv_label_set_text(s_info_lbl, buf);
        } else {
            lv_label_set_text(s_info_lbl, "NO ALARM");
        }
        break;
    }
}

static void ui_task(void *arg)
{
    (void)arg;
    ui_state_t s;
    for (;;) {
        if (xQueueReceive(s_state_q, &s, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (lvgl_port_lock(0)) {
                render(&s);
                lvgl_port_unlock();
            }
        }
    }
}

void display_ui_post(const ui_state_t *s)
{
    if (s_state_q != NULL) {
        xQueueOverwrite(s_state_q, s);
    }
}

/* ILI9341 RDDID(0x04)를 읽어 로그로 남긴다(parts-list E.7). 모듈에 따라 SDO가 연결되지 않아 0이 나올 수 있다 */
static void log_panel_id(esp_lcd_panel_io_handle_t io)
{
    uint8_t id[4] = {0};
    if (esp_lcd_panel_io_rx_param(io, 0x04, id, sizeof(id)) == ESP_OK) {
        ESP_LOGI(TAG, "RDDID: %02x %02x %02x %02x", id[0], id[1], id[2], id[3]);
    } else {
        ESP_LOGW(TAG, "RDDID read failed");
    }
}

esp_err_t display_ui_start(const display_ui_pins_t *p)
{
    ESP_RETURN_ON_ERROR(backlight_init(p->bl), TAG, "backlight");

    spi_bus_config_t bus = {
        .sclk_io_num = p->sclk,
        .mosi_io_num = p->mosi,
        .miso_io_num = p->miso,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_BUF_LINES * 2,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = p->dc,
        .cs_gpio_num = p->cs,
        .pclk_hz = LCD_PIXEL_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io), TAG, "panel io");
    log_panel_id(io);

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = p->rst,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &panel_cfg, &panel), TAG, "panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "panel init");
    esp_lcd_panel_invert_color(panel, LCD_INVERT_COLOR);
    esp_lcd_panel_disp_on_off(panel, true);

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "lvgl port");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = LCD_H_RES * LCD_BUF_LINES,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = true,   /* 세로 패널(240×320)을 가로로 */
            .mirror_x = LCD_MIRROR_X,
            .mirror_y = LCD_MIRROR_Y,
        },
        .flags = {
            .buff_dma = true,      /* SPI DMA는 내부 RAM이 빠르다 */
            .swap_bytes = true,
        },
    };
    if (lvgl_port_add_disp(&disp_cfg) == NULL) {
        return ESP_FAIL;
    }

    s_state_q = xQueueCreate(1, sizeof(ui_state_t));
    if (s_state_q == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (lvgl_port_lock(0)) {
        build_screen();
        ui_state_t blank = { .time_known = false, .next_alarm_local = -1 };
        render(&blank);
        lvgl_port_unlock();
    }

    display_ui_set_brightness(80);
    if (xTaskCreate(ui_task, "ui", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
