#include "cli.h"
#include "axon/repository.h"
#include "axon/dependency.h"
#include "axon/transaction.h"
#include "axon/database.h"
#include "axon/color.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int cmd_get(int argc, char **argv, const axon_config_t *cfg) {
    axon_config_t local_cfg = *cfg;
    bool keep_cache = false;

    bool overwrite = false;
    bool noconfirm = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            if (cfg->color) {
                printf("%susage:%s  %saxon get%s [%soptions%s] %s<package...>%s\n\n",
                       ANSI_BOLD, ANSI_RESET, ANSI_BOLD_CYAN, ANSI_RESET, ANSI_YELLOW, ANSI_RESET, ANSI_BOLD_WHITE, ANSI_RESET);
                printf("%soptions:%s\n", ANSI_BOLD_BLUE, ANSI_RESET);
                printf("    %s-y, --noconfirm%s     do not prompt for confirmation\n", ANSI_BOLD_CYAN, ANSI_RESET);
                printf("    %s--overwrite%s         overwrite conflicting non-critical files\n", ANSI_BOLD_CYAN, ANSI_RESET);
                printf("    %s--keep-cache%s        keep downloaded package archive in cache\n", ANSI_BOLD_CYAN, ANSI_RESET);
                printf("    %s--cache-dir <path>%s  specify package cache directory\n", ANSI_BOLD_CYAN, ANSI_RESET);
                printf("    %s--root <path>%s       specify alternate installation root\n", ANSI_BOLD_CYAN, ANSI_RESET);
                printf("    %s-v, --verbose%s       display verbose download output\n", ANSI_BOLD_CYAN, ANSI_RESET);
                printf("    %s-h, --help%s          display this help message\n", ANSI_BOLD_CYAN, ANSI_RESET);
            } else {
                printf("usage:  axon get [options] <package...>\n\n");
                printf("options:\n");
                printf("    -y, --noconfirm    do not prompt for confirmation\n");
                printf("    --overwrite        overwrite conflicting non-critical files\n");
                printf("    --keep-cache       keep downloaded package archive in cache\n");
                printf("    --cache-dir <path> specify package cache directory\n");
                printf("    --root <path>      specify alternate installation root\n");
                printf("    -v, --verbose      display verbose download output\n");
                printf("    -h, --help         display this help message\n");
            }
            return 0;
        } else if (strcmp(argv[i], "-y") == 0 || strcmp(argv[i], "--noconfirm") == 0) {
            noconfirm = true;
        } else if (strcmp(argv[i], "--overwrite") == 0) {
            overwrite = true;
        } else if (strcmp(argv[i], "--keep-cache") == 0) {
            keep_cache = true;
        } else if (strcmp(argv[i], "--cache-dir") == 0 && i + 1 < argc) {
            local_cfg.cache_path = argv[++i];
        } else if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            local_cfg.root_dir = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            local_cfg.verbose = true;
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

    axon_repo_mgr_t mgr;
    if (axon_repo_mgr_init(&mgr, &local_cfg) != 0) {
        if (cfg->color) {
            fprintf(stderr, "%serror:%s failed to initialize repository manager\n", ANSI_BOLD_RED, ANSI_RESET);
        } else {
            fprintf(stderr, "error: failed to initialize repository manager\n");
        }
        return 1;
    }

    axon_pkg_list_t *to_install = axon_pkg_list_new();
    if (!to_install) {
        axon_repo_mgr_free(&mgr);
        return 1;
    }

    int overall_status = 0;

    for (int i = 1; i < argc; i++) {
        const char *pkgname = argv[i];
        if (pkgname[0] == '-') {
            if ((strcmp(pkgname, "--cache-dir") == 0 || strcmp(pkgname, "--root") == 0) && i + 1 < argc) {
                i++;
            }
            continue;
        }

        /* Prevent modifying critical system packages on the host root filesystem */
        if (axon_is_protected_pkg(pkgname) && (!local_cfg.root_dir || strcmp(local_cfg.root_dir, "/") == 0)) {
            if (cfg->color) {
                fprintf(stderr, "%serror:%s package '%s%s%s' is a protected system package and cannot be modified on the host root filesystem\n",
                        ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD_WHITE, pkgname, ANSI_RESET);
            } else {
                fprintf(stderr, "error: package '%s' is a protected system package and cannot be modified on the host root filesystem\n", pkgname);
            }
            overall_status = 1;
            continue;
        }

        if (axon_db_is_installed(&local_cfg, pkgname)) {
            if (cfg->color) {
                printf("%swarning:%s package '%s%s%s' is already installed\n",
                       ANSI_BOLD_YELLOW, ANSI_RESET, ANSI_BOLD_WHITE, pkgname, ANSI_RESET);
            } else {
                printf("warning: package '%s' is already installed\n", pkgname);
            }
            continue;
        }

        if (cfg->color) {
            printf("%s::%s Resolving dependencies for %s%s%s...\n",
                   ANSI_BOLD_BLUE, ANSI_RESET, ANSI_BOLD_WHITE, pkgname, ANSI_RESET);
        } else {
            printf(":: Resolving dependencies for %s...\n", pkgname);
        }

        int rrc = axon_resolve_deps(&mgr, pkgname, to_install);
        if (rrc != 0) {
            if (cfg->color) {
                fprintf(stderr, "%serror:%s failed to resolve dependencies for package '%s%s%s'\n",
                        ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD_WHITE, pkgname, ANSI_RESET);
            } else {
                fprintf(stderr, "error: failed to resolve dependencies for package '%s'\n", pkgname);
            }
            overall_status = 1;
        }
    }

    if (to_install->count == 0) {
        if (overall_status == 0) {
            printf("there is nothing to do\n");
        }
        axon_pkg_list_free(to_install);
        axon_repo_mgr_free(&mgr);
        return overall_status;
    }

    if (cfg->color) {
        printf("\n%s::%s %sPackages to install (%zu):%s\n",
               ANSI_BOLD_BLUE, ANSI_RESET, ANSI_BOLD, to_install->count, ANSI_RESET);
        for (size_t i = 0; i < to_install->count; i++) {
            axon_package_t *p = to_install->packages[i];
            printf("   %s%s%s %s%s%s [%s%s%s]\n",
                   ANSI_BOLD_WHITE, p->name ? p->name : "unknown", ANSI_RESET,
                   ANSI_BOLD_GREEN, p->version ? p->version : "", ANSI_RESET,
                   ANSI_BOLD_MAGENTA, p->repo ? p->repo : "remote", ANSI_RESET);
        }
        printf("\n");
    } else {
        printf("\n:: Packages to install (%zu):\n", to_install->count);
        for (size_t i = 0; i < to_install->count; i++) {
            axon_package_t *p = to_install->packages[i];
            printf("   %s %s [%s]\n",
                   p->name ? p->name : "unknown",
                   p->version ? p->version : "",
                   p->repo ? p->repo : "remote");
        }
        printf("\n");
    }

    /* Calculate and display download & installation sizes */
    size_t total_dl_size = 0;
    size_t total_inst_size = 0;
    for (size_t i = 0; i < to_install->count; i++) {
        axon_package_t *p = to_install->packages[i];
        if (p->csize > 0) total_dl_size += p->csize;
        if (p->isize > 0) total_inst_size += p->isize;
    }

    char dl_str[32], inst_str[32];
    axon_format_size(total_dl_size, dl_str, sizeof(dl_str));
    axon_format_size(total_inst_size, inst_str, sizeof(inst_str));

    if (cfg->color) {
        printf("%sTotal Download Size:%s    %10s\n", ANSI_BOLD, ANSI_RESET, dl_str);
        printf("%sTotal Installed Size:%s   %10s\n\n", ANSI_BOLD, ANSI_RESET, inst_str);
    } else {
        printf("Total Download Size:    %10s\n", dl_str);
        printf("Total Installed Size:   %10s\n\n", inst_str);
    }

    /* Interactive confirmation */
    if (!noconfirm) {
        if (cfg->color) {
            printf("%s::%s %sProceed with installation?%s %s[Y/n]%s ",
                   ANSI_BOLD_BLUE, ANSI_RESET, ANSI_BOLD, ANSI_RESET, ANSI_DIM, ANSI_RESET);
        } else {
            printf(":: Proceed with installation? [Y/n] ");
        }
        fflush(stdout);

        char response[64];
        if (fgets(response, sizeof(response), stdin) == NULL) {
            printf("\n");
            axon_pkg_list_free(to_install);
            axon_repo_mgr_free(&mgr);
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
                printf("%s::%s %sInstallation cancelled by user.%s\n",
                       ANSI_BOLD_YELLOW, ANSI_RESET, ANSI_BOLD, ANSI_RESET);
            } else {
                printf(":: Installation cancelled by user.\n");
            }
            axon_pkg_list_free(to_install);
            axon_repo_mgr_free(&mgr);
            return 0;
        }
        printf("\n");
    }

    const char *cache_dir = local_cfg.cache_path ? local_cfg.cache_path : "/var/cache/axon/pkg";
    char **downloaded_paths = calloc(to_install->count, sizeof(char *));
    if (!downloaded_paths) {
        axon_pkg_list_free(to_install);
        axon_repo_mgr_free(&mgr);
        return 1;
    }

    /* Phase 1: Download all packages */
    bool download_failed = false;
    for (size_t i = 0; i < to_install->count; i++) {
        axon_package_t *pkg = to_install->packages[i];
        if (!pkg || !pkg->name) continue;

        if (cfg->color) {
            printf("%s::%s Downloading package archive for %s%s%s...\n",
                   ANSI_BOLD_BLUE, ANSI_RESET, ANSI_BOLD_WHITE, pkg->name, ANSI_RESET);
        } else {
            printf(":: Downloading package archive for %s...\n", pkg->name);
        }

        int drc = axon_repo_download_pkg(&mgr, pkg, cache_dir, &downloaded_paths[i]);
        if (drc != 0 || !downloaded_paths[i]) {
            if (cfg->color) {
                fprintf(stderr, "%serror:%s failed to download package archive for %s\n",
                        ANSI_BOLD_RED, ANSI_RESET, pkg->name);
            } else {
                fprintf(stderr, "error: failed to download package archive for %s\n", pkg->name);
            }
            download_failed = true;
            break;
        }
    }

    if (download_failed) {
        if (!keep_cache) {
            for (size_t i = 0; i < to_install->count; i++) {
                if (downloaded_paths[i]) unlink(downloaded_paths[i]);
            }
        }
        for (size_t i = 0; i < to_install->count; i++) free(downloaded_paths[i]);
        free(downloaded_paths);
        axon_pkg_list_free(to_install);
        axon_repo_mgr_free(&mgr);
        return 1;
    }

    /* Phase 2: Pre-Commit Conflict Checking across all downloaded packages */
    if (overwrite) {
        if (cfg->color) {
            printf("%s::%s Overwrite mode enabled, skipping conflict checks for non-critical files...\n",
                   ANSI_BOLD_YELLOW, ANSI_RESET);
        } else {
            printf(":: Overwrite mode enabled, skipping conflict checks for non-critical files...\n");
        }
    } else {
        if (cfg->color) {
            printf("%s::%s Checking for file conflicts...\n", ANSI_BOLD_BLUE, ANSI_RESET);
        } else {
            printf(":: Checking for file conflicts...\n");
        }

        bool has_conflicts = false;
        for (size_t i = 0; i < to_install->count; i++) {
            axon_package_t *pkg = to_install->packages[i];
            if (!downloaded_paths[i]) continue;

            char **conflicts = NULL;
            size_t conflict_count = 0;
            axon_archive_check_conflicts(downloaded_paths[i], local_cfg.root_dir, pkg->name, &local_cfg, &conflicts, &conflict_count);
            if (conflict_count > 0) {
                has_conflicts = true;
                if (cfg->color) {
                    fprintf(stderr, "%serror:%s failed to commit transaction (%s%s%s: conflicting files)\n",
                            ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD_WHITE, pkg->name, ANSI_RESET);
                    for (size_t c = 0; c < conflict_count; c++) {
                        fprintf(stderr, "   %s%s%s\n", ANSI_BOLD_WHITE, conflicts[c], ANSI_RESET);
                        free(conflicts[c]);
                    }
                } else {
                    fprintf(stderr, "error: failed to commit transaction (%s: conflicting files)\n", pkg->name);
                    for (size_t c = 0; c < conflict_count; c++) {
                        fprintf(stderr, "   %s\n", conflicts[c]);
                        free(conflicts[c]);
                    }
                }
                free(conflicts);
            }
        }

        if (has_conflicts) {
            if (cfg->color) {
                fprintf(stderr, "%serror:%s aborting transaction: no files were written to disk\n", ANSI_BOLD_RED, ANSI_RESET);
                fprintf(stderr, "%snote:%s use '%s--overwrite%s' to allow overwriting conflicting files\n",
                        ANSI_BOLD_CYAN, ANSI_RESET, ANSI_BOLD_WHITE, ANSI_RESET);
            } else {
                fprintf(stderr, "error: aborting transaction: no files were written to disk\n");
                fprintf(stderr, "note: use '--overwrite' to allow overwriting conflicting files\n");
            }
            if (!keep_cache) {
                for (size_t i = 0; i < to_install->count; i++) {
                    if (downloaded_paths[i]) unlink(downloaded_paths[i]);
                }
            }
            for (size_t i = 0; i < to_install->count; i++) free(downloaded_paths[i]);
            free(downloaded_paths);
            axon_pkg_list_free(to_install);
            axon_repo_mgr_free(&mgr);
            return 1;
        }
    }

    /* Phase 3: Extract, install, and record in local database */
    for (size_t i = 0; i < to_install->count; i++) {
        axon_package_t *pkg = to_install->packages[i];
        char *archive_path = downloaded_paths[i];
        if (!pkg || !pkg->name || !archive_path) continue;

        if (cfg->color) {
            printf("%s::%s Extracting %s%s%s to %s%s%s...\n",
                   ANSI_BOLD_BLUE, ANSI_RESET,
                   ANSI_BOLD_WHITE, pkg->name, ANSI_RESET,
                   ANSI_BOLD_WHITE, local_cfg.root_dir, ANSI_RESET);
        } else {
            printf(":: Extracting %s to %s...\n", pkg->name, local_cfg.root_dir);
        }

        char **installed_files = NULL;
        size_t file_count = 0;
        int erc = axon_archive_extract_pkg(archive_path, local_cfg.root_dir, &installed_files, &file_count);
        if (erc != 0) {
            if (cfg->color) {
                fprintf(stderr, "%serror:%s failed to extract package archive '%s'\n",
                        ANSI_BOLD_RED, ANSI_RESET, archive_path);
            } else {
                fprintf(stderr, "error: failed to extract package archive '%s'\n", archive_path);
            }
            if (!keep_cache) {
                unlink(archive_path);
            }
            overall_status = 1;
            continue;
        }

        /* Record installation in local database */
        axon_db_record_install(&local_cfg, pkg, (const char **)installed_files, file_count);

        for (size_t f = 0; f < file_count; f++) {
            free(installed_files[f]);
        }
        free(installed_files);

        /* Remove downloaded archive */
        if (!keep_cache) {
            if (cfg->color) {
                printf("%s::%s Removing downloaded archive %s%s%s...\n",
                       ANSI_BOLD_BLUE, ANSI_RESET, ANSI_DIM, archive_path, ANSI_RESET);
            } else {
                printf(":: Removing downloaded archive %s...\n", archive_path);
            }
            unlink(archive_path);
        }

        if (cfg->color) {
            printf("%s==>%s %sSuccessfully installed %s%s%s %s%s%s\n",
                   ANSI_BOLD_GREEN, ANSI_RESET, ANSI_BOLD,
                   ANSI_BOLD_WHITE, pkg->name, ANSI_RESET,
                   ANSI_BOLD_GREEN, pkg->version ? pkg->version : "", ANSI_RESET);
        } else {
            printf("==> Successfully installed %s %s\n", pkg->name, pkg->version ? pkg->version : "");
        }
    }

    for (size_t i = 0; i < to_install->count; i++) {
        free(downloaded_paths[i]);
    }
    free(downloaded_paths);
    axon_pkg_list_free(to_install);
    axon_repo_mgr_free(&mgr);
    return overall_status;
}
