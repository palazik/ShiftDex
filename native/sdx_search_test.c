/*
 * ShiftDex - tests for the native search.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Usage: sdx_search_test <scratch-dir>
 */
#define _GNU_SOURCE
#include "sdx_search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                    \
        }                                                                  \
    } while (0)

typedef struct {
    int count;
    char first_path[256];
    long first_line;
    long first_column;
    char first_text[512];
} hits;

static int collect(const char *rel, long line, long column, const char *text, size_t len, void *user)
{
    hits *h = user;
    if (h->count++ == 0) {
        snprintf(h->first_path, sizeof h->first_path, "%s", rel);
        h->first_line = line;
        h->first_column = column;
        snprintf(h->first_text, sizeof h->first_text, "%.*s", (int)len, text);
    }
    return 0;
}

static void put(const char *dir, const char *name, const char *data, size_t len)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(2);
    }
    fwrite(data, 1, len, f);
    fclose(f);
}

static hits run(const char *root, sdx_search_options opt, int *rc, sdx_search_stats *stats)
{
    hits h;
    memset(&h, 0, sizeof h);
    *rc = sdx_search(root, &opt, collect, NULL, &h, stats);
    return h;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <scratch-dir>\n", argv[0]);
        return 2;
    }
    char sub[1024], cache[1024];
    snprintf(sub, sizeof sub, "%s/smali", argv[1]);
    snprintf(cache, sizeof cache, "%s/.cache", argv[1]);
    mkdir(argv[1], 0755);
    mkdir(sub, 0755);
    mkdir(cache, 0755);

    const char *smali =
        ".class public LMain;\n"
        "    const-string v0, \"Hello World\"\r\n"
        "    invoke-static {v0}, LLog;->d(Ljava/lang/String;)V\n"
        "last line without newline hello";
    put(sub, "Main.smali", smali, strlen(smali));
    put(argv[1], "notes.txt", "nothing here\n", 13);
    put(argv[1], "blob.bin", "Hello\0World Hello", 17);
    put(cache, "ignored.txt", "Hello World\n", 12);

    int rc;
    sdx_search_stats stats;
    sdx_search_options opt;

    memset(&opt, 0, sizeof opt);
    opt.pattern = "Hello";
    opt.skip_top = ".cache";
    hits h = run(argv[1], opt, &rc, &stats);
    CHECK(rc == SDX_OK);
    CHECK(h.count == 1); /* binary file and .cache are skipped */
    CHECK(strcmp(h.first_path, "smali/Main.smali") == 0);
    CHECK(h.first_line == 2);
    CHECK(h.first_column == 23);
    CHECK(strcmp(h.first_text, "const-string v0, \"Hello World\"") == 0); /* trimmed, no \r */
    CHECK(stats.files_scanned == 2);

    opt.ignore_case = 1;
    h = run(argv[1], opt, &rc, &stats);
    CHECK(h.count == 2); /* also the last line, which has no newline */

    opt.skip_top = NULL;
    h = run(argv[1], opt, &rc, &stats);
    CHECK(h.count == 3);

    memset(&opt, 0, sizeof opt);
    opt.pattern = "invoke-(static|virtual) \\{v[0-9]+\\}";
    opt.regex = 1;
    h = run(argv[1], opt, &rc, &stats);
    CHECK(rc == SDX_OK && h.count == 1 && h.first_line == 3 && h.first_column == 5);

    opt.pattern = "(unclosed";
    h = run(argv[1], opt, &rc, &stats);
    CHECK(rc == SDX_ERR_PATTERN);

    memset(&opt, 0, sizeof opt);
    opt.pattern = "main";
    opt.names_only = 1;
    opt.ignore_case = 1;
    h = run(argv[1], opt, &rc, &stats);
    CHECK(h.count == 1 && h.first_line == 0);
    CHECK(strcmp(h.first_path, "smali/Main.smali") == 0);

    memset(&opt, 0, sizeof opt);
    opt.pattern = "l";
    opt.max_results = 2;
    h = run(argv[1], opt, &rc, &stats);
    CHECK(h.count == 2 && stats.truncated == 1);

    opt.pattern = "";
    h = run(argv[1], opt, &rc, &stats);
    CHECK(rc == SDX_ERR_ARGS);

    opt.pattern = "x";
    h = run("/nonexistent/shiftdex", opt, &rc, &stats);
    CHECK(rc == SDX_ERR_ROOT);

    CHECK(sdx_is_binary((const unsigned char *)"a\0b", 3) == 1);
    CHECK(sdx_is_binary((const unsigned char *)"plain", 5) == 0);

    if (failures == 0)
        puts("native search: all checks passed");
    return failures ? 1 : 0;
}
