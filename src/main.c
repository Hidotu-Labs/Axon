#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "cli/cli.h"
#include "axon/config.h"
#include "axon/color.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define AXON_VERSION "0.1.0"

static void print_banner(bool color) {
    if (color) {
        printf("%sAxon Package Manager%s %s%s%s (%sAvoryOS%s)\n",
               ANSI_BOLD_CYAN, ANSI_RESET,
               ANSI_BOLD_GREEN, AXON_VERSION, ANSI_RESET,
               ANSI_BOLD_MAGENTA, ANSI_RESET);
        printf("%sIndependent package manager with Arch Linux fallback support%s\n\n",
               ANSI_DIM, ANSI_RESET);
    } else {
        printf("Axon Package Manager %s (AvoryOS)\n", AXON_VERSION);
        printf("Independent package manager with Arch Linux fallback support\n\n");
    }
}

static void print_usage(const char *prog, bool color) {
    (void)prog;
    if (color) {
        printf("%susage:%s  %saxon%s %s<operation>%s [...]\n\n",
               ANSI_BOLD, ANSI_RESET, ANSI_BOLD_CYAN, ANSI_RESET, ANSI_BOLD, ANSI_RESET);
        printf("%soperations:%s\n", ANSI_BOLD_BLUE, ANSI_RESET);
        printf("    %saxon%s {%s-h%s %s--help%s}\n",
               ANSI_BOLD_CYAN, ANSI_RESET, ANSI_BOLD_GREEN, ANSI_RESET, ANSI_BOLD_GREEN, ANSI_RESET);
        printf("    %saxon%s {%s-v%s %s--version%s}\n",
               ANSI_BOLD_CYAN, ANSI_RESET, ANSI_BOLD_GREEN, ANSI_RESET, ANSI_BOLD_GREEN, ANSI_RESET);
        printf("    %saxon%s %slook%s [%soptions%s] %s<query...>%s      search for packages (AvoryOS & Arch repos)\n",
               ANSI_BOLD_CYAN, ANSI_RESET, ANSI_BOLD_GREEN, ANSI_RESET, ANSI_YELLOW, ANSI_RESET, ANSI_BOLD_WHITE, ANSI_RESET);
        printf("    %saxon%s %sget%s  [%soptions%s] %s<package...>%s    download and install packages\n",
               ANSI_BOLD_CYAN, ANSI_RESET, ANSI_BOLD_GREEN, ANSI_RESET, ANSI_YELLOW, ANSI_RESET, ANSI_BOLD_WHITE, ANSI_RESET);
        printf("    %saxon%s %srm%s   [%soptions%s] %s<package...>%s    remove packages from the system\n",
               ANSI_BOLD_CYAN, ANSI_RESET, ANSI_BOLD_GREEN, ANSI_RESET, ANSI_YELLOW, ANSI_RESET, ANSI_BOLD_WHITE, ANSI_RESET);
        printf("    %saxon%s %supd%s                            synchronize repository databases\n",
               ANSI_BOLD_CYAN, ANSI_RESET, ANSI_BOLD_GREEN, ANSI_RESET);
        printf("    %saxon%s %supg%s                            upgrade installed packages\n",
               ANSI_BOLD_CYAN, ANSI_RESET, ANSI_BOLD_GREEN, ANSI_RESET);
        printf("    %saxon%s %sinfo%s [%soptions%s] %s<package...>%s    view detailed package metadata\n\n",
               ANSI_BOLD_CYAN, ANSI_RESET, ANSI_BOLD_GREEN, ANSI_RESET, ANSI_YELLOW, ANSI_RESET, ANSI_BOLD_WHITE, ANSI_RESET);
        printf("%soptions:%s\n", ANSI_BOLD_BLUE, ANSI_RESET);
        printf("    %s-i, --info%s         view package details (with look)\n", ANSI_BOLD_CYAN, ANSI_RESET);
        printf("    %s--color%s            force colored output\n", ANSI_BOLD_CYAN, ANSI_RESET);
        printf("    %s--no-color%s         disable colored output\n", ANSI_BOLD_CYAN, ANSI_RESET);
        printf("    %s--sync-dir <path>%s  specify repository sync database directory\n\n", ANSI_BOLD_CYAN, ANSI_RESET);
        printf("use '%saxon%s %s<operation>%s {%s-h --help%s}' for available options of a specific operation\n",
               ANSI_BOLD_CYAN, ANSI_RESET, ANSI_BOLD, ANSI_RESET, ANSI_BOLD_GREEN, ANSI_RESET);
    } else {
        printf("usage:  axon <operation> [...]\n");
        printf("operations:\n");
        printf("    axon {-h --help}\n");
        printf("    axon {-v --version}\n");
        printf("    axon look [options] <query...>      search for packages (AvoryOS & Arch repos)\n");
        printf("    axon get  [options] <package...>    download and install packages\n");
        printf("    axon rm   [options] <package...>    remove packages from the system\n");
        printf("    axon upd                            synchronize repository databases\n");
        printf("    axon upg                            upgrade installed packages\n");
        printf("    axon info [options] <package...>    view detailed package metadata\n\n");
        printf("options:\n");
        printf("    -i, --info         view package details (with look)\n");
        printf("    --color            force colored output\n");
        printf("    --no-color         disable colored output\n");
        printf("    --sync-dir <path>  specify repository sync database directory\n\n");
        printf("use 'axon <operation> {-h --help}' for available options of a specific operation\n");
    }
}

