#include "cli.h"
#include "axon/database.h"
#include "axon/color.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int cmd_remove(int argc, char **argv, const axon_config_t *cfg) {
    axon_config_t local_cfg = *cfg;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            if (cfg->color) {
                printf("%susage:%s  %saxon rm%s [%soptions%s] %s<package...>%s\n\n",
                       ANSI_BOLD, ANSI_RESET, ANSI_BOLD_CYAN, ANSI_RESET, ANSI_YELLOW, ANSI_RESET, ANSI_BOLD_WHITE, ANSI_RESET);
                printf("%soptions:%s\n", ANSI_BOLD_BLUE, ANSI_RESET);
                printf("    %s--root <path>%s      specify alternate installation root\n", ANSI_BOLD_CYAN, ANSI_RESET);
                printf("    %s-h, --help%s         display this help message\n", ANSI_BOLD_CYAN, ANSI_RESET);
            } else {
                printf("usage:  axon rm [options] <package...>\n\n");
                printf("options:\n");
                printf("    --root <path>      specify alternate installation root\n");
                printf("    -h, --help         display this help message\n");
            }
            return 0;
        } else if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            local_cfg.root_dir = argv[++i];
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

    int overall_status = 0;

    for (int i = 1; i < argc; i++) {
        const char *pkgname = argv[i];
        if (pkgname[0] == '-') {
            if (strcmp(pkgname, "--root") == 0 && i + 1 < argc) {
                i++;
            }
            continue;
        }

        if (axon_is_protected_pkg(pkgname) && (!local_cfg.root_dir || strcmp(local_cfg.root_dir, "/") == 0)) {
            if (cfg->color) {
                fprintf(stderr, "%serror:%s package '%s%s%s' is a protected system package and cannot be removed\n",
                        ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD_WHITE, pkgname, ANSI_RESET);
            } else {
                fprintf(stderr, "error: package '%s' is a protected system package and cannot be removed\n", pkgname);
            }
            overall_status = 1;
            continue;
        }

        if (!axon_db_is_installed(&local_cfg, pkgname)) {
            if (cfg->color) {
                fprintf(stderr, "%serror:%s target not found: %s%s%s\n",
                        ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD_WHITE, pkgname, ANSI_RESET);
            } else {
                fprintf(stderr, "error: target not found: %s\n", pkgname);
            }
            overall_status = 1;
            continue;
        }

        if (cfg->color) {
            printf("%s::%s Removing package %s%s%s...\n",
                   ANSI_BOLD_BLUE, ANSI_RESET, ANSI_BOLD_WHITE, pkgname, ANSI_RESET);
        } else {
            printf(":: Removing package %s...\n", pkgname);
        }

        int rc = axon_db_remove_package(&local_cfg, pkgname);
        if (rc == 0) {
            if (cfg->color) {
                printf("%s==>%s %sSuccessfully removed %s%s%s\n",
                       ANSI_BOLD_GREEN, ANSI_RESET, ANSI_BOLD,
                       ANSI_BOLD_WHITE, pkgname, ANSI_RESET);
            } else {
                printf("==> Successfully removed %s\n", pkgname);
            }
        } else {
            if (cfg->color) {
                fprintf(stderr, "%serror:%s failed to remove package %s%s%s\n",
                        ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD_WHITE, pkgname, ANSI_RESET);
            } else {
                fprintf(stderr, "error: failed to remove package %s\n", pkgname);
            }
            overall_status = 1;
        }
    }

    return overall_status;
}
