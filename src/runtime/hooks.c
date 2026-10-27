#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "runtime.h"
#include "axon/database.h"
#include <dirent.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    char type[32];
    char op[32];
    char value[1024];
} manifest_record_t;

typedef struct {
    char type[32];
    char op[32];
    char **targets;
    size_t target_count;
} hook_trigger_t;

typedef struct {
    char *description;
    char *when;
    char *exec;
    char **depends;
    size_t depends_count;
    bool abort_on_fail;
    bool needs_targets;
} hook_action_t;

typedef struct {
    char name[256];
    char full_path[PATH_MAX];
} hook_entry_t;

static int compare_hook_entries(const void *a, const void *b) {
    const hook_entry_t *ha = (const hook_entry_t *)a;
    const hook_entry_t *hb = (const hook_entry_t *)b;
    return strcmp(ha->name, hb->name);
}

/* Parse quoted string into argv array */
static char **parse_exec_command(const char *exec_str, int *out_argc) {
    if (!exec_str) return NULL;
    char **argv = malloc(sizeof(char *) * 128);
    if (!argv) return NULL;
    int argc = 0;

    const char *p = exec_str;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        char buf[1024];
        int bi = 0;
        char quote = 0;

        while (*p) {
            if (!quote && (*p == '\'' || *p == '"')) {
                quote = *p++;
            } else if (quote && *p == quote) {
                quote = 0;
                p++;
            } else if (!quote && (*p == ' ' || *p == '\t')) {
                break;
            } else if (quote != '\'' && *p == '\\' && *(p + 1)) {
                p++;
                if (bi < (int)sizeof(buf) - 1) buf[bi++] = *p++;
            } else {
                if (bi < (int)sizeof(buf) - 1) buf[bi++] = *p++;
            }
        }
        buf[bi] = '\0';
        argv[argc++] = strdup(buf);
        if (argc >= 126) break;
    }
    argv[argc] = NULL;
    if (out_argc) *out_argc = argc;
    return argv;
}

