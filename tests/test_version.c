#include "axon/package.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    printf("Running version comparison tests...\n");

    /* Basic numeric comparison */
    assert(axon_version_cmp("1.0", "1.0") == 0);
    assert(axon_version_cmp("1.0", "1.1") < 0);
    assert(axon_version_cmp("2.0", "1.9") > 0);
    assert(axon_version_cmp("1.10", "1.2") > 0);

    /* Package release comparison */
    assert(axon_version_cmp("1.0-1", "1.0-2") < 0);
    assert(axon_version_cmp("1.0-2", "1.0-1") > 0);
    assert(axon_version_cmp("1.0-1", "1.0-1") == 0);

    /* Epoch comparison */
    assert(axon_version_cmp("1:1.0", "2.0") > 0);
    assert(axon_version_cmp("1:1.0-1", "1:1.0-2") < 0);
    assert(axon_version_cmp("2:1.0", "1:2.0") > 0);

    /* Alphanumeric and pre-release */
    assert(axon_version_cmp("1.0a", "1.0b") < 0);
    assert(axon_version_cmp("1.0.1", "1.0a") > 0);

    printf("All version comparison tests passed!\n");
    return 0;
}
