#include "axon/package.h"
#include "axon/database.h"
#include <archive.h>
#include <archive_entry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool axon_is_protected_pkg(const char *name) {
    if (!name) return false;
    static const char *protected_pkgs[] = {
        "filesystem",
        "glibc",
        "pacman",
        "shadow",
        "systemd",
        "systemd-libs",
        "systemd-sysvcompat",
        "pam",
        "bash",
        "coreutils",
        "util-linux",
        "sudo",
        "polkit",
        "linux-api-headers",
        "iana-etc",
        "tzdata",
        NULL
    };
    for (int i = 0; protected_pkgs[i]; i++) {
        if (strcmp(name, protected_pkgs[i]) == 0) return true;
    }
    return false;
}

bool axon_is_critical_file(const char *rel_path) {
    if (!rel_path) return false;
    while (*rel_path == '/') rel_path++;

    static const char *critical_files[] = {
        "etc/passwd",
        "etc/shadow",
        "etc/group",
        "etc/gshadow",
        "etc/passwd-",
        "etc/shadow-",
        "etc/group-",
        "etc/gshadow-",
        "etc/subuid",
        "etc/subgid",
        "etc/sudoers",
        "etc/fstab",
        "etc/crypttab",
        "etc/hostname",
        "etc/hosts",
        "etc/resolv.conf",
        "etc/vconsole.conf",
        "etc/locale.conf",
        "etc/locale.gen",
        "etc/machine-id",
        "etc/nsswitch.conf",
        "etc/ld.so.conf",
        NULL
    };

    for (int i = 0; critical_files[i]; i++) {
        if (strcmp(rel_path, critical_files[i]) == 0) return true;
    }

    if (strncmp(rel_path, "etc/sudoers.d/", 14) == 0) return true;
    if (strncmp(rel_path, "etc/pam.d/", 10) == 0) return true;
    if (strncmp(rel_path, "etc/ld.so.conf.d/", 17) == 0) return true;

    return false;
}

static bool is_config_file(const char *rel_path) {
    if (!rel_path) return false;
    while (*rel_path == '/') rel_path++;
    return (strncmp(rel_path, "etc/", 4) == 0);
}

int axon_archive_check_conflicts(const char *archive_path, const char *dest_dir, const char *pkgname, const void *cfg, char ***conflicts, size_t *out_count) {
    if (!archive_path || !dest_dir || !conflicts || !out_count) return -1;
    *conflicts = NULL;
    *out_count = 0;

    struct archive *a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    if (archive_read_open_filename(a, archive_path, 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return -1;
    }

    struct archive_entry *entry;
    char target_path[4096];

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (archive_entry_filetype(entry) == AE_IFDIR) {
            archive_read_data_skip(a);
            continue;
        }

        const char *current_path = archive_entry_pathname(entry);
        while (*current_path == '/' || (*current_path == '.' && *(current_path + 1) == '/')) {
            current_path++;
        }

        if (strcmp(current_path, ".PKGINFO") == 0 ||
            strcmp(current_path, ".BUILDINFO") == 0 ||
            strcmp(current_path, ".MTREE") == 0 ||
            strcmp(current_path, ".INSTALL") == 0) {
            archive_read_data_skip(a);
            continue;
        }

        /* Config files under /etc/ are diverted safely to .axonnew */
        if (is_config_file(current_path)) {
            archive_read_data_skip(a);
            continue;
        }

        if (strcmp(dest_dir, "/") == 0) {
            snprintf(target_path, sizeof(target_path), "/%s", current_path);
        } else {
            snprintf(target_path, sizeof(target_path), "%s/%s", dest_dir, current_path);
        }

        /* Check if file exists */
        struct stat st;
        if (lstat(target_path, &st) == 0 && !S_ISDIR(st.st_mode)) {
            char *owner = NULL;
            if (cfg) {
                axon_db_check_ownership((const axon_config_t *)cfg, current_path, &owner);
            }

            /* If already owned by this package (upgrade scenario), allow */
            if (owner && pkgname && strncmp(owner, pkgname, strlen(pkgname)) == 0 &&
                (owner[strlen(pkgname)] == '-' || owner[strlen(pkgname)] == '\0')) {
                free(owner);
                archive_read_data_skip(a);
                continue;
            }

            char conflict_msg[4608];
            if (owner) {
                snprintf(conflict_msg, sizeof(conflict_msg), "%s exists in filesystem (owned by %s)", target_path, owner);
                free(owner);
            } else {
                snprintf(conflict_msg, sizeof(conflict_msg), "%s exists in filesystem", target_path);
            }

            char **new_c = realloc(*conflicts, sizeof(char *) * (*out_count + 1));
            if (new_c) {
                *conflicts = new_c;
                (*conflicts)[*out_count] = strdup(conflict_msg);
                (*out_count)++;
            }
        }

        archive_read_data_skip(a);
    }

    archive_read_close(a);
    archive_read_free(a);
    return 0;
}

