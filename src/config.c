#include "axon/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static char *normalize_repo_url(const char *url) {
    if (!url) return NULL;
    /* If url starts with github://owner/repo */
    if (strncmp(url, "github://", 9) == 0) {
        char buf[512];
        const char *p = url + 9;
        snprintf(buf, sizeof(buf), "https://raw.githubusercontent.com/%s/main/packages.txt", p);
        return strdup(buf);
    }
    /* If url starts with https://github.com/owner/repo without raw.githubusercontent.com */
    if (strncmp(url, "https://github.com/", 19) == 0 && !strstr(url, "raw.githubusercontent.com")) {
        const char *p = url + 19;
        char owner_repo[256];
        strncpy(owner_repo, p, sizeof(owner_repo) - 1);
        owner_repo[sizeof(owner_repo) - 1] = '\0';
        char *git = strstr(owner_repo, ".git");
        if (git) *git = '\0';
        size_t len = strlen(owner_repo);
        while (len > 0 && owner_repo[len - 1] == '/') owner_repo[--len] = '\0';

        char buf[512];
        snprintf(buf, sizeof(buf), "https://raw.githubusercontent.com/%s/main/packages.txt", owner_repo);
        return strdup(buf);
    }
    return strdup(url);
}

int axon_config_add_repo(axon_config_t *cfg, const char *name, const char *url, bool is_arch, int priority) {
    if (!cfg || !name || !url) return -1;

    for (size_t i = 0; i < cfg->repo_count; i++) {
        if (cfg->repos[i].name && strcmp(cfg->repos[i].name, name) == 0) {
            free(cfg->repos[i].url);
            cfg->repos[i].url = normalize_repo_url(url);
            cfg->repos[i].is_arch = is_arch;
            cfg->repos[i].priority = priority;
            return 0;
        }
    }

    axon_repo_entry_t *new_repos = realloc(cfg->repos, sizeof(axon_repo_entry_t) * (cfg->repo_count + 1));
    if (!new_repos) return -1;
    cfg->repos = new_repos;

    cfg->repos[cfg->repo_count].name = strdup(name);
    cfg->repos[cfg->repo_count].url = normalize_repo_url(url);
    cfg->repos[cfg->repo_count].is_arch = is_arch;
    cfg->repos[cfg->repo_count].priority = priority;
    cfg->repo_count++;

    return 0;
}

int axon_config_init(axon_config_t *cfg) {
    if (!cfg) return -1;
    memset(cfg, 0, sizeof(axon_config_t));

    const char *env_root = getenv("AXON_ROOT_DIR");
    cfg->root_dir = strdup(env_root && *env_root ? env_root : "/");

    const char *env_db = getenv("AXON_DB_PATH");
    cfg->db_path = strdup(env_db && *env_db ? env_db : "/var/lib/pacman");

    const char *env_cache = getenv("AXON_CACHE_PATH");
    cfg->cache_path = strdup(env_cache && *env_cache ? env_cache : "/var/cache/axon/pkg");

    const char *env_sync = getenv("AXON_SYNC_PATH");
    cfg->sync_path = strdup(env_sync && *env_sync ? env_sync : "/var/lib/axon/sync");

    cfg->arch_sync_path = strdup("/var/lib/pacman/sync");

    const char *env_mirror = getenv("AXON_MIRRORLIST");
    cfg->mirrorlist_path = strdup(env_mirror && *env_mirror ? env_mirror : "/etc/pacman.d/mirrorlist");

    cfg->color = isatty(STDOUT_FILENO);
    cfg->verbose = false;

    /* Auto-load configuration file */
    const char *env_conf = getenv("AXON_CONF");
    if (env_conf && access(env_conf, R_OK) == 0) {
        axon_config_load(cfg, env_conf);
    } else if (access("./axon.conf", R_OK) == 0) {
        axon_config_load(cfg, "./axon.conf");
    } else if (access("/etc/axon/axon.conf", R_OK) == 0) {
        axon_config_load(cfg, "/etc/axon/axon.conf");
    }

    /* If no repositories configured, default to ascension on GitHub */
    if (cfg->repo_count == 0) {
        axon_config_add_repo(cfg, "ascension", "https://raw.githubusercontent.com/Hidotu-Labs/Axon-Packages/main/packages.txt", false, 10);
    }

    return 0;
}

