/**
 * timeline_sync.c — Sync timeline table from pm_logs to engine DB
 *
 * Copies new timeline rows from main ~/.hermes/pm_logs/timeline.db
 * into engine/timeline.db so the engine's unified timeline table
 * has real data instead of being empty.
 *
 * Build: gcc -O2 -Wall timeline_sync.c -o timeline_sync -lsqlite3 -lm
 * Run:   ./timeline_sync              # incremental copy
 *        ./timeline_sync full         # full reset + copy
 * Cron:  every 5 min crontab entry
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h>

#define ENGINE_DB  "./timeline.db"
#define MAIN_DB    "/home/wubu2/.hermes/pm_logs/timeline.db"
#define HB_PATH    "/home/wubu2/.hermes/infra/heartbeats/timeline-sync.heartbeat"

static int heartbeat(void) {
    FILE *f = fopen(HB_PATH, "w");
    if (!f) return -1;
    fprintf(f, "%ld\n", time(NULL));
    fclose(f);
    return 0;
}

static int copy_rows(sqlite3 *src, sqlite3 *dst, long long min_ts, int full) {
    const char *sql;
    if (full) {
        sql = "SELECT ts, source, category, data, collected_at FROM timeline ORDER BY ts";
    } else {
        sql = "SELECT ts, source, category, data, collected_at FROM timeline WHERE ts > ?1 ORDER BY ts";
    }

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(src, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "ERROR: prepare src: %s\n", sqlite3_errmsg(src));
        return -1;
    }

    if (!full) {
        sqlite3_bind_int64(stmt, 1, min_ts);
    }

    // Begin transaction on dst
    sqlite3_exec(dst, "BEGIN TRANSACTION", NULL, NULL, NULL);

    // Prepare insert on dst
    sqlite3_stmt *ins = NULL;
    const char *ins_sql = "INSERT OR IGNORE INTO timeline (ts, source, category, data, collected_at) VALUES (?1, ?2, ?3, ?4, ?5)";
    if (sqlite3_prepare_v2(dst, ins_sql, -1, &ins, NULL) != SQLITE_OK) {
        fprintf(stderr, "ERROR: prepare dst: %s\n", sqlite3_errmsg(dst));
        sqlite3_finalize(stmt);
        return -1;
    }

    int count = 0;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        long long ts = sqlite3_column_int64(stmt, 0);
        const char *source = (const char *)sqlite3_column_text(stmt, 1);
        const char *cat = (const char *)sqlite3_column_text(stmt, 2);
        const char *data = (const char *)sqlite3_column_text(stmt, 3);
        long long collected = sqlite3_column_int64(stmt, 4);

        sqlite3_bind_int64(ins, 1, ts);
        sqlite3_bind_text(ins, 2, source, -1, SQLITE_STATIC);
        sqlite3_bind_text(ins, 3, cat ? cat : "unknown", -1, SQLITE_STATIC);
        sqlite3_bind_text(ins, 4, data, -1, SQLITE_STATIC);
        sqlite3_bind_int64(ins, 5, collected);

        int irc = sqlite3_step(ins);
        if (irc == SQLITE_DONE) count++;
        sqlite3_reset(ins);
    }

    sqlite3_exec(dst, "COMMIT", NULL, NULL, NULL);

    sqlite3_finalize(ins);
    sqlite3_finalize(stmt);

    fprintf(stderr, "INFO: copied %d timeline rows\n", count);
    return count;
}

int main(int argc, char **argv) {
    int full = (argc > 1 && strcmp(argv[1], "full") == 0);

    sqlite3 *src = NULL;
    sqlite3 *dst = NULL;

    if (sqlite3_open(MAIN_DB, &src) != SQLITE_OK) {
        fprintf(stderr, "ERROR: cannot open main DB: %s\n", sqlite3_errmsg(src));
        return 1;
    }
    if (sqlite3_open(ENGINE_DB, &dst) != SQLITE_OK) {
        fprintf(stderr, "ERROR: cannot open engine DB: %s\n", sqlite3_errmsg(dst));
        sqlite3_close(src);
        return 1;
    }

    // Get max ts currently in engine timeline
    long long max_ts = 0;
    if (!full) {
        sqlite3_stmt *m = NULL;
        if (sqlite3_prepare_v2(dst, "SELECT COALESCE(MAX(ts), 0) FROM timeline", -1, &m, NULL) == SQLITE_OK) {
            if (sqlite3_step(m) == SQLITE_ROW) {
                max_ts = sqlite3_column_int64(m, 0);
            }
            sqlite3_finalize(m);
        }
    }

    if (full) {
        // Clear engine timeline table
        sqlite3_exec(dst, "DELETE FROM timeline", NULL, NULL, NULL);
        fprintf(stderr, "INFO: full sync — cleared engine timeline\n");
    }

    fprintf(stderr, "INFO: syncing from ts=%lld (full=%d)\n", max_ts, full);

    int ret = copy_rows(src, dst, max_ts, full);

    if (ret >= 0) {
        fprintf(stderr, "INFO: sync complete — %d rows\n", ret);
        heartbeat();
    }

    sqlite3_close(dst);
    sqlite3_close(src);
    return ret >= 0 ? 0 : 1;
}
