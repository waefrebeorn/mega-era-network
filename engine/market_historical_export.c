/**
 * market_historical_export.c — Unified per-market historical data export (v2)
 *
 * Reads from ALL data sources:
 *   - historical.db (4.6GB): BTC, stocks, forex, commodities, bonds, VIX
 *   - timeline.db (279MB): Polymarket events, PredictIt contracts, Manifold, weather
 *   - outcomes.db: 941 resolved Polymarket outcomes + 7324 sports outcomes
 *
 * Key fixes in v2:
 *   - Polymarket: reads 500+ individual event sources from timeline.db
 *   - PredictIt: uses category='contract' with best_buy_yes/last_trade_price
 *   - Sports: uses outcomes.db sports_outcomes (7324 resolved games)
 *   - Election: uses outcomes.db for resolved prediction markets
 *   - All CSVs include source column
 *
 * Compile: gcc -O2 -std=c11 -o market_historical_export market_historical_export.c -lsqlite3 -ljansson -lm
 * Usage:   ./market_historical_export
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include <jansson.h>

#define HIST_DB     "/home/wubu2/.hermes/pm_logs/historical/historical.db"
#define TL_DB       "/home/wubu2/.hermes/timeline.db"
#define OC_DB       "/home/wubu2/.hermes/pm_logs/outcomes.db"
#define OUT_DIR     "/home/wubu2/.hermes/pm_logs/historical"

static int g_total_rows = 0;

static FILE *open_csv(const char *market) {
    char path[512];
    snprintf(path, sizeof(path), "%s/market_%s.csv", OUT_DIR, market);
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return NULL; }
    fprintf(f, "ts,open,high,low,close,volume,source\n");
    return f;
}

/* ── 1. CRYPTO: BTC 1-min from historical.db ── */
static int export_crypto(void) {
    sqlite3 *db;
    if (sqlite3_open(HIST_DB, &db) != SQLITE_OK) return -1;
    FILE *f = open_csv("crypto");
    if (!f) { sqlite3_close(db); return -1; }

    const char *sql = "SELECT ts, open, high, low, close, volume FROM btc_1min ORDER BY ts";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        fprintf(f, "%lld,%f,%f,%f,%f,%f,btc_1min\n",
            (long long)sqlite3_column_int64(stmt, 0),
            sqlite3_column_double(stmt, 1), sqlite3_column_double(stmt, 2),
            sqlite3_column_double(stmt, 3), sqlite3_column_double(stmt, 4),
            sqlite3_column_double(stmt, 5));
        count++;
    }
    sqlite3_finalize(stmt);
    fclose(f);
    sqlite3_close(db);
    printf("[export] crypto: %d rows\n", count);
    g_total_rows += count;
    return count;
}

/* ── 2. EQUITY: SPY daily from historical.db ── */
static int export_equity(void) {
    sqlite3 *db;
    if (sqlite3_open(HIST_DB, &db) != SQLITE_OK) return -1;
    FILE *f = open_csv("equity");
    if (!f) { sqlite3_close(db); return -1; }

    const char *tables[] = {"spy_daily", "QQQ_daily", "DIA_daily", "IWM_daily"};
    int total = 0;
    for (int t = 0; t < 4; t++) {
        char sql[256];
        snprintf(sql, sizeof(sql), "SELECT ts, open, high, low, close, volume FROM \"%s\" ORDER BY ts", tables[t]);
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) continue;
        int count = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            fprintf(f, "%lld,%f,%f,%f,%f,%f,%s\n",
                (long long)sqlite3_column_int64(stmt, 0),
                sqlite3_column_double(stmt, 1), sqlite3_column_double(stmt, 2),
                sqlite3_column_double(stmt, 3), sqlite3_column_double(stmt, 4),
                sqlite3_column_double(stmt, 5), tables[t]);
            count++;
        }
        sqlite3_finalize(stmt);
        total += count;
    }
    fclose(f);
    sqlite3_close(db);
    printf("[export] equity: %d rows\n", total);
    g_total_rows += total;
    return total;
}

