#include "axon/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int axon_config_init(axon_config_t *cfg) {
    if (!cfg) return -1;
    memset(cfg, 0, sizeof(axon_config_t));

    cfg->root_dir = strdup("/");
    cfg->db_path = strdup("/var/lib/axon");
    cfg->cache_path = strdup("/var/cache/axon/pkg");
    cfg->sync_path = strdup("/var/lib/axon/sync");
    cfg->arch_sync_path = strdup("/var/lib/pacman/sync");
    cfg->mirrorlist_path = strdup("/etc/axon/mirrorlist");
    cfg->color = isatty(STDOUT_FILENO);
    cfg->verbose = false;

    return 0;
}

int axon_config_load(axon_config_t *cfg, const char *path) {
    if (!cfg) return -1;
    if (!path) return 0;

    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        char key[128], val[384];
        if (sscanf(p, "%127[^=]=%383s", key, val) == 2) {
            /* Trim */
            char *ek = key + strlen(key) - 1;
            while (ek > key && (*ek == ' ' || *ek == '\t')) *ek-- = '\0';

            if (strcmp(key, "RootDir") == 0) {
                free(cfg->root_dir);
                cfg->root_dir = strdup(val);
            } else if (strcmp(key, "DBPath") == 0) {
                free(cfg->db_path);
                cfg->db_path = strdup(val);
            } else if (strcmp(key, "CacheDir") == 0) {
                free(cfg->cache_path);
                cfg->cache_path = strdup(val);
            } else if (strcmp(key, "SyncDir") == 0) {
                free(cfg->sync_path);
                cfg->sync_path = strdup(val);
            } else if (strcmp(key, "ArchSyncDir") == 0) {
                free(cfg->arch_sync_path);
                cfg->arch_sync_path = strdup(val);
            } else if (strcmp(key, "MirrorList") == 0) {
                free(cfg->mirrorlist_path);
                cfg->mirrorlist_path = strdup(val);
            } else if (strcmp(key, "Color") == 0) {
                cfg->color = (strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0 || strcasecmp(val, "auto") == 0);
            }
        }
    }

    fclose(fp);
    return 0;
}

void axon_config_free(axon_config_t *cfg) {
    if (!cfg) return;
    free(cfg->root_dir);
    free(cfg->db_path);
    free(cfg->cache_path);
    free(cfg->sync_path);
    free(cfg->arch_sync_path);
    free(cfg->mirrorlist_path);

    for (size_t i = 0; i < cfg->repo_count; i++) {
        free(cfg->repos[i].name);
        free(cfg->repos[i].url);
    }
    free(cfg->repos);
    memset(cfg, 0, sizeof(axon_config_t));
}
