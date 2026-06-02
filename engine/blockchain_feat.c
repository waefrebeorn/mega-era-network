/**
 * blockchain_feat.c — Extract on-chain features from timeline.db for engine
 * Reads blockchain_data table, normalizes metrics, writes to JSON cache.
 *
 * T088: bridge blockchain.com data → engine-readable JSON cache.
 * Output: /home/wubu2/.hermes/options_cache/blockchain_features.json
 *
 * Compile: gcc -O3 -Wall -Wextra -o blockchain_feat blockchain_feat.c -lsqlite3 -lm -ljansson
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <math.h>
#include <jansson.h>
#include <time.h>

#define DB_PATH "/home/wubu2/money-room/engine/timeline.db"
#define CACHE_PATH "/home/wubu2/.hermes/options_cache/blockchain_features.json"

// Table: blockchain_data(chart_id TEXT, obs_date TEXT, value REAL, name TEXT, updated_at TEXT)

int main(void) {
    sqlite3 *db;
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        fprintf(stderr, "[BLOCKCHAIN_FEAT] Cannot open DB: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    // ── Get unique chart types ──
    sqlite3_stmt *s;
    const char *sql = "SELECT DISTINCT chart_id FROM blockchain_data ORDER BY chart_id";
    if (sqlite3_prepare_v2(db, sql, -1, &s, NULL) != SQLITE_OK) {
        fprintf(stderr, "[BLOCKCHAIN_FEAT] Query failed: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    json_t *out = json_object();
    int count = 0;

    while (sqlite3_step(s) == SQLITE_ROW) {
        const char *chart_id = (const char *)sqlite3_column_text(s, 0);
        if (!chart_id) continue;

        // Get latest value for this chart
        sqlite3_stmt *s2;
        const char *v_sql = "SELECT value FROM blockchain_data WHERE chart_id = ? "
                            "ORDER BY obs_date DESC LIMIT 1";
        if (sqlite3_prepare_v2(db, v_sql, -1, &s2, NULL) != SQLITE_OK) {
            continue;
        }
        sqlite3_bind_text(s2, 1, chart_id, -1, SQLITE_STATIC);

        double latest_val = 0;
        int found = 0;
        if (sqlite3_step(s2) == SQLITE_ROW) {
            latest_val = sqlite3_column_double(s2, 0);
            found = 1;
        }
        sqlite3_finalize(s2);
        if (!found) continue;

        // Get 7-day min/max for adaptive normalization
        sqlite3_stmt *s3;
        const char *r_sql = "SELECT MIN(value), MAX(value) FROM blockchain_data "
                            "WHERE chart_id = ? AND obs_date >= date('now', '-7 days')";
        double min_val = 0, max_val = 0;
        if (sqlite3_prepare_v2(db, r_sql, -1, &s3, NULL) == SQLITE_OK) {
            sqlite3_bind_text(s3, 1, chart_id, -1, SQLITE_STATIC);
            if (sqlite3_step(s3) == SQLITE_ROW) {
                min_val = sqlite3_column_double(s3, 0);
                max_val = sqlite3_column_double(s3, 1);
            }
            sqlite3_finalize(s3);
        }

        // Normalize to [0,1]
        float norm = 0.5f;
        if (max_val > min_val && max_val > 0) {
            double clamped = fmax(min_val, fmin(latest_val, max_val));
            norm = (float)((clamped - min_val) / (max_val - min_val));
        }

        // Normalize chart_id: replace hyphens with underscores for valid JSON keys
        char key[128];
        for (int i = 0; chart_id[i]; i++) {
            key[i] = (chart_id[i] == '-') ? '_' : chart_id[i];
            key[i+1] = '\0';
        }

        json_object_set_new(out, key, json_real(norm));
        count++;
    }
    sqlite3_finalize(s);

    // Add timestamp metadata
    json_object_set_new(out, "_count", json_integer(count));
    json_object_set_new(out, "_timestamp", json_integer((json_int_t)time(NULL)));

    // ── Write cache ──
    // Ensure directory exists
    char *slash = strrchr(CACHE_PATH, '/');
    if (slash) {
        char dir[256];
        size_t len = slash - CACHE_PATH;
        strncpy(dir, CACHE_PATH, len);
        dir[len] = '\0';
        char mkcmd[512];
        snprintf(mkcmd, sizeof(mkcmd), "mkdir -p %s", dir);
        int r = system(mkcmd);
        (void)r;
    }

    FILE *f = fopen(CACHE_PATH, "w");
    if (!f) {
        fprintf(stderr, "[BLOCKCHAIN_FEAT] Cannot write %s\n", CACHE_PATH);
        json_decref(out);
        sqlite3_close(db);
        return 1;
    }
    json_dumpf(out, f, JSON_INDENT(2) | JSON_SORT_KEYS);
    fclose(f);

    fprintf(stderr, "[BLOCKCHAIN_FEAT] Wrote %d on-chain metrics to %s\n", count, CACHE_PATH);
    json_decref(out);
    sqlite3_close(db);
    return 0;
}
