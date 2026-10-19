#include "axon/package.h"
#include "axon/database.h"
#include "axon/repository.h"
#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int create_test_tar(const char *tar_path) {
    struct archive *a = archive_write_new();
    archive_write_set_format_pax_restricted(a);
    if (archive_write_open_filename(a, tar_path) != ARCHIVE_OK) {
        archive_write_free(a);
        return -1;
    }

    /* 1. Add .PKGINFO metadata file (should be ignored by extractor) */
    struct archive_entry *entry = archive_entry_new();
    archive_entry_set_pathname(entry, ".PKGINFO");
    archive_entry_set_size(entry, 16);
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_write_header(a, entry);
    archive_write_data(a, "pkgname = test\n", 16);
    archive_entry_free(entry);

    /* 2. Add usr/bin/testapp file */
    const char *payload = "#!/bin/sh\necho test\n";
    entry = archive_entry_new();
    archive_entry_set_pathname(entry, "usr/bin/testapp");
    archive_entry_set_size(entry, strlen(payload));
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0755);
    archive_write_header(a, entry);
    archive_write_data(a, payload, strlen(payload));
    archive_entry_free(entry);

    archive_write_close(a);
    archive_write_free(a);
    return 0;
}

static int create_config_tar(const char *tar_path) {
    struct archive *a = archive_write_new();
    archive_write_set_format_pax_restricted(a);
    if (archive_write_open_filename(a, tar_path) != ARCHIVE_OK) {
        archive_write_free(a);
        return -1;
    }

    const char *payload = "dangerous_root_passwd_data\n";
    struct archive_entry *entry = archive_entry_new();
    archive_entry_set_pathname(entry, "etc/passwd");
    archive_entry_set_size(entry, strlen(payload));
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_write_header(a, entry);
    archive_write_data(a, payload, strlen(payload));
    archive_entry_free(entry);

    archive_write_close(a);
    archive_write_free(a);
    return 0;
}

