/**
 * data_quality_scorer.c — D35: Data quality scoring per source
 *
 * Scores each data source 0.0-1.0 based on recency, completeness, consistency.
 * Reads timeline.db to check freshness, writes JSON quality report.
 *
 * Compile: gcc -O2 -o data_quality_scorer data_quality_scorer.c -lsqlite3 -ljansson -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include <jansson.h>
#include <math.h>

#define DB_PATH     "/home/wubu2/.hermes/pm_logs/timeline.db"
#define REPORT_PATH "/home/wubu2/money-room/docs/data/data_quality.json"

typedef struct {
    char   name[64];
    float  score;
    int    age_seconds;
    int    row_count;
    int    null_count;
    int    range_errors;
    char   status[8];
} SourceScore;

static void query_source(sqlite3 *db, const char *source,
                        int *row_count, int *null_count, int *range_errors,
                        int *age_seconds) {
    char sql[512];
    sqlite3_stmt *stmt;

    *row_count = 0;
    *null_count = 0;
    *range_errors = 0;
    *age_seconds = 999999;

    snprintf(sql, sizeof(sql),
             "SELECT COUNT(*), MAX(timestamp), SUM(CASE WHEN close IS NULL OR close <= 0 THEN 1 ELSE 0 END) "
             "FROM market_data WHERE source='%s'", source);

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            *row_count = sqlite3_column_int(stmt, 0);
            int64_t latest_ts = sqlite3_column_int64(stmt, 1);
            *null_count = sqlite3_column_int(stmt, 2);
            if (latest_ts > 0) {
                *age_seconds = (int)(time(NULL) - latest_ts);
            }
        }
        sqlite3_finalize(stmt);
    }

    snprintf(sql, sizeof(sql),
             "SELECT COUNT(*) FROM market_data WHERE source='%s' AND (close <= 0 OR close > 1000000)", source);

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            *range_errors = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
}

static void score_source(SourceScore *ss, int age_s, int rows, int nulls, int range_err) {
    float recency;
    if (age_s < 300) recency = 1.0f;
    else if (age_s < 3600) recency = 0.5f;
    else if (age_s < 14400) recency = 0.2f;
    else recency = 0.0f;

    float completeness = rows > 0 ? (float)(rows - nulls) / rows : 0.0f;
    if (completeness > 1.0f) completeness = 1.0f;
    if (completeness < 0.0f) completeness = 0.0f;

    float consistency = rows > 0
        ? 1.0f - fminf((float)range_err / rows, 1.0f)
        : 0.0f;

    ss->score = recency * 0.4f + completeness * 0.3f + consistency * 0.3f;

    if (ss->score >= 0.7f) strncpy(ss->status, "OK", sizeof(ss->status));
    else if (ss->score >= 0.4f) strncpy(ss->status, "WARN", sizeof(ss->status));
    else strncpy(ss->status, "STALE", sizeof(ss->status));
}

int main(void) {
    sqlite3 *db;
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        fprintf(stderr, "D35: Cannot open DB: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    const char *sources[] = {
        "yahoo_btc", "yahoo_eth", "yahoo_sp500", "yahoo_vix",
        "coingecko", "kraken", "coinbase", "binance",
        "forex", "fred", "cboe", "fear_greed",
        "orderbook", "cvd", "funding",
        NULL
    };

    SourceScore scores[32];
    int nsrc = 0;
    int total_issues = 0;

    for (int i = 0; sources[i] && nsrc < 32; i++) {
        SourceScore *ss = &scores[nsrc];
        strncpy(ss->name, sources[i], 63);
        ss->name[63] = '\0';

        int rows = 0, nulls = 0, range_err = 0, age_s = 999999;
        query_source(db, sources[i], &rows, &nulls, &range_err, &age_s);
        ss->row_count = rows;
        ss->null_count = nulls;
        ss->range_errors = range_err;
        ss->age_seconds = age_s;

        score_source(ss, age_s, rows, nulls, range_err);
        if (ss->score < 0.4f) total_issues++;
        nsrc++;
    }

    json_t *root = json_object();
    json_t *sources_arr = json_array();
    json_object_set_new(root, "timestamp", json_integer(time(NULL)));
    json_object_set_new(root, "sources_stale", json_integer(total_issues));

    for (int i = 0; i < nsrc; i++) {
        json_t *s = json_object();
        json_object_set_new(s, "name", json_string(scores[i].name));
        json_object_set_new(s, "score", json_real(scores[i].score));
        json_object_set_new(s, "age_s", json_integer(scores[i].age_seconds));
        json_object_set_new(s, "rows", json_integer(scores[i].row_count));
        json_object_set_new(s, "nulls", json_integer(scores[i].null_count));
        json_object_set_new(s, "range_err", json_integer(scores[i].range_errors));
        json_object_set_new(s, "status", json_string(scores[i].status));
        json_array_append_new(sources_arr, s);
    }
    json_object_set_new(root, "sources", sources_arr);

    char *json_str = json_dumps(root, JSON_COMPACT);
    if (json_str) {
        FILE *f = fopen(REPORT_PATH, "w");
        if (f) {
            fprintf(f, "%s\n", json_str);
            fclose(f);
            printf("D35: Quality report written (%d sources, %d stale)\n", nsrc, total_issues);
        } else {
            fprintf(stderr, "D35: Cannot write %s\n", REPORT_PATH);
        }
        free(json_str);
    }

    json_decref(root);
    sqlite3_close(db);

    return total_issues > 0 ? 1 : 0;
}
