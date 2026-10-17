#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "axon/database.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int axon_db_check_ownership(const axon_config_t *cfg, const char *filepath, char **owner_pkg) {
    if (!filepath || !owner_pkg) return -1;
    *owner_pkg = NULL;

    const char *db_dir = "/var/lib/pacman/local";
    if (cfg && cfg->db_path) {
        char local_path[512];
        snprintf(local_path, sizeof(local_path), "%s/local", cfg->db_path);
        struct stat st;
        if (stat(local_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            db_dir = local_path;
        }
    }

    DIR *dir = opendir(db_dir);
    if (!dir) return -1;

    /* Strip leading slash for relative matching */
    const char *rel_path = (*filepath == '/') ? filepath + 1 : filepath;

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
                *owner_pkg = strdup(de->d_name);
                fclose(fp);
                closedir(dir);
                return 0;
            }
        }
        fclose(fp);
    }

    closedir(dir);
    return -1;
}
