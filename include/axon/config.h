#ifndef AXON_CONFIG_H
#define AXON_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *name;
    char *url;
    bool is_arch;
    int priority; /* Lower number = higher priority */
} axon_repo_entry_t;

typedef struct {
    char *root_dir;
    char *db_path;
    char *cache_path;
    char *sync_path;
    char *arch_sync_path;
    char *mirrorlist_path;
    bool color;
    bool verbose;
    axon_repo_entry_t *repos;
    size_t repo_count;
} axon_config_t;

int axon_config_init(axon_config_t *cfg);
int axon_config_load(axon_config_t *cfg, const char *path);
int axon_config_add_repo(axon_config_t *cfg, const char *name, const char *url, bool is_arch, int priority);
void axon_config_free(axon_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* AXON_CONFIG_H */
