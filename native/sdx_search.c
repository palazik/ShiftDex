/*
 * ShiftDex - unpack, edit and repack APK and JAR files
 * Copyright (C) 2026 palaziks
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _GNU_SOURCE
#include "sdx_search.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SDX_DEFAULT_MAX_FILE (8u * 1024u * 1024u)
#define SDX_MAX_LINE_TEXT 400
#define SDX_MAX_DEPTH 128

typedef struct {
    const sdx_search_options *opt;
    sdx_match_fn on_match;
    sdx_stop_fn should_stop;
    void *user;
    sdx_search_stats stats;

    char *needle;             /* lower-cased when ignore_case */
    size_t needle_len;
    regex_t regex;
    int has_regex;

    char *buffer;             /* reused file buffer */
    size_t buffer_cap;
    size_t max_file;
    size_t root_len;
    int stop;
} search_ctx;

int sdx_is_binary(const unsigned char *data, size_t length)
{
    size_t probe = length < 4096 ? length : 4096;
    return memchr(data, 0, probe) != NULL;
}

/* Case-insensitive (ASCII) substring search; needle is already lower case. */
static const char *find_nocase(const char *hay, size_t hay_len,
                               const char *needle, size_t needle_len)
{
    if (needle_len == 0 || hay_len < needle_len)
        return NULL;
    const unsigned char first = (unsigned char)needle[0];
    const char *end = hay + (hay_len - needle_len);
    for (const char *p = hay; p <= end; p++) {
        if ((unsigned char)tolower((unsigned char)*p) != first)
            continue;
        size_t i = 1;
        while (i < needle_len &&
               (unsigned char)tolower((unsigned char)p[i]) == (unsigned char)needle[i])
            i++;
        if (i == needle_len)
            return p;
    }
    return NULL;
}

/* Returns the 0-based byte offset of the first match in the line, or -1. */
static long match_line(search_ctx *ctx, char *line, size_t len)
{
    if (ctx->has_regex) {
        /* regexec wants a C string; the caller guarantees line[len] is writable */
        char saved = line[len];
        regmatch_t hit;
        line[len] = '\0';
        int rc = regexec(&ctx->regex, line, 1, &hit, 0);
        line[len] = saved;
        return rc == 0 ? (long)hit.rm_so : -1;
    }
    const char *found = ctx->opt->ignore_case
        ? find_nocase(line, len, ctx->needle, ctx->needle_len)
        : memmem(line, len, ctx->needle, ctx->needle_len);
    return found ? (long)(found - line) : -1;
}

static void report(search_ctx *ctx, const char *rel, long line, long column,
                   const char *text, size_t len)
{
    /* trim leading blanks so results line up, and clip very long lines */
    size_t skip = 0;
    while (skip < len && (text[skip] == ' ' || text[skip] == '\t'))
        skip++;
    text += skip;
    len -= skip;
    if (len > SDX_MAX_LINE_TEXT) {
        len = SDX_MAX_LINE_TEXT;
        /* do not cut a UTF-8 sequence in half */
        while (len > 0 && ((unsigned char)text[len] & 0xC0) == 0x80)
            len--;
    }
    ctx->stats.matches++;
    if (ctx->on_match(rel, line, column, text, len, ctx->user))
        ctx->stop = 1;
    if (ctx->opt->max_results && ctx->stats.matches >= ctx->opt->max_results) {
        ctx->stats.truncated = 1;
        ctx->stop = 1;
    }
}

static void search_file(search_ctx *ctx, const char *path, size_t size)
{
    const char *rel = path + ctx->root_len;

    if (ctx->opt->names_only) {
        ctx->stats.files_scanned++;
        /* work on a copy: match_line may write a terminator */
        size_t len = strlen(rel);
        if (len + 1 > ctx->buffer_cap) {
            char *grown = realloc(ctx->buffer, len + 1);
            if (!grown)
                return;
            ctx->buffer = grown;
            ctx->buffer_cap = len + 1;
        }
        memcpy(ctx->buffer, rel, len + 1);
        if (match_line(ctx, ctx->buffer, len) >= 0)
            report(ctx, rel, 0, 0, rel, len);
        return;
    }

    if (size == 0 || size > ctx->max_file)
        return;
    if (size + 1 > ctx->buffer_cap) {
        char *grown = realloc(ctx->buffer, size + 1);
        if (!grown)
            return;
        ctx->buffer = grown;
        ctx->buffer_cap = size + 1;
    }

    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return;
    size_t total = 0;
    while (total < size) {
        ssize_t got = read(fd, ctx->buffer + total, size - total);
        if (got <= 0)
            break;
        total += (size_t)got;
    }
    close(fd);
    if (total == 0 || sdx_is_binary((const unsigned char *)ctx->buffer, total))
        return;
    ctx->stats.files_scanned++;
    ctx->buffer[total] = '\n'; /* sentinel; one spare byte was allocated */

    long line_no = 1;
    char *line = ctx->buffer;
    char *end = ctx->buffer + total;
    while (line < end && !ctx->stop) {
        char *eol = memchr(line, '\n', (size_t)(end - line));
        if (!eol)
            eol = end;
        size_t len = (size_t)(eol - line);
        if (len > 0 && line[len - 1] == '\r')
            len--;
        if (len >= ctx->needle_len || ctx->has_regex) {
            long at = match_line(ctx, line, len);
            if (at >= 0)
                report(ctx, rel, line_no, at + 1, line, len);
        }
        line = eol + 1;
        line_no++;
    }
}

