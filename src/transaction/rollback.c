#include "axon/transaction.h"
#include <stdio.h>

int axon_transaction_rollback(axon_transaction_t *tx) {
    if (!tx) return -1;
    printf("Rolling back transaction...\n");
    return 0;
}
