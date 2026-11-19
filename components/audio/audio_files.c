#include "audio_files.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool audio_name_ok(const char *name)
{
    size_t len = strlen(name);
    if (len == 0 || len >= AUDIO_NAME_MAX || name[0] == '.') {
        return false;
    }
    if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL || strstr(name, "..") != NULL) {
        return false;
    }
    return true;
}

bool audio_is_mp3_name(const char *name)
{
    size_t len = strlen(name);
    if (len < 5 || name[0] == '.') {   /* 최소 "a.mp3" */
        return false;
    }
    const char *ext = name + len - 4;
    return ext[0] == '.' && tolower((unsigned char)ext[1]) == 'm' && tolower((unsigned char)ext[2]) == 'p' &&
           ext[3] == '3';
}

bool audio_path_join(char *out, size_t n, const char *dir, const char *name)
{
    if (n == 0) {
        return false;
    }
    out[0] = '\0';
    if (!audio_name_ok(name)) {
        return false;
    }
    int w = snprintf(out, n, "%s/%s", dir, name);
    if (w < 0 || (size_t)w >= n) {
        out[0] = '\0';
        return false;
    }
    return true;
}

static int cmp_names(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

void audio_sort_names(char names[][AUDIO_NAME_MAX], size_t count)
{
    qsort(names, count, AUDIO_NAME_MAX, cmp_names);
}
