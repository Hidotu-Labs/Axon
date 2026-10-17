#include "cli.h"
#include "axon/database.h"
#include "axon/color.h"
#include <stdio.h>
#include <string.h>

int cmd_remove(int argc, char **argv, const axon_config_t *cfg) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            if (cfg->color) {
                printf("%susage:%s  %saxon rm%s [%soptions%s] %s<package...>%s\n\n",
                       ANSI_BOLD, ANSI_RESET, ANSI_BOLD_CYAN, ANSI_RESET, ANSI_YELLOW, ANSI_RESET, ANSI_BOLD_WHITE, ANSI_RESET);
                printf("%soptions:%s\n", ANSI_BOLD_BLUE, ANSI_RESET);
                printf("    %s-h, --help%s         display this help message\n", ANSI_BOLD_CYAN, ANSI_RESET);
            } else {
                printf("usage:  axon rm [options] <package...>\n\n");
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

    for (int i = 1; i < argc; i++) {
        const char *pkgname = argv[i];
        if (pkgname[0] == '-') continue;

        if (!axon_db_is_installed(cfg, pkgname)) {
            if (cfg->color) {
                fprintf(stderr, "%serror:%s target not found: %s%s%s\n",
                        ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD_WHITE, pkgname, ANSI_RESET);
            } else {
                fprintf(stderr, "error: target not found: %s\n", pkgname);
            }
            continue;
        }

        if (cfg->color) {
            printf("%s::%s Preparing removal of %s%s%s...\n",
                   ANSI_BOLD_BLUE, ANSI_RESET, ANSI_BOLD_WHITE, pkgname, ANSI_RESET);
        } else {
            printf(":: Preparing removal of %s...\n", pkgname);
        }
    }

    return 0;
}
