#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "axon/package.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

axon_package_t *axon_package_new(void) {
    axon_package_t *pkg = calloc(1, sizeof(axon_package_t));
    return pkg;
}

static void free_string_array(char **arr, size_t count) {
    if (!arr) return;
    for (size_t i = 0; i < count; i++) {
        free(arr[i]);
    }
    free(arr);
}

void axon_package_free(axon_package_t *pkg) {
    if (!pkg) return;
    free(pkg->name);
    free(pkg->base);
    free(pkg->version);
    free(pkg->desc);
    free(pkg->url);
    free(pkg->arch);
    free(pkg->repo);
    free(pkg->filename);
    free(pkg->sha256sum);
    free(pkg->pgpsig);
    free(pkg->packager);

    free_string_array(pkg->licenses, pkg->licenses_count);
    free_string_array(pkg->depends, pkg->depends_count);
    free_string_array(pkg->optdepends, pkg->optdepends_count);
    free_string_array(pkg->provides, pkg->provides_count);
    free_string_array(pkg->conflicts, pkg->conflicts_count);
    free_string_array(pkg->replaces, pkg->replaces_count);

    free(pkg);
}

static char **clone_string_array(char **arr, size_t count) {
    if (!arr || count == 0) return NULL;
    char **new_arr = malloc(sizeof(char *) * count);
    if (!new_arr) return NULL;
    for (size_t i = 0; i < count; i++) {
        new_arr[i] = strdup(arr[i]);
    }
    return new_arr;
}

axon_package_t *axon_package_clone(const axon_package_t *src) {
    if (!src) return NULL;
    axon_package_t *dst = axon_package_new();
    if (!dst) return NULL;

    if (src->name) dst->name = strdup(src->name);
    if (src->base) dst->base = strdup(src->base);
    if (src->version) dst->version = strdup(src->version);
    if (src->desc) dst->desc = strdup(src->desc);
    if (src->url) dst->url = strdup(src->url);
    if (src->arch) dst->arch = strdup(src->arch);
    if (src->repo) dst->repo = strdup(src->repo);
    if (src->filename) dst->filename = strdup(src->filename);
    if (src->sha256sum) dst->sha256sum = strdup(src->sha256sum);
    if (src->pgpsig) dst->pgpsig = strdup(src->pgpsig);
    if (src->packager) dst->packager = strdup(src->packager);

    dst->csize = src->csize;
    dst->isize = src->isize;
    dst->builddate = src->builddate;
    dst->is_arch = src->is_arch;

    dst->licenses = clone_string_array(src->licenses, src->licenses_count);
    dst->licenses_count = src->licenses_count;
    dst->depends = clone_string_array(src->depends, src->depends_count);
    dst->depends_count = src->depends_count;
    dst->optdepends = clone_string_array(src->optdepends, src->optdepends_count);
    dst->optdepends_count = src->optdepends_count;
    dst->provides = clone_string_array(src->provides, src->provides_count);
    dst->provides_count = src->provides_count;
    dst->conflicts = clone_string_array(src->conflicts, src->conflicts_count);
    dst->conflicts_count = src->conflicts_count;
    dst->replaces = clone_string_array(src->replaces, src->replaces_count);
    dst->replaces_count = src->replaces_count;

    return dst;
}

axon_pkg_list_t *axon_pkg_list_new(void) {
    axon_pkg_list_t *list = calloc(1, sizeof(axon_pkg_list_t));
    if (!list) return NULL;
    list->capacity = 16;
    list->packages = malloc(sizeof(axon_package_t *) * list->capacity);
    return list;
}

void axon_pkg_list_free(axon_pkg_list_t *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        axon_package_free(list->packages[i]);
    }
    free(list->packages);
    free(list);
}

int axon_pkg_list_add(axon_pkg_list_t *list, axon_package_t *pkg) {
    if (!list || !pkg) return -1;
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity * 2;
        axon_package_t **new_pkgs = realloc(list->packages, sizeof(axon_package_t *) * new_cap);
        if (!new_pkgs) return -1;
        list->packages = new_pkgs;
        list->capacity = new_cap;
    }
    list->packages[list->count++] = pkg;
    return 0;
}

