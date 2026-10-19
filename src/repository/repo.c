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

int axon_repo_search(axon_repo_mgr_t *mgr, const char *query, axon_pkg_list_t *results) {
    if (!results) return -1;
    const axon_config_t *cfg = mgr ? mgr->cfg : NULL;
    return axon_arch_search_all(cfg, query, results);
}

axon_package_t *axon_repo_find(axon_repo_mgr_t *mgr, const char *pkgname) {
    if (!pkgname) return NULL;
    const axon_config_t *cfg = mgr ? mgr->cfg : NULL;

    /* Search configured repos / fallback Arch */
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

    /* Standard repositories to sync */
    static const char *default_repos[] = {"core", "extra", "multilib", NULL};
    const char **repos_to_sync = default_repos;
    size_t repo_count = 3;

    /* If manager has configured repos, use those */
    char **custom_repo_names = NULL;
    if (mgr->count > 0) {
        custom_repo_names = malloc(sizeof(char *) * (mgr->count + 1));
        if (custom_repo_names) {
            for (size_t i = 0; i < mgr->count; i++) {
                custom_repo_names[i] = mgr->repos[i].name;
            }
            custom_repo_names[mgr->count] = NULL;
            repos_to_sync = (const char **)custom_repo_names;
            repo_count = mgr->count;
        }
    }

    int overall_status = 0;

    for (size_t r = 0; r < repo_count; r++) {
        const char *repo_name = repos_to_sync[r];
        if (!repo_name) continue;

        if (color) {
            printf("%s::%s Synchronizing %s%s%s repository database...\n",
                   ANSI_BOLD_BLUE, ANSI_RESET, ANSI_BOLD_MAGENTA, repo_name, ANSI_RESET);
        } else {
            printf(":: Synchronizing %s repository database...\n", repo_name);
        }

        char dest_file[512];
        snprintf(dest_file, sizeof(dest_file), "%s/%s.db", sync_dir, repo_name);

        char display_name[64];
        snprintf(display_name, sizeof(display_name), "%s.db", repo_name);

        bool success = false;
        bool local_file_error = false;

        /* Try mirrors in sequence */
        for (size_t m = 0; m < mirror_count; m++) {
            char *url = format_repo_url(mirrors[m], repo_name, arch);
            if (!url) continue;

            if (cfg->verbose) {
                printf("   Trying %s\n", url);
            }

            int rc = axon_download_file(url, dest_file, display_name, true, color);
            free(url);

            if (rc == 0) {
                success = true;

                /* Create .db.tar.gz symlink for compatibility */
                char gz_link[512];
                snprintf(gz_link, sizeof(gz_link), "%s/%s.db.tar.gz", sync_dir, repo_name);
                remove(gz_link);
                symlink(display_name, gz_link);

                break;
            } else if (rc == -2) {
                local_file_error = true;
                break;
            }
        }

        if (!success) {
            if (!local_file_error) {
                if (color) {
                    fprintf(stderr, "  %serror:%s failed to download %s%s.db%s from all mirrors\n",
                            ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD_WHITE, repo_name, ANSI_RESET);
                } else {
                    fprintf(stderr, "  error: failed to download %s.db from all mirrors\n", repo_name);
                }
            }
            overall_status = -1;
        }
    }

    free(custom_repo_names);
    axon_mirrorlist_free(mirrors, mirror_count);
    return overall_status;
}

int axon_repo_download_pkg(axon_repo_mgr_t *mgr, const axon_package_t *pkg, const char *dest_dir, char **out_filepath) {
    if (!mgr || !mgr->cfg || !pkg || !dest_dir) return -1;
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

    const char *pkg_arch = (pkg->arch && strlen(pkg->arch) > 0) ? pkg->arch : repo_arch;

    char generated_filename[512];
    const char *filename = pkg->filename;
    if (!filename || strlen(filename) == 0) {
        snprintf(generated_filename, sizeof(generated_filename), "%s-%s-%s.pkg.tar.zst",
                 pkg->name ? pkg->name : "package",
                 pkg->version ? pkg->version : "1.0",
                 pkg_arch);
        filename = generated_filename;
    }

    char dest_file[1024];
    snprintf(dest_file, sizeof(dest_file), "%s/%s", dest_dir, filename);

    /* If file already exists and matches expected size (if csize > 0), reuse it */
    struct stat st;
    if (stat(dest_file, &st) == 0 && S_ISREG(st.st_mode) && (pkg->csize == 0 || (size_t)st.st_size == pkg->csize)) {
        if (out_filepath) {
            *out_filepath = strdup(dest_file);
        }
        return 0;
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

    const char *repo_name = pkg->repo ? pkg->repo : "extra";

    bool success = false;
    for (size_t m = 0; m < mirror_count; m++) {
        char base_url[1024];
        base_url[0] = '\0';
        const char *src = mirrors[m];
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
                size_t alen = strlen(repo_arch);
                if (alen >= rem) break;
                memcpy(dst, repo_arch, alen);
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

        if (cfg->verbose) {
            printf("   Downloading from %s\n", pkg_url);
        }

        int rc = axon_download_file(pkg_url, dest_file, filename, true, color);
        if (rc == 0) {
            success = true;
            break;
        } else if (rc == -2) {
            break;
        }
    }

    axon_mirrorlist_free(mirrors, mirror_count);

    if (success) {
        if (out_filepath) {
            *out_filepath = strdup(dest_file);
        }
        return 0;
    }

    return -1;
}
