// Reproducer for ForwardIterator missed-row bug.
//
// Public contract (rocksdb/options.h on `ReadOptions::tailing`):
//   "It will return records that were inserted into the database after
//    the creation of the iterator."
//
// Actual behavior: when the predecessor of a late insert lives in an
// immutable source (L0 SST or immutable memtable), ForwardIterator::Next()
// does not re-seek `mutable_iter_`, so a key inserted into the mutable
// memtable behind the cached cursor is silently skipped.
//
// The bug requires (a) a flush before the late insert, and (b) the iterator
// to be advancing through an immutable source at the time of the insert.
// Five variants are exercised; the headline failure is "[tail, B late, flushed]".
//
// Build (from rocksdb root):
//     make static_lib && make -C examples c_tailing_iterator_missed_row_example
// Run:
//     examples/c_tailing_iterator_missed_row_example
// Expected on a buggy build:
//     [tail, B late,  flushed]   ... A C   (B MISSED)
//     [tail, B late,  flushed, primed] ... A C D   (B MISSED)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rocksdb/c.h"

static const char *DB_PATH = "/tmp/rocksdb_tailing_iterator_missed_row_db";

static void die(const char *what, char *err) {
    fprintf(stderr, "%s: %s\n", what, err ? err : "(null)");
    exit(1);
}

static void rmrf(const char *path) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    (void)system(cmd);
}

typedef struct {
    rocksdb_t *db;
    rocksdb_options_t *opts;
    rocksdb_writeoptions_t *wopts;
    rocksdb_readoptions_t *ropts;
    rocksdb_flushoptions_t *fopts;
} Env;

static void env_open(Env *e) {
    rmrf(DB_PATH);
    e->opts = rocksdb_options_create();
    rocksdb_options_set_create_if_missing(e->opts, 1);
    // Keep flushed L0 files in L0 for the duration of the test.
    rocksdb_options_set_disable_auto_compactions(e->opts, 1);

    char *err = NULL;
    e->db = rocksdb_open(e->opts, DB_PATH, &err);
    if (err) die("open", err);

    e->wopts = rocksdb_writeoptions_create();
    rocksdb_writeoptions_disable_WAL(e->wopts, 1);
    e->ropts = rocksdb_readoptions_create();
    rocksdb_readoptions_set_tailing(e->ropts, 1);
    e->fopts = rocksdb_flushoptions_create();
    rocksdb_flushoptions_set_wait(e->fopts, 1);
}

static void env_close(Env *e) {
    rocksdb_close(e->db);
    rocksdb_options_destroy(e->opts);
    rocksdb_writeoptions_destroy(e->wopts);
    rocksdb_readoptions_destroy(e->ropts);
    rocksdb_flushoptions_destroy(e->fopts);
    rmrf(DB_PATH);
}

static void put_str(Env *e, const char *k, const char *v) {
    char *err = NULL;
    rocksdb_put(e->db, e->wopts, k, strlen(k), v, strlen(v), &err);
    if (err) die("put", err);
}

static void flush_db(Env *e) {
    char *err = NULL;
    rocksdb_flush(e->db, e->fopts, &err);
    if (err) die("flush", err);
}

// FLAG_LATE    : insert B *after* SeekToFirst has yielded A, before Next.
// FLAG_NOFLUSH : do NOT flush after seeding A,C.
// FLAG_NOTAIL  : open a regular (non-tailing) snapshot iterator.
// FLAG_PRIME   : insert D > C into the memtable BEFORE opening the iterator,
//                making mutable_iter_ Valid at creation with its cached cursor
//                past the eventual late insert B.
#define FLAG_LATE     1
#define FLAG_NOFLUSH  2
#define FLAG_NOTAIL   4
#define FLAG_PRIME    8

static int run_one(int flags, char observed[][8], int max_observed) {
    Env e;
    env_open(&e);

    put_str(&e, "A", "vA");
    put_str(&e, "C", "vC");
    if (!(flags & FLAG_NOFLUSH)) flush_db(&e);

    if (flags & FLAG_PRIME) put_str(&e, "D", "vD");
    if (!(flags & FLAG_LATE)) put_str(&e, "B", "vB");

    rocksdb_readoptions_t *ropts =
        (flags & FLAG_NOTAIL) ? rocksdb_readoptions_create() : e.ropts;
    rocksdb_iterator_t *it = rocksdb_create_iterator(e.db, ropts);
    rocksdb_iter_seek_to_first(it);

    int count = 0;
    while (rocksdb_iter_valid(it)) {
        size_t klen = 0;
        const char *k = rocksdb_iter_key(it, &klen);
        if (count < max_observed) {
            int n = (int)klen < 7 ? (int)klen : 7;
            memcpy(observed[count], k, n);
            observed[count][n] = '\0';
        }
        count++;

        if ((flags & FLAG_LATE) && count == 1) put_str(&e, "B", "vB");
        rocksdb_iter_next(it);
    }

    char *err = NULL;
    rocksdb_iter_get_error(it, &err);
    if (err) die("iter error", err);
    rocksdb_iter_destroy(it);
    if (flags & FLAG_NOTAIL) rocksdb_readoptions_destroy(ropts);
    env_close(&e);
    return count;
}

static void show(const char *label, int flags) {
    char observed[8][8];
    int n = run_one(flags, observed, 8);
    printf("%-36s observed %d:", label, n);
    for (int i = 0; i < n; i++) printf(" %s", observed[i]);
    int saw_b = 0;
    for (int i = 0; i < n; i++) if (strcmp(observed[i], "B") == 0) saw_b = 1;
    printf("    %s\n", saw_b ? "(B visible)" : "(B MISSED)");
}

int main(void) {
    show("[tail, B early, flushed]",         0);
    show("[tail, B late,  flushed]",         FLAG_LATE);
    show("[tail, B late,  no flush]",        FLAG_LATE | FLAG_NOFLUSH);
    show("[snap, B late,  flushed]",         FLAG_LATE | FLAG_NOTAIL);
    show("[tail, B late,  flushed, primed]", FLAG_LATE | FLAG_PRIME);
    return 0;
}
