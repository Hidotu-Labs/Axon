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
#include <time.h>
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

static int parallel_xferinfo_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;
    axon_download_task_t *task = (axon_download_task_t *)clientp;
    if (!task) return 0;
    task->dlnow = (int64_t)dlnow;
    if (dltotal > 0) {
        task->dltotal = (int64_t)dltotal;
    }
    return 0;
}

static int64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int start_download_task(CURLM *multi, axon_download_task_t *task, bool is_tty, bool color) {
    if (!task || task->current_url_idx >= task->url_count) {
        if (task) task->status = -1;
        return -1;
    }

    char part_path[2048];
    int part_path_len = snprintf(part_path, sizeof(part_path), "%s.part.XXXXXX", task->dest_path);
    if (part_path_len < 0 || (size_t)part_path_len >= sizeof(part_path)) {
        task->status = -1;
        return -1;
    }

    int part_fd = mkstemp(part_path);
    if (part_fd < 0) {
        task->status = -1;
        return -1;
    }

    FILE *fp = fdopen(part_fd, "wb");
    if (!fp) {
        close(part_fd);
        unlink(part_path);
        task->status = -1;
        return -1;
    }

    CURL *easy = curl_easy_init();
    if (!easy) {
        fclose(fp);
        unlink(part_path);
        task->status = -1;
        return -1;
    }

    strncpy(task->part_path, part_path, sizeof(task->part_path) - 1);
    task->part_path[sizeof(task->part_path) - 1] = '\0';
    task->fp = fp;
    task->easy = easy;
    task->dlnow = 0;
    if (task->expected_size > 0) {
        task->dltotal = (int64_t)task->expected_size;
    }

    const char *url = task->urls[task->current_url_idx];
    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_file_cb);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, "Axon/1.0 (AvoryOS)");
    curl_easy_setopt(easy, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 15L);
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, parallel_xferinfo_cb);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, task);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, task);

    CURLMcode mrc = curl_multi_add_handle(multi, easy);
    if (mrc != CURLM_OK) {
        fclose(fp);
        task->fp = NULL;
        unlink(task->part_path);
        curl_easy_cleanup(easy);
        task->easy = NULL;
        task->status = -1;
        return -1;
    }

    task->status = 2; /* active */
    if (!is_tty) {
        if (task->current_url_idx == 0) {
            if (color) {
                printf("  %s::%s Downloading %s%s%s...\n",
                       ANSI_BOLD_BLUE, ANSI_RESET, ANSI_BOLD_WHITE, task->display_name ? task->display_name : "package", ANSI_RESET);
            } else {
                printf("  :: Downloading %s...\n", task->display_name ? task->display_name : "package");
            }
        } else {
            if (color) {
                printf("  %s::%s Retrying %s%s%s on mirror %zu...\n",
                       ANSI_BOLD_YELLOW, ANSI_RESET, ANSI_BOLD_WHITE, task->display_name ? task->display_name : "package", ANSI_RESET, task->current_url_idx + 1);
            } else {
                printf("  :: Retrying %s on mirror %zu...\n", task->display_name ? task->display_name : "package", task->current_url_idx + 1);
            }
        }
        fflush(stdout);
    }
    return 0;
}

