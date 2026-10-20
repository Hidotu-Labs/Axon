#ifndef AXON_REPOSITORY_H
#define AXON_REPOSITORY_H

#include "axon/package.h"
#include "axon/config.h"
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *name;
    char *db_file;
    char *url;
    bool is_arch;
    int priority;
} axon_repo_t;

typedef struct {
    axon_repo_t *repos;
    size_t count;
    const axon_config_t *cfg;
} axon_repo_mgr_t;

/* Repository Manager */
int axon_repo_mgr_init(axon_repo_mgr_t *mgr, const axon_config_t *cfg);
void axon_repo_mgr_free(axon_repo_mgr_t *mgr);

/* Search functions */
int axon_repo_search(axon_repo_mgr_t *mgr, const char *query, axon_pkg_list_t *results);
axon_package_t *axon_repo_find(axon_repo_mgr_t *mgr, const char *pkgname);

/* Custom / GitHub Repository specific searching */
int axon_custom_repo_search(const axon_config_t *cfg, const axon_repo_t *repo, const char *query, axon_pkg_list_t *results);
axon_package_t *axon_custom_repo_find(const axon_config_t *cfg, const axon_repo_t *repo, const char *pkgname);

/* Arch Linux repository specific searching */
int axon_arch_search_db(const char *db_path, const char *repo_name, const char *query, axon_pkg_list_t *results);
int axon_arch_search_all(const axon_config_t *cfg, const char *query, axon_pkg_list_t *results);
axon_package_t *axon_arch_find_pkg(const axon_config_t *cfg, const char *pkgname);

/* Mirrorlist */
int axon_mirrorlist_load(const char *path, char ***mirrors, size_t *count);
void axon_mirrorlist_free(char **mirrors, size_t count);

/* File utilities */
int axon_mkdir_p(const char *path, mode_t mode);

/* Downloader */
int axon_download_file(const char *url, const char *dest_path, const char *display_name, bool show_progress, bool color);
int axon_repo_sync_all(axon_repo_mgr_t *mgr);
int axon_repo_download_pkg(axon_repo_mgr_t *mgr, const axon_package_t *pkg, const char *dest_dir, char **out_filepath);

#ifdef __cplusplus
}
#endif

#endif /* AXON_REPOSITORY_H */
