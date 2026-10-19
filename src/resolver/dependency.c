#include "axon/dependency.h"
#include "axon/database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int resolve_recursive(axon_repo_mgr_t *mgr, const char *pkgname, axon_pkg_list_t *order, char **visited, size_t *visited_count) {
    if (!pkgname || *visited_count >= 1024) return -1;

    /* Strip version constraints like >=, <=, =, > */
    char clean_name[128];
    strncpy(clean_name, pkgname, sizeof(clean_name) - 1);
    clean_name[sizeof(clean_name) - 1] = '\0';
    char *op = strpbrk(clean_name, "<>=");
    if (op) *op = '\0';

    /* Trim trailing whitespace */
    size_t clen = strlen(clean_name);
    while (clen > 0 && (clean_name[clen - 1] == ' ' || clean_name[clen - 1] == '\t')) {
        clean_name[--clen] = '\0';
    }

    /* Check if already visited in this resolution pass */
    for (size_t i = 0; i < *visited_count; i++) {
        if (strcmp(visited[i], clean_name) == 0) {
            return 0;
        }
    }

    /* Check if already scheduled in order */
    for (size_t i = 0; i < order->count; i++) {
        if (order->packages[i]->name && strcmp(order->packages[i]->name, clean_name) == 0) {
            return 0;
        }
    }

    visited[*visited_count] = strdup(clean_name);
    (*visited_count)++;

    /* Check if already installed on the system */
    if (mgr->cfg && axon_db_is_installed(mgr->cfg, clean_name)) {
        return 0;
    }

    /* Check if protected system package on live host */
    if (axon_is_protected_pkg(clean_name)) {
        if (!mgr->cfg || !mgr->cfg->root_dir || strcmp(mgr->cfg->root_dir, "/") == 0) {
            return 0;
        }
    }

    axon_package_t *pkg = axon_repo_find(mgr, clean_name);
    if (!pkg) {
        return -1;
    }

    /* Resolve dependencies first (depth-first) */
    for (size_t i = 0; i < pkg->depends_count; i++) {
        int r = resolve_recursive(mgr, pkg->depends[i], order, visited, visited_count);
        if (r != 0) {
            /* Check if dependency is satisfied by an installed package */
            char dep_clean[128];
            strncpy(dep_clean, pkg->depends[i], sizeof(dep_clean) - 1);
            dep_clean[sizeof(dep_clean) - 1] = '\0';
            char *dop = strpbrk(dep_clean, "<>=");
            if (dop) *dop = '\0';
            size_t dlen = strlen(dep_clean);
            while (dlen > 0 && (dep_clean[dlen - 1] == ' ' || dep_clean[dlen - 1] == '\t')) {
                dep_clean[--dlen] = '\0';
            }
            if (mgr->cfg && axon_db_is_installed(mgr->cfg, dep_clean)) {
                continue;
            }
            if (axon_is_protected_pkg(dep_clean)) {
                if (!mgr->cfg || !mgr->cfg->root_dir || strcmp(mgr->cfg->root_dir, "/") == 0) {
                    continue;
                }
            }
            axon_package_free(pkg);
            return -1;
        }
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
