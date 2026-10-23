#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "axon/repository.h"
#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int package_matches_query(const axon_package_t *pkg, const char *query) {
    if (!query || strlen(query) == 0) return 1;

    if (pkg->name && strcasestr(pkg->name, query)) return 1;
    if (pkg->desc && strcasestr(pkg->desc, query)) return 1;

    for (size_t i = 0; i < pkg->provides_count; i++) {
        if (pkg->provides[i] && strcasestr(pkg->provides[i], query)) return 1;
    }

    return 0;
}

int axon_arch_search_db(const char *db_path, const char *repo_name, const char *query, axon_pkg_list_t *results) {
    if (!db_path || !repo_name || !results) return -1;

    struct stat st;
    if (stat(db_path, &st) != 0) {
        return -1;
    }

    struct archive *a = archive_read_new();
    if (!a) return -1;

    archive_read_support_filter_all(a);
    archive_read_support_format_tar(a);

    if (archive_read_open_filename(a, db_path, 65536) != ARCHIVE_OK) {
        archive_read_free(a);
        return -1;
    }

    struct archive_entry *entry;
    char *desc_buf = malloc(65536);
    if (!desc_buf) {
        archive_read_free(a);
        return -1;
    }

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *pathname = archive_entry_pathname(entry);
        if (pathname && strstr(pathname, "/desc")) {
            la_ssize_t size = archive_read_data(a, desc_buf, 65535);
            if (size > 0) {
                desc_buf[size] = '\0';
                axon_package_t *pkg = axon_package_new();
                if (pkg) {
                    if (axon_package_parse_desc(pkg, desc_buf) == 0) {
                        pkg->repo = strdup(repo_name);
                        pkg->is_arch = true;

                        if (package_matches_query(pkg, query)) {
                            axon_pkg_list_add(results, pkg);
                            continue;
                        }
                    }
                    axon_package_free(pkg);
                }
            }
        } else {
            archive_read_data_skip(a);
        }
    }

    free(desc_buf);
    archive_read_close(a);
    archive_read_free(a);
    return 0;
}

