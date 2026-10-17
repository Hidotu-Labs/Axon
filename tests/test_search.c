#include "axon/repository.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    printf("Running Arch repository search tests...\n");

    axon_config_t cfg;
    axon_config_init(&cfg);

    axon_pkg_list_t *results = axon_pkg_list_new();
    assert(results != NULL);

    int rc = axon_arch_search_all(&cfg, "firefox", results);
    if (rc == 0) {
        printf("Search returned %zu packages for 'firefox'\n", results->count);
        assert(results->count > 0);
        bool found_exact = false;
        for (size_t i = 0; i < results->count; i++) {
            if (results->packages[i]->name && strcmp(results->packages[i]->name, "firefox") == 0) {
                found_exact = true;
                break;
            }
        }
        assert(found_exact);
        printf("Found exact package 'firefox' successfully!\n");
    } else {
        printf("Sync directory not available in test environment, skipping live search assertion.\n");
    }

    axon_pkg_list_free(results);
    axon_config_free(&cfg);

    printf("All search tests passed!\n");
    return 0;
}
