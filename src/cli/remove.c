#include "cli.h"
#include "axon/database.h"
#include "axon/color.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int cmd_remove(int argc, char **argv, const axon_config_t *cfg) {
    axon_config_t local_cfg = *cfg;
    bool noconfirm = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            if (cfg->color) {
                printf("%susage:%s  %saxon rm%s [%soptions%s] %s<package...>%s\n\n",
                       ANSI_BOLD, ANSI_RESET, ANSI_BOLD_CYAN, ANSI_RESET, ANSI_YELLOW, ANSI_RESET, ANSI_BOLD_WHITE, ANSI_RESET);
                printf("%soptions:%s\n", ANSI_BOLD_BLUE, ANSI_RESET);
                printf("    %s-y, --noconfirm%s     do not prompt for confirmation\n", ANSI_BOLD_CYAN, ANSI_RESET);
                printf("    %s--root <path>%s       specify alternate installation root\n", ANSI_BOLD_CYAN, ANSI_RESET);
                printf("    %s-h, --help%s          display this help message\n", ANSI_BOLD_CYAN, ANSI_RESET);
            } else {
                printf("usage:  axon rm [options] <package...>\n\n");
                printf("options:\n");
                printf("    -y, --noconfirm    do not prompt for confirmation\n");
                printf("    --root <path>      specify alternate installation root\n");
                printf("    -h, --help         display this help message\n");
            }
            return 0;
        } else if (strcmp(argv[i], "-y") == 0 || strcmp(argv[i], "--noconfirm") == 0) {
            noconfirm = true;
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
    const char **to_remove = malloc(sizeof(char *) * argc);
    size_t to_remove_count = 0;

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

        to_remove[to_remove_count++] = pkgname;
    }

    if (to_remove_count == 0) {
        free(to_remove);
        return overall_status;
    }

    if (cfg->color) {
        printf("\n%s::%s %sPackages to remove (%zu):%s\n",
               ANSI_BOLD_BLUE, ANSI_RESET, ANSI_BOLD, to_remove_count, ANSI_RESET);
        for (size_t i = 0; i < to_remove_count; i++) {
            printf("   %s%s%s\n", ANSI_BOLD_WHITE, to_remove[i], ANSI_RESET);
        }
        printf("\n");
    } else {
        printf("\n:: Packages to remove (%zu):\n", to_remove_count);
        for (size_t i = 0; i < to_remove_count; i++) {
            printf("   %s\n", to_remove[i]);
        }
        printf("\n");
    }

    /* Interactive confirmation */
    if (!noconfirm) {
        if (cfg->color) {
            printf("%s::%s %sDo you want to remove these packages?%s %s[Y/n]%s ",
                   ANSI_BOLD_BLUE, ANSI_RESET, ANSI_BOLD, ANSI_RESET, ANSI_DIM, ANSI_RESET);
        } else {
            printf(":: Do you want to remove these packages? [Y/n] ");
        }
        fflush(stdout);

        char response[64];
        if (fgets(response, sizeof(response), stdin) == NULL) {
            printf("\n");
            free(to_remove);
            return 1;
        }

        char *r = response;
        while (*r == ' ' || *r == '\t') r++;
        size_t rlen = strlen(r);
        while (rlen > 0 && (r[rlen - 1] == '\n' || r[rlen - 1] == '\r' || r[rlen - 1] == ' ' || r[rlen - 1] == '\t')) {
            r[--rlen] = '\0';
        }

        if (rlen > 0 && r[0] != 'y' && r[0] != 'Y') {
            if (cfg->color) {
                printf("%s::%s %sRemoval cancelled by user.%s\n",
                       ANSI_BOLD_YELLOW, ANSI_RESET, ANSI_BOLD, ANSI_RESET);
            } else {
                printf(":: Removal cancelled by user.\n");
            }
            free(to_remove);
            return 0;
        }
        printf("\n");
    }

    for (size_t i = 0; i < to_remove_count; i++) {
        const char *pkgname = to_remove[i];

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

    free(to_remove);
    return overall_status;
}