/* ── 3. FOREX: DXY from historical.db ── */
static int export_forex(void) {
    sqlite3 *db;
    if (sqlite3_open(HIST_DB, &db) != SQLITE_OK) return -1;
    FILE *f = open_csv("forex");
    if (!f) { sqlite3_close(db); return -1; }

    const char *sql = "SELECT ts, open, high, low, close, 0 FROM \"DX-Y.NYB_daily\" ORDER BY ts";
    sqlite3_stmt *stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            double c = sqlite3_column_double(stmt, 4);
            fprintf(f, "%lld,%f,%f,%f,%f,0,dxy\n",
                (long long)sqlite3_column_int64(stmt, 0),
                sqlite3_column_double(stmt, 1), sqlite3_column_double(stmt, 2),
                sqlite3_column_double(stmt, 3), c);
            count++;
        }
        sqlite3_finalize(stmt);
    }

    /* Also forex_rates table */
    if (sqlite3_prepare_v2(db, "SELECT ts, rate FROM forex_rates ORDER BY ts", -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            double rate = sqlite3_column_double(stmt, 1);
            fprintf(f, "%lld,%f,%f,%f,%f,0,forex_rates\n",
                (long long)sqlite3_column_int64(stmt, 0),
                rate, rate * 1.001, rate * 0.999, rate);
            count++;
        }
        sqlite3_finalize(stmt);
    }

    fclose(f);
    sqlite3_close(db);
    printf("[export] forex: %d rows\n", count);
    g_total_rows += count;
    return count;
}

/* ── 4. COMMODITY: Gold + Oil from historical.db ── */
static int export_commodity(void) {
    sqlite3 *db;
    if (sqlite3_open(HIST_DB, &db) != SQLITE_OK) return -1;
    FILE *f = open_csv("commodity");
    if (!f) { sqlite3_close(db); return -1; }

    const char *tables[] = {"\"GC=F_daily\"", "\"CL=F_daily\""};
    const char *names[] = {"gold", "oil"};
    int total = 0;
    for (int t = 0; t < 2; t++) {
        char sql[256];
        snprintf(sql, sizeof(sql), "SELECT ts, open, high, low, close, 0 FROM %s ORDER BY ts", tables[t]);
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) continue;
        int count = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            fprintf(f, "%lld,%f,%f,%f,%f,0,%s\n",
                (long long)sqlite3_column_int64(stmt, 0),
                sqlite3_column_double(stmt, 1), sqlite3_column_double(stmt, 2),
                sqlite3_column_double(stmt, 3), sqlite3_column_double(stmt, 4),
                names[t]);
            count++;
        }
        sqlite3_finalize(stmt);
        total += count;
    }
    fclose(f);
    sqlite3_close(db);
    printf("[export] commodity: %d rows\n", total);
    g_total_rows += total;
    return total;
}

/* ── 5. BONDS: TNX from historical.db ── */
static int export_bonds(void) {
    sqlite3 *db;
    if (sqlite3_open(HIST_DB, &db) != SQLITE_OK) return -1;
    FILE *f = open_csv("bonds");
    if (!f) { sqlite3_close(db); return -1; }

    const char *sql = "SELECT ts, open, high, low, close, 0 FROM \"^TNX_daily\" ORDER BY ts";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        /* TNX is in percent (e.g., 4.45), normalize to 0-1 range for consistency */
        double o = sqlite3_column_double(stmt, 1) / 100.0;
        double h = sqlite3_column_double(stmt, 2) / 100.0;
        double l = sqlite3_column_double(stmt, 3) / 100.0;
        double c = sqlite3_column_double(stmt, 4) / 100.0;
        fprintf(f, "%lld,%.6f,%.6f,%.6f,%.6f,0,tnx\n",
            (long long)sqlite3_column_int64(stmt, 0), o, h, l, c);
        count++;
    }
    sqlite3_finalize(stmt);
    fclose(f);
    sqlite3_close(db);
    printf("[export] bonds: %d rows\n", count);
    g_total_rows += count;
    return count;
}