int main(void) {
    printf("Running archive safety, conflict checking, and extraction unit tests...\n");

    /* 1. Verify protected package detection */
    assert(axon_is_protected_pkg("filesystem"));
    assert(axon_is_protected_pkg("glibc"));
    assert(axon_is_protected_pkg("pacman"));
    assert(axon_is_protected_pkg("bash"));
    assert(axon_is_protected_pkg("coreutils"));
    assert(axon_is_protected_pkg("sudo"));
    assert(!axon_is_protected_pkg("onefetch"));
    assert(!axon_is_protected_pkg("fortune-mod"));

    /* 2. Verify critical system files protection */
    assert(axon_is_critical_file("etc/passwd"));
    assert(axon_is_critical_file("/etc/passwd"));
    assert(axon_is_critical_file("etc/shadow"));
    assert(axon_is_critical_file("etc/group"));
    assert(axon_is_critical_file("etc/sudoers"));
    assert(axon_is_critical_file("etc/sudoers.d/custom"));
    assert(!axon_is_critical_file("usr/bin/testapp"));

    const char *test_dir = "/tmp/axon_archive_unit_test";
    const char *tar_path = "/tmp/axon_archive_unit_test/testpkg-1.0-1-x86_64.pkg.tar";
    const char *cfg_tar_path = "/tmp/axon_archive_unit_test/cfgpkg-1.0-1-x86_64.pkg.tar";
    const char *root_dir = "/tmp/axon_archive_unit_test/root";
    const char *db_dir = "/tmp/axon_archive_unit_test/db";

    axon_mkdir_p(test_dir, 0755);
    axon_mkdir_p(root_dir, 0755);
    axon_mkdir_p(db_dir, 0755);

    /* 3. Create a test archive */
    int r = create_test_tar(tar_path);
    assert(r == 0);

    struct stat st;
    assert(stat(tar_path, &st) == 0 && S_ISREG(st.st_mode));

    /* 4. Extract archive using axon_archive_extract_pkg */
    char **files = NULL;
    size_t file_count = 0;
    r = axon_archive_extract_pkg(tar_path, root_dir, &files, &file_count);
    assert(r == 0);
    assert(file_count == 1);
    assert(strcmp(files[0], "usr/bin/testapp") == 0);

    /* Verify extracted file exists and has content */
    char extracted_path[512];
    snprintf(extracted_path, sizeof(extracted_path), "%s/usr/bin/testapp", root_dir);
    assert(stat(extracted_path, &st) == 0 && S_ISREG(st.st_mode));

    /* Verify .PKGINFO was not extracted to root */
    char pkginfo_path[512];
    snprintf(pkginfo_path, sizeof(pkginfo_path), "%s/.PKGINFO", root_dir);
    assert(stat(pkginfo_path, &st) != 0);

    /* 5. Pre-Commit Conflict Checking: test conflict when file already exists */
    char **conflicts = NULL;
    size_t conflict_count = 0;
    axon_config_t cfg;
    axon_config_init(&cfg);
    free(cfg.db_path);
    cfg.db_path = strdup(db_dir);
    free(cfg.root_dir);
    cfg.root_dir = strdup(root_dir);

    /* Archive contains usr/bin/testapp, which now exists on disk and is not yet in db */
    r = axon_archive_check_conflicts(tar_path, root_dir, "other_pkg", &cfg, &conflicts, &conflict_count);
    assert(r == 0);
    assert(conflict_count == 1);
    assert(strstr(conflicts[0], "usr/bin/testapp exists in filesystem") != NULL);
    for (size_t i = 0; i < conflict_count; i++) free(conflicts[i]);
    free(conflicts);

    /* 6. Verify .axonnew protection for /etc/passwd:
          Pre-create /etc/passwd with safe content. */
    char etc_dir[512];
    snprintf(etc_dir, sizeof(etc_dir), "%s/etc", root_dir);
    axon_mkdir_p(etc_dir, 0755);

    char original_passwd[512];
    snprintf(original_passwd, sizeof(original_passwd), "%s/etc/passwd", root_dir);
    FILE *pfp = fopen(original_passwd, "w");
    assert(pfp != NULL);
    fputs("safe_original_passwd\n", pfp);
    fclose(pfp);

    /* Create archive with malicious/default etc/passwd */
    r = create_config_tar(cfg_tar_path);
    assert(r == 0);

    char **cfg_files = NULL;
    size_t cfg_count = 0;
    r = axon_archive_extract_pkg(cfg_tar_path, root_dir, &cfg_files, &cfg_count);
    assert(r == 0);
    assert(cfg_count == 1);
    assert(strcmp(cfg_files[0], "etc/passwd.axonnew") == 0);

    /* Verify original /etc/passwd was NOT overwritten! */
    pfp = fopen(original_passwd, "r");
    assert(pfp != NULL);
    char line[128] = {0};
    assert(fgets(line, sizeof(line), pfp) != NULL);
    fclose(pfp);
    assert(strcmp(line, "safe_original_passwd\n") == 0);

    /* Verify /etc/passwd.axonnew was created instead */
    char axonnew_path[512];
    snprintf(axonnew_path, sizeof(axonnew_path), "%s/etc/passwd.axonnew", root_dir);
    assert(stat(axonnew_path, &st) == 0 && S_ISREG(st.st_mode));
    pfp = fopen(axonnew_path, "r");
    assert(pfp != NULL);
    assert(fgets(line, sizeof(line), pfp) != NULL);
    fclose(pfp);
    assert(strcmp(line, "dangerous_root_passwd_data\n") == 0);

    /* Cleanup config test files */
    unlink(original_passwd);
    unlink(axonnew_path);
    rmdir(etc_dir);
    unlink(cfg_tar_path);
    for (size_t i = 0; i < cfg_count; i++) free(cfg_files[i]);
    free(cfg_files);

    /* 7. Record installation in local database */
    axon_package_t pkg;
    memset(&pkg, 0, sizeof(axon_package_t));
    pkg.name = "testapp";
    pkg.version = "1.0-1";
    pkg.desc = "A unit test package";

    r = axon_db_record_install(&cfg, &pkg, (const char **)files, file_count);
    assert(r == 0);

    /* 8. Verify database check */
    assert(axon_db_is_installed(&cfg, "testapp"));

    /* 9. Remove package */
    r = axon_db_remove_package(&cfg, "testapp");
    assert(r == 0);
    assert(stat(extracted_path, &st) != 0);
    assert(!axon_db_is_installed(&cfg, "testapp"));

    /* Cleanup */
    unlink(tar_path);
    for (size_t i = 0; i < file_count; i++) {
        free(files[i]);
    }
    free(files);
    axon_config_free(&cfg);

    rmdir("/tmp/axon_archive_unit_test/root/usr/bin");
    rmdir("/tmp/axon_archive_unit_test/root/usr");
    rmdir("/tmp/axon_archive_unit_test/root");
    rmdir("/tmp/axon_archive_unit_test/db/local");
    rmdir("/tmp/axon_archive_unit_test/db");
    rmdir("/tmp/axon_archive_unit_test");

    printf("All archive safety, conflict checking, and extraction unit tests passed!\n");
    return 0;
}
