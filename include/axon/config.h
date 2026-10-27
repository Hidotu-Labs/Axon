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
    int parallel_downloads;
    const char **assume_installed; /* Borrowed bare dependency names, command lifetime. */
    size_t assume_installed_count;
    axon_repo_entry_t *repos;
    size_t repo_count;
} axon_config_t;

int axon_config_init(axon_config_t *cfg);
int axon_config_load(axon_config_t *cfg, const char *path);
int axon_config_add_repo(axon_config_t *cfg, const char *name, const char *url, bool is_arch, int priority);
/* Requires an existing root; redirects all package state below its canonical path. */
int axon_config_set_root(axon_config_t *cfg, const char *root);
void axon_config_free(axon_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* AXON_CONFIG_H */