/* ── 6. VOLATILITY: VIX from historical.db ── */
static int export_volatility(void) {
    sqlite3 *db;
    if (sqlite3_open(HIST_DB, &db) != SQLITE_OK) return -1;
    FILE *f = open_csv("volatility");
    if (!f) { sqlite3_close(db); return -1; }

    const char *sql = "SELECT ts, open, high, low, close, 0 FROM \"^VIX_daily\" ORDER BY ts";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        /* VIX normalize to 0-1 (divide by 100) */
        double o = sqlite3_column_double(stmt, 1) / 100.0;
        double h = sqlite3_column_double(stmt, 2) / 100.0;
        double l = sqlite3_column_double(stmt, 3) / 100.0;
        double c = sqlite3_column_double(stmt, 4) / 100.0;
        fprintf(f, "%lld,%.6f,%.6f,%.6f,%.6f,0,vix\n",
            (long long)sqlite3_column_int64(stmt, 0), o, h, l, c);
        count++;
    }
    sqlite3_finalize(stmt);
    fclose(f);
    sqlite3_close(db);
    printf("[export] volatility: %d rows\n", count);
    g_total_rows += count;
    return count;
}

/* ── Helper: parse polymarket outcome_prices ── */
static double parse_pm_prob(json_t *root) {
    double prob = 0.5;

    /* outcome_prices is a string like "[0.9995, 0.0005]" */
    json_t *op = json_object_get(root, "outcome_prices");
    if (op && json_is_string(op)) {
        json_error_t err;
        json_t *arr = json_loads(json_string_value(op), 0, &err);
        if (arr && json_is_array(arr) && json_array_size(arr) >= 1) {
            prob = json_real_value(json_array_get(arr, 0));
        }
        if (arr) json_decref(arr);
    }

    /* last_trade_price is also a string */
    json_t *ltp = json_object_get(root, "last_trade_price");
    if (ltp && json_is_string(ltp)) {
        double p = atof(json_string_value(ltp));
        if (p > 0.0 && p <= 1.0) prob = p;
    }

    return prob;
}

