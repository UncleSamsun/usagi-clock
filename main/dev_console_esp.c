#include "dev_console_esp.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "alarm_core.h"
#include "esp_console.h"
#include "esp_log.h"
#include "timekeeper.h"

static const char *TAG = "console";

/* 10진 정수 하나를 엄격하게 읽는다(뒤에 다른 글자가 있으면 실패) */
static bool parse_i64(const char *s, int64_t *out)
{
    char *end = NULL;
    long long v = strtoll(s, &end, 10);
    if (s[0] == '\0' || *end != '\0') {
        return false;
    }
    *out = (int64_t)v;
    return true;
}

/* settime <utc_epoch>  예) PowerShell: [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() */
static int cmd_settime(int argc, char **argv)
{
    int64_t utc = 0;
    if (argc != 2 || !parse_i64(argv[1], &utc)) {
        printf("usage: settime <utc_epoch_seconds>\n");
        return 1;
    }
    esp_err_t err = timekeeper_set_utc(utc);
    if (err == ESP_ERR_INVALID_ARG) {
        printf("out of range (2026-01-01 .. 2099-12-31 UTC)\n");
        return 1;
    }
    if (err != ESP_OK) {
        printf("system time set, but RTC write failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("ok\n");
    return 0;
}

static int cmd_time(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int64_t local = 0;
    if (!timekeeper_now_local(&local)) {
        printf("unknown (use settime)\n");
        return 0;
    }
    alarm_civil_t c;
    alarm_civil_from_epoch(local, &c);
    printf("local %04d-%02u-%02u %02u:%02u:%02u (KST), utc %" PRId64 "\n",
           c.year, c.month, c.day, c.hour, c.minute, c.second, local - ALARM_TZ_OFFSET_S);
    return 0;
}


void dev_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "usagi>";
    repl_cfg.max_cmdline_length = 160;

    esp_console_dev_usb_serial_jtag_config_t hw_cfg = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    if (esp_console_new_repl_usb_serial_jtag(&hw_cfg, &repl_cfg, &repl) != ESP_OK) {
        ESP_LOGE(TAG, "console start failed");
        return;
    }
    esp_console_register_help_command();

    const esp_console_cmd_t cmds[] = {
        { .command = "settime", .help = "set time: settime <utc_epoch>", .func = cmd_settime },
        { .command = "time", .help = "show current time", .func = cmd_time },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_console_cmd_register(&cmds[i]);
    }

    esp_console_start_repl(repl);
}
