/**
 * predictit_collector.c — PredictIt Prediction Market Data Collector
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include <curl/curl.h>
#include <jansson.h>

#define PI_API "https://www.predictit.org/api/marketdata/all/"
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
    curl_easy_setopt(c, CURLOPT_USERAGENT, "predictit-collector/1.0");
    CURLcode res = curl_easy_perform(c);
    curl_easy_cleanup(c);
    if (res != CURLE_OK) { free(buf.data); return NULL; }
    return buf.data;
}

static const char *json_get_str(json_t *obj, const char *key) {
    json_t *v = json_object_get(obj, key);
    return (v && json_is_string(v)) ? json_string_value(v) : "";
}
static double json_get_num(json_t *obj, const char *key) {
    json_t *v = json_object_get(obj, key);
    if (!v) return 0;
    if (json_is_string(v)) return atof(json_string_value(v));
    if (json_is_number(v)) return json_number_value(v);
    return 0;
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

static long long parse_pi_ts(const char *iso) {
    if (!iso || !*iso || strcmp(iso, "NA") == 0) return 0;
    struct tm tm = {0};
    if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) >= 6) {
        tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_isdst = 0;
        return (long long)mktime(&tm);
    }
    return 0;
}

static char *sanitize(const char *s) {
    static char buf[128];
    int j = 0;
    for (int i = 0; s[i] && j < 120; i++) {
        char c = s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') buf[j++] = c;
        else if (c == ' ') buf[j++] = '_';
    }
    buf[j] = '\0';
    return buf;
}

int main(void) {
    printf("\n  PREDICTIT COLLECTOR\n  timeline.db\n\n");
    curl_global_init(CURL_GLOBAL_DEFAULT);
    db_init();

    char *resp = http_get(PI_API);
    if (!resp) { printf("  FAILED to fetch\n"); db_close(); curl_global_cleanup(); return 1; }

    json_error_t err;
    json_t *root = json_loads(resp, 0, &err);
    free(resp);
    if (!root) { printf("  JSON error: %s\n", err.text); db_close(); curl_global_cleanup(); return 1; }

    json_t *markets = json_object_get(root, "markets");
    if (!markets || !json_is_array(markets)) { json_decref(root); db_close(); curl_global_cleanup(); return 1; }

    int n = (int)json_array_size(markets);
    printf("  Found %d markets\n", n);
    int total = 0;
    for (int i = 0; i < n; i++) {
        json_t *m = json_array_get(markets, i);
        int market_id = (int)json_get_num(m, "id");
        const char *market_name = json_get_str(m, "shortName");
        json_t *contracts = json_object_get(m, "contracts");

        if (!contracts || !json_is_array(contracts)) continue;

        int n_contracts = (int)json_array_size(contracts);
        for (int j = 0; j < n_contracts; j++) {
            json_t *c = json_array_get(contracts, j);
            const char *cname = json_get_str(c, "shortName");
            json_t *price_j = json_object_get(c, "lastTradePrice");
            if (!cname || *cname == '\0' || !price_j) continue;

            double price = json_is_string(price_j) ? atof(json_string_value(price_j)) : json_number_value(price_j);
            const char *cstatus = json_get_str(c, "status");
            const char *cend = json_get_str(c, "dateEnd");

            long long ts = parse_pi_ts(cend);
            if (ts == 0) ts = time(NULL);

            char source[256];
            snprintf(source, sizeof(source), "predictit_%d_%s", market_id, sanitize(cname));

            int is_politics = (strstr(market_name, "President") || strstr(market_name, "Senate") ||
                               strstr(market_name, "House") || strstr(market_name, "Governor") ||
                               strstr(market_name, "Election")) ? 1 : 0;
            const char *cat = is_politics ? "predictit_politics" : "predictit_other";

            char data_json[1024];
            snprintf(data_json, sizeof(data_json),
                "{\"market_id\":%d,\"market\":\"%s\",\"contract\":\"%s\",\"status\":\"%s\",\"price\":%.4f}",
                market_id, market_name, cname, cstatus, price);

            db_insert(source, ts, cat, data_json);
            total++;
        }
    }

    json_decref(root);
    db_close();
    curl_global_cleanup();
    printf("  Done: %d contracts collected\n\n", total);
    return 0;
}
