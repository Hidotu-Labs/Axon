#include "axon/lifecycle.h"
#include "axon/repository.h"
#include "axon/database.h"
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

#ifndef AXON_LIBEXECDIR
#define AXON_LIBEXECDIR "/usr/local/libexec/axon"
#endif

static int wait_child(pid_t pid) {
    int status;
    if (pid < 0) return -1;
    while (waitpid(pid, &status, 0) < 0) if (errno != EINTR) return -1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static const char *runtime_path(char *path, size_t size) {
    /* A source checkout keeps libexec beside its binary. */
    ssize_t n = readlink("/proc/self/exe", path, size - 1);
    if (n > 0) {
        path[n] = '\0';
        char *slash = strrchr(path, '/');
        if (slash) {
            *slash = '\0';
            size_t used = strlen(path);
            snprintf(path + used, size - used, "/libexec/axon-runtime.py");
            if (access(path, R_OK) == 0) return path;
            snprintf(path + used, size - used, "/../libexec/axon/axon-runtime.py");
            if (access(path, R_OK) == 0) return path;
        }
    }
    snprintf(path, size, "%s/axon-runtime.py", AXON_LIBEXECDIR);
    return access(path, R_OK) == 0 ? path : NULL;
}

int axon_runtime_command(int argc, char **argv, const axon_config_t *cfg) {
    char helper[PATH_MAX], executable[PATH_MAX];
    if (!runtime_path(helper, sizeof(helper))) {
        fprintf(stderr, "error: Axon runtime helper not installed\n"); return -1;
    }
    ssize_t n = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (n < 0) return -1;
    executable[n] = '\0';
    char **args = calloc((size_t)argc + 13, sizeof(char *));
    if (!args) return -1;
    size_t k = 0;
    args[k++] = "python3"; args[k++] = helper;
    for (int i = 0; i < argc; i++) args[k++] = argv[i];
    args[k++] = "--root"; args[k++] = cfg->root_dir;
    args[k++] = "--db-path"; args[k++] = cfg->db_path;
    args[k++] = "--axon-executable"; args[k++] = executable;
    args[k++] = "--cache-dir"; args[k++] = cfg->cache_path;
    args[k++] = "--sync-dir"; args[k++] = cfg->sync_path;
    pid_t pid = fork();
    if (pid == 0) { execvp(args[0], args); perror("python3"); _exit(127); }
    free(args);
    return wait_child(pid);
}

int axon_scriptlet(const axon_config_t *cfg, const char *archive_path,
                   const axon_package_t *pkg, const char *phase) {
    struct archive *a = archive_read_new();
    if (!a) return -1;
    archive_read_support_filter_all(a); archive_read_support_format_all(a);
    if (archive_read_open_filename(a, archive_path, 65536) != ARCHIVE_OK) {
        archive_read_free(a); return -1;
    }
    FILE *script = NULL;
    struct archive_entry *entry;
    int result;
    while ((result = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        const char *name = archive_entry_pathname(entry);
        while (!strncmp(name, "./", 2)) name += 2;
        if (strcmp(name, ".INSTALL")) { archive_read_data_skip(a); continue; }
        if (archive_entry_filetype(entry) != AE_IFREG) { archive_read_free(a); return -1; }
        script = tmpfile();
        if (!script) { archive_read_free(a); return -1; }
        char data[8192]; la_ssize_t size;
        while ((size = archive_read_data(a, data, sizeof(data))) > 0)
            if (fwrite(data, 1, (size_t)size, script) != (size_t)size) { fclose(script); archive_read_free(a); return -1; }
        if (size < 0) { fclose(script); archive_read_free(a); return -1; }
        break;
    }
    archive_read_free(a);
    if (!script) return result == ARCHIVE_EOF ? 0 : -1;
    /* Function names and versions are arguments, never interpolated shell code. */
    fputs("\nif declare -F -- \"$1\" >/dev/null; then \"$@\"; fi\n", script);
    if (fflush(script) != 0) { fclose(script); return -1; }
    rewind(script);
    pid_t pid = fork();
    if (pid == 0) {
        if (dup2(fileno(script), STDIN_FILENO) < 0) _exit(127);
        char *args[] = {"scriptlet", "--phase", (char *)phase, "--version", pkg->version};
        _exit(axon_runtime_command(5, args, cfg) == 0 ? 0 : 1);
    }
    int rc = wait_child(pid); fclose(script);
    if (rc) fprintf(stderr, "error: %s failed for %s (target bash/chroot must work)\n", phase, pkg->name);
    return rc;
}

int axon_hooks(const axon_config_t *cfg, const axon_pkg_list_t *packages,
               char **archives, const char *phase, char **manifest_path) {
    if (*manifest_path) {
        char *args[] = {"hooks", "--phase", (char *)phase, "--manifest", *manifest_path};
        return axon_runtime_command(5, args, cfg);
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
        char *args[] = {"hooks", "--phase", (char *)phase, "--manifest", path};
        rc = axon_runtime_command(5, args, cfg);
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
        if (!installed || !remote || !installed->version || !remote->version ||
            strcmp(installed->version, remote->version) || !remote->filename || strchr(remote->filename, '/')) rc = -1;
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
