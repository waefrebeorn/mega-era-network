/**
 * coingecko_fallback.c — D40: CoinGecko fallback when primary BTC feed is stale
 *
 * Reads the BTC 1-min CSV, checks if last timestamp is >1h old.
 * If stale, queries CoinGecko API directly for BTC price and appends
 * a synthetic 1-min OHLCV row to the CSV. Reports result.
 *
 * Build: gcc -O2 -o coingecko_fallback coingecko_fallback.c -lcurl -ljansson
 * Usage: ./coingecko_fallback
 * Cron: every 30m (runs before cycle_all_rooms)
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>
#include <jansson.h>

#define BTC_CSV   "/home/wubu2/.hermes/pm_logs/historical/btc_1min_latest.csv"
#define CG_API    "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd&include_24hr_vol=true"

/* Buffer for curl response */
typedef struct { char *data; size_t len; } RespBuf;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *user) {
    RespBuf *b = (RespBuf *)user;
    size_t total = size * nmemb;
    char *new = realloc(b->data, b->len + total + 1);
    if (!new) return 0;
    memcpy(new + b->len, ptr, total);
    b->data = new;
    b->len += total;
    b->data[b->len] = '\0';
    return total;
}

int main(void) {
    // Step 1: Check BTC CSV freshness
    FILE *f = fopen(BTC_CSV, "r");
    if (!f) {
        fprintf(stderr, "[FALLBACK] ERROR: Cannot open %s\n", BTC_CSV);
        return 1;
    }

    // Find last line
    char last_line[256] = {0};
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
        if (len > 0) memcpy(last_line, buf, len < 255 ? len : 255);
    }
    fclose(f);

    if (strlen(last_line) == 0) {
        fprintf(stderr, "[FALLBACK] ERROR: Empty CSV\n");
        return 1;
    }

    // Parse last timestamp
    int64_t last_ts = 0;
    if (sscanf(last_line, "%ld", &last_ts) != 1 || last_ts == 0) {
        fprintf(stderr, "[FALLBACK] ERROR: Cannot parse timestamp: %s\n", last_line);
        return 1;
    }

    time_t now = time(NULL);
    int64_t age_sec = (int64_t)now - last_ts;
    fprintf(stderr, "[FALLBACK] BTC CSV last row age: %lds (%.1fh)\n",
            (long)age_sec, age_sec / 3600.0);

    if (age_sec < 3600) {
        // Fresh enough — no fallback needed
        fprintf(stderr, "[FALLBACK] OK: BTC CSV is fresh (age=%lds)\n", (long)age_sec);
        return 0;
    }

    // Step 2: Query CoinGecko API
    fprintf(stderr, "[FALLBACK] BTC CSV stale — querying CoinGecko API...\n");

    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[FALLBACK] ERROR: curl_easy_init failed\n");
        return 1;
    }

    RespBuf resp = {NULL, 0};
    curl_easy_setopt(curl, CURLOPT_URL, CG_API);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "[FALLBACK] ERROR: curl failed: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        free(resp.data);
        return 1;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code != 200) {
        fprintf(stderr, "[FALLBACK] ERROR: HTTP %ld from CoinGecko\n", http_code);
        free(resp.data);
        return 1;
    }

    // Step 3: Parse JSON response: {"bitcoin":{"usd":73500,"usd_24h_vol":...}}
    json_error_t jerr;
    json_t *root = json_loads(resp.data, 0, &jerr);
    free(resp.data);

    if (!root) {
        fprintf(stderr, "[FALLBACK] ERROR: JSON parse: %s\n", jerr.text);
        return 1;
    }

    json_t *btc = json_object_get(root, "bitcoin");
    if (!btc || !json_is_object(btc)) {
        fprintf(stderr, "[FALLBACK] ERROR: No 'bitcoin' in response\n");
        json_decref(root);
        return 1;
    }

    json_t *jprice = json_object_get(btc, "usd");
    if (!jprice || !(json_is_number(jprice))) {
        fprintf(stderr, "[FALLBACK] ERROR: No valid 'usd' price\n");
        json_decref(root);
        return 1;
    }

    double price = json_is_real(jprice) ? json_real_value(jprice) : (double)json_integer_value(jprice);
    json_decref(root);

    // Step 4: Append synthetic row to BTC CSV
    // Format: timestamp,open,high,low,close,volume
    int64_t fallback_ts = (int64_t)now - (now % 60); // Round to minute
    FILE *csv = fopen(BTC_CSV, "a");
    if (!csv) {
        fprintf(stderr, "[FALLBACK] ERROR: Cannot append to %s\n", BTC_CSV);
        return 1;
    }

    fprintf(csv, "%ld,%.2f,%.2f,%.2f,%.2f,0\n",
            (long)fallback_ts, price, price, price, price);
    fclose(csv);

    fprintf(stderr, "[FALLBACK] ✅ Appended CoinGecko BTC=%.2f to CSV (ts=%ld)\n",
            price, (long)fallback_ts);
    return 0;
}
