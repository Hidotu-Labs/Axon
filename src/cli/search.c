#include "cli.h"
#include "axon/repository.h"
#include "axon/color.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_search(int argc, char **argv, const axon_config_t *cfg) {
    /* Support look -i or look --info, and -h */
    bool info_mode = false;
    const char *query = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            if (cfg->color) {
                printf("%susage:%s  %saxon look%s [%soptions%s] %s<query>%s\n\n",
                       ANSI_BOLD, ANSI_RESET, ANSI_BOLD_CYAN, ANSI_RESET, ANSI_YELLOW, ANSI_RESET, ANSI_BOLD_WHITE, ANSI_RESET);
                printf("%soptions:%s\n", ANSI_BOLD_BLUE, ANSI_RESET);
                printf("    %s-i, --info%s         view detailed package information\n", ANSI_BOLD_CYAN, ANSI_RESET);
                printf("    %s-h, --help%s         display this help message\n", ANSI_BOLD_CYAN, ANSI_RESET);
            } else {
                printf("usage:  axon look [options] <query>\n\n");
                printf("options:\n");
                printf("    -i, --info         view detailed package information\n");
                printf("    -h, --help         display this help message\n");
            }
            return 0;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--info") == 0) {
            info_mode = true;
        } else if (argv[i][0] != '-') {
            query = argv[i];
        }
    }

    if (!query) {
        if (cfg->color) {
            fprintf(stderr, "%serror:%s no search query specified (use -h for help)\n", ANSI_BOLD_RED, ANSI_RESET);
        } else {
            fprintf(stderr, "error: no search query specified (use -h for help)\n");
        }
        return 1;
    }

    axon_repo_mgr_t mgr;
    if (axon_repo_mgr_init(&mgr, cfg) != 0) {
        if (cfg->color) {
            fprintf(stderr, "%serror:%s failed to initialize repository manager\n", ANSI_BOLD_RED, ANSI_RESET);
        } else {
            fprintf(stderr, "error: failed to initialize repository manager\n");
        }
        return 1;
    }

    axon_pkg_list_t *results = axon_pkg_list_new();
    if (!results) {
        axon_repo_mgr_free(&mgr);
        return 1;
    }

    int rc = axon_repo_search(&mgr, query, results);
    if (rc != 0 && results->count == 0) {
        if (cfg->color) {
            fprintf(stderr, "%swarning:%s could not access repository sync databases.\n", ANSI_BOLD_YELLOW, ANSI_RESET);
            fprintf(stderr, "%s::%s Run '%saxon upd%s' to synchronize repositories or check sync directory path.\n\n",
                    ANSI_BOLD_BLUE, ANSI_RESET, ANSI_BOLD_GREEN, ANSI_RESET);
        } else {
            fprintf(stderr, "warning: could not access repository sync databases.\n");
            fprintf(stderr, ":: Run 'axon upd' to synchronize repositories or check sync directory path.\n\n");
        }
    }

    if (results->count == 0) {
        if (cfg->color) {
            printf("%serror:%s package matching '%s%s%s' was not found\n",
                   ANSI_BOLD_RED, ANSI_RESET, ANSI_BOLD_WHITE, query, ANSI_RESET);
        } else {
            printf("error: package matching '%s' was not found\n", query);
        }
    } else {
        if (info_mode) {
            for (size_t i = 0; i < results->count; i++) {
                axon_package_print_details(results->packages[i], cfg->color);
                if (i + 1 < results->count) {
                    if (cfg->color) {
                        printf("\n%s------------------------------------------------------------%s\n\n", ANSI_DIM, ANSI_RESET);
                    } else {
                        printf("\n------------------------------------------------------------\n\n");
                    }
                }
            }
        } else {
            for (size_t i = 0; i < results->count; i++) {
                axon_package_print_summary(results->packages[i], cfg->color);
            }
            if (cfg->color) {
                printf("\n%s==>%s Found %s%zu%s package(s) matching '%s%s%s'.\n",
                       ANSI_BOLD_GREEN, ANSI_RESET, ANSI_BOLD_WHITE, results->count, ANSI_RESET, ANSI_BOLD_CYAN, query, ANSI_RESET);
            } else {
                printf("\n==> Found %zu package(s) matching '%s'.\n", results->count, query);
            }
        }
    }

    axon_pkg_list_free(results);
    axon_repo_mgr_free(&mgr);
    return (results ? 0 : 1);
}
