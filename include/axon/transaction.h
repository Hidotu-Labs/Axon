#ifndef AXON_TRANSACTION_H
#define AXON_TRANSACTION_H

#include "axon/package.h"
#include "axon/config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AXON_TX_INSTALL,
    AXON_TX_REMOVE,
    AXON_TX_UPGRADE
} axon_tx_type_t;

typedef struct {
    axon_tx_type_t type;
    axon_package_t *pkg;
    char *pkgname;
} axon_tx_step_t;

typedef struct {
    const axon_config_t *cfg;
    axon_tx_step_t *steps;
    size_t count;
    size_t capacity;
    bool prepared;
    bool committed;
} axon_transaction_t;

axon_transaction_t *axon_transaction_create(const axon_config_t *cfg);
int axon_transaction_add_install(axon_transaction_t *tx, const axon_package_t *pkg);
int axon_transaction_add_remove(axon_transaction_t *tx, const char *pkgname);
int axon_transaction_prepare(axon_transaction_t *tx);
int axon_transaction_commit(axon_transaction_t *tx);
int axon_transaction_rollback(axon_transaction_t *tx);
void axon_transaction_free(axon_transaction_t *tx);

#ifdef __cplusplus
}
#endif

#endif /* AXON_TRANSACTION_H */
