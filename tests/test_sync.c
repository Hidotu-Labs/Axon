#include "axon/repository.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    printf("Running repository sync and mirrorlist unit tests...\n");

    /* Test 1: axon_mkdir_p */
    const char *test_dir = "/tmp/axon_test_sync_dir/sub/nested";
    int r = axon_mkdir_p(test_dir, 0755);
    assert(r == 0);

    struct stat st;
    assert(stat(test_dir, &st) == 0 && S_ISDIR(st.st_mode));

    /* Clean up test dirs */
    rmdir("/tmp/axon_test_sync_dir/sub/nested");
    rmdir("/tmp/axon_test_sync_dir/sub");
    rmdir("/tmp/axon_test_sync_dir");

    /* Test 2: axon_mirrorlist_load fallback mirrors */
    char **mirrors = NULL;
    size_t count = 0;
    r = axon_mirrorlist_load(NULL, &mirrors, &count);
    assert(r == 0);
    assert(count > 0);
    assert(mirrors != NULL);
    assert(mirrors[0] != NULL);
    assert(strncmp(mirrors[0], "http", 4) == 0);

    printf("Loaded %zu mirrors successfully.\n", count);
    for (size_t i = 0; i < (count < 3 ? count : 3); i++) {
        printf("  Mirror [%zu]: %s\n", i, mirrors[i]);
    }

    axon_mirrorlist_free(mirrors, count);

    /* Test 3: axon_mirrorlist_load from custom file */
    const char *tmp_mirrorlist = "/tmp/axon_test_mirrorlist";
    FILE *fp = fopen(tmp_mirrorlist, "w");
    assert(fp != NULL);
    fprintf(fp, "# Test mirror file\n");
    fprintf(fp, "Server = https://test.mirror.org/arch/$repo/os/$arch\n");
    fprintf(fp, "https://another.mirror.com/repo/$repo/os/$arch\n");
    fclose(fp);

    mirrors = NULL;
    count = 0;
    r = axon_mirrorlist_load(tmp_mirrorlist, &mirrors, &count);
    assert(r == 0);
    assert(count == 2);
    assert(strcmp(mirrors[0], "https://test.mirror.org/arch/$repo/os/$arch") == 0);
    assert(strcmp(mirrors[1], "https://another.mirror.com/repo/$repo/os/$arch") == 0);

    axon_mirrorlist_free(mirrors, count);
    remove(tmp_mirrorlist);

    printf("All repository sync and mirrorlist tests passed!\n");
    return 0;
}
