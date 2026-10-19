#include "cli.h"
#include "axon/repository.h"
#include "axon/color.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_sync(int argc, char **argv, const axon_config_t *cfg) {
    axon_config_t local_cfg = *cfg;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            if (cfg->color) {
                printf("%susage:%s  %saxon upd%s [%soptions%s]\n\n",
                       ANSI_BOLD, ANSI_RESET, ANSI_BOLD_CYAN, ANSI_RESET, ANSI_YELLOW, ANSI_RESET);
                printf("%soptions:%s\n", ANSI_BOLD_BLUE, ANSI_RESET);
                printf("    %s--mirrorlist <path>%s  specify custom mirrorlist file\n", ANSI_BOLD_CYAN, ANSI_RESET);
                printf("    %s--sync-dir <path>%s    specify destination directory for .db files\n", ANSI_BOLD_CYAN, ANSI_RESET);
                printf("    %s-v, --verbose%s        display detailed download and mirror attempts\n", ANSI_BOLD_CYAN, ANSI_RESET);
                printf("    %s-h, --help%s           display this help message\n", ANSI_BOLD_CYAN, ANSI_RESET);
            } else {
                printf("usage:  axon upd [options]\n\n");
                printf("options:\n");
                printf("    --mirrorlist <path>  specify custom mirrorlist file\n");
                printf("    --sync-dir <path>    specify destination directory for .db files\n");
                printf("    -v, --verbose        display detailed download and mirror attempts\n");
                printf("    -h, --help           display this help message\n");
            }
            return 0;
        } else if (strcmp(argv[i], "--mirrorlist") == 0 && i + 1 < argc) {
            local_cfg.mirrorlist_path = argv[++i];
        } else if (strcmp(argv[i], "--sync-dir") == 0 && i + 1 < argc) {
            local_cfg.sync_path = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            local_cfg.verbose = true;
        }
    }

    if (cfg->color) {
        printf("%s::%s %sSynchronizing repository databases...%s\n",
               ANSI_BOLD_BLUE, ANSI_RESET, ANSI_BOLD, ANSI_RESET);
    } else {
        printf(":: Synchronizing repository databases...\n");
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

    int rc = axon_repo_sync_all(&mgr);
    if (rc == 0) {
        if (cfg->color) {
            printf("%s==>%s %sSynchronization complete.%s\n",
                   ANSI_BOLD_GREEN, ANSI_RESET, ANSI_BOLD, ANSI_RESET);
        } else {
            printf("==> Synchronization complete.\n");
        }
    } else {
        if (cfg->color) {
            fprintf(stderr, "%serror:%s failed to synchronize repository databases\n", ANSI_BOLD_RED, ANSI_RESET);
        } else {
            fprintf(stderr, "error: failed to synchronize repository databases\n");
        }
    }

    axon_repo_mgr_free(&mgr);
    return rc;
}
