#include "dev_console_esp.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "alarm_core.h"
#include "alarm_service_esp.h"
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

/* ===== alarm 명령: alarm_service를 통해서만 엔진을 바꾼다 ===== */

static alarm_t s_list[ALARM_MAX];   /* 콘솔 작업 하나만 쓰므로 정적 버퍼 */

static int find_index(size_t n, uint8_t id)
{
    for (size_t i = 0; i < n; i++) {
        if (s_list[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

static bool parse_u8(const char *s, int base, uint8_t *out)
{
    char *end = NULL;
    long v = strtol(s, &end, base);
    if (s[0] == '\0' || *end != '\0' || v < 0 || v > 255) {
        return false;
    }
    *out = (uint8_t)v;
    return true;
}

static int apply_list(size_t n)
{
    esp_err_t err = alarm_service_set_alarms(s_list, n);
    if (err != ESP_OK) {
        printf("rejected: %s (check ranges: hour 0-23, minute 0-59, pre 0-60, days 00-7f)\n", esp_err_to_name(err));
        return 1;
    }
    printf("ok\n");
    return 0;
}

static void print_alarm(const alarm_t *a)
{
    printf("id=%-3u %s %02u:%02u days=%02x pre=%-2u vol=%u/%u label=\"%s\" song=\"%s\" clips=\"%s\"\n",
           a->id, a->enabled ? "on " : "off", a->hour, a->minute, a->days_mask, a->pre_minutes,
           a->pre_volume, a->main_volume, a->label, a->song, a->clipset);
}

/*
 * alarm list
 * alarm add <id> <HH:MM> <days_hex> <pre_min> [label]   예) alarm add 1 07:00 1f 5 work
 * alarm del <id> | alarm clear | alarm on <id> | alarm off <id>
 * alarm song <id> <name> | alarm clips <id> <set>
 */
static int cmd_alarm(int argc, char **argv)
{
    size_t n = alarm_service_get_alarms(s_list, ALARM_MAX);
    const char *sub = argc >= 2 ? argv[1] : "list";

    if (strcmp(sub, "list") == 0) {
        if (n == 0) {
            printf("(no alarms)\n");
        }
        for (size_t i = 0; i < n; i++) {
            print_alarm(&s_list[i]);
        }
        return 0;
    }
    if (strcmp(sub, "clear") == 0) {
        return apply_list(0);
    }

    uint8_t id = 0;
    if (argc < 3 || !parse_u8(argv[2], 10, &id) || id == 0) {
        printf("usage: alarm add <id> <HH:MM> <days_hex> <pre_min> [label] | del|on|off <id> | song|clips <id> <name> | list | clear\n");
        return 1;
    }
    int idx = find_index(n, id);

    if (strcmp(sub, "add") == 0) {
        unsigned hh = 0, mm = 0;
        uint8_t days = 0, pre = 0;
        if (argc < 6 || sscanf(argv[3], "%u:%u", &hh, &mm) != 2 || !parse_u8(argv[4], 16, &days) ||
            !parse_u8(argv[5], 10, &pre)) {
            printf("usage: alarm add <id> <HH:MM> <days_hex> <pre_min> [label]\n");
            return 1;
        }
        alarm_t a = { .id = id, .enabled = true, .hour = (uint8_t)hh, .minute = (uint8_t)mm,
                      .days_mask = days, .pre_minutes = pre, .pre_volume = 70, .main_volume = 70 };
        if (argc >= 7) {
            strncpy(a.label, argv[6], sizeof(a.label) - 1);
        }
        if (idx >= 0) {
            s_list[idx] = a;          /* 같은 id면 바꾼다 */
        } else if (n < ALARM_MAX) {
            s_list[n++] = a;
        } else {
            printf("full (%d alarms)\n", ALARM_MAX);
            return 1;
        }
        return apply_list(n);
    }

    if (idx < 0) {
        printf("no alarm with id %u\n", id);
        return 1;
    }
    if (strcmp(sub, "del") == 0) {
        s_list[idx] = s_list[n - 1];
        return apply_list(n - 1);
    }
    if (strcmp(sub, "on") == 0 || strcmp(sub, "off") == 0) {
        s_list[idx].enabled = strcmp(sub, "on") == 0;
        return apply_list(n);
    }
    if ((strcmp(sub, "song") == 0 || strcmp(sub, "clips") == 0) && argc >= 4) {
        if (strcmp(sub, "song") == 0) {
            memset(s_list[idx].song, 0, sizeof(s_list[idx].song));
            strncpy(s_list[idx].song, argv[3], sizeof(s_list[idx].song) - 1);
        } else {
            memset(s_list[idx].clipset, 0, sizeof(s_list[idx].clipset));
            strncpy(s_list[idx].clipset, argv[3], sizeof(s_list[idx].clipset) - 1);
        }
        return apply_list(n);
    }
    printf("unknown subcommand\n");
    return 1;
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
        { .command = "alarm", .help = "alarm list|add|del|on|off|song|clips|clear", .func = cmd_alarm },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_console_cmd_register(&cmds[i]);
    }

    esp_console_start_repl(repl);
}
