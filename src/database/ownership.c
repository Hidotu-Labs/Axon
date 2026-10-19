#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "axon/database.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char *find_owner_in_dir(const char *db_dir, const char *rel_path) {
    if (!db_dir || !rel_path) return NULL;
    DIR *dir = opendir(db_dir);
    if (!dir) return NULL;

    char *owner = NULL;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char files_path[1024];
        snprintf(files_path, sizeof(files_path), "%s/%s/files", db_dir, de->d_name);
        FILE *fp = fopen(files_path, "r");
        if (!fp) continue;

        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            size_t l = strlen(line);
            while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) {
                line[--l] = '\0';
            }
            if (strcmp(line, rel_path) == 0) {
                owner = strdup(de->d_name);
                break;
            }
        }
        fclose(fp);
        if (owner) break;
    }

    closedir(dir);
    return owner;
}

int axon_db_check_ownership(const axon_config_t *cfg, const char *filepath, char **owner_pkg) {
    if (!filepath || !owner_pkg) return -1;
    *owner_pkg = NULL;

    /* Strip leading slash for relative matching */
    const char *rel_path = (*filepath == '/') ? filepath + 1 : filepath;

    /* 1. Check Axon local database */
    if (cfg && cfg->db_path) {
        char axon_local[512];
        snprintf(axon_local, sizeof(axon_local), "%s/local", cfg->db_path);
        *owner_pkg = find_owner_in_dir(axon_local, rel_path);
        if (*owner_pkg) return 0;
    }

    /* 2. Check Pacman local database */
    char pacman_local[512];
    if (cfg && cfg->root_dir && strcmp(cfg->root_dir, "/") != 0) {
        snprintf(pacman_local, sizeof(pacman_local), "%s/var/lib/pacman/local", cfg->root_dir);
    } else {
        snprintf(pacman_local, sizeof(pacman_local), "/var/lib/pacman/local");
    }

    *owner_pkg = find_owner_in_dir(pacman_local, rel_path);
    if (*owner_pkg) return 0;

    return -1;
}