static void add_to_array(char ***arr, size_t *count, const char *val) {
    char **new_arr = realloc(*arr, sizeof(char *) * (*count + 1));
    if (!new_arr) return;
    *arr = new_arr;
    (*arr)[*count] = strdup(val);
    (*count)++;
}

int axon_package_parse_desc(axon_package_t *pkg, const char *content) {
    if (!pkg || !content) return -1;

    char *copy = strdup(content);
    if (!copy) return -1;

    char *line = strtok(copy, "\r\n");
    char current_tag[64] = {0};

    while (line) {
        /* Trim whitespace */
        while (*line == ' ' || *line == '\t') line++;

        if (line[0] == '%' && line[strlen(line) - 1] == '%') {
            strncpy(current_tag, line, sizeof(current_tag) - 1);
            current_tag[sizeof(current_tag) - 1] = '\0';
        } else if (strlen(line) > 0 && current_tag[0] != '\0') {
            if (strcmp(current_tag, "%NAME%") == 0) {
                if (!pkg->name) pkg->name = strdup(line);
            } else if (strcmp(current_tag, "%BASE%") == 0) {
                if (!pkg->base) pkg->base = strdup(line);
            } else if (strcmp(current_tag, "%VERSION%") == 0) {
                if (!pkg->version) pkg->version = strdup(line);
            } else if (strcmp(current_tag, "%DESC%") == 0) {
                if (!pkg->desc) pkg->desc = strdup(line);
            } else if (strcmp(current_tag, "%URL%") == 0) {
                if (!pkg->url) pkg->url = strdup(line);
            } else if (strcmp(current_tag, "%ARCH%") == 0) {
                if (!pkg->arch) pkg->arch = strdup(line);
            } else if (strcmp(current_tag, "%FILENAME%") == 0) {
                if (!pkg->filename) pkg->filename = strdup(line);
            } else if (strcmp(current_tag, "%SHA256SUM%") == 0) {
                if (!pkg->sha256sum) pkg->sha256sum = strdup(line);
            } else if (strcmp(current_tag, "%PGPSIG%") == 0) {
                if (!pkg->pgpsig) pkg->pgpsig = strdup(line);
            } else if (strcmp(current_tag, "%PACKAGER%") == 0) {
                if (!pkg->packager) pkg->packager = strdup(line);
            } else if (strcmp(current_tag, "%CSIZE%") == 0) {
                pkg->csize = (size_t)strtoull(line, NULL, 10);
            } else if (strcmp(current_tag, "%ISIZE%") == 0) {
                pkg->isize = (size_t)strtoull(line, NULL, 10);
            } else if (strcmp(current_tag, "%BUILDDATE%") == 0) {
                pkg->builddate = (time_t)strtoll(line, NULL, 10);
            } else if (strcmp(current_tag, "%LICENSE%") == 0) {
                add_to_array(&pkg->licenses, &pkg->licenses_count, line);
            } else if (strcmp(current_tag, "%DEPENDS%") == 0) {
                add_to_array(&pkg->depends, &pkg->depends_count, line);
            } else if (strcmp(current_tag, "%OPTDEPENDS%") == 0) {
                add_to_array(&pkg->optdepends, &pkg->optdepends_count, line);
            } else if (strcmp(current_tag, "%PROVIDES%") == 0) {
                add_to_array(&pkg->provides, &pkg->provides_count, line);
            } else if (strcmp(current_tag, "%CONFLICTS%") == 0) {
                add_to_array(&pkg->conflicts, &pkg->conflicts_count, line);
            } else if (strcmp(current_tag, "%REPLACES%") == 0) {
                add_to_array(&pkg->replaces, &pkg->replaces_count, line);
            }
        }
        line = strtok(NULL, "\r\n");
    }

    free(copy);
    return 0;
}

