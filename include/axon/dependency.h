#ifndef AXON_DEPENDENCY_H
#define AXON_DEPENDENCY_H

#include "axon/package.h"
#include "axon/repository.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resolves dependency tree for a package into resolution_order */
int axon_resolve_deps(axon_repo_mgr_t *mgr, const char *pkgname, axon_pkg_list_t *resolution_order);

/* Checks if package conflicts with any installed or staged packages */
int axon_check_conflicts(const axon_config_t *cfg, const axon_package_t *pkg, char ***conflicts, size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* AXON_DEPENDENCY_H */
