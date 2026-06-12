/**
 * sports_align.c — Unified sports training data alignment
 *
 * Combines:
 *   1. sports_data.json (429 recent games with 20-dim features + outcomes)
 *   2. outcomes.db sports_outcomes (7,324 historical games 2018-2026)
 *   3. Polymarket sports events from timeline.db (52 sources, outcome_prices)
 *   4. Kalshi sports markets (fetched from API)
 *
 * Output: market_sports_aligned.csv
 *   ts, open, high, low, close, outcome, league, home_team, away_team, feature_0..feature_19
 *
 * For each game:
 *   - open = market probability (from Polymarket/Kalshi, or 0.5 default)
 *   - close = actual outcome (1=home win, 0=away win, 0.5=tie)
 *   - features = 20-dim vector from sports_data.json or computed from historical
 *
 * Compile: gcc -O2 -std=c11 -o sports_align sports_align.c -lcurl -ljansson -lsqlite3 -lm
 * Usage:   ./sports_align
 */
#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <curl/curl.h>
#include <jansson.h>
#include <sqlite3.h>

#define SPORTS_JSON   "/home/wubu2/money-room/data/multi_market/sports_data.json"
#define OUTCOMES_DB   "/home/wubu2/.hermes/pm_logs/outcomes.db"
#define TIMELINE_DB   "/home/wubu2/.hermes/timeline.db"
#define OUT_FILE      "/home/wubu2/.hermes/pm_logs/historical/market_sports_aligned.csv"
#define KALSHI_API    "https://api.elections.kalshi.com/trade-api/v2"

#define MAX_FEATURES 20
#define MAX_GAMES 50000

typedef struct {
    int64_t ts;
    char league[16];
    char home_team[64];
    char away_team[64];
    double open;       /* Market probability */
    double high;
    double low;
    double close;      /* Actual outcome: 1=home win, 0=away, 0.5=tie */
    double features[MAX_FEATURES];
    int has_features;
} GameRecord;

static GameRecord g_games[MAX_GAMES];
static int g_n_games = 0;

/* ── HTTP helper ── */
typedef struct { char *data; size_t len; size_t cap; } HttpBuf;

static size_t http_write(void *ptr, size_t sz, size_t nmemb, void *user) {
    size_t total = sz * nmemb;
    HttpBuf *b = (HttpBuf*)user;
    if (b->len + total >= b->cap) {
        b->cap = b->len + total + 65536;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, ptr, total);
    b->len += total;
    b->data[b->len] = 0;
    return total;
}

static HttpBuf http_get(const char *url) {
    HttpBuf b = {calloc(1, 65536), 0, 65536};
    CURL *curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &b);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "sports-align/1.0");
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    return b;
}

/* ── Load sports_data.json (429 recent games with features) ── */
static void load_sports_json(void) {
    json_error_t err;
    json_t *root = json_load_file(SPORTS_JSON, 0, &err);
    if (!root) {
        fprintf(stderr, "[sports_align] Cannot load %s: %s\n", SPORTS_JSON, err.text);
        return;
    }

    if (!json_is_array(root)) {
        fprintf(stderr, "[sports_align] sports_data.json is not an array\n");
        json_decref(root);
        return;
    }

    size_t n = json_array_size(root);
    printf("[sports_align] Loading %zu games from sports_data.json\n", n);

    for (size_t i = 0; i < n && g_n_games < MAX_GAMES; i++) {
        json_t *g = json_array_get(root, i);
        if (!g) continue;

        GameRecord *r = &g_games[g_n_games];
        memset(r, 0, sizeof(*r));

        r->ts = (int64_t)json_real_value(json_object_get(g, "game_time"));
        const char *league = json_string_value(json_object_get(g, "league"));
        const char *home = json_object_get(g, "home_team") ? json_string_value(json_object_get(g, "home_team")) : "";
        const char *away = json_object_get(g, "away_team") ? json_string_value(json_object_get(g, "away_team")) : "";

        strncpy(r->league, league ? league : "?", sizeof(r->league) - 1);
        strncpy(r->home_team, home, sizeof(r->home_team) - 1);
        strncpy(r->away_team, away, sizeof(r->away_team) - 1);

        /* Outcome: 1=home win, 0=away, 0.5=tie */
        double home_score = json_real_value(json_object_get(g, "home_score"));
        double away_score = json_real_value(json_object_get(g, "away_score"));
        if (home_score > away_score) r->close = 1.0;
        else if (away_score > home_score) r->close = 0.0;
        else r->close = 0.5;

        /* Use spread as market proxy for open */
        double spread = json_real_value(json_object_get(g, "spread"));
        /* Convert spread to probability: simple logistic */
        r->open = 1.0 / (1.0 + exp(-spread * 0.5));
        r->high = r->open + 0.05;
        r->low = r->open - 0.05;
        if (r->high > 1.0) r->high = 1.0;
        if (r->low < 0.0) r->low = 0.0;

        /* Load features */
        json_t *feats = json_object_get(g, "features");
        if (feats && json_is_array(feats)) {
            size_t nf = json_array_size(feats);
            if (nf > MAX_FEATURES) nf = MAX_FEATURES;
            for (size_t f = 0; f < nf; f++) {
                r->features[f] = json_real_value(json_array_get(feats, f));
            }
            r->has_features = 1;
        }

        g_n_games++;
    }

    json_decref(root);
    printf("[sports_align] Loaded %d games from sports_data.json\n", g_n_games);
}

