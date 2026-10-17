#include "cli.h"
#include "axon/repository.h"
#include "axon/color.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_info(int argc, char **argv, const axon_config_t *cfg) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            if (cfg->color) {
                printf("%susage:%s  %saxon info%s [%soptions%s] %s<package>%s\n\n",
                       ANSI_BOLD, ANSI_RESET, ANSI_BOLD_CYAN, ANSI_RESET, ANSI_YELLOW, ANSI_RESET, ANSI_BOLD_WHITE, ANSI_RESET);
                printf("%soptions:%s\n", ANSI_BOLD_BLUE, ANSI_RESET);
                printf("    %s-h, --help%s         display this help message\n", ANSI_BOLD_CYAN, ANSI_RESET);
            } else {
                printf("usage:  axon info [options] <package>\n\n");
                printf("options:\n");
                printf("    -h, --help         display this help message\n");
            }
            return 0;
        }
    }

    if (argc < 2) {
        if (cfg->color) {
            fprintf(stderr, "%serror:%s %sno targets specified (use -h for help)%s\n",
                    ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD, ANSI_RESET);
        } else {
            fprintf(stderr, "error: no targets specified (use -h for help)\n");
        }
        return 1;
    }

    const char *pkgname = argv[1];

    axon_repo_mgr_t mgr;
    if (axon_repo_mgr_init(&mgr, cfg) != 0) {
        if (cfg->color) {
            fprintf(stderr, "%serror:%s failed to initialize repository manager\n", ANSI_BOLD_RED, ANSI_RESET);
        } else {
            fprintf(stderr, "error: failed to initialize repository manager\n");
        }
        return 1;
    }

    axon_package_t *pkg = axon_repo_find(&mgr, pkgname);
    if (!pkg) {
        if (cfg->color) {
            fprintf(stderr, "%serror:%s package '%s%s%s' was not found\n",
                    ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD_WHITE, pkgname, ANSI_RESET);
        } else {
            fprintf(stderr, "error: package '%s' was not found\n", pkgname);
        }
        axon_repo_mgr_free(&mgr);
        return 1;
    }

    axon_package_print_details(pkg, cfg->color);

    axon_package_free(pkg);
    axon_repo_mgr_free(&mgr);
    return 0;
}
