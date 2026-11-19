/*
 * ShiftDex - unpack, edit and repack APK and JAR files
 * Copyright (C) 2026 palaziks
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Native search over an unpacked project. A decoded APK easily holds tens
 * of thousands of smali files, so this is written in plain C and kept free
 * of allocations in the per-line path.
 */
#ifndef SDX_SEARCH_H
#define SDX_SEARCH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sdx_search_options {
    const char *pattern;      /* text or POSIX extended regex, UTF-8 */
    int ignore_case;
    int regex;
    int names_only;           /* match file paths instead of contents */
    size_t max_results;       /* 0 = unlimited */
    size_t max_file_size;     /* larger files are skipped; 0 = 8 MiB */
    const char *skip_top;     /* top-level folder to ignore, may be NULL */
} sdx_search_options;

typedef struct sdx_search_stats {
    size_t files_scanned;
    size_t matches;
    int truncated;            /* stopped at max_results */
} sdx_search_stats;

/*
 * Called once per hit. rel_path is relative to the search root. line and
 * column are 1-based; both are 0 for a file name match. text is the
 * matching line (not NUL terminated, clipped to a sane length).
 * Return non-zero to stop the search.
 */
typedef int (*sdx_match_fn)(const char *rel_path, long line, long column,
                            const char *text, size_t text_len, void *user);

/* Polled between files. Return non-zero to cancel. May be NULL. */
typedef int (*sdx_stop_fn)(void *user);

enum {
    SDX_OK = 0,
    SDX_ERR_ARGS = -1,
    SDX_ERR_PATTERN = -2,     /* the regular expression did not compile */
    SDX_ERR_ROOT = -3,        /* the root folder could not be read */
    SDX_STOPPED = 1           /* cancelled by a callback */
};

int sdx_search(const char *root, const sdx_search_options *options,
               sdx_match_fn on_match, sdx_stop_fn should_stop, void *user,
               sdx_search_stats *stats);

/* Heuristic used by the search and by the editor: NUL bytes mean binary. */
int sdx_is_binary(const unsigned char *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* SDX_SEARCH_H */