/* ── Load historical outcomes from outcomes.db ── */
static void load_outcomes_db(void) {
    sqlite3 *db;
    if (sqlite3_open(OUTCOMES_DB, &db) != SQLITE_OK) {
        fprintf(stderr, "[sports_align] Cannot open %s\n", OUTCOMES_DB);
        return;
    }

    const char *sql =
        "SELECT game_time, league, home_team, away_team, home_score, away_score, winner "
        "FROM sports_outcomes "
        "WHERE home_score IS NOT NULL AND away_score IS NOT NULL "
        "ORDER BY game_time";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[sports_align] Query error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && g_n_games < MAX_GAMES) {
        GameRecord *r = &g_games[g_n_games];
        memset(r, 0, sizeof(*r));

        r->ts = sqlite3_column_int64(stmt, 0);
        const char *league = (const char*)sqlite3_column_text(stmt, 1);
        const char *home = (const char*)sqlite3_column_text(stmt, 2);
        const char *away = (const char*)sqlite3_column_text(stmt, 3);
        int home_score = sqlite3_column_int(stmt, 4);
        int away_score = sqlite3_column_int(stmt, 5);
        const char *winner = (const char*)sqlite3_column_text(stmt, 6);

        strncpy(r->league, league ? league : "?", sizeof(r->league) - 1);
        strncpy(r->home_team, home ? home : "", sizeof(r->home_team) - 1);
        strncpy(r->away_team, away ? away : "", sizeof(r->away_team) - 1);

        /* Outcome */
        if (winner && strcmp(winner, "TIE") == 0) {
            r->close = 0.5;
        } else if (winner && home && strcmp(winner, home) == 0) {
            r->close = 1.0;
        } else {
            r->close = 0.0;
        }

        /* No market data for historical — use score diff as proxy */
        double score_diff = (double)(home_score - away_score);
        r->open = 1.0 / (1.0 + exp(-score_diff * 0.3));
        r->high = r->open + 0.1;
        r->low = r->open - 0.1;
        if (r->high > 1.0) r->high = 1.0;
        if (r->low < 0.0) r->low = 0.0;

        /* Compute basic features from scores */
        r->features[0] = score_diff / 10.0;  /* Normalized score diff */
        r->features[1] = (home_score + away_score) / 20.0;  /* Total score */
        r->features[2] = (double)home_score / 10.0;
        r->features[3] = (double)away_score / 10.0;
        /* Rest are 0 */
        r->has_features = 1;

        g_n_games++;
        count++;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    printf("[sports_align] Loaded %d historical games from outcomes.db (total: %d)\n", count, g_n_games);
}

