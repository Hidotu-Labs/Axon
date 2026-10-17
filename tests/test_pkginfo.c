#include "axon/package.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(void) {
    printf("Running pkginfo parsing tests...\n");

    const char *sample_desc =
        "%NAME%\n"
        "axon-test\n\n"
        "%BASE%\n"
        "axon-test\n\n"
        "%VERSION%\n"
        "1.2.3-1\n\n"
        "%DESC%\n"
        "A package manager test suite\n\n"
        "%URL%\n"
        "https://avoryos.org\n\n"
        "%ARCH%\n"
        "x86_64\n\n"
        "%CSIZE%\n"
        "1048576\n\n"
        "%ISIZE%\n"
        "2097152\n\n"
        "%DEPENDS%\n"
        "glibc\n"
        "libarchive\n\n"
        "%PROVIDES%\n"
        "test-provider=1.0\n\n";

    axon_package_t *pkg = axon_package_new();
    assert(pkg != NULL);

    int rc = axon_package_parse_desc(pkg, sample_desc);
    assert(rc == 0);
    assert(strcmp(pkg->name, "axon-test") == 0);
    assert(strcmp(pkg->version, "1.2.3-1") == 0);
    assert(strcmp(pkg->desc, "A package manager test suite") == 0);
    assert(strcmp(pkg->url, "https://avoryos.org") == 0);
    assert(pkg->csize == 1048576);
    assert(pkg->isize == 2097152);
    assert(pkg->depends_count == 2);
    assert(strcmp(pkg->depends[0], "glibc") == 0);
    assert(strcmp(pkg->depends[1], "libarchive") == 0);
    assert(pkg->provides_count == 1);
    assert(strcmp(pkg->provides[0], "test-provider=1.0") == 0);

    axon_package_free(pkg);

    printf("All pkginfo tests passed!\n");
    return 0;
}
