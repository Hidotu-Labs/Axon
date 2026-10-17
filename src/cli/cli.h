#ifndef AXON_CLI_H
#define AXON_CLI_H

#include "axon/config.h"

#ifdef __cplusplus
extern "C" {
#endif

int cmd_get(int argc, char **argv, const axon_config_t *cfg);
int cmd_remove(int argc, char **argv, const axon_config_t *cfg);
int cmd_search(int argc, char **argv, const axon_config_t *cfg);
int cmd_info(int argc, char **argv, const axon_config_t *cfg);
int cmd_sync(int argc, char **argv, const axon_config_t *cfg);
int cmd_upgrade(int argc, char **argv, const axon_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* AXON_CLI_H */