/* ── Load Polymarket sports probabilities from timeline.db ── */
static void load_polymarket_sports(void) {
    sqlite3 *db;
    if (sqlite3_open(TIMELINE_DB, &db) != SQLITE_OK) return;

    /* Get latest Polymarket sports event data */
    const char *sql =
        "SELECT source, data FROM timeline "
        "WHERE source LIKE 'polymarket_will-the-%' "
        "AND (source LIKE '%nba%' OR source LIKE '%nhl%' OR source LIKE '%nfl%' "
        "OR source LIKE '%mlb%' OR source LIKE '%stanley%' OR source LIKE '%finals%' "
        "OR source LIKE '%world-cup%' OR source LIKE '%fifa%') "
        "ORDER BY ts DESC";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return;
    }

    int updated = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *source = (const char*)sqlite3_column_text(stmt, 0);
        const char *data = (const char*)sqlite3_column_text(stmt, 1);
        if (!data) continue;

        json_error_t err;
        json_t *root = json_loads(data, 0, &err);
        if (!root) continue;

        /* Parse Polymarket data */
        /* New format: {"open": 0.7955, "close": 0.7955, "high": 0.7955, "low": 0.7955, ...} */
        /* Old format: {"outcome_prices": ["0.784", "0.216"], ...} */
        double prob = 0.5;
        
        /* Try new format first: close field */
        json_t *close_val = json_object_get(root, "close");
        if (close_val && json_is_real(close_val)) {
            prob = json_real_value(close_val);
        }
        
        /* Try old format: outcome_prices */
        if (prob < 0.01 || prob > 0.99) {
            json_t *op = json_object_get(root, "outcome_prices");
            if (op && json_is_string(op)) {
                json_t *arr = json_loads(json_string_value(op), 0, &err);
                if (arr && json_is_array(arr) && json_array_size(arr) >= 1) {
                    prob = json_real_value(json_array_get(arr, 0));
                }
                if (arr) json_decref(arr);
            }
        }
        
        /* Also try last_trade_price */
        if (prob < 0.01 || prob > 0.99) {
            json_t *ltp = json_object_get(root, "last_trade_price");
            if (ltp && json_is_string(ltp)) {
                prob = atof(json_string_value(ltp));
            }
        }

        /* Extract team name from source */
        /* Source: polymarket_will-the-new-york-knicks-win-the-2026-nba-finals */
        char team[64] = "";
        const char *p = strstr(source, "will-the-");
        if (p) {
            p += 9; /* skip "will-the-" */
            int i = 0;
            /* Copy until "-win-" or "-the-" after team name */
            while (*p && i < 60) {
                /* Stop at "-win-" which marks end of team name */
                if (p[0] == '-' && p[1] == 'w' && p[2] == 'i' && p[3] == 'n' && p[4] == '-') break;
                /* Also stop at "--" (double dash = end) */
                if (p[0] == '-' && p[1] == '-') break;
                team[i++] = (*p == '-') ? ' ' : *p;
                p++;
            }
            team[i] = 0;
            /* Trim trailing spaces */
            while (i > 0 && team[i-1] == ' ') team[--i] = 0;
        }

        /* Try to match with existing games by team name and league */
        for (int i = 0; i < g_n_games; i++) {
            GameRecord *r = &g_games[i];
            /* Check if team name appears in home or away */
            if (strcasestr(r->home_team, team) || strcasestr(r->away_team, team)) {
                /* Check league match */
                int league_match = 0;
                if (strstr(source, "nba") && strcmp(r->league, "NBA") == 0) league_match = 1;
                if (strstr(source, "nhl") && strcmp(r->league, "NHL") == 0) league_match = 1;
                if (strstr(source, "nfl") && strcmp(r->league, "NFL") == 0) league_match = 1;
                if (strstr(source, "mlb") && strcmp(r->league, "MLB") == 0) league_match = 1;

                if (league_match) {
                    /* Update market probability */
                    /* If team matches home, use prob directly; if away, use 1-prob */
                    if (strcasestr(r->home_team, team)) {
                        r->open = prob;
                    } else {
                        r->open = 1.0 - prob;
                    }
                    r->high = r->open + 0.05;
                    r->low = r->open - 0.05;
                    if (r->high > 1.0) r->high = 1.0;
                    if (r->low < 0.0) r->low = 0.0;
                    updated++;
                    break; /* Only update first match per source */
                }
            }
        }

        json_decref(root);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    printf("[sports_align] Updated %d games with Polymarket probabilities\n", updated);
}

