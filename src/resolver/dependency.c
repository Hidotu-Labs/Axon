#include "axon/dependency.h"
#include "axon/database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct { char name[256]; char op[3]; char version[256]; } requirement_t;

static int parse_requirement(const char *text, requirement_t *out) {
    if (!text || !*text || strlen(text) >= 512) return -1;
    memset(out, 0, sizeof(*out));
    const char *op = strpbrk(text, "<>=");
    size_t n = op ? (size_t)(op - text) : strlen(text);
    if (!n || n >= sizeof(out->name)) return -1;
    memcpy(out->name, text, n);
    if (out->name[0] == '-' || out->name[0] == '.') return -1;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)out->name[i]) && !strchr("@._+-", out->name[i])) return -1;
    if (!op) return 0;
    out->op[0] = *op++;
    if (*op == '=' && out->op[0] != '=') out->op[1] = *op++;
    if (!*op || strlen(op) >= sizeof(out->version) || strpbrk(op, "<>= \t\r\n")) return -1;
    strcpy(out->version, op);
    return 0;
}

static bool version_matches(const char *version, const requirement_t *r) {
    if (!r->op[0]) return true;
    if (!version || !*version) return false;
    int cmp = axon_version_cmp(version, r->version);
    if (!strcmp(r->op, "=")) return cmp == 0;
    if (!strcmp(r->op, ">=")) return cmp >= 0;
    if (!strcmp(r->op, "<=")) return cmp <= 0;
    if (!strcmp(r->op, ">")) return cmp > 0;
    if (!strcmp(r->op, "<")) return cmp < 0;
    return false;
}

bool axon_package_satisfies(const axon_package_t *pkg, const char *dependency) {
    requirement_t r;
    if (!pkg || parse_requirement(dependency, &r) != 0) return false;
    if (pkg->name && !strcmp(pkg->name, r.name) && version_matches(pkg->version, &r)) return true;
    for (size_t i = 0; i < pkg->provides_count; i++) {
        requirement_t p;
        if (parse_requirement(pkg->provides[i], &p) == 0 && !strcmp(p.name, r.name) &&
            version_matches(p.op[0] == '=' ? p.version : NULL, &r)) return true;
    }
    return false;
}

bool axon_dependency_installed(const axon_config_t *cfg, const char *dependency) {
    requirement_t r;
    if (parse_requirement(dependency, &r) != 0) return false;
    axon_package_t *direct = axon_db_get_installed(cfg, r.name);
    bool matched = axon_package_satisfies(direct, dependency);
    axon_package_free(direct);
    if (matched) return true;
    axon_pkg_list_t *installed = axon_pkg_list_new();
    if (!installed) return false;
    axon_db_list_installed(cfg, installed);
    for (size_t i = 0; !matched && i < installed->count; i++)
        matched = axon_package_satisfies(installed->packages[i], dependency);
    axon_pkg_list_free(installed);
    return matched;
}

static int resolve_recursive(axon_repo_mgr_t *mgr, const char *dependency,
                             axon_pkg_list_t *order, axon_package_t **active, size_t depth) {
    requirement_t r;
    if (depth >= 1024 || parse_requirement(dependency, &r) != 0) return -1;
    if (mgr->cfg) {
        for (size_t i = 0; i < mgr->cfg->assume_installed_count; i++)
            if (!strcmp(mgr->cfg->assume_installed[i], r.name)) return 0;
    }
    for (size_t i = 0; i < order->count; i++) {
        if (axon_package_satisfies(order->packages[i], dependency)) return 0;
        if (order->packages[i]->name && !strcmp(order->packages[i]->name, r.name)) return -1;
    }
    for (size_t i = 0; i < depth; i++) {
        if (axon_package_satisfies(active[i], dependency)) return 0;
        if (!strcmp(active[i]->name, r.name)) return -1;
    }
    if (axon_dependency_installed(mgr->cfg, dependency)) return 0;

    /* Refuse an implicit upgrade until replacement/removal can be transactional. */
    axon_package_t *installed = axon_db_get_installed(mgr->cfg, r.name);
    if (installed) {
        fprintf(stderr, "error: installed %s %s does not satisfy %s; upgrade required\n",
                r.name, installed->version ? installed->version : "unknown", dependency);
        axon_package_free(installed);
        return -1;
    }
    axon_package_t *pkg = axon_repo_find(mgr, r.name);
    if (!axon_package_satisfies(pkg, dependency)) {
        axon_package_free(pkg); pkg = NULL;
        axon_pkg_list_t *providers = axon_pkg_list_new();
        if (!providers) return -1;
        axon_repo_search(mgr, r.name, providers);
        for (size_t i = 0; i < providers->count; i++) {
            if (axon_package_satisfies(providers->packages[i], dependency)) {
                pkg = axon_package_clone(providers->packages[i]); break;
            }
        }
        axon_pkg_list_free(providers);
    }
    if (!pkg) {
        fprintf(stderr, "error: no package satisfies %s\n", dependency);
        return -1;
    }
    if (axon_is_protected_pkg(pkg->name) && (!mgr->cfg->root_dir || !strcmp(mgr->cfg->root_dir, "/"))) {
        fprintf(stderr, "error: refusing protected dependency %s on live root\n", pkg->name);
        axon_package_free(pkg); return -1;
    }
    active[depth] = pkg;
    for (size_t i = 0; i < pkg->depends_count; i++) {
        if (resolve_recursive(mgr, pkg->depends[i], order, active, depth + 1) != 0) {
            axon_package_free(pkg); return -1;
        }
    }
    if (axon_pkg_list_add(order, pkg) != 0) { axon_package_free(pkg); return -1; }
    return 0;
}

int axon_resolve_deps(axon_repo_mgr_t *mgr, const char *dependency, axon_pkg_list_t *order) {
    if (!mgr || !mgr->cfg || !order) return -1;
    axon_package_t *active[1024];
    return resolve_recursive(mgr, dependency, order, active, 0);
}