static void render_slots(axon_download_task_t **slots, int num_slots, int *lines_rendered, bool color, size_t task_count) {
    if (task_count == 1) {
        axon_download_task_t *t = slots[0];
        if (!t) return;
        const char *name = t->display_name ? t->display_name : "download";
        char now_str[32], tot_str[32];
        format_size((curl_off_t)t->dlnow, now_str, sizeof(now_str));
        if (t->dltotal > 0) {
            format_size((curl_off_t)t->dltotal, tot_str, sizeof(tot_str));
            int percent = (int)((t->dlnow * 100) / t->dltotal);
            if (percent > 100) percent = 100;
            int bar_width = 20;
            int filled = (percent * bar_width) / 100;
            char bar[32];
            for (int b = 0; b < bar_width; b++) bar[b] = (b < filled) ? '#' : '-';
            bar[bar_width] = '\0';
            if (color) {
                printf("\r  %s%-24.24s%s %s%8s%s / %-8s %s[%s]%s %s%3d%%%s\033[K",
                       ANSI_BOLD_CYAN, name, ANSI_RESET,
                       ANSI_BOLD_YELLOW, now_str, ANSI_RESET,
                       tot_str,
                       ANSI_BOLD_GREEN, bar, ANSI_RESET,
                       ANSI_BOLD_WHITE, percent, ANSI_RESET);
            } else {
                printf("\r  %-24.24s %8s / %-8s [%s] %3d%%\033[K",
                       name, now_str, tot_str, bar, percent);
            }
        } else if (t->dlnow > 0) {
            if (color) {
                printf("\r  %s%-24.24s%s %s%8s%s downloaded\033[K",
                       ANSI_BOLD_CYAN, name, ANSI_RESET,
                       ANSI_BOLD_YELLOW, now_str, ANSI_RESET);
            } else {
                printf("\r  %-24.24s %8s downloaded\033[K", name, now_str);
            }
        }
        fflush(stdout);
        *lines_rendered = 1;
        return;
    }

    if (*lines_rendered > 0) {
        printf("\033[%dA\r", *lines_rendered);
    }

    for (int s = 0; s < num_slots; s++) {
        axon_download_task_t *t = slots[s];
        if (t) {
            const char *name = t->display_name ? t->display_name : "download";
            char now_str[32], tot_str[32];
            format_size((curl_off_t)t->dlnow, now_str, sizeof(now_str));
            if (t->dltotal > 0) {
                format_size((curl_off_t)t->dltotal, tot_str, sizeof(tot_str));
                int percent = (int)((t->dlnow * 100) / t->dltotal);
                if (percent > 100) percent = 100;
                int bar_width = 18;
                int filled = (percent * bar_width) / 100;
                char bar[32];
                for (int b = 0; b < bar_width; b++) bar[b] = (b < filled) ? '#' : '-';
                bar[bar_width] = '\0';
                if (color) {
                    printf("  %s%-24.24s%s %s%8s%s / %-8s %s[%s]%s %s%3d%%%s\033[K\n",
                           ANSI_BOLD_CYAN, name, ANSI_RESET,
                           ANSI_BOLD_YELLOW, now_str, ANSI_RESET,
                           tot_str,
                           ANSI_BOLD_GREEN, bar, ANSI_RESET,
                           ANSI_BOLD_WHITE, percent, ANSI_RESET);
                } else {
                    printf("  %-24.24s %8s / %-8s [%s] %3d%%\033[K\n",
                           t->display_name, now_str, tot_str, bar, percent);
                }
            } else {
                if (color) {
                    printf("  %s%-24.24s%s %s%8s%s downloaded\033[K\n",
                           ANSI_BOLD_CYAN, name, ANSI_RESET,
                           ANSI_BOLD_YELLOW, now_str, ANSI_RESET);
                } else {
                    printf("  %-24.24s %8s downloaded\033[K\n", name, now_str);
                }
            }
        } else {
            printf("\033[K\n");
        }
    }
    fflush(stdout);
    *lines_rendered = num_slots;
}