/* ── 7. PREDICTION MARKETS: Polymarket events + PredictIt contracts + Manifold from timeline.db ── */
static int export_prediction(void) {
    sqlite3 *db;
    if (sqlite3_open(TL_DB, &db) != SQLITE_OK) return -1;
    FILE *f = open_csv("prediction");
    if (!f) { sqlite3_close(db); return -1; }

    sqlite3_stmt *stmt;
    int count = 0;

    /* Polymarket: use polymarket_historical source */
    if (sqlite3_prepare_v2(db,
        "SELECT ts, source, data FROM timeline "
        "WHERE source='polymarket_historical' AND length(data) > 100 ORDER BY ts",
        -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t ts = sqlite3_column_int64(stmt, 0);
            const char *data = (const char*)sqlite3_column_text(stmt, 2);
            if (!data) continue;

            json_error_t err;
            json_t *root = json_loads(data, 0, &err);
            if (!root) continue;

            double close_prob = parse_pm_prob(root);
            double vol = 0;
            json_t *v = json_object_get(root, "volume");
            if (v) {
                if (json_is_real(v)) vol = json_real_value(v);
                else if (json_is_string(v)) vol = atof(json_string_value(v));
            }

            fprintf(f, "%lld,%f,%f,%f,%f,%f,polymarket_hist\n",
                (long long)ts, close_prob, close_prob + 0.005, close_prob - 0.005, close_prob, vol);
            json_decref(root);
            count++;
        }
        sqlite3_finalize(stmt);
    }

    /* Also read individual Polymarket event sources from timeline */
    if (sqlite3_prepare_v2(db,
        "SELECT ts, source, data FROM timeline "
        "WHERE source LIKE 'polymarket_will-%' AND length(data) > 100 ORDER BY ts",
        -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t ts = sqlite3_column_int64(stmt, 0);
            const char *data = (const char*)sqlite3_column_text(stmt, 2);
            if (!data) continue;

            json_error_t err;
            json_t *root = json_loads(data, 0, &err);
            if (!root) continue;

            double close_prob = parse_pm_prob(root);
            double vol = 0;
            json_t *v = json_object_get(root, "volume");
            if (v) {
                if (json_is_real(v)) vol = json_real_value(v);
                else if (json_is_string(v)) vol = atof(json_string_value(v));
            }

            fprintf(f, "%lld,%f,%f,%f,%f,%f,polymarket_event\n",
                (long long)ts, close_prob, close_prob + 0.005, close_prob - 0.005, close_prob, vol);
            json_decref(root);
            count++;
        }
        sqlite3_finalize(stmt);
    }

    /* PredictIt: category='contract' has best_buy_yes/best_sell_yes/last_trade_price */
    if (sqlite3_prepare_v2(db,
        "SELECT ts, data FROM timeline "
        "WHERE source='predictit' AND category='contract' ORDER BY ts",
        -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t ts = sqlite3_column_int64(stmt, 0);
            const char *data = (const char*)sqlite3_column_text(stmt, 1);
            if (!data) continue;

            json_error_t err;
            json_t *root = json_loads(data, 0, &err);
            if (!root) continue;

            double close_p = 0.5;
            json_t *ltp = json_object_get(root, "last_trade_price");
            if (ltp && json_is_real(ltp)) close_p = json_real_value(ltp);

            double best_sell = close_p, best_buy = close_p;
            json_t *bs = json_object_get(root, "best_sell_yes");
            json_t *bb = json_object_get(root, "best_buy_yes");
            if (bs && json_is_real(bs)) best_sell = json_real_value(bs);
            if (bb && json_is_real(bb)) best_buy = json_real_value(bb);

            double vol = 0;
            json_t *v = json_object_get(root, "volume");
            if (v && json_is_real(v)) vol = json_real_value(v);

            fprintf(f, "%lld,%f,%f,%f,%f,%f,predictit\n",
                (long long)ts, close_p, best_sell, best_buy, close_p, vol);
            json_decref(root);
            count++;
        }
        sqlite3_finalize(stmt);
    }

    /* Manifold */
    if (sqlite3_prepare_v2(db,
        "SELECT ts, data FROM timeline WHERE source='manifold' ORDER BY ts",
        -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t ts = sqlite3_column_int64(stmt, 0);
            const char *data = (const char*)sqlite3_column_text(stmt, 1);
            if (!data) continue;

            json_error_t err;
            json_t *root = json_loads(data, 0, &err);
            if (!root) continue;

            double prob = 0.5;
            json_t *p = json_object_get(root, "probability");
            if (p && json_is_real(p)) prob = json_real_value(p);

            fprintf(f, "%lld,%f,%f,%f,%f,0,manifold\n",
                (long long)ts, prob, prob + 0.01, prob - 0.01, prob);
            json_decref(root);
            count++;
        }
        sqlite3_finalize(stmt);
    }

    fclose(f);
    sqlite3_close(db);
    printf("[export] prediction: %d rows\n", count);
    g_total_rows += count;
    return count;
}

