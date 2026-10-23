#include "axon/dependency.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    axon_package_t pkg = {0};
    pkg.name = "library"; pkg.version = "2:3.1-4";
    assert(axon_package_satisfies(&pkg, "library"));
    assert(axon_package_satisfies(&pkg, "library>=2:3.1"));
    assert(axon_package_satisfies(&pkg, "library=2:3.1"));
    assert(axon_package_satisfies(&pkg, "library<2:3.2"));
    assert(!axon_package_satisfies(&pkg, "library>2:3.1-4"));
    assert(!axon_package_satisfies(&pkg, "library<=1:9.0"));
    assert(!axon_package_satisfies(&pkg, "library>=3:1.0"));
    assert(!axon_package_satisfies(&pkg, "library==2"));
    assert(!axon_package_satisfies(&pkg, "library>="));
    char *provides[] = {"virtual=2.0", "unversioned"};
    pkg.provides = provides; pkg.provides_count = 2;
    assert(axon_package_satisfies(&pkg, "virtual>=1.5"));
    assert(!axon_package_satisfies(&pkg, "virtual>=3"));
    assert(axon_package_satisfies(&pkg, "unversioned"));
    assert(!axon_package_satisfies(&pkg, "unversioned>=1"));
    assert(!axon_package_satisfies(&pkg, "unknown"));
    puts("Dependency version and provides checks passed");
    return 0;
}