/* ── Load Kalshi sports markets ── */
static void load_kalshi_sports(void) {
    char url[512];
    snprintf(url, sizeof(url), "%s/markets?limit=500", KALSHI_API);

    HttpBuf resp = http_get(url);
    if (!resp.data || resp.len < 100) {
        fprintf(stderr, "[sports_align] Kalshi API request failed\n");
        free(resp.data);
        return;
    }

    json_error_t err;
    json_t *root = json_loads(resp.data, 0, &err);
    free(resp.data);
    if (!root) {
        fprintf(stderr, "[sports_align] Kalshi JSON parse error: %s\n", err.text);
        return;
    }

    json_t *markets = json_object_get(root, "markets");
    if (!markets || !json_is_array(markets)) {
        json_decref(root);
        return;
    }

    int sports_count = 0;
    int updated = 0;
    size_t n = json_array_size(markets);

    for (size_t i = 0; i < n; i++) {
        json_t *m = json_array_get(markets, i);
        if (!m) continue;

        const char *ticker = json_string_value(json_object_get(m, "ticker"));
        if (!ticker) continue;

        /* Filter for sports markets */
        if (!strstr(ticker, "SPORTS")) continue;
        sports_count++;

        /* Get outcome prices */
        json_t *outcomes = json_object_get(m, "outcome_prices");
        if (!outcomes || !json_is_array(outcomes)) continue;

        /* Get title for team matching */
        const char *title = json_string_value(json_object_get(m, "title"));

        /* Try to match with existing games */
        if (title) {
            for (int j = 0; j < g_n_games; j++) {
                GameRecord *r = &g_games[j];
                if (strcasestr(title, r->home_team) || strcasestr(title, r->away_team)) {
                    /* Found a match — use first outcome price */
                    if (json_array_size(outcomes) >= 1) {
                        double prob = json_real_value(json_array_get(outcomes, 0));
                        if (prob > 0.0 && prob < 1.0) {
                            if (strcasestr(title, r->home_team)) {
                                r->open = prob;
                            } else {
                                r->open = 1.0 - prob;
                            }
                            r->high = r->open + 0.03;
                            r->low = r->open - 0.03;
                            if (r->high > 1.0) r->high = 1.0;
                            if (r->low < 0.0) r->low = 0.0;
                            updated++;
                        }
                    }
                    break;
                }
            }
        }
    }

    json_decref(root);
    printf("[sports_align] Kalshi: %d sports markets, updated %d games\n", sports_count, updated);
}

/* ── Write aligned CSV ── */
static void write_csv(void) {
    FILE *f = fopen(OUT_FILE, "w");
    if (!f) { perror(OUT_FILE); return; }

    /* Header */
    fprintf(f, "ts,open,high,low,close,league,home_team,away_team");
    for (int i = 0; i < MAX_FEATURES; i++) {
        fprintf(f, ",feat_%d", i);
    }
    fprintf(f, "\n");

    int count = 0;
    for (int i = 0; i < g_n_games; i++) {
        GameRecord *r = &g_games[i];
        /* Skip games with no features */
        if (!r->has_features) continue;

        fprintf(f, "%lld,%.6f,%.6f,%.6f,%.1f,%s,%s,%s",
            (long long)r->ts, r->open, r->high, r->low, r->close,
            r->league, r->home_team, r->away_team);
        for (int j = 0; j < MAX_FEATURES; j++) {
            fprintf(f, ",%.6f", r->features[j]);
        }
        fprintf(f, "\n");
        count++;
    }

    fclose(f);
    printf("[sports_align] Wrote %d games to %s\n", count, OUT_FILE);
}

/* ── Deduplicate: keep latest record per game ── */
static void dedup_games(void) {
    /* Simple O(n^2) dedup by ts+league */
    int write = 0;
    for (int i = 0; i < g_n_games; i++) {
        int dup = 0;
        for (int j = 0; j < write; j++) {
            if (g_games[i].ts == g_games[j].ts &&
                strcmp(g_games[i].league, g_games[j].league) == 0 &&
                strcmp(g_games[i].home_team, g_games[j].home_team) == 0) {
                dup = 1;
                break;
            }
        }
        if (!dup) {
            if (write != i) g_games[write] = g_games[i];
            write++;
        }
    }
    printf("[sports_align] Dedup: %d -> %d games\n", g_n_games, write);
    g_n_games = write;
}

int main(void) {
    printf("=== Sports Data Alignment ===\n\n");

    /* Load data sources in order */
    load_sports_json();      /* 429 recent games with features */
    load_outcomes_db();      /* 7,324 historical games */
    dedup_games();           /* Remove duplicates */
    load_polymarket_sports(); /* Update with Polymarket probabilities */
    load_kalshi_sports();    /* Update with Kalshi probabilities */

    /* Write output */
    write_csv();

    printf("\n=== Done: %d total aligned games ===\n", g_n_games);
    return 0;
}
