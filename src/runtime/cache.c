#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "runtime.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int axon_regenerate_caches(const axon_config_t *cfg) {
    const char *root = (cfg && cfg->root_dir && *cfg->root_dir) ? cfg->root_dir : "/";
    struct {
        const char *tool;
        const char *const argv[5];
        const char *require_dir;
    } tools[] = {
        {"usr/bin/ldconfig", {"/usr/bin/ldconfig", NULL}, NULL},
        {"usr/bin/update-mime-database", {"/usr/bin/update-mime-database", "/usr/share/mime", NULL}, "usr/share/mime"},
        {"usr/bin/fc-cache", {"/usr/bin/fc-cache", "-f", NULL}, NULL},
        {"usr/bin/glib-compile-schemas", {"/usr/bin/glib-compile-schemas", "/usr/share/glib-2.0/schemas", NULL}, "usr/share/glib-2.0/schemas"},
        {"usr/bin/gio-querymodules", {"/usr/bin/gio-querymodules", "/usr/lib/gio/modules", NULL}, "usr/lib/gio/modules"},
        {"usr/bin/gdk-pixbuf-query-loaders", {"/usr/bin/gdk-pixbuf-query-loaders", "--update-cache", NULL}, NULL},
        {"usr/bin/gtk-query-immodules-3.0", {"/usr/bin/gtk-query-immodules-3.0", "--update-cache", NULL}, NULL},
        {"usr/bin/update-desktop-database", {"/usr/bin/update-desktop-database", "/usr/share/applications", NULL}, "usr/share/applications"}
    };

    int failed = 0;
    for (size_t i = 0; i < sizeof(tools) / sizeof(tools[0]); i++) {
        char tool_path[1024];
        axon_target_path(root, tools[i].tool, tool_path, sizeof(tool_path));
        if (access(tool_path, X_OK) == 0) {
            if (tools[i].require_dir) {
                char req_path[1024];
                axon_target_path(root, tools[i].require_dir, req_path, sizeof(req_path));
                struct stat st;
                if (stat(req_path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            }
            if (axon_in_target(root, (char *const *)tools[i].argv, NULL, 0) != 0) {
                failed++;
            }
        }
    }

    /* Icon caches */
    char icon_cache_tool[1024];
    axon_target_path(root, "usr/bin/gtk-update-icon-cache", icon_cache_tool, sizeof(icon_cache_tool));
    if (access(icon_cache_tool, X_OK) == 0) {
        char icons_dir[1024];
        axon_target_path(root, "usr/share/icons", icons_dir, sizeof(icons_dir));
        DIR *dir = opendir(icons_dir);
        if (dir) {
            struct dirent *de;
            while ((de = readdir(dir)) != NULL) {
                if (de->d_name[0] == '.') continue;
                char theme_index[PATH_MAX];
                snprintf(theme_index, sizeof(theme_index), "%s/%s/index.theme", icons_dir, de->d_name);
                if (access(theme_index, F_OK) == 0) {
                    char icon_arg[512];
                    snprintf(icon_arg, sizeof(icon_arg), "/usr/share/icons/%s", de->d_name);
                    char *const icon_argv[] = {"/usr/bin/gtk-update-icon-cache", "-f", "-t", icon_arg, NULL};
                    if (axon_in_target(root, icon_argv, NULL, 0) != 0) {
                        failed++;
                    }
                }
            }
            closedir(dir);
        }
    }

    return failed > 0 ? -1 : 0;
}