static const char *get_arch_sync_dir(const axon_config_t *cfg) {
    const char *env_dir = getenv("AXON_SYNC_PATH");
    if (!cfg && env_dir && strlen(env_dir) > 0) {
        struct stat st;
        if (stat(env_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            return env_dir;
        }
    }

    if (cfg && cfg->sync_path) {
        struct stat st;
        if (stat(cfg->sync_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            return cfg->sync_path;
        }
    }

    if (cfg && cfg->arch_sync_path) {
        struct stat st;
        if (stat(cfg->arch_sync_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            return cfg->arch_sync_path;
        }
    }

    if (cfg && cfg->root_dir && strcmp(cfg->root_dir, "/") != 0) return NULL;

    /* Standard pacman sync fallback */
    const char *default_pacman = "/var/lib/pacman/sync";
    struct stat st;
    if (stat(default_pacman, &st) == 0 && S_ISDIR(st.st_mode)) {
        return default_pacman;
    }

    return NULL;
}

int axon_arch_search_all(const axon_config_t *cfg, const char *query, axon_pkg_list_t *results) {
    const char *sync_dir = get_arch_sync_dir(cfg);
    if (!sync_dir) {
        return -1;
    }

    DIR *dir = opendir(sync_dir);
    if (!dir) return -1;

    /* Preferred order: core, extra, multilib, then others */
    static const char *priority_repos[] = {"core", "extra", "multilib", NULL};

    for (int p = 0; priority_repos[p]; p++) {
        char db_path[512];
        snprintf(db_path, sizeof(db_path), "%s/%s.db", sync_dir, priority_repos[p]);
        struct stat st;
        if (stat(db_path, &st) == 0) {
            axon_arch_search_db(db_path, priority_repos[p], query, results);
        }
    }

    /* Any other .db files in sync_dir */
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        size_t len = strlen(de->d_name);
        if (len > 3 && strcmp(de->d_name + len - 3, ".db") == 0) {
            char repo_name[256];
            size_t copy_len = len - 3;
            if (copy_len >= sizeof(repo_name)) copy_len = sizeof(repo_name) - 1;
            memcpy(repo_name, de->d_name, copy_len);
            repo_name[copy_len] = '\0';

            /* Skip already searched priority repos */
            bool already_done = false;
            for (int p = 0; priority_repos[p]; p++) {
                if (strcmp(repo_name, priority_repos[p]) == 0) {
                    already_done = true;
                    break;
                }
            }
            if (already_done) continue;

            char db_path[512];
            snprintf(db_path, sizeof(db_path), "%s/%s", sync_dir, de->d_name);
            axon_arch_search_db(db_path, repo_name, query, results);
        }
    }

    closedir(dir);
    return 0;
}

static axon_package_t *axon_arch_find_in_db(const char *db_path, const char *repo_name, const char *pkgname) {
    if (!db_path || !repo_name || !pkgname) return NULL;

    struct stat st;
    if (stat(db_path, &st) != 0) return NULL;

    struct archive *a = archive_read_new();
    if (!a) return NULL;

    archive_read_support_filter_all(a);
    archive_read_support_format_tar(a);

    if (archive_read_open_filename(a, db_path, 65536) != ARCHIVE_OK) {
        archive_read_free(a);
        return NULL;
    }

    size_t pkglen = strlen(pkgname);
    struct archive_entry *entry;
    axon_package_t *found = NULL;
    char *desc_buf = malloc(65536);
    if (!desc_buf) {
        archive_read_free(a);
        return NULL;
    }

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *pathname = archive_entry_pathname(entry);
        if (pathname && strncmp(pathname, pkgname, pkglen) == 0 && pathname[pkglen] == '-' && strstr(pathname, "/desc")) {
            la_ssize_t size = archive_read_data(a, desc_buf, 65535);
            if (size > 0) {
                desc_buf[size] = '\0';
                axon_package_t *pkg = axon_package_new();
                if (pkg) {
                    if (axon_package_parse_desc(pkg, desc_buf) == 0 &&
                        pkg->name && strcmp(pkg->name, pkgname) == 0) {
                        pkg->repo = strdup(repo_name);
                        pkg->is_arch = true;
                        found = pkg;
                        break;
                    }
                    axon_package_free(pkg);
                }
            }
        } else {
            archive_read_data_skip(a);
        }
    }

    free(desc_buf);
    archive_read_close(a);
    archive_read_free(a);
    return found;
}

axon_package_t *axon_arch_find_pkg(const axon_config_t *cfg, const char *pkgname) {
    if (!pkgname) return NULL;

    const char *sync_dir = get_arch_sync_dir(cfg);
    if (!sync_dir) return NULL;

    /* Check priority repos first */
    static const char *priority_repos[] = {"core", "extra", "multilib", NULL};
    for (int p = 0; priority_repos[p]; p++) {
        char db_path[512];
        snprintf(db_path, sizeof(db_path), "%s/%s.db", sync_dir, priority_repos[p]);
        axon_package_t *found = axon_arch_find_in_db(db_path, priority_repos[p], pkgname);
        if (found) return found;
    }

    /* Check any other .db files */
    DIR *dir = opendir(sync_dir);
    if (!dir) return NULL;

    axon_package_t *found = NULL;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        size_t len = strlen(de->d_name);
        if (len > 3 && strcmp(de->d_name + len - 3, ".db") == 0) {
            char repo_name[256];
            size_t copy_len = len - 3;
            if (copy_len >= sizeof(repo_name)) copy_len = sizeof(repo_name) - 1;
            memcpy(repo_name, de->d_name, copy_len);
            repo_name[copy_len] = '\0';

            bool already_done = false;
            for (int p = 0; priority_repos[p]; p++) {
                if (strcmp(repo_name, priority_repos[p]) == 0) {
                    already_done = true;
                    break;
                }
            }
            if (already_done) continue;

            char db_path[512];
            snprintf(db_path, sizeof(db_path), "%s/%s", sync_dir, de->d_name);
            found = axon_arch_find_in_db(db_path, repo_name, pkgname);
            if (found) break;
        }
    }

    closedir(dir);
    return found;
}
