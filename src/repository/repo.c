#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "axon/repository.h"
#include "axon/color.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

int axon_repo_mgr_init(axon_repo_mgr_t *mgr, const axon_config_t *cfg) {
    if (!mgr || !cfg) return -1;
    memset(mgr, 0, sizeof(axon_repo_mgr_t));
    mgr->cfg = cfg;

    if (cfg->repo_count > 0) {
        mgr->repos = calloc(cfg->repo_count, sizeof(axon_repo_t));
        if (!mgr->repos) return -1;
        mgr->count = cfg->repo_count;

        for (size_t i = 0; i < cfg->repo_count; i++) {
            mgr->repos[i].name = cfg->repos[i].name ? strdup(cfg->repos[i].name) : NULL;
            mgr->repos[i].url = cfg->repos[i].url ? strdup(cfg->repos[i].url) : NULL;
            mgr->repos[i].is_arch = cfg->repos[i].is_arch;
            mgr->repos[i].priority = cfg->repos[i].priority;
        }
    }

    return 0;
}

void axon_repo_mgr_free(axon_repo_mgr_t *mgr) {
    if (!mgr) return;
    for (size_t i = 0; i < mgr->count; i++) {
        free(mgr->repos[i].name);
        free(mgr->repos[i].db_file);
        free(mgr->repos[i].url);
    }
    free(mgr->repos);
    mgr->repos = NULL;
    mgr->count = 0;
}

static char *get_custom_manifest_path(const axon_config_t *cfg, const axon_repo_t *repo) {
    if (!repo || !repo->name) return NULL;
    const char *sync_dir = (cfg && cfg->sync_path) ? cfg->sync_path : "/var/lib/axon/sync";

    char path[512];
    snprintf(path, sizeof(path), "%s/%s.txt", sync_dir, repo->name);
    if (access(path, R_OK) == 0) return strdup(path);

    snprintf(path, sizeof(path), "%s/%s.db", sync_dir, repo->name);
    if (access(path, R_OK) == 0) return strdup(path);

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/axon_sync_%s.txt", repo->name);
    if (access(tmp_path, R_OK) == 0) return strdup(tmp_path);

    /* If manifest not cached yet and url is available, attempt download to tmp */
    if (repo->url && (strncmp(repo->url, "http://", 7) == 0 || strncmp(repo->url, "https://", 8) == 0)) {
        if (axon_download_file(repo->url, tmp_path, repo->name, false, false) == 0) {
            return strdup(tmp_path);
        }
    }

    return NULL;
}

static axon_package_t *parse_manifest_line(const char *line, const char *repo_name) {
    if (!line) return NULL;
    char buf[2048];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *p = buf;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') return NULL;

    size_t len = strlen(p);
    while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == '\n' || p[len - 1] == ' ' || p[len - 1] == '\t')) {
        p[--len] = '\0';
    }

    /* Expected format: name|version|size|url|desc|[depends]|[sha256] */
    char *tokens[8];
    int count = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(p, "|", &saveptr);
    while (tok && count < 8) {
        tokens[count++] = tok;
        tok = strtok_r(NULL, "|", &saveptr);
    }

    if (count < 4) return NULL;

    const char *name = tokens[0];
    const char *version = tokens[1];
    const char *size_str = tokens[2];
    const char *url = tokens[3];
    const char *desc = (count > 4) ? tokens[4] : "";
    const char *depends = (count > 5) ? tokens[5] : NULL;
    const char *sha256 = (count > 6) ? tokens[6] : NULL;

    axon_package_t *pkg = axon_package_new();
    if (!pkg) return NULL;

    pkg->name = strdup(name);
    pkg->version = strdup(version);
    pkg->csize = (size_t)atoll(size_str);
    pkg->isize = pkg->csize;
    pkg->url = strdup(url);
    pkg->desc = strdup(desc);
    pkg->repo = strdup(repo_name);
    pkg->arch = strdup("x86_64");
    pkg->is_arch = false;

    const char *slash = strrchr(url, '/');
    pkg->filename = strdup(slash ? slash + 1 : name);

    if (depends && strlen(depends) > 0) {
        char dep_buf[512];
        strncpy(dep_buf, depends, sizeof(dep_buf) - 1);
        dep_buf[sizeof(dep_buf) - 1] = '\0';
        char *d_save = NULL;
        char *d_tok = strtok_r(dep_buf, ", ", &d_save);
        while (d_tok) {
            char **new_deps = realloc(pkg->depends, sizeof(char *) * (pkg->depends_count + 1));
            if (new_deps) {
                pkg->depends = new_deps;
                pkg->depends[pkg->depends_count++] = strdup(d_tok);
            }
            d_tok = strtok_r(NULL, ", ", &d_save);
        }
    }

    if (sha256 && strlen(sha256) > 0) {
        pkg->sha256sum = strdup(sha256);
    }

    return pkg;
}

