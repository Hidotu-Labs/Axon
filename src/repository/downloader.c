#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "axon/repository.h"
#include "axon/color.h"
#include <curl/curl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int axon_mkdir_p(const char *path, mode_t mode) {
    if (!path || !*path) return -1;

    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

typedef struct {
    const char *display_name;
    bool color;
    curl_off_t last_dlnow;
} download_progress_ctx_t;

static void format_size(curl_off_t bytes, char *out, size_t out_len) {
    if (bytes >= 1024 * 1024 * 1024) {
        snprintf(out, out_len, "%.2f GiB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024 * 1024) {
        snprintf(out, out_len, "%.2f MiB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(out, out_len, "%.1f KiB", (double)bytes / 1024.0);
    } else {
        snprintf(out, out_len, "%lld B", (long long)bytes);
    }
}

static int xferinfo_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;
    download_progress_ctx_t *ctx = (download_progress_ctx_t *)clientp;
    if (!ctx) return 0;

    const char *name = ctx->display_name ? ctx->display_name : "download";
    char now_str[32], tot_str[32];
    format_size(dlnow, now_str, sizeof(now_str));

    if (dltotal > 0) {
        format_size(dltotal, tot_str, sizeof(tot_str));
        int percent = (int)((dlnow * 100) / dltotal);
        if (percent > 100) percent = 100;

        int bar_width = 24;
        int filled = (percent * bar_width) / 100;
        char bar[32];
        for (int i = 0; i < bar_width; i++) {
            bar[i] = (i < filled) ? '#' : '-';
        }
        bar[bar_width] = '\0';

        if (ctx->color) {
            printf("\r  %s%-16s%s %s%8s%s / %-8s %s[%s]%s %s%3d%%%s\033[K",
                   ANSI_BOLD_CYAN, name, ANSI_RESET,
                   ANSI_BOLD_YELLOW, now_str, ANSI_RESET,
                   tot_str,
                   ANSI_BOLD_GREEN, bar, ANSI_RESET,
                   ANSI_BOLD_WHITE, percent, ANSI_RESET);
        } else {
            printf("\r  %-16s %8s / %-8s [%s] %3d%%\033[K",
                   name, now_str, tot_str, bar, percent);
        }
    } else if (dlnow > 0) {
        if (ctx->color) {
            printf("\r  %s%-16s%s %s%8s%s downloaded",
                   ANSI_BOLD_CYAN, name, ANSI_RESET,
                   ANSI_BOLD_YELLOW, now_str, ANSI_RESET);
        } else {
            printf("\r  %-16s %8s downloaded", name, now_str);
        }
    }

    fflush(stdout);
    return 0;
}

static size_t write_file_cb(void *ptr, size_t size, size_t nmemb, void *stream) {
    return fwrite(ptr, size, nmemb, (FILE *)stream);
}

int axon_download_file(const char *url, const char *dest_path, const char *display_name, bool show_progress, bool color) {
    if (!url || !dest_path) return -1;

    char part_path[2048];
    int part_path_len = snprintf(part_path, sizeof(part_path), "%s.part.XXXXXX", dest_path);
    if (part_path_len < 0 || (size_t)part_path_len >= sizeof(part_path)) {
        errno = ENAMETOOLONG;
        if (color) {
            fprintf(stderr, "  %serror:%s temporary download path is too long\n",
                    ANSI_BOLD_RED, ANSI_RESET);
        } else {
            fprintf(stderr, "  error: temporary download path is too long\n");
        }
        return -2;
    }

    int part_fd = mkstemp(part_path);
    if (part_fd < 0) {
        if (color) {
            fprintf(stderr, "  %serror:%s failed to create '%s%s%s': %s\n",
                    ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD_WHITE, part_path, ANSI_RESET, strerror(errno));
        } else {
            fprintf(stderr, "  error: failed to create '%s': %s\n", part_path, strerror(errno));
        }
        return -2;
    }

    FILE *fp = fdopen(part_fd, "wb");
    if (!fp) {
        int saved_errno = errno;
        close(part_fd);
        remove(part_path);
        errno = saved_errno;
        if (color) {
            fprintf(stderr, "  %serror:%s failed to open temporary download '%s%s%s': %s\n",
                    ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD_WHITE, part_path, ANSI_RESET, strerror(errno));
        } else {
            fprintf(stderr, "  error: failed to open temporary download '%s': %s\n",
                    part_path, strerror(errno));
        }
        return -2;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        remove(part_path);
        return -1;
    }

    download_progress_ctx_t prog_ctx = {
        .display_name = display_name,
        .color = color,
        .last_dlnow = 0
    };

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Axon/1.0 (AvoryOS)");
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 15L);

    if (show_progress) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog_ctx);
    } else {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    }

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    fclose(fp);
    curl_easy_cleanup(curl);

    if (show_progress && res == CURLE_OK) {
        printf("\n");
    }

    if (res != CURLE_OK || (http_code != 0 && (http_code < 200 || http_code >= 300))) {
        remove(part_path);
        return -1;
    }

    /* Atomically publish the uniquely named temporary download. */
    if (rename(part_path, dest_path) != 0) {
        if (color) {
            fprintf(stderr, "  %serror:%s failed to finalize '%s%s%s': %s\n",
                    ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD_WHITE, dest_path, ANSI_RESET, strerror(errno));
        } else {
            fprintf(stderr, "  error: failed to finalize '%s': %s\n", dest_path, strerror(errno));
        }
        remove(part_path);
        return -2;
    }

    return 0;
}
