#include "axon/lifecycle.h"
#include "axon/repository.h"
#include "axon/database.h"
#include "runtime/runtime.h"
#include <archive.h>
#include <archive_entry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

int axon_scriptlet(const axon_config_t *cfg, const char *archive_path,
                   const axon_package_t *pkg, const char *phase) {
    struct archive *a = archive_read_new();
    if (!a) return -1;
    archive_read_support_filter_all(a); archive_read_support_format_all(a);
    if (archive_read_open_filename(a, archive_path, 65536) != ARCHIVE_OK) {
        archive_read_free(a); return -1;
    }
    char *script_data = NULL;
    size_t script_len = 0;
    struct archive_entry *entry;
    int result;
    while ((result = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        const char *name = archive_entry_pathname(entry);
        while (!strncmp(name, "./", 2)) name += 2;
        if (strcmp(name, ".INSTALL")) { archive_read_data_skip(a); continue; }
        if (archive_entry_filetype(entry) != AE_IFREG) { archive_read_free(a); return -1; }

        size_t cap = 16384;
        script_data = malloc(cap);
        if (!script_data) { archive_read_free(a); return -1; }

        la_ssize_t sz;
        char chunk[4096];
        while ((sz = archive_read_data(a, chunk, sizeof(chunk))) > 0) {
            if (script_len + (size_t)sz + 128 >= cap) {
                cap *= 2;
                char *nb = realloc(script_data, cap);
                if (!nb) { free(script_data); archive_read_free(a); return -1; }
                script_data = nb;
            }
            memcpy(script_data + script_len, chunk, sz);
            script_len += (size_t)sz;
        }
        if (sz < 0) { free(script_data); archive_read_free(a); return -1; }
        break;
    }
    archive_read_free(a);
    if (!script_data) return result == ARCHIVE_EOF ? 0 : -1;

    /* Append function runner */
    const char *runner = "\nif declare -F -- \"$1\" >/dev/null; then \"$@\"; fi\n";
    memcpy(script_data + script_len, runner, strlen(runner));
    script_len += strlen(runner);
    script_data[script_len] = '\0';

    char *const cmd[] = {"/bin/bash", "-s", "--", (char *)phase, (char *)(pkg->version ? pkg->version : "1.0"), NULL};
    int rc = axon_in_target(cfg->root_dir, cmd, script_data, script_len);
    free(script_data);
    if (rc) fprintf(stderr, "error: %s failed for %s (target bash/chroot must work)\n", phase, pkg->name);
    return rc;
}

int axon_hooks(const axon_config_t *cfg, const axon_pkg_list_t *packages,
               char **archives, const char *phase, char **manifest_path) {
    if (*manifest_path) {
        return axon_run_hooks(cfg, *manifest_path, phase);
    }
    char path[] = "/tmp/axon-transaction-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    FILE *manifest = fdopen(fd, "w");
    if (!manifest) { close(fd); unlink(path); return -1; }
    int rc = 0;
    for (size_t i = 0; i < packages->count && !rc; i++) {
        const axon_package_t *pkg = packages->packages[i];
        fprintf(manifest, "Package\tInstall\t%s\t%s\n", pkg->name, pkg->version);
        struct archive *a = archive_read_new();
        archive_read_support_filter_all(a); archive_read_support_format_all(a);
        if (archive_read_open_filename(a, archives[i], 65536) != ARCHIVE_OK) rc = -1;
        struct archive_entry *entry;
        int status = ARCHIVE_EOF;
        while (!rc && (status = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
            const char *name = archive_entry_pathname(entry);
            while (!strncmp(name, "./", 2)) name += 2;
            if (strpbrk(name, "\t\n\r") || name[0] == '/' || strstr(name, "../")) { rc = -1; break; }
            if (strcmp(name, ".PKGINFO") && strcmp(name, ".BUILDINFO") &&
                strcmp(name, ".MTREE") && strcmp(name, ".INSTALL") && strcmp(name, ".CHANGELOG")) {
                char dest[PATH_MAX];
                snprintf(dest, sizeof(dest), "%s/%s", cfg->root_dir, name);
                struct stat st;
                fprintf(manifest, "Path\t%s\t%s\n", lstat(dest, &st) == 0 ? "Upgrade" : "Install", name);
            }
            archive_read_data_skip(a);
        }
        if (!rc && status != ARCHIVE_EOF) rc = -1;
        archive_read_free(a);
    }
    if (fclose(manifest) != 0) rc = -1;
    if (!rc) {
        rc = axon_run_hooks(cfg, path, phase);
    }
    if (rc == 0) {
        *manifest_path = strdup(path);
        if (!*manifest_path) rc = -1;
    }
    if (rc) unlink(path);
    return rc;
}

/* Empty-root bootstrap deliberately defers post_install until bash and its
 * runtime exist. Require the installed version's exact cached archive. */
int axon_replay_post_install(int argc, char **argv, const axon_config_t *cfg) {
    if (argc < 2 || !strcmp(cfg->root_dir, "/")) {
        fprintf(stderr, "usage: axon replay-post-install --root TARGET <package...>\n");
        return 1;
    }
    axon_repo_mgr_t mgr;
    if (axon_repo_mgr_init(&mgr, cfg) != 0) return 1;
    axon_pkg_list_t *packages = axon_pkg_list_new();
    char **archives = calloc((size_t)argc, sizeof(char *));
    int rc = packages && archives ? 0 : -1;
    for (int i = 1; i < argc && !rc; i++) {
        axon_package_t *installed = axon_db_get_installed(cfg, argv[i]);
        axon_package_t *remote = axon_repo_find(&mgr, argv[i]);
        bool ver_match = (installed && remote && installed->version && remote->version &&
                          strcmp(installed->version, remote->version) == 0);
        if (!ver_match && installed && remote && installed->version && remote->version) {
            char clean_inst[256], clean_rem[256];
            snprintf(clean_inst, sizeof(clean_inst), "%s", installed->version);
            snprintf(clean_rem, sizeof(clean_rem), "%s", remote->version);
            char *hi = strrchr(clean_inst, '-');
            if (hi && strcmp(hi, "-1") == 0 && !strchr(clean_rem, '-')) *hi = '\0';
            char *hr = strrchr(clean_rem, '-');
            if (hr && strcmp(hr, "-1") == 0 && !strchr(clean_inst, '-')) *hr = '\0';
            if (strcmp(clean_inst, clean_rem) == 0 || axon_version_cmp(installed->version, remote->version) == 0) {
                ver_match = true;
            }
        }
        if (!installed || !remote || !ver_match || !remote->filename || strchr(remote->filename, '/')) rc = -1;
        if (!rc) {
            size_t size = strlen(cfg->cache_path) + strlen(remote->filename) + 2;
            archives[i - 1] = malloc(size);
            if (!archives[i - 1]) rc = -1;
            else {
                snprintf(archives[i - 1], size, "%s/%s", cfg->cache_path, remote->filename);
                if (access(archives[i - 1], R_OK) != 0) rc = -1;
            }
        }
        axon_package_free(installed);
        if (!rc && axon_pkg_list_add(packages, remote) == 0) remote = NULL;
        else rc = -1;
        axon_package_free(remote);
    }
    for (size_t i = 0; !rc && i < packages->count; i++)
        rc = axon_scriptlet(cfg, archives[i], packages->packages[i], "post_install");
    char *manifest = NULL;
    if (!rc) rc = axon_hooks(cfg, packages, archives, "PostTransaction", &manifest);
    if (manifest) { unlink(manifest); free(manifest); }
    if (archives) { for (int i = 0; i < argc; i++) free(archives[i]); free(archives); }
    axon_pkg_list_free(packages); axon_repo_mgr_free(&mgr);
    if (rc) fprintf(stderr, "error: bootstrap replay requires matching installed metadata and cached archives\n");
    return rc ? 1 : 0;
}