int axon_custom_repo_search(const axon_config_t *cfg, const axon_repo_t *repo, const char *query, axon_pkg_list_t *results) {
    if (!repo || !results) return -1;
    char *manifest_file = get_custom_manifest_path(cfg, repo);
    if (!manifest_file) return -1;

    FILE *fp = fopen(manifest_file, "r");
    free(manifest_file);
    if (!fp) return -1;

    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        axon_package_t *pkg = parse_manifest_line(line, repo->name);
        if (!pkg) continue;

        bool match = true;
        if (query && strlen(query) > 0) {
            match = false;
            if (pkg->name && strcasestr(pkg->name, query)) match = true;
            else if (pkg->desc && strcasestr(pkg->desc, query)) match = true;
        }

        if (match) {
            axon_pkg_list_add(results, pkg);
        } else {
            axon_package_free(pkg);
        }
    }

    fclose(fp);
    return 0;
}

axon_package_t *axon_custom_repo_find(const axon_config_t *cfg, const axon_repo_t *repo, const char *pkgname) {
    if (!repo || !pkgname) return NULL;
    char *manifest_file = get_custom_manifest_path(cfg, repo);
    if (!manifest_file) return NULL;

    FILE *fp = fopen(manifest_file, "r");
    free(manifest_file);
    if (!fp) return NULL;

    char line[2048];
    axon_package_t *found = NULL;
    while (fgets(line, sizeof(line), fp)) {
        axon_package_t *pkg = parse_manifest_line(line, repo->name);
        if (!pkg) continue;

        if (pkg->name && strcmp(pkg->name, pkgname) == 0) {
            found = pkg;
            break;
        }
        axon_package_free(pkg);
    }

    fclose(fp);
    return found;
}

int axon_repo_search(axon_repo_mgr_t *mgr, const char *query, axon_pkg_list_t *results) {
    if (!results) return -1;
    const axon_config_t *cfg = mgr ? mgr->cfg : NULL;

    /* 1. Search custom / GitHub repositories first */
    if (mgr && mgr->count > 0) {
        for (size_t i = 0; i < mgr->count; i++) {
            if (mgr->repos[i].is_arch) continue;
            axon_custom_repo_search(cfg, &mgr->repos[i], query, results);
        }
    }

    /* 2. Fallback / supplementary search in Arch Linux repositories */
    axon_arch_search_all(cfg, query, results);
    return (results->count > 0) ? 0 : -1;
}

axon_package_t *axon_repo_find(axon_repo_mgr_t *mgr, const char *pkgname) {
    if (!pkgname) return NULL;
    const axon_config_t *cfg = mgr ? mgr->cfg : NULL;

    /* 1. First, search custom / GitHub repositories configured in mgr */
    if (mgr && mgr->count > 0) {
        for (size_t i = 0; i < mgr->count; i++) {
            if (mgr->repos[i].is_arch) continue;
            axon_package_t *pkg = axon_custom_repo_find(cfg, &mgr->repos[i], pkgname);
            if (pkg) return pkg;
        }
    }

    /* 2. Fallback to Arch Linux repositories */
    return axon_arch_find_pkg(cfg, pkgname);
}

static void add_mirror(char ***mirrors, size_t *count, const char *url) {
    char **new_arr = realloc(*mirrors, sizeof(char *) * (*count + 1));
    if (!new_arr) return;
    *mirrors = new_arr;
    (*mirrors)[*count] = strdup(url);
    (*count)++;
}

