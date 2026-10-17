#include "axon/dependency.h"
#include "axon/database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int axon_check_conflicts(const axon_config_t *cfg, const axon_package_t *pkg, char ***conflicts, size_t *count) {
    if (!pkg || !conflicts || !count) return -1;
    *conflicts = NULL;
    *count = 0;

    for (size_t i = 0; i < pkg->conflicts_count; i++) {
        const char *c = pkg->conflicts[i];
        if (axon_db_is_installed(cfg, c)) {
            char **new_arr = realloc(*conflicts, sizeof(char *) * (*count + 1));
            if (!new_arr) return -1;
            *conflicts = new_arr;
            (*conflicts)[*count] = strdup(c);
            (*count)++;
        }
    }

    return 0;
}
