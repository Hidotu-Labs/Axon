#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

char *axon_target_path(const char *root, const char *relative, char *out, size_t out_sz) {
    if (!root || !relative || !out || out_sz == 0) return NULL;
    while (*relative == '/') relative++;
    if (!*root || strcmp(root, "/") == 0) {
        snprintf(out, out_sz, "/%s", relative);
    } else {
        snprintf(out, out_sz, "%s/%s", root, relative);
    }
    return out;
}

int axon_in_target(const char *root, char *const argv[], const char *data, size_t data_len) {
    if (!argv || !argv[0]) return -1;

    /* Print command line for visibility */
    printf(":: target: ");
    for (int i = 0; argv[i]; i++) {
        printf("%s%s", i > 0 ? " " : "", argv[i]);
    }
    printf("\n");
    fflush(stdout);

    int pipe_in[2] = {-1, -1};
    if (data && data_len > 0) {
        if (pipe(pipe_in) < 0) return -1;
    }

    bool is_host = (!root || !*root || strcmp(root, "/") == 0);

    /* For non-host root, ensure /dev devices exist for bind-mounting */
    char created_devs[4][512];
    int created_count = 0;
    if (!is_host) {
        char dev_dir[512];
        snprintf(dev_dir, sizeof(dev_dir), "%s/dev", root);
        mkdir(dev_dir, 0755);
        const char *devices[] = {"null", "zero", "random", "urandom"};
        for (int i = 0; i < 4; i++) {
            char dev_path[512];
            snprintf(dev_path, sizeof(dev_path), "%s/dev/%s", root, devices[i]);
            if (access(dev_path, F_OK) != 0) {
                FILE *f = fopen(dev_path, "w");
                if (f) {
                    fclose(f);
                    snprintf(created_devs[created_count++], sizeof(created_devs[0]), "%s", dev_path);
                }
            }
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        if (pipe_in[0] >= 0) { close(pipe_in[0]); close(pipe_in[1]); }
        for (int i = 0; i < created_count; i++) unlink(created_devs[i]);
        return -1;
    }

    if (pid == 0) {
        /* Child */
        if (pipe_in[0] >= 0) {
            close(pipe_in[1]);
            dup2(pipe_in[0], STDIN_FILENO);
            close(pipe_in[0]);
        }

        setenv("HOME", "/", 1);
        setenv("PATH", "/usr/bin:/usr/sbin:/bin:/sbin", 1);
        setenv("LC_ALL", "C", 1);

        if (is_host) {
            execvp(argv[0], argv);
            _exit(127);
        } else {
            const char *wrapper = "root=$1; shift; "
                                  "for device in null zero random urandom; do "
                                  "mount --bind \"/dev/$device\" \"$root/dev/$device\" || exit 126; done; "
                                  "exec chroot -- \"$root\" \"$@\"";

            int argc = 0;
            while (argv[argc]) argc++;

            char **uargv = malloc(sizeof(char *) * (argc + 8));
            if (!uargv) _exit(127);
            int idx = 0;
            uargv[idx++] = "unshare";
            uargv[idx++] = "-rm";
            uargv[idx++] = "/bin/bash";
            uargv[idx++] = "-c";
            uargv[idx++] = (char *)wrapper;
            uargv[idx++] = "axon-target";
            uargv[idx++] = (char *)root;
            for (int i = 0; i < argc; i++) {
                uargv[idx++] = argv[i];
            }
            uargv[idx] = NULL;

            execvp("unshare", uargv);
            _exit(127);
        }
    }

    /* Parent */
    if (pipe_in[1] >= 0) {
        close(pipe_in[0]);
        size_t written = 0;
        while (written < data_len) {
            ssize_t n = write(pipe_in[1], data + written, data_len - written);
            if (n <= 0) break;
            written += (size_t)n;
        }
        close(pipe_in[1]);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) { status = -1; break; }
    }

    for (int i = 0; i < created_count; i++) {
        unlink(created_devs[i]);
    }

    if (status < 0) return -1;
    return (WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
}
