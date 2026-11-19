#include "web_internal.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "audio.h"
#include "audio_files.h"
#include "cJSON.h"
#include "esp_log.h"
#include "sdcard.h"

static const char *TAG = "web_sounds";

#define UPLOAD_CHUNK     8192
#define LIST_MAX         64
#define CLIPSET_DIRS_MAX 16

/* set 쿼리가 있으면 /sd/clips/<set>, 없으면 /sd/sounds. 세트 이름이 규칙 위반이면 false */
static bool target_dir(httpd_req_t *req, char *dir, size_t n, bool *is_clip)
{
    char set[AUDIO_NAME_MAX];
    *is_clip = web_query_get(req, "set", set, sizeof(set));
    if (!*is_clip) {
        snprintf(dir, n, "%s", SD_SOUNDS_DIR);
        return true;
    }
    return audio_path_join(dir, n, SD_CLIPS_DIR, set);
}

static void add_names(cJSON *arr, const char *dir, bool with_size)
{
    static char names[LIST_MAX][AUDIO_NAME_MAX];
    size_t n = sdcard_list_mp3(dir, names, LIST_MAX);
    for (size_t i = 0; i < n; i++) {
        if (!with_size) {
            cJSON_AddItemToArray(arr, cJSON_CreateString(names[i]));
            continue;
        }
        char path[AUDIO_PATH_MAX];
        struct stat st;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", names[i]);
        double size = 0;
        if (audio_path_join(path, sizeof(path), dir, names[i]) && stat(path, &st) == 0) {
            size = (double)st.st_size;
        }
        cJSON_AddNumberToObject(o, "size", size);
        cJSON_AddItemToArray(arr, o);
    }
}

static esp_err_t sounds_get(httpd_req_t *req)
{
    if (!sdcard_ensure_mounted()) {
        return web_send_error(req, "503 Service Unavailable", "no SD card");
    }
    cJSON *root = cJSON_CreateObject();
    add_names(cJSON_AddArrayToObject(root, "songs"), SD_SOUNDS_DIR, true);
    cJSON *sets = cJSON_AddObjectToObject(root, "clipsets");
    static char set_names[CLIPSET_DIRS_MAX][AUDIO_NAME_MAX];
    size_t ns = sdcard_list_dirs(SD_CLIPS_DIR, set_names, CLIPSET_DIRS_MAX);
    for (size_t i = 0; i < ns; i++) {
        char dir[AUDIO_PATH_MAX];
        if (audio_path_join(dir, sizeof(dir), SD_CLIPS_DIR, set_names[i])) {
            add_names(cJSON_AddArrayToObject(sets, set_names[i]), dir, false);
        }
    }
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (s == NULL) {
        return web_send_error(req, "500 Internal Server Error", "out of memory");
    }
    esp_err_t r = web_send_json(req, "200 OK", s);
    cJSON_free(s);
    return r;
}

static esp_err_t sounds_post(httpd_req_t *req)
{
    char raw[AUDIO_NAME_MAX * 2], name[AUDIO_NAME_MAX], dir[AUDIO_PATH_MAX], path[AUDIO_PATH_MAX], tmp[AUDIO_PATH_MAX];
    bool is_clip = false;

    if (req->content_len == 0 || req->content_len > WEB_UPLOAD_MAX) {
        return web_send_error(req, "413 Payload Too Large", "file must be 1 byte - 20 MB");
    }
    if (!web_query_get(req, "name", raw, sizeof(raw)) || !web_sanitize_upload_name(raw, name, sizeof(name))) {
        return web_send_error(req, "400 Bad Request", "bad file name");
    }
    if (!target_dir(req, dir, sizeof(dir), &is_clip)) {
        return web_send_error(req, "400 Bad Request", "bad clip set name");
    }
    if (!sdcard_ensure_mounted()) {
        return web_send_error(req, "503 Service Unavailable", "no SD card");
    }
    uint64_t total = 0, free_bytes = 0;
    if (sdcard_space(&total, &free_bytes) && free_bytes < (uint64_t)req->content_len + 64 * 1024) {
        return web_send_error(req, "507 Insufficient Storage", "SD card is full");
    }

    mkdir(is_clip ? SD_CLIPS_DIR : SD_SOUNDS_DIR, 0775);
    mkdir(dir, 0775);
    if (!audio_path_join(path, sizeof(path), dir, name) || !audio_path_join(tmp, sizeof(tmp), dir, "upload.tmp")) {
        return web_send_error(req, "400 Bad Request", "path too long");
    }

    FILE *f = fopen(tmp, "wb");
    if (f == NULL) {
        return web_send_error(req, "500 Internal Server Error", "cannot write SD card");
    }
    static char chunk[UPLOAD_CHUNK];
    size_t left = req->content_len;
    bool ok = true;
    while (left > 0) {
        int r = httpd_req_recv(req, chunk, left < sizeof(chunk) ? left : sizeof(chunk));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0 || fwrite(chunk, 1, (size_t)r, f) != (size_t)r) {
            ok = false;
            break;
        }
        left -= (size_t)r;
    }
    fclose(f);
    if (!ok) {
        unlink(tmp);
        ESP_LOGW(TAG, "upload aborted: %s", name);
        return web_send_error(req, "500 Internal Server Error", "upload failed");
    }
    unlink(path);                 /* 같은 이름이면 바꾼다(FAT의 rename은 덮어쓰지 않는다) */
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return web_send_error(req, "500 Internal Server Error", "rename failed");
    }
    ESP_LOGI(TAG, "uploaded %s (%u bytes)", path, (unsigned)req->content_len);

    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", name);
    char out[160];
    cJSON_PrintPreallocated(o, out, sizeof(out), false);
    cJSON_Delete(o);
    return web_send_json(req, "201 Created", out);
}

