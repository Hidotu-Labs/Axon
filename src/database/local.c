#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "axon/database.h"
#include "axon/repository.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int axon_db_init(const axon_config_t *cfg) {
    if (!cfg || !cfg->db_path) return -1;
    char path[512];
    snprintf(path, sizeof(path), "%s/local", cfg->db_path);
    if (axon_mkdir_p(path, 0755) != 0) return -1;
    return 0;
}

static bool dir_has_package(const char *dir_path, const char *pkgname) {
    if (!dir_path || !pkgname) return false;
    DIR *dir = opendir(dir_path);
    if (!dir) return false;

    bool found = false;
    struct dirent *de;
    size_t name_len = strlen(pkgname);

    while ((de = readdir(dir)) != NULL) {
        if (strncmp(de->d_name, pkgname, name_len) == 0 && de->d_name[name_len] == '-') {
            found = true;
            break;
        }
    }

    closedir(dir);
    return found;
}

static bool dir_provides_package(const char *dir_path, const char *dep_name) {
    if (!dir_path || !dep_name) return false;
    DIR *dir = opendir(dir_path);
    if (!dir) return false;

    struct dirent *de;
    bool found = false;
    char path[1024];

    while (!found && (de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s/desc", dir_path, de->d_name);
        FILE *fp = fopen(path, "r");
        if (!fp) continue;

        char line[256];
        bool in_provides = false;
        while (fgets(line, sizeof(line), fp)) {
            size_t l = strlen(line);
            while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
            if (strcmp(line, "%PROVIDES%") == 0) {
                in_provides = true;
                continue;
            }
            if (in_provides) {
                if (line[0] == '%' || line[0] == '\0') {
                    in_provides = false;
                    continue;
                }
                char *eq = strpbrk(line, "<>=");
                if (eq) *eq = '\0';
                if (strcmp(line, dep_name) == 0) {
                    found = true;
                    break;
                }
            }
        }
        fclose(fp);
    }

    closedir(dir);
    return found;
}

static axon_package_t *dir_get_package(const char *dir_path, const char *pkgname) {
    if (!dir_path || !pkgname) return NULL;
    DIR *dir = opendir(dir_path);
    if (!dir) return NULL;

    axon_package_t *pkg = NULL;
    struct dirent *de;
    size_t name_len = strlen(pkgname);

    while ((de = readdir(dir)) != NULL) {
        if (strncmp(de->d_name, pkgname, name_len) == 0 && de->d_name[name_len] == '-') {
            char desc_path[1024];
            snprintf(desc_path, sizeof(desc_path), "%s/%s/desc", dir_path, de->d_name);
            FILE *fp = fopen(desc_path, "r");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                long sz = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                char *buf = malloc(sz + 1);
                if (buf) {
                    size_t read_bytes = fread(buf, 1, sz, fp);
                    buf[read_bytes] = '\0';
                    pkg = axon_package_new();
                    if (pkg) {
                        axon_package_parse_desc(pkg, buf);
                        pkg->repo = strdup("local");
                    }
                    free(buf);
                }
                fclose(fp);
            }
            break;
        }
    }

    closedir(dir);
    return pkg;
}

static void get_pacman_local_dir(const axon_config_t *cfg, char *out_buf, size_t out_len) {
    if (cfg && cfg->root_dir && strcmp(cfg->root_dir, "/") != 0) {
        snprintf(out_buf, out_len, "%s/var/lib/pacman/local", cfg->root_dir);
    } else {
        snprintf(out_buf, out_len, "/var/lib/pacman/local");
    }
}

bool axon_db_is_installed(const axon_config_t *cfg, const char *pkgname) {
    if (!pkgname) return false;

    /* 1. Check Axon local database */
    if (cfg && cfg->db_path) {
        char axon_local[512];
        snprintf(axon_local, sizeof(axon_local), "%s/local", cfg->db_path);
        if (dir_has_package(axon_local, pkgname)) {
            return true;
        }
    }

    /* 2. Check Pacman local database */
    char pacman_local[512];
    get_pacman_local_dir(cfg, pacman_local, sizeof(pacman_local));
    if (dir_has_package(pacman_local, pkgname)) {
        return true;
    }

    /* 3. Check provides in Axon local db */
    if (cfg && cfg->db_path) {
        char axon_local[512];
        snprintf(axon_local, sizeof(axon_local), "%s/local", cfg->db_path);
        if (dir_provides_package(axon_local, pkgname)) {
            return true;
        }
    }

    /* 4. Check provides in Pacman local db */
    if (dir_provides_package(pacman_local, pkgname)) {
        return true;
    }

    return false;
}

