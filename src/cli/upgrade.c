#include "cli.h"
#include "axon/repository.h"
#include "axon/database.h"
#include "axon/color.h"
#include <stdio.h>
#include <string.h>

int cmd_upgrade(int argc, char **argv, const axon_config_t *cfg) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            if (cfg->color) {
                printf("%susage:%s  %saxon upg%s\n\nupgrade installed packages\n",
                       ANSI_BOLD, ANSI_RESET, ANSI_BOLD_CYAN, ANSI_RESET);
            } else {
                printf("usage:  axon upg\n\nupgrade installed packages\n");
            }
            return 0;
        }
    }

    if (cfg->color) {
        printf("%s::%s %sChecking for system upgrades...%s\n",
               ANSI_BOLD_BLUE, ANSI_RESET, ANSI_BOLD, ANSI_RESET);
    } else {
        printf(":: Checking for system upgrades...\n");
    }

    axon_pkg_list_t *installed = axon_pkg_list_new();
    if (!installed) return 1;

    if (axon_db_list_installed(cfg, installed) != 0 || installed->count == 0) {
        printf("No installed packages found to upgrade.\n");
        axon_pkg_list_free(installed);
        return 0;
    }

    axon_repo_mgr_t mgr;
    if (axon_repo_mgr_init(&mgr, cfg) != 0) {
        axon_pkg_list_free(installed);
        return 1;
    }

    size_t upg_count = 0;
    for (size_t i = 0; i < installed->count; i++) {
        axon_package_t *local_pkg = installed->packages[i];
        if (!local_pkg->name) continue;

        axon_package_t *remote_pkg = axon_repo_find(&mgr, local_pkg->name);
        if (remote_pkg) {
            if (axon_version_cmp(remote_pkg->version, local_pkg->version) > 0) {
                if (cfg->color) {
                    printf("  %s%-25s%s %s%s%s -> %s%s%s [%s%s%s]\n",
                           ANSI_BOLD_WHITE, local_pkg->name, ANSI_RESET,
                           ANSI_RED, local_pkg->version, ANSI_RESET,
                           ANSI_BOLD_GREEN, remote_pkg->version, ANSI_RESET,
                           ANSI_BOLD_MAGENTA, remote_pkg->repo ? remote_pkg->repo : "remote", ANSI_RESET);
                } else {
                    printf("  %-25s %s -> %s [%s]\n",
                           local_pkg->name,
                           local_pkg->version,
                           remote_pkg->version,
                           remote_pkg->repo ? remote_pkg->repo : "remote");
                }
                upg_count++;
            }
            axon_package_free(remote_pkg);
        }
    }

    if (upg_count == 0) {
        if (cfg->color) {
            printf("%s==>%s %sAll packages are up to date.%s\n",
                   ANSI_BOLD_GREEN, ANSI_RESET, ANSI_BOLD, ANSI_RESET);
        } else {
            printf("==> All packages are up to date.\n");
        }
    } else {
        if (cfg->color) {
            printf("\n%s==>%s Total packages to upgrade: %s%zu%s\n",
                   ANSI_BOLD_GREEN, ANSI_RESET, ANSI_BOLD_YELLOW, upg_count, ANSI_RESET);
        } else {
            printf("\n==> Total packages to upgrade: %zu\n", upg_count);
        }
    }

    axon_repo_mgr_free(&mgr);
    axon_pkg_list_free(installed);
    return 0;
}
