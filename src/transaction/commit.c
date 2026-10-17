#include "axon/transaction.h"
#include <stdio.h>

int axon_transaction_commit(axon_transaction_t *tx) {
    if (!tx || !tx->prepared) return -1;
    tx->committed = true;
    return 0;
}