void axon_mirrorlist_free(char **mirrors, size_t count) {
    if (!mirrors) return;
    for (size_t i = 0; i < count; i++) {
        free(mirrors[i]);
    }
    free(mirrors);
}

static int parse_mirror_file(const char *path, char ***mirrors, size_t *count) {
    if (!path) return -1;
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[512];
    size_t added = 0;
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        /* Strip trailing newlines */
        size_t l = strlen(p);
        while (l > 0 && (p[l - 1] == '\r' || p[l - 1] == '\n' || p[l - 1] == ' ' || p[l - 1] == '\t')) {
            p[--l] = '\0';
        }

        /* Check Server = <url> */
        if (strncmp(p, "Server", 6) == 0) {
            p += 6;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '=') p++;
            while (*p == ' ' || *p == '\t') p++;
        }

        if (strncmp(p, "http://", 7) == 0 || strncmp(p, "https://", 8) == 0) {
            add_mirror(mirrors, count, p);
            added++;
        }
    }

    fclose(fp);
    return (added > 0) ? 0 : -1;
}

int axon_mirrorlist_load(const char *path, char ***mirrors, size_t *count) {
    if (!mirrors || !count) return -1;
    *mirrors = NULL;
    *count = 0;

    /* 1. Try provided path */
    if (path && parse_mirror_file(path, mirrors, count) == 0) {
        return 0;
    }

    /* 2. Try AXON_MIRRORLIST environment variable */
    const char *env_path = getenv("AXON_MIRRORLIST");
    if (env_path && parse_mirror_file(env_path, mirrors, count) == 0) {
        return 0;
    }

    /* 3. Try /etc/axon/mirrorlist */
    if (parse_mirror_file("/etc/axon/mirrorlist", mirrors, count) == 0) {
        return 0;
    }

    /* 4. Try /etc/pacman.d/mirrorlist */
    if (parse_mirror_file("/etc/pacman.d/mirrorlist", mirrors, count) == 0) {
        return 0;
    }

    /* 5. Fallback built-in reliable mirrors */
    static const char *fallback_mirrors[] = {
        "https://geo.mirror.pkgbuild.com/$repo/os/$arch",
        "https://archlinux.cachyos.org/repo/$repo/os/$arch",
        "https://mirror.rackspace.com/archlinux/$repo/os/$arch",
        "https://mirrors.kernel.org/archlinux/$repo/os/$arch",
        NULL
    };

    for (int i = 0; fallback_mirrors[i]; i++) {
        add_mirror(mirrors, count, fallback_mirrors[i]);
    }

    return 0;
}

static char *format_repo_url(const char *template_url, const char *repo_name, const char *arch) {
    char buffer[1024];
    buffer[0] = '\0';

    const char *src = template_url;
    char *dst = buffer;
    size_t rem = sizeof(buffer) - 1;

    while (*src && rem > 0) {
        if (strncmp(src, "$repo", 5) == 0) {
            size_t rlen = strlen(repo_name);
            if (rlen >= rem) break;
            memcpy(dst, repo_name, rlen);
            dst += rlen;
            rem -= rlen;
            src += 5;
        } else if (strncmp(src, "$arch", 5) == 0) {
            size_t alen = strlen(arch);
            if (alen >= rem) break;
            memcpy(dst, arch, alen);
            dst += alen;
            rem -= alen;
            src += 5;
        } else {
            *dst++ = *src++;
            rem--;
        }
    }
    *dst = '\0';

    /* Append /<repo_name>.db if not already in URL */
    if (!strstr(buffer, ".db")) {
        size_t blen = strlen(buffer);
        if (blen > 0 && buffer[blen - 1] != '/') {
            strncat(buffer, "/", sizeof(buffer) - strlen(buffer) - 1);
        }
        strncat(buffer, repo_name, sizeof(buffer) - strlen(buffer) - 1);
        strncat(buffer, ".db", sizeof(buffer) - strlen(buffer) - 1);
    }

    return strdup(buffer);
}