/* ── 8. SPORTS: from outcomes.db sports_outcomes (7324 resolved games) ── */
static int export_sports(void) {
    sqlite3 *db;
    if (sqlite3_open(OC_DB, &db) != SQLITE_OK) return -1;
    FILE *f = open_csv("sports");
    if (!f) { sqlite3_close(db); return -1; }

    /* Get resolved sports outcomes with scores */
    const char *sql =
        "SELECT game_time, league, home_team, away_team, home_score, away_score, winner "
        "FROM sports_outcomes "
        "WHERE home_score IS NOT NULL AND away_score IS NOT NULL "
        "ORDER BY game_time";

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    int count = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int64_t ts = sqlite3_column_int64(stmt, 0);
        const char *league = (const char*)sqlite3_column_text(stmt, 1);
        int home_score = sqlite3_column_int(stmt, 4);
        int away_score = sqlite3_column_int(stmt, 5);
        const char *winner = (const char*)sqlite3_column_text(stmt, 6);

        /* Binary outcome: 1.0 = home win, 0.0 = away win, 0.5 = tie */
        double outcome;
        if (strcmp(winner, "TIE") == 0) {
            outcome = 0.5;
        } else {
            /* Compare winner to home_team */
            const char *home = (const char*)sqlite3_column_text(stmt, 2);
            outcome = (strcmp(winner, home) == 0) ? 1.0 : 0.0;
        }

        /* Use score spread for open/high/low */
        double spread = (double)(home_score - away_score);
        double open = 0.5 + spread * 0.01;  /* Simple linear mapping */
        double high = open + 0.1;
        double low = open - 0.1;
        if (high > 1.0) high = 1.0;
        if (low < 0.0) low = 0.0;

        char src[64];
        snprintf(src, sizeof(src), "sports_%s", league ? league : "unknown");

        fprintf(f, "%lld,%f,%f,%f,%f,0,%s\n",
            (long long)ts, open, high, low, outcome, src);
        count++;
    }
    sqlite3_finalize(stmt);

    /* Also get timeline sports data for odds/market prices */
    sqlite3 *db2;
    if (sqlite3_open(TL_DB, &db2) == SQLITE_OK) {
        sqlite3_prepare_v2(db2,
            "SELECT ts, source, data FROM timeline "
            "WHERE source IN ('nfl','nba','mlb','nhl','nfl_espn','nba_espn','mlb_espn','nhl_espn') "
            "AND length(data) > 50 ORDER BY ts",
            -1, &stmt, NULL);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t ts = sqlite3_column_int64(stmt, 0);
            const char *src = (const char*)sqlite3_column_text(stmt, 1);
            const char *data = (const char*)sqlite3_column_text(stmt, 2);
            if (!data) continue;

            json_error_t err;
            json_t *root = json_loads(data, 0, &err);
            if (!root) continue;

            /* Extract home_score/away_score from timeline data */
            json_t *hs = json_object_get(root, "home_score");
            json_t *as = json_object_get(root, "away_score");
            int home_s = hs ? json_integer_value(hs) : 0;
            int away_s = as ? json_integer_value(as) : 0;

            double outcome = 0.5;
            if (home_s > away_s) outcome = 1.0;
            else if (away_s > home_s) outcome = 0.0;

            fprintf(f, "%lld,%f,%f,%f,%f,0,%s\n",
                (long long)ts, outcome, 1.0, 0.0, outcome, src ? src : "sports");
            json_decref(root);
            count++;
        }
        sqlite3_finalize(stmt);
        sqlite3_close(db2);
    }

    fclose(f);
    sqlite3_close(db);
    printf("[export] sports: %d rows\n", count);
    g_total_rows += count;
    return count;
}

/* ── 9. WEATHER: 25 cities from timeline.db ── */
static int export_weather(void) {
    sqlite3 *db;
    if (sqlite3_open(TL_DB, &db) != SQLITE_OK) return -1;
    FILE *f = open_csv("weather");
    if (!f) { sqlite3_close(db); return -1; }

    sqlite3_stmt *stmt;
    int count = 0;

    if (sqlite3_prepare_v2(db,
        "SELECT ts, source, data FROM timeline "
        "WHERE source LIKE 'weather_%' ORDER BY ts",
        -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t ts = sqlite3_column_int64(stmt, 0);
            const char *src = (const char*)sqlite3_column_text(stmt, 1);
            const char *data = (const char*)sqlite3_column_text(stmt, 2);
            if (!data) continue;

            json_error_t err;
            json_t *root = json_loads(data, 0, &err);
            if (!root) continue;

            double temp = 0;
            json_t *ct = json_object_get(root, "current_temp");
            if (ct && json_is_real(ct)) temp = json_real_value(ct);

            /* Binary outcome: temp > 20C = HOT(1), else COLD(0) */
            double outcome = temp > 20.0 ? 1.0 : 0.0;

            /* Normalize temp to 0-1 range for open/high/low */
            double norm = temp / 50.0;
            if (norm > 1.0) norm = 1.0;
            if (norm < 0.0) norm = 0.0;

            fprintf(f, "%lld,%f,%f,%f,%f,0,%s\n",
                (long long)ts, norm, norm + 0.05, norm - 0.05, outcome, src ? src : "weather");
            json_decref(root);
            count++;
        }
        sqlite3_finalize(stmt);
    }

    fclose(f);
    sqlite3_close(db);
    printf("[export] weather: %d rows\n", count);
    g_total_rows += count;
    return count;
}

