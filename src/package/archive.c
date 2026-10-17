#include "axon/package.h"
#include <archive.h>
#include <archive_entry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int axon_archive_extract(const char *archive_path, const char *dest_dir) {
    if (!archive_path || !dest_dir) return -1;

    struct archive *a = archive_read_new();
    struct archive *ext = archive_write_disk_new();
    struct archive_entry *entry;
    int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS;
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

    char target_path[4096];
    while ((r = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        const char *current_path = archive_entry_pathname(entry);
        
        /* Skip leading slash or ./ */
        while (*current_path == '/' || (*current_path == '.' && *(current_path + 1) == '/')) {
            current_path++;
        }

        snprintf(target_path, sizeof(target_path), "%s/%s", dest_dir, current_path);
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