static void format_size(size_t bytes, char *out, size_t out_len) {
    if (bytes >= 1024 * 1024 * 1024) {
        snprintf(out, out_len, "%.2f GiB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024 * 1024) {
        snprintf(out, out_len, "%.2f MiB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(out, out_len, "%.2f KiB", (double)bytes / 1024.0);
    } else {
        snprintf(out, out_len, "%zu B", bytes);
    }
}

void axon_package_print_summary(const axon_package_t *pkg, bool color) {
    if (!pkg) return;
    const char *repo = pkg->repo ? pkg->repo : "unknown";
    const char *name = pkg->name ? pkg->name : "unknown";
    const char *ver = pkg->version ? pkg->version : "?";
    const char *desc = pkg->desc ? pkg->desc : "";

    if (color) {
        if (pkg->is_arch) {
            printf("\033[1;34m[arch]\033[0m \033[1;35m%s\033[0m/\033[1;37m%s\033[0m \033[1;32m%s\033[0m\n",
                   repo, name, ver);
        } else {
            printf("\033[1;36m[avory]\033[0m \033[1;35m%s\033[0m/\033[1;37m%s\033[0m \033[1;32m%s\033[0m\n",
                   repo, name, ver);
        }
        printf("    %s\n", desc);
    } else {
        printf("[%s] %s/%s %s\n    %s\n",
               pkg->is_arch ? "arch" : "avory",
               repo, name, ver, desc);
    }
}

void axon_package_print_details(const axon_package_t *pkg, bool color) {
    if (!pkg) return;
    char csz[32], isz[32];
    format_size(pkg->csize, csz, sizeof(csz));
    format_size(pkg->isize, isz, sizeof(isz));

    if (color) {
        printf("\033[1;36m%-18s:\033[0m \033[1;35m%s\033[0m\n", "Repository", pkg->repo ? pkg->repo : "(unknown)");
        printf("\033[1;36m%-18s:\033[0m \033[1;34m%s\033[0m\n", "Source", pkg->is_arch ? "Arch Linux (fallback)" : "AvoryOS");
        printf("\033[1;36m%-18s:\033[0m \033[1;37m%s\033[0m\n", "Name", pkg->name ? pkg->name : "(unknown)");
        printf("\033[1;36m%-18s:\033[0m \033[1;32m%s\033[0m\n", "Version", pkg->version ? pkg->version : "(unknown)");
        printf("\033[1;36m%-18s:\033[0m %s\n", "Description", pkg->desc ? pkg->desc : "");
        printf("\033[1;36m%-18s:\033[0m %s\n", "Architecture", pkg->arch ? pkg->arch : "(unknown)");
        printf("\033[1;36m%-18s:\033[0m \033[4;34m%s\033[0m\n", "URL", pkg->url ? pkg->url : "(none)");

        if (pkg->licenses_count > 0) {
            printf("\033[1;36m%-18s:\033[0m ", "Licenses");
            for (size_t i = 0; i < pkg->licenses_count; i++) {
                printf("%s%s", pkg->licenses[i], (i + 1 < pkg->licenses_count) ? ", " : "\n");
            }
        }

        if (pkg->depends_count > 0) {
            printf("\033[1;36m%-18s:\033[0m ", "Depends On");
            for (size_t i = 0; i < pkg->depends_count; i++) {
                printf("\033[32m%s\033[0m%s", pkg->depends[i], (i + 1 < pkg->depends_count) ? "  " : "\n");
            }
        }

        if (pkg->optdepends_count > 0) {
            printf("\033[1;36m%-18s:\033[0m\n", "Optional Deps");
            for (size_t i = 0; i < pkg->optdepends_count; i++) {
                printf("    \033[33m%s\033[0m\n", pkg->optdepends[i]);
            }
        }

        if (pkg->provides_count > 0) {
            printf("\033[1;36m%-18s:\033[0m ", "Provides");
            for (size_t i = 0; i < pkg->provides_count; i++) {
                printf("%s%s", pkg->provides[i], (i + 1 < pkg->provides_count) ? ", " : "\n");
            }
        }

        if (pkg->conflicts_count > 0) {
            printf("\033[1;36m%-18s:\033[0m ", "Conflicts With");
            for (size_t i = 0; i < pkg->conflicts_count; i++) {
                printf("\033[1;31m%s\033[0m%s", pkg->conflicts[i], (i + 1 < pkg->conflicts_count) ? ", " : "\n");
            }
        }

        printf("\033[1;36m%-18s:\033[0m \033[1;33m%s\033[0m\n", "Download Size", csz);
        printf("\033[1;36m%-18s:\033[0m \033[1;33m%s\033[0m\n", "Installed Size", isz);
        printf("\033[1;36m%-18s:\033[0m %s\n", "Packager", pkg->packager ? pkg->packager : "(unknown)");

        if (pkg->builddate > 0) {
            char datebuf[64];
            struct tm *tm_info = localtime(&pkg->builddate);
            if (tm_info) {
                strftime(datebuf, sizeof(datebuf), "%c", tm_info);
                printf("\033[1;36m%-18s:\033[0m %s\n", "Build Date", datebuf);
            }
        }

        if (pkg->sha256sum) {
            printf("\033[1;36m%-18s:\033[0m \033[2m%s\033[0m\n", "SHA256 Sum", pkg->sha256sum);
        }
    } else {
        const char *h_fmt = "%-18s: %s\n";

        printf(h_fmt, "Repository", pkg->repo ? pkg->repo : "(unknown)");
        printf(h_fmt, "Source", pkg->is_arch ? "Arch Linux (fallback)" : "AvoryOS");
        printf(h_fmt, "Name", pkg->name ? pkg->name : "(unknown)");
        printf(h_fmt, "Version", pkg->version ? pkg->version : "(unknown)");
        printf(h_fmt, "Description", pkg->desc ? pkg->desc : "");
        printf(h_fmt, "Architecture", pkg->arch ? pkg->arch : "(unknown)");
        printf(h_fmt, "URL", pkg->url ? pkg->url : "(none)");

        if (pkg->licenses_count > 0) {
            printf("%-18s: ", "Licenses");
            for (size_t i = 0; i < pkg->licenses_count; i++) {
                printf("%s%s", pkg->licenses[i], (i + 1 < pkg->licenses_count) ? ", " : "\n");
            }
        }

        if (pkg->depends_count > 0) {
            printf("%-18s: ", "Depends On");
            for (size_t i = 0; i < pkg->depends_count; i++) {
                printf("%s%s", pkg->depends[i], (i + 1 < pkg->depends_count) ? ", " : "\n");
            }
        }

        if (pkg->optdepends_count > 0) {
            printf("%-18s:\n", "Optional Deps");
            for (size_t i = 0; i < pkg->optdepends_count; i++) {
                printf("    %s\n", pkg->optdepends[i]);
            }
        }

        if (pkg->provides_count > 0) {
            printf("%-18s: ", "Provides");
            for (size_t i = 0; i < pkg->provides_count; i++) {
                printf("%s%s", pkg->provides[i], (i + 1 < pkg->provides_count) ? ", " : "\n");
            }
        }

        if (pkg->conflicts_count > 0) {
            printf("%-18s: ", "Conflicts With");
            for (size_t i = 0; i < pkg->conflicts_count; i++) {
                printf("%s%s", pkg->conflicts[i], (i + 1 < pkg->conflicts_count) ? ", " : "\n");
            }
        }

        printf(h_fmt, "Download Size", csz);
        printf(h_fmt, "Installed Size", isz);
        printf(h_fmt, "Packager", pkg->packager ? pkg->packager : "(unknown)");

        if (pkg->builddate > 0) {
            char datebuf[64];
            struct tm *tm_info = localtime(&pkg->builddate);
            if (tm_info) {
                strftime(datebuf, sizeof(datebuf), "%c", tm_info);
                printf(h_fmt, "Build Date", datebuf);
            }
        }

        if (pkg->sha256sum) {
            printf(h_fmt, "SHA256 Sum", pkg->sha256sum);
        }
    }
}