axon_package_t *axon_db_get_installed(const axon_config_t *cfg, const char *pkgname) {
    if (!pkgname) return NULL;

    /* 1. Check Axon local database */
    if (cfg && cfg->db_path) {
        char axon_local[512];
        snprintf(axon_local, sizeof(axon_local), "%s/local", cfg->db_path);
        axon_package_t *pkg = dir_get_package(axon_local, pkgname);
        if (pkg) return pkg;
    }

    /* 2. Check Pacman local database */
    char pacman_local[512];
    get_pacman_local_dir(cfg, pacman_local, sizeof(pacman_local));
    return dir_get_package(pacman_local, pkgname);
}

static int list_installed_dir(const char *db_dir, axon_pkg_list_t *list) {
    if (!list) return -1;

    DIR *dir = opendir(db_dir);
    if (!dir) return -1;

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char desc_path[1024];
        snprintf(desc_path, sizeof(desc_path), "%s/%s/desc", db_dir, de->d_name);
        FILE *fp = fopen(desc_path, "r");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            char *buf = malloc(sz + 1);
            if (buf) {
                size_t read_bytes = fread(buf, 1, sz, fp);
                buf[read_bytes] = '\0';
                axon_package_t *pkg = axon_package_new();
                if (pkg) {
                    axon_package_parse_desc(pkg, buf);
                    pkg->repo = strdup("local");
                    axon_pkg_list_add(list, pkg);
                }
                free(buf);
            }
            fclose(fp);
        }
    }

    closedir(dir);
    return 0;
}

int axon_db_list_installed(const axon_config_t *cfg, axon_pkg_list_t *list) {
    if (!cfg || !list) return -1;
    char path[1024];
    snprintf(path, sizeof(path), "%s/local", cfg->db_path);
    int a = list_installed_dir(path, list);
    get_pacman_local_dir(cfg, path, sizeof(path));
    axon_pkg_list_t *pacman = axon_pkg_list_new();
    if (!pacman) return -1;
    int b = list_installed_dir(path, pacman);
    for (size_t i = 0; i < pacman->count; i++) {
        bool duplicate = false;
        for (size_t j = 0; j < list->count; j++)
            if (!strcmp(list->packages[j]->name, pacman->packages[i]->name)) duplicate = true;
        if (!duplicate) axon_pkg_list_add(list, axon_package_clone(pacman->packages[i]));
    }
    axon_pkg_list_free(pacman);
    return a == 0 || b == 0 ? 0 : -1;
}

int axon_db_record_install(const axon_config_t *cfg, const axon_package_t *pkg, const char **files, size_t file_count) {
    if (!cfg || !cfg->db_path || !pkg || !pkg->name || !pkg->version) return -1;

    char local_dir[512];
    snprintf(local_dir, sizeof(local_dir), "%s/local", cfg->db_path);
    if (axon_mkdir_p(local_dir, 0755) != 0) return -1;

    char pkg_dir[768];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s/%s-%s", local_dir, pkg->name, pkg->version);
    if (axon_mkdir_p(pkg_dir, 0755) != 0) return -1;

    /* Write desc file */
    char desc_file[1024];
    snprintf(desc_file, sizeof(desc_file), "%s/desc", pkg_dir);
    FILE *fp = fopen(desc_file, "w");
    if (!fp) return -1;
    if (fp) {
        fprintf(fp, "%%NAME%%\n%s\n\n", pkg->name);
        fprintf(fp, "%%VERSION%%\n%s\n\n", pkg->version);
        if (pkg->base) fprintf(fp, "%%BASE%%\n%s\n\n", pkg->base);
        if (pkg->desc) fprintf(fp, "%%DESC%%\n%s\n\n", pkg->desc);
        if (pkg->url) fprintf(fp, "%%URL%%\n%s\n\n", pkg->url);
        if (pkg->arch) fprintf(fp, "%%ARCH%%\n%s\n\n", pkg->arch);
        if (pkg->builddate > 0) fprintf(fp, "%%BUILDDATE%%\n%lld\n\n", (long long)pkg->builddate);
        fprintf(fp, "%%INSTALLDATE%%\n%lld\n\n", (long long)time(NULL));
        if (pkg->packager) fprintf(fp, "%%PACKAGER%%\n%s\n\n", pkg->packager);
        if (pkg->isize > 0) fprintf(fp, "%%SIZE%%\n%zu\n\n", pkg->isize);
        fprintf(fp, "%%REASON%%\n0\n\n");

        if (pkg->licenses_count > 0 && pkg->licenses) {
            fprintf(fp, "%%LICENSE%%\n");
            for (size_t i = 0; i < pkg->licenses_count; i++) {
                fprintf(fp, "%s\n", pkg->licenses[i]);
            }
            fprintf(fp, "\n");
        }

        if (pkg->depends_count > 0 && pkg->depends) {
            fprintf(fp, "%%DEPENDS%%\n");
            for (size_t i = 0; i < pkg->depends_count; i++) {
                fprintf(fp, "%s\n", pkg->depends[i]);
            }
            fprintf(fp, "\n");
        }

        if (pkg->provides_count) {
            fprintf(fp, "%%PROVIDES%%\n");
            for (size_t i = 0; i < pkg->provides_count; i++) fprintf(fp, "%s\n", pkg->provides[i]);
            fprintf(fp, "\n");
        }
        int write_error = ferror(fp);
        if (fclose(fp) != 0 || write_error) return -1;
    }

    /* Write files file */
    char files_file[1024];
    snprintf(files_file, sizeof(files_file), "%s/files", pkg_dir);
    FILE *ffp = fopen(files_file, "w");
    if (!ffp) return -1;
    if (ffp) {
        fprintf(ffp, "%%FILES%%\n");
        for (size_t i = 0; i < file_count; i++) {
            if (files[i]) {
                fprintf(ffp, "%s\n", files[i]);
            }
        }
        int write_error = ferror(ffp);
        if (fclose(ffp) != 0 || write_error) return -1;
    }

    return 0;
}