static char *axon_format_mirror_pkg_url(const char *mirror, const char *repo_name, const char *pkg_arch, const char *filename) {
    if (!mirror || !repo_name || !pkg_arch || !filename) return NULL;
    char base_url[1024];
    base_url[0] = '\0';
    const char *src = mirror;
    char *dst = base_url;
    size_t rem = sizeof(base_url) - 1;

    while (*src && rem > 0) {
        if (strncmp(src, "$repo", 5) == 0) {
            size_t rlen = strlen(repo_name);
            if (rlen >= rem) break;
            memcpy(dst, repo_name, rlen);
            dst += rlen;
            rem -= rlen;
            src += 5;
        } else if (strncmp(src, "$arch", 5) == 0) {
            size_t alen = strlen(pkg_arch);
            if (alen >= rem) break;
            memcpy(dst, pkg_arch, alen);
            dst += alen;
            rem -= alen;
            src += 5;
        } else {
            *dst++ = *src++;
            rem--;
        }
    }
    *dst = '\0';

    char *db_pos = strstr(base_url, ".db");
    if (db_pos) *db_pos = '\0';
    size_t blen = strlen(base_url);
    if (blen > 0 && base_url[blen - 1] == '/') {
        base_url[blen - 1] = '\0';
    }

    char pkg_url[1536];
    snprintf(pkg_url, sizeof(pkg_url), "%s/%s", base_url, filename);
    return strdup(pkg_url);
}