int axon_config_load(axon_config_t *cfg, const char *path) {
    if (!cfg) return -1;
    if (!path) return 0;

    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char current_section[128] = "";
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;

        /* Strip trailing newline / whitespace */
        size_t l = strlen(p);
        while (l > 0 && (p[l - 1] == '\r' || p[l - 1] == '\n' || p[l - 1] == ' ' || p[l - 1] == '\t')) {
            p[--l] = '\0';
        }

        /* Check section header [section] */
        if (*p == '[') {
            char *end_b = strchr(p, ']');
            if (end_b) {
                *end_b = '\0';
                strncpy(current_section, p + 1, sizeof(current_section) - 1);
                current_section[sizeof(current_section) - 1] = '\0';
                continue;
            }
        }

        char key[128], val[384];
        if (sscanf(p, "%127[^=]=%383s", key, val) == 2) {
            /* Trim key */
            char *ek = key + strlen(key) - 1;
            while (ek > key && (*ek == ' ' || *ek == '\t')) *ek-- = '\0';

            /* If inside a repository section */
            if (strlen(current_section) > 0 && strcasecmp(current_section, "options") != 0) {
                if (strcasecmp(key, "Server") == 0 || strcasecmp(key, "URL") == 0) {
                    axon_config_add_repo(cfg, current_section, val, false, 10);
                }
                continue;
            }

            if (strcmp(key, "RootDir") == 0) {
                free(cfg->root_dir);
                cfg->root_dir = strdup(val);
            } else if (strcmp(key, "DBPath") == 0) {
                free(cfg->db_path);
                cfg->db_path = strdup(val);
            } else if (strcmp(key, "CacheDir") == 0) {
                free(cfg->cache_path);
                cfg->cache_path = strdup(val);
            } else if (strcmp(key, "SyncDir") == 0) {
                free(cfg->sync_path);
                cfg->sync_path = strdup(val);
            } else if (strcmp(key, "ArchSyncDir") == 0) {
                free(cfg->arch_sync_path);
                cfg->arch_sync_path = strdup(val);
            } else if (strcmp(key, "MirrorList") == 0) {
                free(cfg->mirrorlist_path);
                cfg->mirrorlist_path = strdup(val);
            } else if (strcmp(key, "Color") == 0) {
                cfg->color = (strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0 || strcasecmp(val, "auto") == 0);
            } else if (strcmp(key, "Repo") == 0) {
                char rname[128], rurl[256];
                if (sscanf(val, "%127s %255s", rname, rurl) == 2) {
                    axon_config_add_repo(cfg, rname, rurl, false, 10);
                }
            }
        }
    }

    fclose(fp);
    return 0;
}

int axon_config_set_root(axon_config_t *cfg, const char *root) {
    if (!cfg || !root || !*root) return -1;
    char *canonical = realpath(root, NULL);
    if (!canonical) return -1;
    struct stat st;
    if (stat(canonical, &st) != 0 || !S_ISDIR(st.st_mode)) {
        free(canonical);
        return -1;
    }
    const char *suffixes[] = { "/var/lib/pacman", "/var/cache/axon/pkg",
                               "/var/lib/axon/sync", "/var/lib/pacman/sync" };
    char *paths[4] = {0};
    for (size_t i = 0; i < 4; i++) {
        size_t size = strlen(canonical) + strlen(suffixes[i]) + 1;
        paths[i] = malloc(size);
        if (!paths[i]) {
            for (size_t j = 0; j < i; j++) free(paths[j]);
            free(canonical);
            return -1;
        }
        snprintf(paths[i], size, "%s%s", strcmp(canonical, "/") == 0 ? "" : canonical, suffixes[i]);
    }
    free(cfg->root_dir); cfg->root_dir = canonical;
    free(cfg->db_path); cfg->db_path = paths[0];
    free(cfg->cache_path); cfg->cache_path = paths[1];
    free(cfg->sync_path); cfg->sync_path = paths[2];
    free(cfg->arch_sync_path); cfg->arch_sync_path = paths[3];
    return 0;
}

void axon_config_free(axon_config_t *cfg) {
    if (!cfg) return;
    free(cfg->root_dir);
    free(cfg->db_path);
    free(cfg->cache_path);
    free(cfg->sync_path);
    free(cfg->arch_sync_path);
    free(cfg->mirrorlist_path);

    for (size_t i = 0; i < cfg->repo_count; i++) {
        free(cfg->repos[i].name);
        free(cfg->repos[i].url);
    }
    free(cfg->repos);
    memset(cfg, 0, sizeof(axon_config_t));
}
