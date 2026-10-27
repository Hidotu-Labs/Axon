#ifndef AXON_DATABASE_H
#define AXON_DATABASE_H

#include "axon/package.h"
#include "axon/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Local database operations */
int axon_db_init(const axon_config_t *cfg);
bool axon_db_is_installed(const axon_config_t *cfg, const char *pkgname);
axon_package_t *axon_db_get_installed(const axon_config_t *cfg, const char *pkgname);
int axon_db_list_installed(const axon_config_t *cfg, axon_pkg_list_t *list);
int axon_db_record_install(const axon_config_t *cfg, const axon_package_t *pkg, const char **files, size_t file_count);
int axon_db_remove_package(const axon_config_t *cfg, const char *pkgname);
int axon_db_migrate_legacy(const axon_config_t *cfg);

/* File ownership */
int axon_db_check_ownership(const axon_config_t *cfg, const char *filepath, char **owner_pkg);

#ifdef __cplusplus
}
#endif

#endif /* AXON_DATABASE_H */
