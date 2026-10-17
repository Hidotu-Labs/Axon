#include "axon/transaction.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

axon_transaction_t *axon_transaction_create(const axon_config_t *cfg) {
    axon_transaction_t *tx = calloc(1, sizeof(axon_transaction_t));
    if (!tx) return NULL;
    tx->cfg = cfg;
    tx->capacity = 16;
    tx->steps = malloc(sizeof(axon_tx_step_t) * tx->capacity);
    return tx;
}

int axon_transaction_add_install(axon_transaction_t *tx, const axon_package_t *pkg) {
    if (!tx || !pkg) return -1;
    if (tx->count >= tx->capacity) {
        size_t new_cap = tx->capacity * 2;
        axon_tx_step_t *new_steps = realloc(tx->steps, sizeof(axon_tx_step_t) * new_cap);
        if (!new_steps) return -1;
        tx->steps = new_steps;
        tx->capacity = new_cap;
    }
    tx->steps[tx->count].type = AXON_TX_INSTALL;
    tx->steps[tx->count].pkg = axon_package_clone(pkg);
    tx->steps[tx->count].pkgname = strdup(pkg->name ? pkg->name : "");
    tx->count++;
    return 0;
}

int axon_transaction_add_remove(axon_transaction_t *tx, const char *pkgname) {
    if (!tx || !pkgname) return -1;
    if (tx->count >= tx->capacity) {
        size_t new_cap = tx->capacity * 2;
        axon_tx_step_t *new_steps = realloc(tx->steps, sizeof(axon_tx_step_t) * new_cap);
        if (!new_steps) return -1;
        tx->steps = new_steps;
        tx->capacity = new_cap;
    }
    tx->steps[tx->count].type = AXON_TX_REMOVE;
    tx->steps[tx->count].pkg = NULL;
    tx->steps[tx->count].pkgname = strdup(pkgname);
    tx->count++;
    return 0;
}

int axon_transaction_prepare(axon_transaction_t *tx) {
    if (!tx) return -1;
    tx->prepared = true;
    return 0;
}

void axon_transaction_free(axon_transaction_t *tx) {
    if (!tx) return;
    for (size_t i = 0; i < tx->count; i++) {
        if (tx->steps[i].pkg) {
            axon_package_free(tx->steps[i].pkg);
        }
        free(tx->steps[i].pkgname);
    }
    free(tx->steps);
    free(tx);
}