/* "/api/sounds/<이름>[/preview]" 에서 이름을 꺼내 URL 디코드 */
static bool name_from_uri(const char *uri, const char *suffix, char *out, size_t n)
{
    const char *p = uri + strlen("/api/sounds/");
    size_t len = strcspn(p, "?");
    size_t sl = suffix != NULL ? strlen(suffix) : 0;
    if (sl > 0) {
        if (len < sl || strncmp(p + len - sl, suffix, sl) != 0) {
            return false;
        }
        len -= sl;
    }
    char enc[AUDIO_NAME_MAX * 3];
    if (len == 0 || len >= sizeof(enc)) {
        return false;
    }
    memcpy(enc, p, len);
    enc[len] = '\0';
    return web_url_decode(enc, out, n) && audio_name_ok(out);
}

static esp_err_t sound_delete(httpd_req_t *req)
{
    char name[AUDIO_NAME_MAX], dir[AUDIO_PATH_MAX], path[AUDIO_PATH_MAX];
    bool is_clip;
    if (!name_from_uri(req->uri, NULL, name, sizeof(name)) || !target_dir(req, dir, sizeof(dir), &is_clip) ||
        !audio_path_join(path, sizeof(path), dir, name)) {
        return web_send_error(req, "400 Bad Request", "bad name");
    }
    if (!sdcard_ensure_mounted() || !sdcard_file_exists(path)) {
        return web_send_error(req, "404 Not Found", "no such file");
    }
    if (unlink(path) != 0) {
        return web_send_error(req, "500 Internal Server Error", "delete failed");
    }
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t sound_preview(httpd_req_t *req)
{
    char name[AUDIO_NAME_MAX];
    if (!name_from_uri(req->uri, "/preview", name, sizeof(name))) {
        return web_send_error(req, "400 Bad Request", "bad name");
    }
    if (g_web_hooks->session_active()) {
        return web_send_error(req, "409 Conflict", "an alarm is active");
    }
    audio_cmd_t cmd = { .op = AUDIO_OP_PREVIEW, .volume = app_settings_get().default_volume };
    strncpy(cmd.name, name, sizeof(cmd.name) - 1);
    audio_post(&cmd);
    httpd_resp_set_status(req, "202 Accepted");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t sounds_post_router(httpd_req_t *req)
{
    /* POST /api/sounds/<이름>/preview 와 POST /api/sounds?name= 를 나눈다 */
    if (strncmp(req->uri, "/api/sounds/", strlen("/api/sounds/")) == 0) {
        return sound_preview(req);
    }
    return sounds_post(req);
}

void web_sounds_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        { .uri = "/api/sounds", .method = HTTP_GET, .handler = sounds_get },
        { .uri = "/api/sounds*", .method = HTTP_POST, .handler = sounds_post_router },
        { .uri = "/api/sounds/*", .method = HTTP_DELETE, .handler = sound_delete },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }
}
