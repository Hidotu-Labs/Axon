#ifndef AXON_PACKAGE_H
#define AXON_PACKAGE_H

#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *name;
    char *base;
    char *version;
    char *desc;
    char *url;
    char *arch;
    char *repo;
    char *filename;
    char *sha256sum;
    char *pgpsig;
    size_t csize;
    size_t isize;
    time_t builddate;
    char *packager;
    char **licenses;
    size_t licenses_count;
    char **depends;
    size_t depends_count;
    char **optdepends;
    size_t optdepends_count;
    char **provides;
    size_t provides_count;
    char **conflicts;
    size_t conflicts_count;
    char **replaces;
    size_t replaces_count;
    bool is_arch;
} axon_package_t;

typedef struct {
    axon_package_t **packages;
    size_t count;
    size_t capacity;
} axon_pkg_list_t;

/* Package lifecycle */
axon_package_t *axon_package_new(void);
void axon_package_free(axon_package_t *pkg);
axon_package_t *axon_package_clone(const axon_package_t *pkg);

/* Package list operations */
axon_pkg_list_t *axon_pkg_list_new(void);
void axon_pkg_list_free(axon_pkg_list_t *list);
int axon_pkg_list_add(axon_pkg_list_t *list, axon_package_t *pkg);

/* Package parsing */
int axon_package_parse_desc(axon_package_t *pkg, const char *content);

/* Package version comparison */
int axon_version_cmp(const char *ver1, const char *ver2);

/* Archive manipulation */
int axon_archive_extract(const char *archive_path, const char *dest_dir);
int axon_archive_extract_pkg(const char *archive_path, const char *dest_dir, char ***out_files, size_t *out_count);

/* System safety & protection */
bool axon_is_protected_pkg(const char *name);
bool axon_is_critical_file(const char *rel_path);
int axon_archive_check_conflicts(const char *archive_path, const char *dest_dir, const char *pkgname, const void *cfg, char ***conflicts, size_t *out_count);

/* Output formatting */
void axon_package_print_summary(const axon_package_t *pkg, bool color);
void axon_package_print_details(const axon_package_t *pkg, bool color);

#ifdef __cplusplus
}
#endif

#endif /* AXON_PACKAGE_H */
