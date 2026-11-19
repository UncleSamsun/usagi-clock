#include "sdcard.h"

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd";

#define SD_HOST          SPI3_HOST
#define SD_FREQ_KHZ      20000          /* [제안] 배선이 길어 오류가 나면 10000 */
#define SD_RETRY_US      (30 * 1000000LL)

static sdcard_pins_t s_pins;
static sdmmc_card_t *s_card;
static bool s_mounted;
static bool s_bus_ok;
static int64_t s_last_try_us = -SD_RETRY_US;
static SemaphoreHandle_t s_lock;

static void try_mount(void)
{
    s_last_try_us = esp_timer_get_time();

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_HOST;
    host.max_freq_khz = SD_FREQ_KHZ;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = s_pins.cs;
    slot.host_id = SD_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,   /* 사용자 파일이 있다: 절대 포맷하지 않는다 */
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot, &mount, &s_card);
    if (err == ESP_OK) {
        s_mounted = true;
        ESP_LOGI(TAG, "mounted: %s, %llu MB", s_card->cid.name,
                 (unsigned long long)((uint64_t)s_card->csd.capacity * s_card->csd.sector_size / (1024 * 1024)));
    } else {
        s_mounted = false;
        ESP_LOGW(TAG, "no card or mount failed: %s", esp_err_to_name(err));
    }
}

esp_err_t sdcard_start(const sdcard_pins_t *pins)
{
    s_pins = *pins;
    s_lock = xSemaphoreCreateMutex();

    spi_bus_config_t bus = {
        .sclk_io_num = pins->sck,
        .mosi_io_num = pins->mosi,
        .miso_io_num = pins->miso,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(SD_HOST, &bus, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi bus: %s", esp_err_to_name(err));
        return err;
    }
    s_bus_ok = true;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    try_mount();
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

bool sdcard_ensure_mounted(void)
{
    if (!s_bus_ok) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_mounted && esp_timer_get_time() - s_last_try_us >= SD_RETRY_US) {
        try_mount();
    }
    bool ok = s_mounted;
    xSemaphoreGive(s_lock);
    return ok;
}

bool sdcard_is_mounted(void)
{
    return s_mounted;
}

void sdcard_mark_failed(void)
{
    if (!s_bus_ok) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_mounted) {
        ESP_LOGW(TAG, "read error: unmounting, will retry");
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
        s_card = NULL;
        s_mounted = false;
        s_last_try_us = esp_timer_get_time();
    }
    xSemaphoreGive(s_lock);
}

static size_t list_entries(const char *dir, char names[][AUDIO_NAME_MAX], size_t max, bool want_dirs)
{
    if (!s_mounted) {
        return 0;
    }
    DIR *d = opendir(dir);
    if (d == NULL) {
        return 0;
    }
    size_t n = 0;
    struct dirent *e;
    while (n < max && (e = readdir(d)) != NULL) {
        bool is_dir = e->d_type == DT_DIR;
        if (is_dir != want_dirs || !audio_name_ok(e->d_name)) {
            continue;
        }
        if (!want_dirs && !audio_is_mp3_name(e->d_name)) {
            continue;
        }
        strncpy(names[n], e->d_name, AUDIO_NAME_MAX - 1);
        names[n][AUDIO_NAME_MAX - 1] = '\0';
        n++;
    }
    closedir(d);
    audio_sort_names(names, n);
    return n;
}

size_t sdcard_list_mp3(const char *dir, char names[][AUDIO_NAME_MAX], size_t max)
{
    return list_entries(dir, names, max, false);
}

size_t sdcard_list_dirs(const char *dir, char names[][AUDIO_NAME_MAX], size_t max)
{
    return list_entries(dir, names, max, true);
}

bool sdcard_file_exists(const char *path)
{
    struct stat st;
    return s_mounted && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool sdcard_space(uint64_t *total, uint64_t *free_bytes)
{
    if (!s_mounted) {
        return false;
    }
    return esp_vfs_fat_info(SD_MOUNT_POINT, total, free_bytes) == ESP_OK;
}
