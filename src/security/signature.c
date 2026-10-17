#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int axon_signature_read(const char *sig_file, char **sig_data, size_t *sig_len) {
    if (!sig_file || !sig_data || !sig_len) return -1;
    FILE *fp = fopen(sig_file, "rb");
    if (!fp) return -1;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buf = malloc(sz);
    if (!buf) {
        fclose(fp);
        return -1;
    }

    size_t read_bytes = fread(buf, 1, sz, fp);
    fclose(fp);

    *sig_data = buf;
    *sig_len = read_bytes;
    return 0;
}