int axon_db_remove_package(const axon_config_t *cfg, const char *pkgname) {
    if (!cfg || !cfg->db_path || !pkgname) return -1;

    /* Never remove protected system packages on the host filesystem */
    if (axon_is_protected_pkg(pkgname) && (!cfg->root_dir || strcmp(cfg->root_dir, "/") == 0)) {
        return -1;
    }

    char local_dir[512];
    snprintf(local_dir, sizeof(local_dir), "%s/local", cfg->db_path);

    DIR *dir = opendir(local_dir);
    if (!dir) return -1;

    struct dirent *de;
    size_t name_len = strlen(pkgname);
    char target_pkg_dir[768] = {0};

    while ((de = readdir(dir)) != NULL) {
        if (strncmp(de->d_name, pkgname, name_len) == 0 && de->d_name[name_len] == '-') {
            snprintf(target_pkg_dir, sizeof(target_pkg_dir), "%s/%s", local_dir, de->d_name);
            break;
        }
    }
    closedir(dir);

    if (target_pkg_dir[0] == '\0') {
        return -1;
    }

    const char *root = (cfg->root_dir && strlen(cfg->root_dir) > 0) ? cfg->root_dir : "/";

    /* Read files list and remove each file */
    char files_path[1024];
    snprintf(files_path, sizeof(files_path), "%s/files", target_pkg_dir);
    FILE *fp = fopen(files_path, "r");
    if (fp) {
        char line[1024];
        bool in_files = false;
        char **dirs_to_prune = NULL;
        size_t dir_count = 0;

        while (fgets(line, sizeof(line), fp)) {
            size_t l = strlen(line);
            while (l > 0 && (line[l - 1] == '\r' || line[l - 1] == '\n' || line[l - 1] == ' ' || line[l - 1] == '\t')) {
                line[--l] = '\0';
            }
            if (strcmp(line, "%FILES%") == 0) {
                in_files = true;
                continue;
            }
            if (!in_files || line[0] == '\0') continue;

            /* Never unlink critical system files under any circumstances */
            if (axon_is_critical_file(line)) {
                continue;
            }

            char full_path[2048];
            if (strcmp(root, "/") == 0) {
                snprintf(full_path, sizeof(full_path), "/%s", line);
            } else {
                snprintf(full_path, sizeof(full_path), "%s/%s", root, line);
            }

            struct stat st;
            if (lstat(full_path, &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    char **new_d = realloc(dirs_to_prune, sizeof(char *) * (dir_count + 1));
                    if (new_d) {
                        dirs_to_prune = new_d;
                        dirs_to_prune[dir_count++] = strdup(full_path);
                    }
                } else {
                    unlink(full_path);
                }
            }
        }
        fclose(fp);

        if (dirs_to_prune) {
            for (size_t i = dir_count; i > 0; i--) {
                rmdir(dirs_to_prune[i - 1]);
                free(dirs_to_prune[i - 1]);
            }
            free(dirs_to_prune);
        }
    }

    /* Remove files inside target_pkg_dir */
    DIR *pkg_d = opendir(target_pkg_dir);
    if (pkg_d) {
        struct dirent *pde;
        while ((pde = readdir(pkg_d)) != NULL) {
            if (strcmp(pde->d_name, ".") == 0 || strcmp(pde->d_name, "..") == 0) continue;
            char entry_path[1024];
            snprintf(entry_path, sizeof(entry_path), "%s/%s", target_pkg_dir, pde->d_name);
            unlink(entry_path);
        }
        closedir(pkg_d);
    }
    rmdir(target_pkg_dir);

    return 0;
}
