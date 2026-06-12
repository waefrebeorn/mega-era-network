/**
 * multi_market_feed_builder.c — Per-market feed generator
 * Reads from historical.db and timeline.db to build per-market feed JSON files.
 * Compile: gcc -O2 -o multi_market_feed_builder multi_market_feed_builder.c -lsqlite3 -lm
 * Usage:   ./multi_market_feed_builder
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <sqlite3.h>
#include <sys/stat.h>

#define HIST_DB   "/home/wubu2/.hermes/pm_logs/historical/historical.db"
#define TL_DB     "/home/wubu2/.hermes/timeline.db"
#define FEED_DIR  "/home/wubu2/.hermes/pm_logs/c_room"

static void write_feed(const char *path, const char *asset, int market_type,
                       double open, double high, double low, double close, double volume,
                       double vix, double sp500) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "{\n  \"asset\": \"%s\",\n  \"market_type\": %d,\n  \"window_ts\": %lld,\n",
            asset, market_type, (long long)time(NULL));
    fprintf(f, "  \"open\": %.6f,\n  \"high\": %.6f,\n  \"low\": %.6f,\n  \"close\": %.6f,\n  \"volume\": %.2f,\n",
            open, high, low, close, volume);
    fprintf(f, "  \"vix\": %.2f,\n  \"sp500\": %.2f,\n  \"fear_greed\": 50.0,\n", vix, sp500);
    fprintf(f, "  \"btc_dominance\": 0.0,\n  \"pump_score\": 0.0,\n");
    fprintf(f, "  \"ob_imbalance\": 0.5,\n  \"ob_depth_ratio\": 0.5,\n");
    fprintf(f, "  \"ob_wall_conc\": 0.1,\n  \"spread_bps\": 5.0\n}\n");
    fclose(f);
    rename(tmp, path);
}

static double get_latest(sqlite3 *db, const char *table, const char *col) {
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT %s FROM \"%s\" ORDER BY ts DESC LIMIT 1", col, table);
    sqlite3_stmt *stmt;
    double val = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            val = sqlite3_column_double(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return val;
}

static void get_latest_ohlc(sqlite3 *db, const char *table, double *o, double *h, double *l, double *c, double *v) {
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT open,high,low,close,volume FROM \"%s\" ORDER BY ts DESC LIMIT 1", table);
    sqlite3_stmt *stmt;
    *o=*h=*l=*c=*v=0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            *o=sqlite3_column_double(stmt,0); *h=sqlite3_column_double(stmt,1);
            *l=sqlite3_column_double(stmt,2); *c=sqlite3_column_double(stmt,3);
            *v=sqlite3_column_double(stmt,4);
        }
        sqlite3_finalize(stmt);
    }
}

static double parse_json_double(const char *data, const char *key) {
    if (!data) return 0;
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(data, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == ':' || *p == '"') p++;
    return atof(p);
}

int main(void) {
    mkdir(FEED_DIR, 0755);
    sqlite3 *hdb, *tdb;
    sqlite3_open(HIST_DB, &hdb);
    sqlite3_open(TL_DB, &tdb);

    double vix = get_latest(hdb, "^VIX_daily", "close");
    double spy = get_latest(hdb, "spy_daily", "close");

    /* 0: CRYPTO */
    double o,h,l,c,v;
    get_latest_ohlc(hdb, "btc_1min", &o,&h,&l,&c,&v);
    write_feed(FEED_DIR "/market_feed_crypto.json", "BTC", 0, o,h,l,c,v, vix, spy);

    /* 1: EQUITY */
    get_latest_ohlc(hdb, "spy_daily", &o,&h,&l,&c,&v);
    write_feed(FEED_DIR "/market_feed_equity.json", "SPY", 1, o,h,l,c,v, vix, spy);

    /* 2: FOREX */
    get_latest_ohlc(hdb, "DX-Y.NYB_daily", &o,&h,&l,&c,&v);
    write_feed(FEED_DIR "/market_feed_forex.json", "DXY", 2, o,h,l,c,v, vix, spy);

    /* 3: COMMODITY */
    get_latest_ohlc(hdb, "GC=F_daily", &o,&h,&l,&c,&v);
    write_feed(FEED_DIR "/market_feed_commodity.json", "GOLD", 3, o,h,l,c,v, vix, spy);

    /* 4: BONDS (normalize yield /10) */
    get_latest_ohlc(hdb, "^TNX_daily", &o,&h,&l,&c,&v);
    write_feed(FEED_DIR "/market_feed_bonds.json", "TNX", 4, o/10,h/10,l/10,c/10,v, vix, spy);

    /* 5: VOLATILITY (normalize /100) */
    write_feed(FEED_DIR "/market_feed_volatility.json", "VIX", 5, vix/100,vix/100+0.01,vix/100-0.01,vix/100,0, vix, spy);

    /* 6: PREDICTION (Polymarket) */
    double pm_close=0.5, pm_vol=0;
    {
        sqlite3_stmt *stmt;
        const char *sql = "SELECT data FROM timeline WHERE source='polymarket_historical' AND data LIKE '%last_trade_price%' ORDER BY ts DESC LIMIT 1";
        if (sqlite3_prepare_v2(tdb, sql, -1, &stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *data = (const char*)sqlite3_column_text(stmt, 0);
                pm_close = parse_json_double(data, "last_trade_price");
                if (pm_close < 0.001 || pm_close > 0.999) pm_close = 0.5;
                pm_vol = parse_json_double(data, "volume");
            }
            sqlite3_finalize(stmt);
        }
    }
    write_feed(FEED_DIR "/market_feed_prediction.json", "PMARKET", 6, pm_close, pm_close+0.005, pm_close-0.005, pm_close, pm_vol, vix, spy);

    /* 7: SPORTS (NFL) */
    double sports_close = 0.5;
    {
        sqlite3_stmt *stmt;
        const char *sql = "SELECT data FROM timeline WHERE source LIKE '%nfl%' ORDER BY ts DESC LIMIT 1";
        if (sqlite3_prepare_v2(tdb, sql, -1, &stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *data = (const char*)sqlite3_column_text(stmt, 0);
                if (data && (strstr(data,"Final") || strstr(data,"completed"))) {
                    double hs = parse_json_double(data, "home_score");
                    double as = parse_json_double(data, "away_score");
                    if (hs > 0 || as > 0) sports_close = hs > as ? 1.0 : 0.0;
                }
            }
            sqlite3_finalize(stmt);
        }
    }
    write_feed(FEED_DIR "/market_feed_sports.json", "NFL", 7, sports_close, 1.0, 0.0, sports_close, 0, vix, spy);

    /* 8: WEATHER (NYC temp binary) */
    double w_temp = 15.0, w_close = 0.5;
    {
        sqlite3_stmt *stmt;
        const char *sql = "SELECT data FROM timeline WHERE source='weather_new_york' ORDER BY ts DESC LIMIT 1";
        if (sqlite3_prepare_v2(tdb, sql, -1, &stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *d = (const char*)sqlite3_column_text(stmt, 0);
                w_temp = parse_json_double(d, "current_temp");
            }
            sqlite3_finalize(stmt);
        }
    }
    w_close = w_temp > 20.0 ? 1.0 : 0.0;
    { double wt = w_temp/50.0;
    write_feed(FEED_DIR "/market_feed_weather.json", "WEATHER", 8, w_close, wt+0.1, wt-0.1, w_close, 0, vix, spy); }

    /* 9: ELECTION (PredictIt) */
    double el_close = 0.5;
    {
        sqlite3_stmt *stmt;
        const char *sql = "SELECT data FROM timeline WHERE source='predictit' AND category='contract' "
                          "AND (data LIKE '%election%' OR data LIKE '%president%' OR data LIKE '%congress%') "
                          "ORDER BY ts DESC LIMIT 1";
        if (sqlite3_prepare_v2(tdb, sql, -1, &stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *data = (const char*)sqlite3_column_text(stmt, 0);
                el_close = parse_json_double(data, "last_trade_price");
                if (el_close < 0.001 || el_close > 0.999) el_close = 0.5;
            }
            sqlite3_finalize(stmt);
        }
    }
    write_feed(FEED_DIR "/market_feed_election.json", "ELECTION", 9, el_close, el_close+0.01, el_close-0.01, el_close, 0, vix, spy);

    sqlite3_close(hdb);
    sqlite3_close(tdb);
    printf("[multi_market_feed] ✅ Wrote 10 per-market feed files\n");
    return 0;
}