int axon_download_parallel(axon_download_task_t *tasks, size_t task_count, int max_concurrent, bool color) {
    if (!tasks || task_count == 0) return 0;
    if (max_concurrent < 1) max_concurrent = 1;
    if (max_concurrent > (int)task_count) max_concurrent = (int)task_count;
    if (max_concurrent > 32) max_concurrent = 32;

    bool is_tty = isatty(STDOUT_FILENO);

    for (size_t i = 0; i < task_count; i++) {
        tasks[i].status = 1; /* pending */
        tasks[i].current_url_idx = 0;
        tasks[i].part_path[0] = '\0';
        tasks[i].fp = NULL;
        tasks[i].easy = NULL;
        tasks[i].dlnow = 0;
        tasks[i].dltotal = (int64_t)tasks[i].expected_size;
    }

    CURLM *multi = curl_multi_init();
    if (!multi) return -1;

    axon_download_task_t *slots[32] = {0};
    size_t next_task_idx = 0;
    size_t completed_count = 0;
    int active_count = 0;
    int lines_rendered = 0;
    int64_t last_render_time = 0;

    while (active_count < max_concurrent && next_task_idx < task_count) {
        axon_download_task_t *t = &tasks[next_task_idx++];
        if (start_download_task(multi, t, is_tty, color) == 0) {
            slots[active_count++] = t;
        } else {
            completed_count++;
        }
    }

    int still_running = 0;
    curl_multi_perform(multi, &still_running);

    while (completed_count < task_count) {
        int numfds = 0;
        curl_multi_poll(multi, NULL, 0, 50, &numfds);
        curl_multi_perform(multi, &still_running);

        CURLMsg *msg;
        int msgs_left = 0;
        while ((msg = curl_multi_info_read(multi, &msgs_left))) {
            if (msg->msg == CURLMSG_DONE) {
                CURL *easy = msg->easy_handle;
                CURLcode result = msg->data.result;
                long http_code = 0;
                curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);

                axon_download_task_t *task = NULL;
                curl_easy_getinfo(easy, CURLINFO_PRIVATE, (char **)&task);

                curl_multi_remove_handle(multi, easy);
                curl_easy_cleanup(easy);
                task->easy = NULL;

                if (task->fp) {
                    fclose(task->fp);
                    task->fp = NULL;
                }

                bool success = (result == CURLE_OK && (http_code == 0 || (http_code >= 200 && http_code < 300)));
                if (success && task->expected_size > 0) {
                    struct stat st;
                    if (stat(task->part_path, &st) != 0 || (size_t)st.st_size != task->expected_size) {
                        success = false;
                    }
                }

                if (success) {
                    if (rename(task->part_path, task->dest_path) == 0) {
                        task->status = 0; /* done */
                        completed_count++;
                        active_count--;
                        for (int s = 0; s < max_concurrent; s++) {
                            if (slots[s] == task) {
                                slots[s] = NULL;
                                break;
                            }
                        }
                        if (!is_tty) {
                            char sz[32];
                            format_size((curl_off_t)task->dlnow, sz, sizeof(sz));
                            if (color) {
                                printf("  %s::%s Downloaded %s%s%s (%s)\n",
                                       ANSI_BOLD_GREEN, ANSI_RESET, ANSI_BOLD_WHITE, task->display_name ? task->display_name : "package", ANSI_RESET, sz);
                            } else {
                                printf("  :: Downloaded %s (%s)\n", task->display_name ? task->display_name : "package", sz);
                            }
                            fflush(stdout);
                        }
                    } else {
                        unlink(task->part_path);
                        task->status = -1;
                        completed_count++;
                        active_count--;
                        for (int s = 0; s < max_concurrent; s++) {
                            if (slots[s] == task) {
                                slots[s] = NULL;
                                break;
                            }
                        }
                    }
                } else {
                    unlink(task->part_path);
                    task->current_url_idx++;
                    if (task->current_url_idx < task->url_count) {
                        if (start_download_task(multi, task, is_tty, color) != 0) {
                            task->status = -1;
                            completed_count++;
                            active_count--;
                            for (int s = 0; s < max_concurrent; s++) {
                                if (slots[s] == task) {
                                    slots[s] = NULL;
                                    break;
                                }
                            }
                        }
                    } else {
                        task->status = -1;
                        completed_count++;
                        active_count--;
                        for (int s = 0; s < max_concurrent; s++) {
                            if (slots[s] == task) {
                                slots[s] = NULL;
                                break;
                            }
                        }
                        if (!is_tty) {
                            if (color) {
                                fprintf(stderr, "  %serror:%s failed to download %s\n",
                                        ANSI_BOLD_RED, ANSI_RESET, task->display_name ? task->display_name : "package");
                            } else {
                                fprintf(stderr, "  error: failed to download %s\n", task->display_name ? task->display_name : "package");
                            }
                            fflush(stderr);
                        }
                    }
                }

                while (active_count < max_concurrent && next_task_idx < task_count) {
                    axon_download_task_t *next_t = &tasks[next_task_idx++];
                    int empty_slot = -1;
                    for (int s = 0; s < max_concurrent; s++) {
                        if (!slots[s]) {
                            empty_slot = s;
                            break;
                        }
                    }
                    if (empty_slot >= 0 && start_download_task(multi, next_t, is_tty, color) == 0) {
                        slots[empty_slot] = next_t;
                        active_count++;
                        curl_multi_perform(multi, &still_running);
                    } else {
                        next_t->status = -1;
                        completed_count++;
                    }
                }
            }
        }

        if (is_tty) {
            int64_t now = get_time_ms();
            if (now - last_render_time >= 60 || completed_count == task_count) {
                render_slots(slots, max_concurrent, &lines_rendered, color, task_count);
                last_render_time = now;
            }
        }
    }

    if (is_tty) {
        if (task_count == 1) {
            printf("\n");
            fflush(stdout);
        } else if (lines_rendered > 0) {
            printf("\033[%dA\r", lines_rendered);
            for (int s = 0; s < lines_rendered; s++) {
                printf("\033[2K\n");
            }
            printf("\033[%dA\r", lines_rendered);
            fflush(stdout);
        }
    }

    curl_multi_cleanup(multi);

    int overall_res = 0;
    for (size_t i = 0; i < task_count; i++) {
        if (tasks[i].status != 0) {
            overall_res = -1;
            break;
        }
    }
    return overall_res;
}
