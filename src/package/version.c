#include "axon/package.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/*
 * Standard rpmvercmp-compatible version segment comparison
 * as used by Arch Linux / libalpm.
 */
static int rpmvercmp(const char *a, const char *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;

    char str1[512], str2[512];
    strncpy(str1, a, sizeof(str1) - 1);
    strncpy(str2, b, sizeof(str2) - 1);
    str1[sizeof(str1) - 1] = '\0';
    str2[sizeof(str2) - 1] = '\0';

    char *one = str1;
    char *two = str2;

    while (*one || *two) {
        while (*one && !isalnum((unsigned char)*one) && *one != '~') one++;
        while (*two && !isalnum((unsigned char)*two) && *two != '~') two++;

        /* Handle tilde '~' for pre-releases */
        if (*one == '~' || *two == '~') {
            if (*one != '~') return 1;
            if (*two != '~') return -1;
            one++;
            two++;
            continue;
        }

        if (!*one && !*two) return 0;
        if (!*one) return -1;
        if (!*two) return 1;

        char *ptr1 = one;
        char *ptr2 = two;
        int isnum = 0;

        if (isdigit((unsigned char)*ptr1)) {
            while (*ptr1 && isdigit((unsigned char)*ptr1)) ptr1++;
            while (*ptr2 && isdigit((unsigned char)*ptr2)) ptr2++;
            isnum = 1;
        } else {
            while (*ptr1 && isalpha((unsigned char)*ptr1)) ptr1++;
            while (*ptr2 && isalpha((unsigned char)*ptr2)) ptr2++;
            isnum = 0;
        }

        char save1 = *ptr1;
        char save2 = *ptr2;
        *ptr1 = '\0';
        *ptr2 = '\0';

        if (isnum) {
            /* If two is not a number, number is newer */
            if (!isdigit((unsigned char)*two)) {
                return 1;
            }
            /* Skip leading zeros */
            char *num1 = one;
            char *num2 = two;
            while (*num1 == '0') num1++;
            while (*num2 == '0') num2++;
            size_t len1 = strlen(num1);
            size_t len2 = strlen(num2);
            if (len1 > len2) return 1;
            if (len1 < len2) return -1;
            int cmp = strcmp(num1, num2);
            if (cmp != 0) return cmp > 0 ? 1 : -1;
        } else {
            if (isdigit((unsigned char)*two)) {
                return -1;
            }
            int cmp = strcmp(one, two);
            if (cmp != 0) return cmp > 0 ? 1 : -1;
        }

        *ptr1 = save1;
        *ptr2 = save2;
        one = ptr1;
        two = ptr2;
    }

    return 0;
}

int axon_version_cmp(const char *ver1, const char *ver2) {
    if (!ver1 && !ver2) return 0;
    if (!ver1) return -1;
    if (!ver2) return 1;
    if (strcmp(ver1, ver2) == 0) return 0;

    int epoch1 = 0, epoch2 = 0;
    const char *v1 = ver1;
    const char *v2 = ver2;

    const char *colon1 = strchr(ver1, ':');
    if (colon1) {
        epoch1 = atoi(ver1);
        v1 = colon1 + 1;
    }

    const char *colon2 = strchr(ver2, ':');
    if (colon2) {
        epoch2 = atoi(ver2);
        v2 = colon2 + 1;
    }

    if (epoch1 > epoch2) return 1;
    if (epoch1 < epoch2) return -1;

    /* Separate version and pkgrel */
    char buf1[256], buf2[256];
    strncpy(buf1, v1, sizeof(buf1) - 1);
    strncpy(buf2, v2, sizeof(buf2) - 1);
    buf1[sizeof(buf1) - 1] = '\0';
    buf2[sizeof(buf2) - 1] = '\0';

    char *rel1 = strrchr(buf1, '-');
    char *rel2 = strrchr(buf2, '-');

    if (rel1) *rel1++ = '\0';
    if (rel2) *rel2++ = '\0';

    int cmp = rpmvercmp(buf1, buf2);
    if (cmp != 0) return cmp;

    if (rel1 && rel2) {
        return rpmvercmp(rel1, rel2);
    } else if (rel1) {
        return 1;
    } else if (rel2) {
        return -1;
    }

    return 0;
}