int axon_archive_extract_pkg(const char *archive_path, const char *dest_dir, char ***out_files, size_t *out_count) {
    if (!archive_path || !dest_dir) return -1;

    struct archive *a = archive_read_new();
    struct archive *ext = archive_write_disk_new();
    struct archive_entry *entry;
    int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS |
                ARCHIVE_EXTRACT_SECURE_NODOTDOT | ARCHIVE_EXTRACT_SECURE_SYMLINKS;
    int r;

    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    archive_write_disk_set_options(ext, flags);
    archive_write_disk_set_standard_lookup(ext);

    if ((r = archive_read_open_filename(a, archive_path, 10240))) {
        fprintf(stderr, "Error opening archive %s: %s\n", archive_path, archive_error_string(a));
        archive_read_free(a);
        archive_write_free(ext);
        return -1;
    }

    if (out_files && out_count) {
        *out_files = NULL;
        *out_count = 0;
    }

    char target_path[4096];
    while ((r = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        const char *current_path = archive_entry_pathname(entry);
        
        /* Skip leading slash or ./ */
        while (*current_path == '/' || (*current_path == '.' && *(current_path + 1) == '/')) {
            current_path++;
        }

        /* Skip Arch package metadata files */
        if (strcmp(current_path, ".PKGINFO") == 0 ||
            strcmp(current_path, ".BUILDINFO") == 0 ||
            strcmp(current_path, ".MTREE") == 0 ||
            strcmp(current_path, ".INSTALL") == 0) {
            archive_read_data_skip(a);
            continue;
        }

        bool diverted = false;
        if (strcmp(dest_dir, "/") == 0) {
            snprintf(target_path, sizeof(target_path), "/%s", current_path);
        } else {
            snprintf(target_path, sizeof(target_path), "%s/%s", dest_dir, current_path);
        }

        /* CRITICAL SAFETY: Protect existing configuration and identity files */
        struct stat st_target;
        if (archive_entry_filetype(entry) != AE_IFDIR && lstat(target_path, &st_target) == 0) {
            if (axon_is_critical_file(current_path)) {
                diverted = true;
                if (strcmp(dest_dir, "/") == 0) {
                    snprintf(target_path, sizeof(target_path), "/%s.axonnew", current_path);
                } else {
                    snprintf(target_path, sizeof(target_path), "%s/%s.axonnew", dest_dir, current_path);
                }
                fprintf(stderr, "warning: protected system file '%s' exists; saving new version as '%s.axonnew'\n",
                        current_path, current_path);
            } else if (is_config_file(current_path)) {
                diverted = true;
                if (strcmp(dest_dir, "/") == 0) {
                    snprintf(target_path, sizeof(target_path), "/%s.axonnew", current_path);
                } else {
                    snprintf(target_path, sizeof(target_path), "%s/%s.axonnew", dest_dir, current_path);
                }
                fprintf(stderr, "warning: existing configuration '%s' preserved; saving new version as '%s.axonnew'\n",
                        current_path, current_path);
            }
        }

        if (out_files && out_count && strlen(current_path) > 0) {
            char rec_path[4096];
            if (diverted) {
                snprintf(rec_path, sizeof(rec_path), "%s.axonnew", current_path);
            } else {
                snprintf(rec_path, sizeof(rec_path), "%s", current_path);
            }
            char **new_files = realloc(*out_files, sizeof(char *) * (*out_count + 1));
            if (new_files) {
                *out_files = new_files;
                (*out_files)[*out_count] = strdup(rec_path);
                (*out_count)++;
            }
        }

        archive_entry_set_pathname(entry, target_path);

        r = archive_write_header(ext, entry);
        if (r < ARCHIVE_OK) {
            fprintf(stderr, "Warning archive_write_header: %s\n", archive_error_string(ext));
        } else if (archive_entry_size(entry) > 0) {
            const void *buff;
            size_t size;
            la_int64_t offset;

            while ((r = archive_read_data_block(a, &buff, &size, &offset)) == ARCHIVE_OK) {
                if (archive_write_data_block(ext, buff, size, offset) < ARCHIVE_OK) {
                    fprintf(stderr, "Warning archive_write_data_block: %s\n", archive_error_string(ext));
                    break;
                }
            }
        }
        archive_write_finish_entry(ext);
    }

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);

    return (r == ARCHIVE_EOF) ? 0 : -1;
}

int axon_archive_extract(const char *archive_path, const char *dest_dir) {
    return axon_archive_extract_pkg(archive_path, dest_dir, NULL, NULL);
}
