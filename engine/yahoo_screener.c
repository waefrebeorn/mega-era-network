/**
 * yahoo_screener.c — T240: Yahoo Finance stock screener collector
 *
 * Fetches predefined screeners (most active, gainers, losers) from Yahoo Finance.
 * No auth needed. Free API.
 *
 * Usage: ./yahoo_screener              # all screeners
 *        ./yahoo_screener gainers       # day gainers only
 *        ./yahoo_screener losers        # day losers only
 *        ./yahoo_screener active        # most active only
 *
 * Build: gcc -O2 yahoo_screener.c -o yahoo_screener -lcurl -ljansson -lsqlite3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include <jansson.h>
#include <sqlite3.h>
#include <sys/stat.h>

#define DB_PATH  "/home/wubu2/.hermes/pm_logs/timeline.db"
#define HB_DIR   "/home/wubu2/.hermes/infra/heartbeats"
#define HB_PATH  HB_DIR "/yahoo-screener.heartbeat"
#define API_BASE "https://query1.finance.yahoo.com/v1/finance/screener/predefined/saved"
#define UA_STR   "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"

struct MemBuf { char *data; size_t size; };

static size_t write_cb(void *p, size_t s, size_t n, void *u) {
    size_t t = s * n; struct MemBuf *m = u;
    char *np = realloc(m->data, m->size + t + 1);
    if (!np) return 0; m->data = np;
    memcpy(m->data + m->size, p, t); m->size += t; m->data[m->size] = 0;
    return t;
}

static char *http_get(const char *url) {
    CURL *c = curl_easy_init(); if (!c) return NULL;
    struct MemBuf mb = {0};
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &mb);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, UA_STR);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode r = curl_easy_perform(c); curl_easy_cleanup(c);
    if (r != CURLE_OK) { free(mb.data); return NULL; }
    return mb.data;
}

static double get_dbl(json_t *obj, const char *key) {
    json_t *v = json_object_get(obj, key);
    if (!v) return 0;
    if (json_is_real(v)) return json_real_value(v);
    if (json_is_integer(v)) return (double)json_integer_value(v);
    if (json_is_object(v)) {
        json_t *raw = json_object_get(v, "raw");
        if (raw && json_is_real(raw)) return json_real_value(raw);
        if (raw && json_is_integer(raw)) return (double)json_integer_value(raw);
    }
    return 0;
}

static long long get_int(json_t *obj, const char *key) {
    json_t *v = json_object_get(obj, key);
    if (!v) return 0;
    if (json_is_integer(v)) return json_integer_value(v);
    if (json_is_object(v)) {
        json_t *raw = json_object_get(v, "raw");
        if (raw && json_is_integer(raw)) return json_integer_value(raw);
    }
    return 0;
}

static const char *get_str(json_t *obj, const char *key) {
    json_t *v = json_object_get(obj, key);
    if (v && json_is_string(v)) return json_string_value(v);
    return "";
}

static void fetch_screener(sqlite3 *db, const char *scr_id, const char *label, long long now) {
    char url[512];
    snprintf(url, sizeof(url), "%s?scrIds=%s&count=25", API_BASE, scr_id);
    
    char *raw = http_get(url);
    if (!raw) {
        printf("[SCREENER] %s: request failed\n", label);
        return;
    }
    
    json_error_t err;
    json_t *root = json_loads(raw, 0, &err);
    free(raw);
    if (!root) {
        printf("[SCREENER] %s: JSON error: %s\n", label, err.text);
        return;
    }
    
    json_t *result = json_object_get(root, "finance");
    if (!result) result = root;
    json_t *results_arr = json_object_get(result, "result");
    if (!results_arr || !json_is_array(results_arr) || json_array_size(results_arr) == 0) {
        printf("[SCREENER] %s: no results\n", label);
        json_decref(root);
        return;
    }
    
    json_t *quotes = json_object_get(json_array_get(results_arr, 0), "quotes");
    if (!quotes || !json_is_array(quotes)) {
        printf("[SCREENER] %s: no quotes\n", label);
        json_decref(root);
        return;
    }
    
    size_t n = json_array_size(quotes);
    int inserted = 0;
    
    sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO stock_screener "
        "(screener_type, symbol, price, change_pct, volume, ts) "
        "VALUES (?,?,?,?,?,?)",
        -1, &stmt, NULL);
    
    for (size_t i = 0; i < n; i++) {
        json_t *q = json_array_get(quotes, i);
        if (!q) continue;
        
        const char *sym = get_str(q, "symbol");
        if (strlen(sym) == 0) continue;
        
        double price = get_dbl(q, "regularMarketPrice");
        double chg = get_dbl(q, "regularMarketChangePercent");
        long long vol = get_int(q, "regularMarketVolume");
        
        sqlite3_bind_text(stmt, 1, label, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, sym, -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 3, price);
        sqlite3_bind_double(stmt, 4, chg);
        sqlite3_bind_int64(stmt, 5, vol);
        sqlite3_bind_int64(stmt, 6, now);
        
        if (sqlite3_step(stmt) == SQLITE_DONE) inserted++;
        sqlite3_reset(stmt);
    }
    
    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    
    /* Print top 5 */
    printf("[SCREENER] %s (%s): %d symbols\n", label, scr_id, inserted);
    for (size_t i = 0; i < (n > 5 ? 5 : n); i++) {
        json_t *q = json_array_get(quotes, i);
        const char *sym = get_str(q, "symbol");
        double price = get_dbl(q, "regularMarketPrice");
        double chg = get_dbl(q, "regularMarketChangePercent");
        printf("  %s: $%.2f (%+.2f%%)\n", sym, price, chg);
    }
    
    json_decref(root);
}

static void write_hb(void) {
    mkdir(HB_DIR, 0755);
    FILE *hf = fopen(HB_PATH, "w");
    if (hf) { fprintf(hf, "%ld\n", (long)time(NULL)); fclose(hf); }
}

int main(int argc, char **argv) {
    const char *filter = NULL;
    if (argc > 1) filter = argv[1];
    
    printf("[SCREENER] Yahoo Finance stock screener\n");
    write_hb();
    
    sqlite3 *db;
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        fprintf(stderr, "[SCREENER] DB: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS stock_screener ("
        "  screener_type TEXT NOT NULL,"
        "  symbol TEXT NOT NULL,"
        "  price REAL, change_pct REAL, volume INTEGER,"
        "  ts INTEGER NOT NULL"
        ");", NULL, NULL, NULL);
    sqlite3_exec(db,
        "CREATE INDEX IF NOT EXISTS idx_scr_ts ON stock_screener(ts);", NULL, NULL, NULL);
    
    long long now = (long long)time(NULL);
    
    struct { const char *id; const char *label; } screeners[] = {
        {"most_actives", "active"},
        {"day_gainers", "gainers"},
        {"day_losers", "losers"},
        {NULL, NULL}
    };
    
    for (int i = 0; screeners[i].id; i++) {
        if (filter && strcmp(filter, screeners[i].label) != 0) continue;
        fetch_screener(db, screeners[i].id, screeners[i].label, now);
    }
    
    sqlite3_close(db);
    printf("[SCREENER] Done\n");
    return 0;
}
