/**
 * manifold_collector.c — Manifold Markets Prediction Data Collector
 * Public API (no auth for market data). Writes to timeline.db with source='manifold_<slug>'.
 * Build: gcc -O3 -o manifold_collector manifold_collector.c -lcurl -ljansson -lsqlite3 -lm
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include <curl/curl.h>
#include <jansson.h>

#define MF_API "https://api.manifold.markets/v0/markets?limit=1000"
#define DB_PATH "/home/wubu2/.hermes/pm_logs/timeline.db"

typedef struct { char *data; size_t len; } http_buf_t;
static size_t write_cb(void *ptr, size_t sz, size_t nm, void *ud) {
    size_t total = sz * nm; http_buf_t *b = (http_buf_t*)ud;
    char *np = realloc(b->data, b->len + total + 1);
    if (!np) return 0; b->data = np;
    memcpy(b->data + b->len, ptr, total);
    b->len += total; b->data[b->len] = '\0';
    return total;
}

static char *http_get(const char *url) {
    CURL *c = curl_easy_init(); if (!c) return NULL;
    http_buf_t buf = {NULL, 0};
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "manifold-collector/1.0");
    CURLcode res = curl_easy_perform(c);
    curl_easy_cleanup(c);
    if (res != CURLE_OK) { free(buf.data); return NULL; }
    return buf.data;
}

static sqlite3 *g_db = NULL;
static void db_init(void) {
    sqlite3_open(DB_PATH, &g_db);
    char *err = NULL;
    sqlite3_exec(g_db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=OFF;",0,0,&err);
}
static void db_insert(const char *source, long long ts, const char *category, const char *data_json) {
    if (!g_db) return;
    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR REPLACE INTO timeline (ts, source, category, data, collected_at) VALUES (?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return;
    sqlite3_bind_int64(stmt, 1, ts);
    sqlite3_bind_text(stmt, 2, source, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, category, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, data_json, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, (long long)time(NULL));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}
static void db_close(void) { if (g_db) sqlite3_close(g_db); }

static long long parse_mf_ts(const char *iso) {
    if (!iso || !*iso) return 0;
    struct tm tm = {0};
    // Manifold uses ISO 8601: 2024-01-15T18:30:00.000Z
    if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) >= 6) {
        tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_isdst = 0;
        return (long long)mktime(&tm);
    }
    return 0;
}

static char *sanitize_slug(const char *slug) {
    static char buf[128];
    int j = 0;
    for (int i = 0; slug[i] && j < 120; i++) {
        char c = slug[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') buf[j++] = c;
        else if (c == ' ') buf[j++] = '_';
    }
    buf[j] = '\0';
    return buf;
}

int main(void) {
    printf("\n  MANIFOLD COLLECTOR — Public Market Data\n  timeline.db\n\n");
    curl_global_init(CURL_GLOBAL_DEFAULT);
    db_init();

    char *resp = http_get(MF_API);
    if (!resp) { printf("  FAILED to fetch\n"); db_close(); curl_global_cleanup(); return 1; }

    json_error_t err;
    json_t *markets = json_loads(resp, 0, &err);
    free(resp);
    if (!markets) { printf("  JSON error: %s\n", err.text); db_close(); curl_global_cleanup(); return 1; }
    if (!json_is_array(markets)) { json_decref(markets); db_close(); curl_global_cleanup(); return 1; }

    int n = (int)json_array_size(markets);
    int total = 0;
    for (int i = 0; i < n; i++) {
        json_t *m = json_array_get(markets, i);
        const char *slug = json_string_value(json_object_get(m, "slug"));
        const char *question = json_string_value(json_object_get(m, "question"));
        const char *prob = json_string_value(json_object_get(m, "probability"));
        const char *volume = json_string_value(json_object_get(m, "volume"));
        const char *close = json_string_value(json_object_get(m, "closeTime"));
        const char *status = json_string_value(json_object_get(m, "isResolved")) ? "resolved" : "open";
        const char *category = json_string_value(json_object_get(m, "topic"));

        if (!slug) continue;

        long long ts = parse_mf_ts(close);
        if (ts == 0) ts = time(NULL);

        char source[128];
        snprintf(source, sizeof(source), "manifold_%s", sanitize_slug(slug));

        const char *cat_name = category ? category : "general";
        char cat[64];
        snprintf(cat, sizeof(cat), "manifold_%s", cat_name);

        char data_json[1024];
        double p = prob ? atof(prob) : 0;
        double vol = volume ? atof(volume) : 0;
        snprintf(data_json, sizeof(data_json),
            "{\"slug\":\"%s\",\"question\":\"%s\",\"status\":\"%s\",\"probability\":%.4f,\"volume\":%.0f}",
            slug, question ? question : "", status, p, vol);

        db_insert(source, ts, cat, data_json);
        total++;
    }

    json_decref(markets);
    db_close();
    curl_global_cleanup();
    printf("  Done: %d markets collected\n\n", total);
    return 0;
}
