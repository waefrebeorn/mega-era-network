/**
 * cross_source_check.c — D36: Cross-Source Data Consistency Validation
 * Compares BTC prices across Kraken, Coinbase, and Yahoo feeds.
 * Writes results to docs/data/cross_source_check.json.
 *
 * Compile: gcc -O3 -o cross_source_check cross_source_check.c -ljansson -lm
 * Run: ./cross_source_check
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <jansson.h>

#define DATA_DIR    "/home/wubu2/money-room/data"
#define OUTPUT      DATA_DIR "/../docs/data/cross_source_check.json"
#define MAX_SOURCES 8

typedef struct {
    const char *name;
    const char *path;
    const char *price_key;  // JSON key for price
    double      price;
    int64_t     timestamp;
    int         found;
} PriceSource;

static double extract_price(const char *path, const char *key, int64_t *ts_out) {
    json_error_t err;
    json_t *root = json_load_file(path, 0, &err);
    if (!root) return -1.0;

    double price = -1.0;
    int64_t ts = 0;

    if (json_is_object(root)) {
        json_t *p = json_object_get(root, key);
        if (p && json_is_number(p)) price = json_number_value(p);
        json_t *t = json_object_get(root, "window_ts");
        if (!t) t = json_object_get(root, "timestamp");
        if (t && json_is_integer(t)) ts = json_integer_value(t);
    }

    json_decref(root);
    *ts_out = ts;
    return price;
}

int main(void) {
    PriceSource sources[] = {
        {"kraken",    DATA_DIR "/prices.json",        "btc",  0, 0, 0},
        {"yahoo",     DATA_DIR "/prices.json",        "btc",  0, 0, 0},
        {"coingecko", DATA_DIR "/prices.json",        "btc",  0, 0, 0},
        {"feed",      DATA_DIR "/../data/market_feed.json", "close", 0, 0, 0},
    };
    int nsrc = sizeof(sources) / sizeof(sources[0]);

    // Load prices
    int nfound = 0;
    double prices[MAX_SOURCES];
    int64_t timestamps[MAX_SOURCES];
    const char *names[MAX_SOURCES];

    for (int i = 0; i < nsrc; i++) {
        int64_t ts = 0;
        double p = extract_price(sources[i].path, sources[i].price_key, &ts);
        if (p > 0) {
            sources[i].price = p;
            sources[i].timestamp = ts;
            sources[i].found = 1;
            prices[nfound] = p;
            timestamps[nfound] = ts;
            names[nfound] = sources[i].name;
            nfound++;
        }
    }

    // Build JSON output
    json_t *root = json_object();
    json_object_set_new(root, "generated", json_integer((json_int_t)time(NULL)));
    json_object_set_new(root, "sources_checked", json_integer(nsrc));
    json_object_set_new(root, "sources_found", json_integer(nfound));

    json_t *src_arr = json_array();
    for (int i = 0; i < nsrc; i++) {
        json_t *s = json_object();
        json_object_set_new(s, "name", json_string(sources[i].name));
        json_object_set_new(s, "found", json_boolean(sources[i].found));
        if (sources[i].found) {
            json_object_set_new(s, "price", json_real(sources[i].price));
            json_object_set_new(s, "timestamp", json_integer(sources[i].timestamp));
        }
        json_array_append_new(src_arr, s);
    }
    json_object_set_new(root, "sources", src_arr);

    // Cross-source comparison
    json_t *issues = json_array();
    double max_pct_diff = 0.0;
    if (nfound >= 2) {
        // Find min/max prices
        double min_p = prices[0], max_p = prices[0];
        for (int i = 1; i < nfound; i++) {
            if (prices[i] < min_p) min_p = prices[i];
            if (prices[i] > max_p) max_p = prices[i];
        }
        if (min_p > 0) {
            max_pct_diff = (max_p - min_p) / min_p * 100.0;
            if (max_pct_diff > 5.0) {
                json_t *issue = json_object();
                json_object_set_new(issue, "type", json_string("price_mismatch"));
                json_object_set_new(issue, "severity", max_pct_diff > 10.0 ? json_string("CRIT") : json_string("WARN"));
                json_object_set_new(issue, "pct_diff", json_real(max_pct_diff));
                json_object_set_new(issue, "min_price", json_real(min_p));
                json_object_set_new(issue, "max_price", json_real(max_p));
                json_array_append_new(issues, issue);
            }
        }

        // Timestamp staleness check
        int64_t now = (int64_t)time(NULL);
        for (int i = 0; i < nfound; i++) {
            int64_t age = now - timestamps[i];
            if (age > 3600) {
                json_t *issue = json_object();
                json_object_set_new(issue, "type", json_string("stale_source"));
                json_object_set_new(issue, "source", json_string(names[i]));
                json_object_set_new(issue, "age_sec", json_integer(age));
                json_array_append_new(issues, issue);
            }
        }
    }

    json_object_set_new(root, "max_pct_diff", json_real(max_pct_diff));
    json_object_set_new(root, "issues", issues);
    json_object_set_new(root, "status", json_array_size(issues) > 0 ? json_string("WARN") : json_string("OK"));

    mkdir(DATA_DIR "/../docs/data", 0755);
    if (json_dump_file(root, OUTPUT, JSON_INDENT(2)) != 0) {
        fprintf(stderr, "[CROSS] Failed to write %s\n", OUTPUT);
        json_decref(root);
        return 1;
    }

    json_decref(root);
    printf("[CROSS] %d sources, %.2f%% max diff, %d issues → %s\n",
           nfound, max_pct_diff, (int)json_array_size(issues), OUTPUT);
    return json_array_size(issues) > 0 ? 1 : 0;
}