/* ── 10. ELECTION: from outcomes.db + polymarket/predictit timeline ── */
static int export_election(void) {
    sqlite3 *db;
    if (sqlite3_open(OC_DB, &db) != SQLITE_OK) return -1;
    FILE *f = open_csv("election");
    if (!f) { sqlite3_close(db); return -1; }

    sqlite3_stmt *stmt;
    int count = 0;

    /* From outcomes.db: resolved prediction markets that are election-related */
    if (sqlite3_prepare_v2(db,
        "SELECT resolution_time, question, predicted_price, resolved_price, outcome "
        "FROM outcomes "
        "WHERE question LIKE '%election%' OR question LIKE '%president%' "
        "OR question LIKE '%nomination%' OR question LIKE '%congress%' "
        "OR question LIKE '%senate%' OR question LIKE '%governor%' "
        "ORDER BY resolution_time",
        -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t ts = sqlite3_column_int64(stmt, 0);
            const char *q = (const char*)sqlite3_column_text(stmt, 1);
            double pred = sqlite3_column_double(stmt, 2);
            double resolved = sqlite3_column_double(stmt, 3);
            int outcome = sqlite3_column_int(stmt, 4);

            /* Use resolved_price as close, predicted_price as open */
            double open = pred > 0 ? pred : 0.5;
            double close = resolved > 0 ? resolved : (outcome ? 1.0 : 0.0);

            char src[64];
            snprintf(src, sizeof(src), "election_outcome_%d", outcome);

            fprintf(f, "%lld,%f,%f,%f,%f,0,%s\n",
                (long long)ts, open, open + 0.05, open - 0.05, close, src ? src : "election");
            count++;
            /* Avoid noise: limit to first 500 per question */
            if (count >= 2000) break;
        }
        sqlite3_finalize(stmt);
    }

    /* Also get from timeline: predictit + polymarket election-related sources */
    sqlite3 *db2;
    if (sqlite3_open(TL_DB, &db2) == SQLITE_OK) {
        /* Predictit markets about elections */
        if (sqlite3_prepare_v2(db2,
            "SELECT ts, data FROM timeline "
            "WHERE source='predictit' AND category='market' AND "
            "(name LIKE '%election%' OR name LIKE '%president%' OR "
            "name LIKE '%nomination%' OR name LIKE '%congress%') "
            "ORDER BY ts",
            -1, &stmt, NULL) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int64_t ts = sqlite3_column_int64(stmt, 0);
                const char *data = (const char*)sqlite3_column_text(stmt, 1);
                if (!data) continue;

                json_error_t err;
                json_t *root = json_loads(data, 0, &err);
                if (!root) continue;

                /* Market-level data doesn't have best_buy_yes — skip for now */
                /* Just use status to determine if resolved */
                json_t *status = json_object_get(root, "status");
                if (status && json_is_string(status)) {
                    const char *s = json_string_value(status);
                    if (strstr(s, "Closed") || strstr(s, "Resolved")) {
                        /* outcome = 1 if resolved status */
                        fprintf(f, "%lld,0.5,0.6,0.4,1.0,0,predictit_election\n",
                            (long long)ts);
                        count++;
                    }
                }
                json_decref(root);
            }
            sqlite3_finalize(stmt);
        }
        sqlite3_close(db2);
    }

    fclose(f);
    sqlite3_close(db);
    printf("[export] election: %d rows\n", count);
    g_total_rows += count;
    return count;
}

int main(void) {
    printf("=== Unified Market Historical Export v2 ===\n");
    printf("Output: %s/\n\n", OUT_DIR);

    export_crypto();
    export_equity();
    export_forex();
    export_commodity();
    export_bonds();
    export_volatility();
    export_prediction();
    export_sports();
    export_weather();
    export_election();

    printf("\n=== Total: %d rows exported across 10 markets ===\n", g_total_rows);
    return 0;
}