int axon_repo_sync_all(axon_repo_mgr_t *mgr) {
    if (!mgr || !mgr->cfg) return -1;
    const axon_config_t *cfg = mgr->cfg;
    bool color = cfg->color;

    const char *sync_dir = cfg->sync_path ? cfg->sync_path : "/var/lib/axon/sync";

    /* Ensure target directory exists and is writable */
    if (axon_mkdir_p(sync_dir, 0755) != 0 || access(sync_dir, W_OK) != 0) {
        if (color) {
            fprintf(stderr, "%serror:%s cannot write to sync directory '%s%s%s': %s\n",
                    ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD_WHITE, sync_dir, ANSI_RESET, strerror(errno));
            if (errno == EACCES || errno == EPERM || geteuid() != 0) {
                fprintf(stderr, "%s::%s Run with sudo or specify a writable directory via '--sync-dir <path>' or AXON_SYNC_PATH.\n",
                        ANSI_BOLD_YELLOW, ANSI_RESET);
            }
        } else {
            fprintf(stderr, "error: cannot write to sync directory '%s': %s\n", sync_dir, strerror(errno));
            if (errno == EACCES || errno == EPERM || geteuid() != 0) {
                fprintf(stderr, ":: Run with sudo or specify a writable directory via '--sync-dir <path>' or AXON_SYNC_PATH.\n");
            }
        }
        return -1;
    }

    /* Load mirrorlist */
    char **mirrors = NULL;
    size_t mirror_count = 0;
    axon_mirrorlist_load(cfg->mirrorlist_path, &mirrors, &mirror_count);

    if (mirror_count == 0) {
        if (color) {
            fprintf(stderr, "%serror:%s no mirror servers configured or available\n", ANSI_BOLD_RED, ANSI_RESET);
        } else {
            fprintf(stderr, "error: no mirror servers configured or available\n");
        }
        return -1;
    }

    /* Detect system architecture */
    struct utsname un;
    const char *arch = "x86_64";
    if (uname(&un) == 0) {
        arch = un.machine;
    }

    /* Count total sync tasks */
    size_t custom_count = 0;
    for (size_t i = 0; i < mgr->count; i++) {
        if (!mgr->repos[i].is_arch && mgr->repos[i].name && mgr->repos[i].url) {
            custom_count++;
        }
    }
    static const char *arch_repos[] = {"core", "extra", "multilib", NULL};
    size_t arch_count = 3;
    size_t total_tasks = custom_count + arch_count;

    axon_download_task_t *tasks = calloc(total_tasks, sizeof(axon_download_task_t));
    if (!tasks) {
        axon_mirrorlist_free(mirrors, mirror_count);
        return -1;
    }

    size_t task_idx = 0;

    /* 1. Custom / GitHub repositories */
    for (size_t i = 0; i < mgr->count; i++) {
        if (mgr->repos[i].is_arch) continue;
        const char *repo_name = mgr->repos[i].name;
        const char *repo_url = mgr->repos[i].url;
        if (!repo_name || !repo_url) continue;

        char dest_file[512];
        snprintf(dest_file, sizeof(dest_file), "%s/%s.txt", sync_dir, repo_name);

        char display_name[64];
        snprintf(display_name, sizeof(display_name), "%s.txt", repo_name);

        axon_download_task_t *t = &tasks[task_idx++];
        t->display_name = strdup(display_name);
        t->dest_path = strdup(dest_file);
        t->urls = malloc(sizeof(char *));
        t->urls[0] = strdup(repo_url);
        t->url_count = 1;
        t->expected_size = 0;
    }

    /* 2. Arch Linux fallback repositories */
    size_t arch_start_idx = task_idx;
    for (size_t r = 0; arch_repos[r]; r++) {
        const char *repo_name = arch_repos[r];
        if (!repo_name) continue;

        char dest_file[512];
        snprintf(dest_file, sizeof(dest_file), "%s/%s.db", sync_dir, repo_name);

        char display_name[64];
        snprintf(display_name, sizeof(display_name), "%s.db", repo_name);

        axon_download_task_t *t = &tasks[task_idx++];
        t->display_name = strdup(display_name);
        t->dest_path = strdup(dest_file);
        t->expected_size = 0;
        t->urls = malloc(sizeof(char *) * mirror_count);
        t->url_count = 0;
        for (size_t m = 0; m < mirror_count; m++) {
            char *url = format_repo_url(mirrors[m], repo_name, arch);
            if (url) {
                t->urls[t->url_count++] = url;
            }
        }
    }

    int max_concurrent = cfg->parallel_downloads > 0 ? cfg->parallel_downloads : 5;
    if (max_concurrent > (int)total_tasks) max_concurrent = (int)total_tasks;

    if (total_tasks == 1) {
        if (color) {
            printf("%s::%s Synchronizing repository database...\n", ANSI_BOLD_BLUE, ANSI_RESET);
        } else {
            printf(":: Synchronizing repository database...\n");
        }
    } else {
        if (color) {
            printf("%s::%s Synchronizing %zu repository databases (%s%d%s concurrent downloads)...\n",
                   ANSI_BOLD_BLUE, ANSI_RESET, total_tasks, ANSI_BOLD_CYAN, max_concurrent, ANSI_RESET);
        } else {
            printf(":: Synchronizing %zu repository databases (%d concurrent downloads)...\n",
                   total_tasks, max_concurrent);
        }
    }

    int drc = axon_download_parallel(tasks, total_tasks, max_concurrent, color);
    int overall_status = (drc == 0) ? 0 : -1;

    /* Create symlinks for Arch repos */
    for (size_t r = 0; arch_repos[r]; r++) {
        size_t idx = arch_start_idx + r;
        if (idx < total_tasks && tasks[idx].status == 0) {
            char gz_link[512];
            snprintf(gz_link, sizeof(gz_link), "%s/%s.db.tar.gz", sync_dir, arch_repos[r]);
            remove(gz_link);
            symlink(tasks[idx].display_name, gz_link);
        } else if (idx < total_tasks) {
            overall_status = -1;
            if (color) {
                fprintf(stderr, "  %serror:%s failed to download %s%s.db%s from all mirrors\n",
                        ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD_WHITE, arch_repos[r], ANSI_RESET);
            } else {
                fprintf(stderr, "  error: failed to download %s.db from all mirrors\n", arch_repos[r]);
            }
        }
    }

    /* Check custom repos status */
    for (size_t i = 0; i < custom_count; i++) {
        if (tasks[i].status != 0) {
            overall_status = -1;
            if (color) {
                fprintf(stderr, "  %serror:%s failed to download %s%s%s\n",
                        ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD_WHITE, tasks[i].display_name, ANSI_RESET);
            } else {
                fprintf(stderr, "  error: failed to download %s\n", tasks[i].display_name);
            }
        }
    }

    for (size_t t = 0; t < total_tasks; t++) {
        free((char *)tasks[t].display_name);
        free((char *)tasks[t].dest_path);
        if (tasks[t].urls) {
            for (size_t u = 0; u < tasks[t].url_count; u++) {
                free(tasks[t].urls[u]);
            }
            free(tasks[t].urls);
        }
    }
    free(tasks);
    axon_mirrorlist_free(mirrors, mirror_count);

    return overall_status;
}