static int keep_entry(const struct dirent *entry)
{
    const char *name = entry->d_name;
    return !(name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')));
}

static void walk(search_ctx *ctx, const char *dir, int depth)
{
    if (depth > SDX_MAX_DEPTH || ctx->stop)
        return;

    struct dirent **entries = NULL;
    int count = scandir(dir, &entries, keep_entry, alphasort);
    if (count < 0)
        return;

    size_t dir_len = strlen(dir);
    for (int i = 0; i < count; i++) {
        if (!ctx->stop) {
            const char *name = entries[i]->d_name;
            size_t name_len = strlen(name);
            char *path = malloc(dir_len + name_len + 2);
            if (path) {
                memcpy(path, dir, dir_len);
                path[dir_len] = '/';
                memcpy(path + dir_len + 1, name, name_len + 1);

                struct stat st;
                /* lstat: symlinks are never followed out of the project */
                if (lstat(path, &st) == 0) {
                    if (S_ISDIR(st.st_mode)) {
                        int skipped = depth == 0 && ctx->opt->skip_top &&
                                      strcmp(name, ctx->opt->skip_top) == 0;
                        if (!skipped)
                            walk(ctx, path, depth + 1);
                    } else if (S_ISREG(st.st_mode)) {
                        if (ctx->should_stop && ctx->should_stop(ctx->user))
                            ctx->stop = 2;
                        else
                            search_file(ctx, path, (size_t)st.st_size);
                    }
                }
                free(path);
            }
        }
        free(entries[i]);
    }
    free(entries);
}

int sdx_search(const char *root, const sdx_search_options *options,
               sdx_match_fn on_match, sdx_stop_fn should_stop, void *user,
               sdx_search_stats *stats)
{
    if (!root || !options || !options->pattern || !options->pattern[0] || !on_match)
        return SDX_ERR_ARGS;

    search_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.opt = options;
    ctx.on_match = on_match;
    ctx.should_stop = should_stop;
    ctx.user = user;
    ctx.max_file = options->max_file_size ? options->max_file_size : SDX_DEFAULT_MAX_FILE;

    if (options->regex) {
        int flags = REG_EXTENDED | REG_NEWLINE | (options->ignore_case ? REG_ICASE : 0);
        if (regcomp(&ctx.regex, options->pattern, flags) != 0)
            return SDX_ERR_PATTERN;
        ctx.has_regex = 1;
    } else {
        ctx.needle_len = strlen(options->pattern);
        ctx.needle = malloc(ctx.needle_len + 1);
        if (!ctx.needle)
            return SDX_ERR_ARGS;
        for (size_t i = 0; i <= ctx.needle_len; i++) {
            unsigned char c = (unsigned char)options->pattern[i];
            ctx.needle[i] = (char)(options->ignore_case ? tolower(c) : c);
        }
    }

    /* normalise the root so relative paths can be cut off by length */
    size_t root_len = strlen(root);
    while (root_len > 1 && root[root_len - 1] == '/')
        root_len--;
    char *clean_root = strndup(root, root_len);
    int rc = SDX_ERR_ROOT;
    struct stat st;
    if (clean_root && stat(clean_root, &st) == 0 && S_ISDIR(st.st_mode)) {
        ctx.root_len = root_len + 1;
        walk(&ctx, clean_root, 0);
        rc = ctx.stop == 2 ? SDX_STOPPED : SDX_OK;
    }

    free(clean_root);
    free(ctx.buffer);
    free(ctx.needle);
    if (ctx.has_regex)
        regfree(&ctx.regex);
    if (stats)
        *stats = ctx.stats;
    return rc;
}