int main(int argc, char **argv) {
    axon_config_t cfg;
    axon_config_init(&cfg);

    /* Environment check for NO_COLOR */
    const char *no_color_env = getenv("NO_COLOR");
    if (no_color_env && strlen(no_color_env) > 0) {
        cfg.color = false;
    }

    /* Pre-scan for color flags */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--color") == 0 || strcmp(argv[i], "--color=always") == 0) {
            cfg.color = true;
        } else if (strcmp(argv[i], "--no-color") == 0 || strcmp(argv[i], "--color=never") == 0) {
            cfg.color = false;
        }
    }

    if (argc < 2) {
        if (cfg.color) {
            fprintf(stderr, "%serror:%s please specify a command (e.g. %slook%s, %sget%s, %srm%s, %supd%s, %supg%s). Run '%saxon -h%s' for help.\n",
                    ANSI_BOLD_RED, ANSI_RESET,
                    ANSI_BOLD_GREEN, ANSI_RESET,
                    ANSI_BOLD_GREEN, ANSI_RESET,
                    ANSI_BOLD_GREEN, ANSI_RESET,
                    ANSI_BOLD_GREEN, ANSI_RESET,
                    ANSI_BOLD_GREEN, ANSI_RESET,
                    ANSI_BOLD_CYAN, ANSI_RESET);
        } else {
            fprintf(stderr, "error: please specify a command (e.g. look, get, rm, upd, upg). Run 'axon -h' for help.\n");
        }
        axon_config_free(&cfg);
        return 1;
    }

    int cmd_idx = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0], cfg.color);
            axon_config_free(&cfg);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_banner(cfg.color);
            axon_config_free(&cfg);
            return 0;
        } else if (strcmp(argv[i], "--color") == 0 || strcmp(argv[i], "--color=always") == 0) {
            cfg.color = true;
        } else if (strcmp(argv[i], "--no-color") == 0 || strcmp(argv[i], "--color=never") == 0) {
            cfg.color = false;
        } else if (strcmp(argv[i], "--sync-dir") == 0 && i + 1 < argc) {
            free(cfg.sync_path);
            cfg.sync_path = strdup(argv[++i]);
        } else if (argv[i][0] != '-') {
            cmd_idx = i;
            break;
        }
    }

    if (cmd_idx == -1) {
        if (cfg.color) {
            fprintf(stderr, "%serror:%s please specify a command (e.g. %slook%s, %sget%s, %srm%s, %supd%s, %supg%s). Run '%saxon -h%s' for help.\n",
                    ANSI_BOLD_RED, ANSI_RESET,
                    ANSI_BOLD_GREEN, ANSI_RESET,
                    ANSI_BOLD_GREEN, ANSI_RESET,
                    ANSI_BOLD_GREEN, ANSI_RESET,
                    ANSI_BOLD_GREEN, ANSI_RESET,
                    ANSI_BOLD_GREEN, ANSI_RESET,
                    ANSI_BOLD_CYAN, ANSI_RESET);
        } else {
            fprintf(stderr, "error: please specify a command (e.g. look, get, rm, upd, upg). Run 'axon -h' for help.\n");
        }
        axon_config_free(&cfg);
        return 1;
    }

    const char *cmd = argv[cmd_idx];
    int sub_argc = argc - cmd_idx;
    char **sub_argv = &argv[cmd_idx];

    int exit_code = 0;

    if (strcmp(cmd, "look") == 0 || strcmp(cmd, "search") == 0) {
        exit_code = cmd_search(sub_argc, sub_argv, &cfg);
    } else if (strcmp(cmd, "get") == 0 || strcmp(cmd, "install") == 0) {
        exit_code = cmd_get(sub_argc, sub_argv, &cfg);
    } else if (strcmp(cmd, "rm") == 0 || strcmp(cmd, "remove") == 0) {
        exit_code = cmd_remove(sub_argc, sub_argv, &cfg);
    } else if (strcmp(cmd, "upd") == 0 || strcmp(cmd, "sync") == 0) {
        exit_code = cmd_sync(sub_argc, sub_argv, &cfg);
    } else if (strcmp(cmd, "upg") == 0 || strcmp(cmd, "upgrade") == 0) {
        exit_code = cmd_upgrade(sub_argc, sub_argv, &cfg);
    } else if (strcmp(cmd, "info") == 0) {
        exit_code = cmd_info(sub_argc, sub_argv, &cfg);
    } else {
        if (cfg.color) {
            fprintf(stderr, "%serror:%s unrecognized operation '%s'\n", ANSI_BOLD_RED, ANSI_RESET, cmd);
        } else {
            fprintf(stderr, "error: unrecognized operation '%s'\n", cmd);
        }
        exit_code = 1;
    }

    axon_config_free(&cfg);
    return exit_code;
}