int axon_repo_download_pkgs(axon_repo_mgr_t *mgr, axon_package_t **pkgs, size_t count, const char *dest_dir, char **out_filepaths) {
    if (!mgr || !mgr->cfg || !pkgs || count == 0 || !dest_dir || !out_filepaths) return -1;
    const axon_config_t *cfg = mgr->cfg;
    bool color = cfg->color;

    if (axon_mkdir_p(dest_dir, 0755) != 0 || access(dest_dir, W_OK) != 0) {
        if (color) {
            fprintf(stderr, "%serror:%s cannot write to cache directory '%s%s%s': %s\n",
                    ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD_WHITE, dest_dir, ANSI_RESET, strerror(errno));
            if (errno == EACCES || errno == EPERM || geteuid() != 0) {
                fprintf(stderr, "%s::%s Run with sudo or specify a writable directory via AXON_CACHE_PATH.\n",
                        ANSI_BOLD_YELLOW, ANSI_RESET);
            }
        } else {
            fprintf(stderr, "error: cannot write to cache directory '%s': %s\n", dest_dir, strerror(errno));
            if (errno == EACCES || errno == EPERM || geteuid() != 0) {
                fprintf(stderr, ":: Run with sudo or specify a writable directory via AXON_CACHE_PATH.\n");
            }
        }
        return -1;
    }

    struct utsname un;
    const char *repo_arch = "x86_64";
    if (uname(&un) == 0) {
        repo_arch = un.machine;
    }

    char **mirrors = NULL;
    size_t mirror_count = 0;
    axon_mirrorlist_load(cfg->mirrorlist_path, &mirrors, &mirror_count);

    axon_download_task_t *tasks = calloc(count, sizeof(axon_download_task_t));
    size_t *task_to_orig = calloc(count, sizeof(size_t));
    if (!tasks || !task_to_orig) {
        free(tasks);
        free(task_to_orig);
        axon_mirrorlist_free(mirrors, mirror_count);
        return -1;
    }

    size_t need_download = 0;

    for (size_t i = 0; i < count; i++) {
        axon_package_t *pkg = pkgs[i];
        if (!pkg || !pkg->name) continue;

        const char *pkg_arch = (pkg->arch && strlen(pkg->arch) > 0) ? pkg->arch : repo_arch;
        char gen_name[512];
        const char *filename = pkg->filename;
        if (!filename || strlen(filename) == 0) {
            snprintf(gen_name, sizeof(gen_name), "%s-%s-%s.pkg.tar.zst",
                     pkg->name ? pkg->name : "package",
                     pkg->version ? pkg->version : "1.0",
                     pkg_arch);
            filename = gen_name;
        }

        char dest_file[1024];
        snprintf(dest_file, sizeof(dest_file), "%s/%s", dest_dir, filename);

        /* Check existing cache */
        struct stat st;
        if (stat(dest_file, &st) == 0 && S_ISREG(st.st_mode) &&
            (pkg->csize == 0 || (size_t)st.st_size == pkg->csize)) {
            out_filepaths[i] = strdup(dest_file);
            continue;
        }

        axon_download_task_t *t = &tasks[need_download];
        task_to_orig[need_download] = i;
        t->display_name = strdup(filename);
        t->dest_path = strdup(dest_file);
        t->expected_size = pkg->csize;

        if (!pkg->is_arch && pkg->url &&
            (strncmp(pkg->url, "http://", 7) == 0 || strncmp(pkg->url, "https://", 8) == 0)) {
            t->urls = malloc(sizeof(char *));
            t->urls[0] = strdup(pkg->url);
            t->url_count = 1;
        } else {
            if (mirror_count == 0) {
                if (color) {
                    fprintf(stderr, "%serror:%s no mirror servers configured or available for %s\n",
                            ANSI_BOLD_RED, ANSI_RESET, pkg->name);
                } else {
                    fprintf(stderr, "error: no mirror servers configured or available for %s\n", pkg->name);
                }
                for (size_t k = 0; k <= need_download; k++) {
                    free((char *)tasks[k].display_name);
                    free((char *)tasks[k].dest_path);
                    if (tasks[k].urls) {
                        for (size_t u = 0; u < tasks[k].url_count; u++) free(tasks[k].urls[u]);
                        free(tasks[k].urls);
                    }
                }
                free(tasks);
                free(task_to_orig);
                axon_mirrorlist_free(mirrors, mirror_count);
                return -1;
            }

            const char *repo_name = pkg->repo ? pkg->repo : "extra";
            t->urls = malloc(sizeof(char *) * mirror_count);
            t->url_count = 0;
            for (size_t m = 0; m < mirror_count; m++) {
                char *u = axon_format_mirror_pkg_url(mirrors[m], repo_name, pkg_arch, filename);
                if (u) {
                    t->urls[t->url_count++] = u;
                }
            }
        }

        need_download++;
    }

    if (need_download == 0) {
        free(tasks);
        free(task_to_orig);
        axon_mirrorlist_free(mirrors, mirror_count);
        return 0;
    }

    int max_concurrent = cfg->parallel_downloads > 0 ? cfg->parallel_downloads : 5;
    if (max_concurrent > (int)need_download) max_concurrent = (int)need_download;

    if (need_download == 1) {
        size_t orig = task_to_orig[0];
        if (color) {
            printf("%s::%s Downloading package archive for %s%s%s...\n",
                   ANSI_BOLD_BLUE, ANSI_RESET, ANSI_BOLD_WHITE, pkgs[orig]->name, ANSI_RESET);
        } else {
            printf(":: Downloading package archive for %s...\n", pkgs[orig]->name);
        }
    } else {
        if (color) {
            printf("%s::%s Retrieving %zu packages (%s%d%s concurrent downloads)...\n",
                   ANSI_BOLD_BLUE, ANSI_RESET, need_download, ANSI_BOLD_CYAN, max_concurrent, ANSI_RESET);
        } else {
            printf(":: Retrieving %zu packages (%d concurrent downloads)...\n",
                   need_download, max_concurrent);
        }
    }

    int rc = axon_download_parallel(tasks, need_download, max_concurrent, color);

    bool any_failed = false;
    for (size_t t = 0; t < need_download; t++) {
        size_t orig = task_to_orig[t];
        if (tasks[t].status == 0) {
            out_filepaths[orig] = strdup(tasks[t].dest_path);
        } else {
            any_failed = true;
            if (color) {
                fprintf(stderr, "%serror:%s failed to download package archive for %s\n",
                        ANSI_BOLD_RED, ANSI_RESET, pkgs[orig]->name);
            } else {
                fprintf(stderr, "error: failed to download package archive for %s\n", pkgs[orig]->name);
            }
        }
    }

    for (size_t t = 0; t < need_download; t++) {
        free((char *)tasks[t].display_name);
        free((char *)tasks[t].dest_path);
        if (tasks[t].urls) {
            for (size_t u = 0; u < tasks[t].url_count; u++) {
                free(tasks[t].urls[u]);
            }
            free(tasks[t].urls);
        }
    }
    free(tasks);
    free(task_to_orig);
    axon_mirrorlist_free(mirrors, mirror_count);

    return (rc == 0 && !any_failed) ? 0 : -1;
}

int axon_repo_download_pkg(axon_repo_mgr_t *mgr, const axon_package_t *pkg, const char *dest_dir, char **out_filepath) {
    if (!pkg) return -1;
    axon_package_t *p = (axon_package_t *)pkg;
    return axon_repo_download_pkgs(mgr, &p, 1, dest_dir, out_filepath);
}