int axon_run_hooks(const axon_config_t *cfg, const char *manifest_file, const char *phase) {
    if (!cfg || !manifest_file || !phase) return 0;

    /* 1. Read transaction manifest */
    FILE *mf = fopen(manifest_file, "r");
    if (!mf) return 0;

    manifest_record_t *records = NULL;
    size_t record_count = 0;
    char line[2048];

    while (fgets(line, sizeof(line), mf)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\r' || line[l - 1] == '\n')) line[--l] = '\0';
        if (!line[0]) continue;

        char *t = strtok(line, "\t");
        char *o = strtok(NULL, "\t");
        char *v = strtok(NULL, "\t");
        if (!t || !o || !v) continue;

        manifest_record_t *new_r = realloc(records, sizeof(manifest_record_t) * (record_count + 1));
        if (!new_r) break;
        records = new_r;

        /* Map 'File' -> 'Path' */
        if (strcmp(t, "File") == 0) t = "Path";

        snprintf(records[record_count].type, sizeof(records[record_count].type), "%s", t);
        snprintf(records[record_count].op, sizeof(records[record_count].op), "%s", o);
        snprintf(records[record_count].value, sizeof(records[record_count].value), "%s", v);
        record_count++;
    }
    fclose(mf);

    if (record_count == 0) {
        free(records);
        return 0;
    }

    /* 2. Discover hook candidates */
    const char *root = (cfg->root_dir && *cfg->root_dir) ? cfg->root_dir : "/";
    const char *hook_dirs[] = {"etc/pacman.d/hooks", "usr/share/libalpm/hooks"};
    hook_entry_t *candidates = NULL;
    size_t cand_count = 0;
    char *masked_hooks[64];
    size_t masked_count = 0;

    for (int d = 0; d < 2; d++) {
        char dir_path[1024];
        axon_target_path(root, hook_dirs[d], dir_path, sizeof(dir_path));
        DIR *dir = opendir(dir_path);
        if (!dir) continue;

        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            size_t nlen = strlen(de->d_name);
            if (nlen < 6 || strcmp(de->d_name + nlen - 5, ".hook") != 0) continue;

            /* Check if already masked or found in higher priority directory */
            bool exists = false;
            for (size_t c = 0; c < cand_count; c++) {
                if (strcmp(candidates[c].name, de->d_name) == 0) {
                    exists = true;
                    break;
                }
            }
            if (exists) continue;

            for (size_t m = 0; m < masked_count; m++) {
                if (strcmp(masked_hooks[m], de->d_name) == 0) {
                    exists = true;
                    break;
                }
            }
            if (exists) continue;

            char hook_path[PATH_MAX];
            snprintf(hook_path, sizeof(hook_path), "%s/%s", dir_path, de->d_name);

            /* Check /dev/null symlink overrides */
            struct stat lst;
            if (lstat(hook_path, &lst) == 0 && S_ISLNK(lst.st_mode)) {
                char link_target[256] = {0};
                ssize_t rlen = readlink(hook_path, link_target, sizeof(link_target) - 1);
                if (rlen > 0 && strcmp(link_target, "/dev/null") == 0) {
                    if (masked_count < 64) {
                        masked_hooks[masked_count++] = strdup(de->d_name);
                    }
                    continue;
                }
            }

            hook_entry_t *new_c = realloc(candidates, sizeof(hook_entry_t) * (cand_count + 1));
            if (!new_c) break;
            candidates = new_c;
            snprintf(candidates[cand_count].name, sizeof(candidates[cand_count].name), "%s", de->d_name);
            snprintf(candidates[cand_count].full_path, sizeof(candidates[cand_count].full_path), "%s", hook_path);
            cand_count++;
        }
        closedir(dir);
    }

    if (cand_count > 1) {
        qsort(candidates, cand_count, sizeof(hook_entry_t), compare_hook_entries);
    }

    bool overall_failed = false;

    /* 3. Evaluate each hook */
    for (size_t h = 0; h < cand_count; h++) {
        FILE *hf = fopen(candidates[h].full_path, "r");
        if (!hf) continue;

        hook_trigger_t triggers[16];
        size_t trigger_count = 0;
        memset(triggers, 0, sizeof(triggers));

        hook_action_t action;
        memset(&action, 0, sizeof(action));

        int section = 0; /* 0 = none, 1 = Trigger, 2 = Action */
        char hline[1024];

        while (fgets(hline, sizeof(hline), hf)) {
            size_t l = strlen(hline);
            while (l > 0 && (hline[l - 1] == '\r' || hline[l - 1] == '\n')) hline[--l] = '\0';
            char *p = hline;
            while (*p == ' ' || *p == '\t') p++;
            if (!*p || *p == '#') continue;

            if (strcmp(p, "[Trigger]") == 0) {
                if (trigger_count < 16) {
                    section = 1;
                    trigger_count++;
                }
                continue;
            } else if (strcmp(p, "[Action]") == 0) {
                section = 2;
                continue;
            }

            char *eq = strchr(p, '=');
            char key[64] = {0};
            char val[1024] = {0};
            if (eq) {
                *eq = '\0';
                char *k = p;
                char *v = eq + 1;
                while (*k == ' ' || *k == '\t') k++;
                char *ke = k + strlen(k) - 1;
                while (ke > k && (*ke == ' ' || *ke == '\t')) *ke-- = '\0';
                while (*v == ' ' || *v == '\t') v++;
                char *ve = v + strlen(v) - 1;
                while (ve > v && (*ve == ' ' || *ve == '\t')) *ve-- = '\0';
                snprintf(key, sizeof(key), "%s", k);
                snprintf(val, sizeof(val), "%s", v);
            } else {
                snprintf(key, sizeof(key), "%s", p);
            }

            if (section == 1 && trigger_count > 0) {
                hook_trigger_t *trig = &triggers[trigger_count - 1];
                if (strcasecmp(key, "Type") == 0) {
                    if (strcasecmp(val, "File") == 0) snprintf(trig->type, sizeof(trig->type), "Path");
                    else snprintf(trig->type, sizeof(trig->type), "%s", val);
                } else if (strcasecmp(key, "Operation") == 0) {
                    snprintf(trig->op, sizeof(trig->op), "%s", val);
                } else if (strcasecmp(key, "Target") == 0) {
                    char **nt = realloc(trig->targets, sizeof(char *) * (trig->target_count + 1));
                    if (nt) {
                        trig->targets = nt;
                        trig->targets[trig->target_count++] = strdup(val);
                    }
                }
            } else if (section == 2) {
                if (strcasecmp(key, "Description") == 0) {
                    free(action.description);
                    action.description = strdup(val);
                } else if (strcasecmp(key, "When") == 0) {
                    free(action.when);
                    action.when = strdup(val);
                } else if (strcasecmp(key, "Exec") == 0) {
                    free(action.exec);
                    action.exec = strdup(val);
                } else if (strcasecmp(key, "Depends") == 0) {
                    /* Space-separated depends */
                    char *tok = strtok(val, " \t");
                    while (tok) {
                        char **nd = realloc(action.depends, sizeof(char *) * (action.depends_count + 1));
                        if (nd) {
                            action.depends = nd;
                            action.depends[action.depends_count++] = strdup(tok);
                        }
                        tok = strtok(NULL, " \t");
                    }
                } else if (strcasecmp(key, "AbortOnFail") == 0) {
                    action.abort_on_fail = true;
                } else if (strcasecmp(key, "NeedsTargets") == 0) {
                    action.needs_targets = true;
                }
            }
        }
        fclose(hf);

        /* Check When */
        if (!action.when || strcmp(action.when, phase) != 0 || !action.exec) {
            /* Cleanup action and triggers */
            free(action.description); free(action.when); free(action.exec);
            for (size_t d = 0; d < action.depends_count; d++) free(action.depends[d]);
            free(action.depends);
            for (size_t t = 0; t < trigger_count; t++) {
                for (size_t tg = 0; tg < triggers[t].target_count; tg++) free(triggers[t].targets[tg]);
                free(triggers[t].targets);
            }
            continue;
        }

        /* Match triggers against manifest records */
        char **matched_targets = NULL;
        size_t matched_count = 0;

        for (size_t t = 0; t < trigger_count; t++) {
            hook_trigger_t *trig = &triggers[t];
            for (size_t r = 0; r < record_count; r++) {
                if (strcmp(records[r].type, trig->type) != 0) continue;
                if (strcmp(records[r].op, trig->op) != 0) continue;

                /* Evaluate patterns */
                bool selected = false;
                for (size_t pat_idx = 0; pat_idx < trig->target_count; pat_idx++) {
                    const char *pat = trig->targets[pat_idx];
                    bool negative = (*pat == '!');
                    if (negative) pat++;
                    if (fnmatch(pat, records[r].value, 0) == 0) {
                        selected = !negative;
                    }
                }

                if (selected) {
                    /* Check if duplicate */
                    bool dup = false;
                    for (size_t m = 0; m < matched_count; m++) {
                        if (strcmp(matched_targets[m], records[r].value) == 0) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup) {
                        char **nm = realloc(matched_targets, sizeof(char *) * (matched_count + 1));
                        if (nm) {
                            matched_targets = nm;
                            matched_targets[matched_count++] = strdup(records[r].value);
                        }
                    }
                }
            }
        }

        if (matched_count == 0) {
            free(action.description); free(action.when); free(action.exec);
            for (size_t d = 0; d < action.depends_count; d++) free(action.depends[d]);
            free(action.depends);
            for (size_t t = 0; t < trigger_count; t++) {
                for (size_t tg = 0; tg < triggers[t].target_count; tg++) free(triggers[t].targets[tg]);
                free(triggers[t].targets);
            }
            continue;
        }

        /* Check Depends */
        bool missing_deps = false;
        for (size_t d = 0; d < action.depends_count; d++) {
            if (!axon_db_is_installed(cfg, action.depends[d])) {
                fprintf(stderr, "error: hook %s has missing Depends: %s\n", candidates[h].name, action.depends[d]);
                missing_deps = true;
            }
        }

        int hook_rc = 0;
        if (missing_deps) {
            hook_rc = 1;
        } else {
            int cmd_argc = 0;
            char **cmd_argv = parse_exec_command(action.exec, &cmd_argc);
            if (cmd_argv && cmd_argc > 0) {
                printf(":: hook %s: %s\n", candidates[h].name, action.description ? action.description : "");
                fflush(stdout);

                char *stdin_data = NULL;
                size_t stdin_len = 0;
                if (action.needs_targets && matched_count > 0) {
                    size_t total_sz = 0;
                    for (size_t m = 0; m < matched_count; m++) total_sz += strlen(matched_targets[m]) + 1;
                    stdin_data = malloc(total_sz + 1);
                    if (stdin_data) {
                        stdin_data[0] = '\0';
                        for (size_t m = 0; m < matched_count; m++) {
                            strcat(stdin_data, matched_targets[m]);
                            strcat(stdin_data, "\n");
                        }
                        stdin_len = strlen(stdin_data);
                    }
                }

                hook_rc = axon_in_target(root, cmd_argv, stdin_data, stdin_len);
                free(stdin_data);

                for (int c = 0; c < cmd_argc; c++) free(cmd_argv[c]);
                free(cmd_argv);
            }
        }

        if (hook_rc != 0) {
            fprintf(stderr, "error: hook %s failed (%d)\n", candidates[h].name, hook_rc);
            if (strcmp(phase, "PreTransaction") == 0 && action.abort_on_fail) {
                for (size_t m = 0; m < matched_count; m++) free(matched_targets[m]);
                free(matched_targets);
                free(action.description); free(action.when); free(action.exec);
                for (size_t d = 0; d < action.depends_count; d++) free(action.depends[d]);
                free(action.depends);
                for (size_t t = 0; t < trigger_count; t++) {
                    for (size_t tg = 0; tg < triggers[t].target_count; tg++) free(triggers[t].targets[tg]);
                    free(triggers[t].targets);
                }
                for (size_t mm = 0; mm < masked_count; mm++) free(masked_hooks[mm]);
                free(candidates);
                free(records);
                return 1;
            }
            overall_failed = true;
        }

        for (size_t m = 0; m < matched_count; m++) free(matched_targets[m]);
        free(matched_targets);
        free(action.description); free(action.when); free(action.exec);
        for (size_t d = 0; d < action.depends_count; d++) free(action.depends[d]);
        free(action.depends);
        for (size_t t = 0; t < trigger_count; t++) {
            for (size_t tg = 0; tg < triggers[t].target_count; tg++) free(triggers[t].targets[tg]);
            free(triggers[t].targets);
        }
    }

    for (size_t mm = 0; mm < masked_count; mm++) free(masked_hooks[mm]);
    free(candidates);
    free(records);

    return (overall_failed && strcmp(phase, "PostTransaction") == 0) ? 1 : 0;
}
