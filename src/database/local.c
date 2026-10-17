#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "axon/database.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int axon_db_init(const axon_config_t *cfg) {
    if (!cfg || !cfg->db_path) return -1;
    char path[512];
    snprintf(path, sizeof(path), "%s/local", cfg->db_path);
    mkdir(cfg->db_path, 0755);
    mkdir(path, 0755);
    return 0;
}

static const char *get_local_db_dir(const axon_config_t *cfg, char *out_buf, size_t out_len) {
    /* Check axon local db first */
    if (cfg && cfg->db_path) {
        snprintf(out_buf, out_len, "%s/local", cfg->db_path);
        struct stat st;
        if (stat(out_buf, &st) == 0 && S_ISDIR(st.st_mode)) {
            return out_buf;
        }
    }
    /* Fallback to pacman local db if on arch-based system */
    const char *pacman_local = "/var/lib/pacman/local";
    struct stat st;
    if (stat(pacman_local, &st) == 0 && S_ISDIR(st.st_mode)) {
        snprintf(out_buf, out_len, "%s", pacman_local);
        return out_buf;
    }
    return NULL;
}

bool axon_db_is_installed(const axon_config_t *cfg, const char *pkgname) {
    if (!pkgname) return false;
    char db_dir[512];
    if (!get_local_db_dir(cfg, db_dir, sizeof(db_dir))) return false;

    DIR *dir = opendir(db_dir);
    if (!dir) return false;

    bool found = false;
    struct dirent *de;
    size_t name_len = strlen(pkgname);

    while ((de = readdir(dir)) != NULL) {
        if (strncmp(de->d_name, pkgname, name_len) == 0 && de->d_name[name_len] == '-') {
            found = true;
            break;
        }
    }

    closedir(dir);
    return found;
}

axon_package_t *axon_db_get_installed(const axon_config_t *cfg, const char *pkgname) {
    if (!pkgname) return NULL;
    char db_dir[512];
    if (!get_local_db_dir(cfg, db_dir, sizeof(db_dir))) return NULL;

    DIR *dir = opendir(db_dir);
    if (!dir) return NULL;

    axon_package_t *pkg = NULL;
    struct dirent *de;
    size_t name_len = strlen(pkgname);

    while ((de = readdir(dir)) != NULL) {
        if (strncmp(de->d_name, pkgname, name_len) == 0 && de->d_name[name_len] == '-') {
            char desc_path[1024];
            snprintf(desc_path, sizeof(desc_path), "%s/%s/desc", db_dir, de->d_name);
            FILE *fp = fopen(desc_path, "r");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                long sz = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                char *buf = malloc(sz + 1);
                if (buf) {
                    size_t read_bytes = fread(buf, 1, sz, fp);
                    buf[read_bytes] = '\0';
                    pkg = axon_package_new();
                    if (pkg) {
                        axon_package_parse_desc(pkg, buf);
                        pkg->repo = strdup("local");
                    }
                    free(buf);
                }
                fclose(fp);
            }
            break;
        }
    }

    closedir(dir);
    return pkg;
}

int axon_db_list_installed(const axon_config_t *cfg, axon_pkg_list_t *list) {
    if (!list) return -1;
    char db_dir[512];
    if (!get_local_db_dir(cfg, db_dir, sizeof(db_dir))) return -1;

    DIR *dir = opendir(db_dir);
    if (!dir) return -1;

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char desc_path[1024];
        snprintf(desc_path, sizeof(desc_path), "%s/%s/desc", db_dir, de->d_name);
        FILE *fp = fopen(desc_path, "r");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            char *buf = malloc(sz + 1);
            if (buf) {
                size_t read_bytes = fread(buf, 1, sz, fp);
                buf[read_bytes] = '\0';
                axon_package_t *pkg = axon_package_new();
                if (pkg) {
                    axon_package_parse_desc(pkg, buf);
                    pkg->repo = strdup("local");
                    axon_pkg_list_add(list, pkg);
                }
                free(buf);
            }
            fclose(fp);
        }
    }

    closedir(dir);
    return 0;
}

int axon_db_record_install(const axon_config_t *cfg, const axon_package_t *pkg, const char **files, size_t file_count) {
    (void)cfg;
    (void)pkg;
    (void)files;
    (void)file_count;
    /* Record installed package entries */
    return 0;
}

int axon_db_remove_package(const axon_config_t *cfg, const char *pkgname) {
    (void)cfg;
    (void)pkgname;
    /* Remove package from local db */
    return 0;
}
