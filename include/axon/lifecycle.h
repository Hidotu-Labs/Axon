#ifndef AXON_LIFECYCLE_H
#define AXON_LIFECYCLE_H
#include "axon/config.h"
#include "axon/package.h"
int axon_replay_post_install(int argc, char **argv, const axon_config_t *cfg);
int axon_runtime_command(int argc, char **argv, const axon_config_t *cfg);
int axon_scriptlet(const axon_config_t *cfg, const char *archive_path,
                   const axon_package_t *pkg, const char *phase);
/* Hook matching uses archive paths before configuration-file diversion. */
int axon_hooks(const axon_config_t *cfg, const axon_pkg_list_t *packages,
               char **archives, const char *phase, char **manifest_path);
#endif
