/**
 * room_feed_gen.c — Per-Room Feed Generator
 * Reads ROOM_DIR env var → room_config.json → generates room-specific market_feed.json
 *
 * For non-BTC rooms, transforms the c_room feed into domain-appropriate data.
 * BTC/financial rooms use the same BTC data but get correctly marked.
 * A04: Prediction market rooms now pull real Manifold binary probabilities
 * from ~/.hermes/timeline.db instead of random ~0.50 drift.
 *
 * Compile: gcc -O2 -Wall -o room_feed_gen room_feed_gen.c -lm -ljansson -lsqlite3
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <stdint.h>
#include <jansson.h>
#include <sqlite3.h>

// Map domain string to MarketType integer
static int domain_to_market_type(const char *domain) {
    if (!domain) return 0;
    if (strcmp(domain, "btc") == 0) return 0;       // MARKET_CRYPTO
    if (strcmp(domain, "stocks") == 0) return 1;    // MARKET_EQUITY
    if (strcmp(domain, "macro") == 0) return 1;     // MARKET_EQUITY
    if (strcmp(domain, "forex") == 0) return 2;     // MARKET_FOREX
    if (strcmp(domain, "options") == 0) return 5;   // MARKET_VOLATILITY
    if (strcmp(domain, "sports") == 0) return 7;    // MARKET_SPORTS
    if (strcmp(domain, "weather") == 0) return 8;   // MARKET_WEATHER
    if (strcmp(domain, "elections") == 0) return 9; // MARKET_ELECTION
    if (strcmp(domain, "consensus") == 0 || strcmp(domain, "manifold") == 0 ||
        strcmp(domain, "prediction") == 0 || strcmp(domain, "polymarket") == 0 ||
        strcmp(domain, "kalshi") == 0 || strcmp(domain, "predictit") == 0 ||
        strcmp(domain, "science_tech") == 0) return 6; // MARKET_PREDICTION
    if (strcmp(domain, "bond") == 0) return 4;      // MARKET_BOND
    if (strcmp(domain, "commodity") == 0) return 3; // MARKET_COMMODITY
    return 0;  // Default MARKET_CRYPTO
}

#define C_ROOM_FEED  "/home/wubu2/.hermes/pm_logs/c_room/market_feed.json"
#define TIMELINE_DB  "/home/wubu2/.hermes/timeline.db"
#define MAX_PATH 1024

// ── Room type → domain mapping ──
typedef struct {
    const char *name;       // room name
    const char *domain;     // data domain: btc, stocks, macro, sports, weather, prediction, consensus, options
    double default_close;   // default close value if no data source
    double base_volatility; // typical daily vol for this domain (0-1)
} RoomConfig;

static RoomConfig ROOM_TYPES[] = {
    {"btc_main",      "btc",         73000.0, 0.035},
    {"crypto_prices", "btc",         73000.0, 0.035},
    {"momentum",      "btc",         73000.0, 0.025},
    {"stocks",        "stocks",      550.0,   0.015},
    {"macro",         "macro",       5000.0,  0.010},
    {"economic",      "macro",       5000.0,  0.008},
    {"options",       "options",     0.5,     0.020},
    {"sports",        "sports",      0.5,     0.050},
    {"weather",       "weather",     0.5,     0.030},
    {"polymarket",    "prediction",  0.5,     0.040},
    {"predictit",     "prediction",  0.5,     0.035},
    {"kalshi",        "prediction",  0.5,     0.030},
    {"elections",     "prediction",  0.5,     0.045},
    {"manifold",      "prediction",  0.5,     0.025},
    {"science_tech",  "prediction",  0.5,     0.020},
    {"consensus",     "consensus",   0.5,     0.015},
    {NULL, NULL, 0, 0}
};

static RoomConfig *find_room(const char *name) {
    for (int i = 0; ROOM_TYPES[i].name; i++) {
        if (strcmp(ROOM_TYPES[i].name, name) == 0)
            return &ROOM_TYPES[i];
    }
    return NULL;
}

// ── Simple DJB2 hash for deterministic market selection ──
static unsigned long hash_str(const char *str) {
    unsigned long h = 5381;
    int c;
    while ((c = *str++)) h = ((h << 5) + h) + (unsigned char)c;
    return h;
}

// ── A04: Query real Manifold binary probabilities from timeline.db ──
// Returns the probability of a binary market at a rotating offset for the given room.
// On failure, returns -1 so caller falls back to random drift.
static double get_manifold_prob(const char *room_name, int64_t window_ts) {
    sqlite3 *db = NULL;
    if (sqlite3_open(TIMELINE_DB, &db) != SQLITE_OK) {
        fprintf(stderr, "[feed_gen] WARN: cannot open timeline.db\n");
        return -1.0;
    }

    // Count total BINARY manifold markets
    const char *count_sql =
        "SELECT COUNT(*) FROM timeline "
        "WHERE source='manifold' AND json_extract(data, '$.outcome_type')='BINARY' "
        "AND json_extract(data, '$.probability') > 0.01 "
        "AND json_extract(data, '$.probability') < 0.99";

    sqlite3_stmt *st = NULL;
    int total = 0;
    if (sqlite3_prepare_v2(db, count_sql, -1, &st, 0) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) total = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }

    if (total < 1) {
        fprintf(stderr, "[feed_gen] WARN: no manifold binary markets found (%d)\n", total);
        sqlite3_close(db);
        return -1.0;
    }

    // Select a deterministic market per room using hash rotation
    // window_ts / 86400 gives the day number — so each room gets a different
    // market each day (but the same market for all calls within a day)
    unsigned long h = hash_str(room_name) + (unsigned long)(window_ts / 86400);
    int offset = (int)(h % total);

    const char *query_sql =
        "SELECT json_extract(data, '$.probability'), json_extract(data, '$.question') "
        "FROM timeline WHERE source='manifold' "
        "AND json_extract(data, '$.outcome_type')='BINARY' "
        "AND json_extract(data, '$.probability') > 0.01 "
        "AND json_extract(data, '$.probability') < 0.99 "
        "LIMIT 1 OFFSET ?1";

    double prob = -1.0;
    if (sqlite3_prepare_v2(db, query_sql, -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, offset);
        if (sqlite3_step(st) == SQLITE_ROW) {
            prob = sqlite3_column_double(st, 0);
            const char *q = (const char *)sqlite3_column_text(st, 1);
            fprintf(stderr, "[feed_gen] %s ← manifold#%d: %.4f \"%s\"\n",
                    room_name, offset, prob, q ? q : "?");
        }
        sqlite3_finalize(st);
    }

    sqlite3_close(db);
    return prob;
}

// ── Load JSON file ──
static json_t *load_json(const char *path) {
    json_error_t err;
    json_t *j = json_load_file(path, 0, &err);
    if (!j) fprintf(stderr, "[feed_gen] WARN: cannot load %s: %s\n", path, err.text);
    return j;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    const char *dir = getenv("ROOM_DIR");
    if (!dir || !dir[0]) {
        fprintf(stderr, "[feed_gen] ERROR: ROOM_DIR not set\n");
        return 1;
    }

    // Read room_config.json to get room name
    char config_path[MAX_PATH];
    snprintf(config_path, sizeof(config_path), "%s/room_config.json", dir);

    json_t *config = load_json(config_path);
    if (!config) return 1;

    // ── A54: Validate room_config.json required fields ──
    const char *required_fields[] = {"name", "market_type", "domain"};
    int nrequired = sizeof(required_fields) / sizeof(required_fields[0]);
    for (int i = 0; i < nrequired; i++) {
        json_t *jf = json_object_get(config, required_fields[i]);
        if (!jf || !json_is_string(jf)) {
            fprintf(stderr, "[FEED_GEN] WARN: room_config.json missing/invalid '%s' field — using defaults\n", required_fields[i]);
        }
    }

    json_t *jname = json_object_get(config, "name");
    char room_name[64] = "unknown";
    if (jname && json_is_string(jname)) {
        const char *n = json_string_value(jname);
        if (n) { strncpy(room_name, n, sizeof(room_name) - 1); room_name[sizeof(room_name) - 1] = '\0'; }
    }
    json_decref(config);

    // Find room type config
    RoomConfig *rc = find_room(room_name);

    // Load the base feed (c_room or domain-specific)
    json_t *base = load_json(C_ROOM_FEED);
    if (!base) return 1;

    // Set window_ts to current timestamp
    json_object_set_new(base, "window_ts", json_integer((json_int_t)time(NULL)));

    double close_val;
    double open_val = 0, high_val = 0, low_val = 0;

    if (rc) {
        if (strcmp(rc->domain, "btc") == 0) {
            // BTC domain — use existing BTC close value
            json_t *jc = json_object_get(base, "close");
            close_val = jc && json_is_real(jc) ? json_real_value(jc) : rc->default_close;
        }
        else if (strcmp(rc->domain, "stocks") == 0 || strcmp(rc->domain, "options") == 0) {
            // Stock/options domain — scale BTC to stock range
            json_t *jc = json_object_get(base, "close");
            close_val = jc && json_is_real(jc) ? json_real_value(jc) * 0.0075 : rc->default_close;
        }
        else if (strcmp(rc->domain, "macro") == 0) {
            // Macro — SP500 level
            json_t *jsp = json_object_get(base, "sp500");
            close_val = jsp && json_is_real(jsp) ? json_real_value(jsp) : rc->default_close;
        }
        else if (strcmp(rc->domain, "sports") == 0) {
            // A04: Sports — use real Manifold binary probability
            json_t *jwin = json_object_get(base, "window_ts");
            int64_t wts = jwin && json_is_integer(jwin) ? json_integer_value(jwin) : (int64_t)time(NULL);
            double mprob = get_manifold_prob(room_name, wts);
            if (mprob > 0.01 && mprob < 0.99) {
                close_val = mprob;
            } else {
                // Fallback to random drift
                double drift = ((double)(rand() % 2001 - 1000) / 10000.0) * rc->base_volatility;
                close_val = rc->default_close + drift;
                if (close_val < 0.01) close_val = 0.01;
                if (close_val > 0.99) close_val = 0.99;
            }
        }
        else {
            // A04: Binary/probability domains (weather, prediction, options, consensus)
            // Use real Manifold binary probability where available
            json_t *jwin = json_object_get(base, "window_ts");
            int64_t wts = jwin && json_is_integer(jwin) ? json_integer_value(jwin) : (int64_t)time(NULL);
            double mprob = get_manifold_prob(room_name, wts);
            if (mprob > 0.01 && mprob < 0.99) {
                close_val = mprob;
            } else {
                // Fallback to random drift when no manifold data available
                double drift = ((double)(rand() % 2001 - 1000) / 10000.0) * rc->base_volatility;
                close_val = rc->default_close + drift;
                if (close_val < 0.01) close_val = 0.01;
                if (close_val > 0.99) close_val = 0.99;
            }
        }

        // Generate OHLC-like around close
        double half_range = close_val * rc->base_volatility * 0.5;
        if (half_range < 0.001) half_range = 0.001;
        double raw_open = close_val + ((double)(rand() % 2001 - 1000) / 1000.0) * half_range;
        open_val = raw_open > 0 ? raw_open : close_val * 0.99;
        high_val = close_val + half_range * (0.5 + (double)(rand() % 1000) / 1000.0);
        low_val = close_val - half_range * (0.5 + (double)(rand() % 1000) / 1000.0);
        if (low_val < 0) low_val = close_val * 0.01;
        if (high_val < low_val) high_val = low_val * 1.01;

        // Set domain field
        json_object_set_new(base, "room_domain", json_string(rc->domain));
        json_object_set_new(base, "room_volatility", json_real(rc->base_volatility));
        json_object_set_new(base, "market_type", json_integer(domain_to_market_type(rc->domain)));
    } else {
        // Unknown room — keep existing close
        json_t *jc = json_object_get(base, "close");
        close_val = jc && json_is_real(jc) ? json_real_value(jc) : 50000.0;
        json_object_set_new(base, "room_domain", json_string("unknown"));
        json_object_set_new(base, "room_volatility", json_real(0.02));
    }

    json_object_set_new(base, "close", json_real(close_val));
    json_object_set_new(base, "open", json_real(open_val));
    json_object_set_new(base, "high", json_real(high_val));
    json_object_set_new(base, "low", json_real(low_val));
    json_object_set_new(base, "volume", json_real(close_val * 100.0));

    // ── Write to ROOM_DIR ──
    char out_path[MAX_PATH];
    snprintf(out_path, sizeof(out_path), "%s/market_feed.json", dir);

    if (json_dump_file(base, out_path, JSON_INDENT(2)) != 0) {
        fprintf(stderr, "[feed_gen] ERROR: write %s failed\n", out_path);
        json_decref(base);
        return 1;
    }

    fprintf(stderr, "[feed_gen] Room=%s domain=%s close=%.4f → %s\n", room_name, rc ? rc->domain : "?", close_val, out_path);
    json_decref(base);
    return 0;
}
