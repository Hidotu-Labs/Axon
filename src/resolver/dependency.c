#include "axon/dependency.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int resolve_recursive(axon_repo_mgr_t *mgr, const char *pkgname, axon_pkg_list_t *order, char **visited, size_t *visited_count) {
    /* Strip version constraints like >=, <=, =, > */
    char clean_name[128];
    strncpy(clean_name, pkgname, sizeof(clean_name) - 1);
    clean_name[sizeof(clean_name) - 1] = '\0';
    char *op = strpbrk(clean_name, "<>=");
    if (op) *op = '\0';

    /* Check if already visited */
    for (size_t i = 0; i < *visited_count; i++) {
        if (strcmp(visited[i], clean_name) == 0) {
            return 0;
        }
    }

    visited[*visited_count] = strdup(clean_name);
    (*visited_count)++;

    axon_package_t *pkg = axon_repo_find(mgr, clean_name);
    if (!pkg) {
        return -1;
    }

    /* Resolve dependencies first (depth-first) */
    for (size_t i = 0; i < pkg->depends_count; i++) {
        resolve_recursive(mgr, pkg->depends[i], order, visited, visited_count);
    }

    axon_pkg_list_add(order, pkg);
    return 0;
}

int axon_resolve_deps(axon_repo_mgr_t *mgr, const char *pkgname, axon_pkg_list_t *resolution_order) {
    if (!mgr || !pkgname || !resolution_order) return -1;

    char *visited[1024];
    size_t visited_count = 0;

    int ret = resolve_recursive(mgr, pkgname, resolution_order, visited, &visited_count);

    for (size_t i = 0; i < visited_count; i++) {
        free(visited[i]);
    }

    return ret;
}
