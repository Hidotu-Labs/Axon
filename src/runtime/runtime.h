#ifndef AXON_RUNTIME_H
#define AXON_RUNTIME_H

#include "axon/config.h"
#include "axon/package.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resolves a path relative to the target root */
char *axon_target_path(const char *root, const char *relative, char *out, size_t out_sz);

/* Runs a command inside target root (or host root if root is "/") with stdin data */
int axon_in_target(const char *root, char *const argv[], const char *data, size_t data_len);

/* ALPM hook processor */
int axon_run_hooks(const axon_config_t *cfg, const char *manifest_file, const char *phase);

/* Cache regenerator (ldconfig, fc-cache, icon caches, etc.) */
int axon_regenerate_caches(const axon_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* AXON_RUNTIME_H */
