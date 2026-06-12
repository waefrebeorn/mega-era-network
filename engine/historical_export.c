/**
 * historical_export.c — Export per-market historical CSVs from historical.db
 * 
 * Reads /home/wubu2/.hermes/pm_logs/historical/historical.db
 * Exports one CSV per market type to /home/wubu2/.hermes/pm_logs/historical/
 *
 * Compile: gcc -O2 -o historical_export historical_export.c -lsqlite3 -lm
 * Usage:   ./historical_export
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#define DB_PATH "/home/wubu2/.hermes/pm_logs/historical/historical.db"
#define OUT_DIR "/home/wubu2/.hermes/pm_logs/historical"

static sqlite3 *db;

static int exec_csv(const char *sql, const char *filename) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", OUT_DIR, filename);
    
    FILE *f = fopen(path, "w");
    if (!f) { perror(filename); return -1; }
    fprintf(f, "ts,open,high,low,close,volume\n");
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[export] prepare failed for %s: %s\n", filename, sqlite3_errmsg(db));
        fclose(f);
        return -1;
    }
    
    int count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int64_t ts = sqlite3_column_int64(stmt, 0);
        double o = sqlite3_column_double(stmt, 1);
        double h = sqlite3_column_double(stmt, 2);
        double l = sqlite3_column_double(stmt, 3);
        double c = sqlite3_column_double(stmt, 4);
        double v = sqlite3_column_double(stmt, 5);
        fprintf(f, "%lld,%.8f,%.8f,%.8f,%.8f,%.2f\n", (long long)ts, o, h, l, c, v);
        count++;
    }
    
    sqlite3_finalize(stmt);
    fclose(f);
    printf("[export] %s: %d rows → %s\n", filename, count, path);
    return count;
}

/* Export crypto: BTC 1-min candles */
static int export_crypto(void) {
    return exec_csv(
        "SELECT ts, open, high, low, close, volume FROM btc_1min ORDER BY ts ASC",
        "market_crypto.csv"
    );
}

/* Export equity: SPY daily (proxy for SP500, DOW, NASDAQ) */
static int export_equity(void) {
    int n = 0;
    n += exec_csv(
        "SELECT ts, open, high, low, close, volume FROM spy_daily ORDER BY ts ASC",
        "market_equity_spy.csv"
    );
    n += exec_csv(
        "SELECT ts, open, high, low, close, volume FROM QQQ_daily ORDER BY ts ASC",
        "market_equity_qqq.csv"
    );
    n += exec_csv(
        "SELECT ts, open, high, low, close, volume FROM DIA_daily ORDER BY ts ASC",
        "market_equity_dia.csv"
    );
    n += exec_csv(
        "SELECT ts, open, high, low, close, volume FROM IWM_daily ORDER BY ts ASC",
        "market_equity_iwm.csv"
    );
    return n;
}

/* Export forex: DX-Y.NYB (Dollar Index) daily */
static int export_forex(void) {
    return exec_csv(
        "SELECT ts, open, high, low, close, 0 FROM \"DX-Y.NYB_daily\" ORDER BY ts ASC",
        "market_forex.csv"
    );
}

/* Export commodity: Gold (GC=F) and Oil (CL=F) */
static int export_commodity(void) {
    int n = 0;
    n += exec_csv(
        "SELECT ts, open, high, low, close, 0 FROM \"GC=F_daily\" ORDER BY ts ASC",
        "market_commodity_gold.csv"
    );
    n += exec_csv(
        "SELECT ts, open, high, low, close, 0 FROM \"CL=F_daily\" ORDER BY ts ASC",
        "market_commodity_oil.csv"
    );
    return n;
}

/* Export bonds: TNX (10yr yield) daily */
static int export_bonds(void) {
    return exec_csv(
        "SELECT ts, open, high, low, close, 0 FROM \"^TNX_daily\" ORDER BY ts ASC",
        "market_bonds.csv"
    );
}

/* Export volatility: VIX daily */
static int export_volatility(void) {
    return exec_csv(
        "SELECT ts, open, high, low, close, 0 FROM \"^VIX_daily\" ORDER BY ts ASC",
        "market_volatility.csv"
    );
}

/* Export prediction market data from timeline.db */
static int export_prediction(void) {
    /* Use the .hermes/timeline.db for Polymarket/Manifold/PredictIt data */
    sqlite3 *tdb;
    int rc = sqlite3_open("/home/wubu2/.hermes/timeline.db", &tdb);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[export] Cannot open timeline.db: %s\n", sqlite3_errmsg(tdb));
        return -1;
    }
    
    char path[512];
    snprintf(path, sizeof(path), "%s/market_prediction.csv", OUT_DIR);
    FILE *f = fopen(path, "w");
    if (!f) { sqlite3_close(tdb); return -1; }
    fprintf(f, "ts,source,question,probability,volume,outcome\n");
    
    const char *sql = "SELECT ts, source, "
        "json_extract(data, '$.question'), "
        "json_extract(data, '$.probability'), "
        "json_extract(data, '$.volume'), "
        "json_extract(data, '$.outcome') "
        "FROM timeline WHERE source IN ('polymarket_historical','manifold','predictit') "
        "ORDER BY ts ASC";
    
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(tdb, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        /* Fallback: try without json_extract for older sqlite */
        sqlite3_finalize(stmt);
        rc = sqlite3_prepare_v2(tdb, 
            "SELECT ts, source, data FROM timeline "
            "WHERE source IN ('polymarket_historical','manifold','predictit') "
            "ORDER BY ts ASC", -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "[export] timeline prepare failed: %s\n", sqlite3_errmsg(tdb));
            fclose(f);
            sqlite3_close(tdb);
            return -1;
        }
        
        int count = 0;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            int64_t ts = sqlite3_column_int64(stmt, 0);
            const char *src = (const char*)sqlite3_column_text(stmt, 1);
            const char *data = (const char*)sqlite3_column_text(stmt, 2);
            fprintf(f, "%lld,%s,\"%s\",0,0,0\n", (long long)ts, src, data ? data : "");
            count++;
        }
        sqlite3_finalize(stmt);
        fclose(f);
        printf("[export] market_prediction.csv: %d rows\n", count);
        sqlite3_close(tdb);
        return count;
    }
    
    int count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int64_t ts = sqlite3_column_int64(stmt, 0);
        const char *src = (const char*)sqlite3_column_text(stmt, 1);
        const char *q = (const char*)sqlite3_column_text(stmt, 2);
        double prob = sqlite3_column_double(stmt, 3);
        double vol = sqlite3_column_double(stmt, 4);
        double outcome = sqlite3_column_double(stmt, 5);
        fprintf(f, "%lld,%s,\"%s\",%.6f,%.2f,%.0f\n", (long long)ts, src, q ? q : "", prob, vol, outcome);
        count++;
    }
    
    sqlite3_finalize(stmt);
    fclose(f);
    printf("[export] market_prediction.csv: %d rows\n", count);
    sqlite3_close(tdb);
    return count;
}

/* Export sports data from timeline.db */
static int export_sports(void) {
    sqlite3 *tdb;
    int rc = sqlite3_open("/home/wubu2/.hermes/timeline.db", &tdb);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[export] Cannot open timeline.db: %s\n", sqlite3_errmsg(tdb));
        return -1;
    }
    
    char path[512];
    snprintf(path, sizeof(path), "%s/market_sports.csv", OUT_DIR);
    FILE *f = fopen(path, "w");
    if (!f) { sqlite3_close(tdb); return -1; }
    fprintf(f, "ts,source,question,probability,volume,outcome\n");
    
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(tdb,
        "SELECT ts, source, data FROM timeline "
        "WHERE source LIKE 'nfl%' OR source LIKE 'nba%' OR source LIKE 'mlb%' "
        "ORDER BY ts ASC", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[export] sports prepare failed: %s\n", sqlite3_errmsg(tdb));
        fclose(f);
        sqlite3_close(tdb);
        return -1;
    }
    
    int count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int64_t ts = sqlite3_column_int64(stmt, 0);
        const char *src = (const char*)sqlite3_column_text(stmt, 1);
        const char *data = (const char*)sqlite3_column_text(stmt, 2);
        fprintf(f, "%lld,%s,\"%s\",0,0,0\n", (long long)ts, src, data ? data : "");
        count++;
    }
    
    sqlite3_finalize(stmt);
    fclose(f);
    printf("[export] market_sports.csv: %d rows\n", count);
    sqlite3_close(tdb);
    return count;
}

/* Export weather data from timeline.db */
static int export_weather(void) {
    sqlite3 *tdb;
    int rc = sqlite3_open("/home/wubu2/.hermes/timeline.db", &tdb);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[export] Cannot open timeline.db: %s\n", sqlite3_errmsg(tdb));
        return -1;
    }
    
    char path[512];
    snprintf(path, sizeof(path), "%s/market_weather.csv", OUT_DIR);
    FILE *f = fopen(path, "w");
    if (!f) { sqlite3_close(tdb); return -1; }
    fprintf(f, "ts,city,temp_c,humidity,wind_speed,pressure,rain_mm\n");
    
    /* Weather data is stored as JSON in timeline.data */
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(tdb,
        "SELECT ts, source, data FROM timeline "
        "WHERE source LIKE 'weather_%' ORDER BY ts ASC", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[export] weather prepare failed: %s\n", sqlite3_errmsg(tdb));
        fclose(f);
        sqlite3_close(tdb);
        return -1;
    }
    
    int count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int64_t ts = sqlite3_column_int64(stmt, 0);
        const char *src = (const char*)sqlite3_column_text(stmt, 1);
        const char *data = (const char*)sqlite3_column_text(stmt, 2);
        
        /* Extract city name from source (weather_new_york → new_york) */
        const char *city = src + 8; /* skip "weather_" */
        
        /* Parse JSON fields - simple extraction */
        char temp[32] = "0", hum[32] = "0", wind[32] = "0", pres[32] = "0", rain[32] = "0";
        if (data) {
            const char *p;
            if ((p = strstr(data, "\"temperature\""))) {
                p += 13;
                while (*p == ' ' || *p == ':') p++;
                int i = 0;
                while (*p >= '-' && (*p <= '9' || *p == '.') && i < 30) temp[i++] = *p++;
                temp[i] = 0;
            }
            if ((p = strstr(data, "\"humidity\""))) {
                p += 10;
                while (*p == ' ' || *p == ':') p++;
                int i = 0;
                while (*p >= '0' && *p <= '9' && i < 30) hum[i++] = *p++;
                hum[i] = 0;
            }
            if ((p = strstr(data, "\"wind_speed\""))) {
                p += 12;
                while (*p == ' ' || *p == ':') p++;
                int i = 0;
                while (*p >= '0' && *p <= '9' && *p != ',' && i < 30) wind[i++] = *p++;
                wind[i] = 0;
            }
            if ((p = strstr(data, "\"pressure\""))) {
                p += 10;
                while (*p == ' ' || *p == ':') p++;
                int i = 0;
                while (*p >= '0' && *p <= '9' && i < 30) pres[i++] = *p++;
                pres[i] = 0;
            }
            if ((p = strstr(data, "\"rain\""))) {
                p += 6;
                while (*p == ' ' || *p == ':') p++;
                int i = 0;
                while (*p >= '0' && *p <= '9' && *p != ',' && i < 30) rain[i++] = *p++;
                rain[i] = 0;
            }
        }
        fprintf(f, "%lld,%s,%s,%s,%s,%s,%s\n", (long long)ts, city, temp, hum, wind, pres, rain);
        count++;
    }
    
    sqlite3_finalize(stmt);
    fclose(f);
    printf("[export] market_weather.csv: %d rows\n", count);
    sqlite3_close(tdb);
    return count;
}

/* Export election data (subset of prediction markets) */
static int export_election(void) {
    sqlite3 *tdb;
    int rc = sqlite3_open("/home/wubu2/.hermes/timeline.db", &tdb);
    if (rc != SQLITE_OK) return -1;
    
    char path[512];
    snprintf(path, sizeof(path), "%s/market_election.csv", OUT_DIR);
    FILE *f = fopen(path, "w");
    if (!f) { sqlite3_close(tdb); return -1; }
    fprintf(f, "ts,source,question,probability,volume,outcome\n");
    
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(tdb,
        "SELECT ts, source, data FROM timeline "
        "WHERE source IN ('polymarket_historical','predictit') "
        "AND (data LIKE '%election%' OR data LIKE '%president%' OR data LIKE '%congress%') "
        "ORDER BY ts ASC", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fclose(f);
        sqlite3_close(tdb);
        return 0; /* No election data is OK */
    }
    
    int count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int64_t ts = sqlite3_column_int64(stmt, 0);
        const char *src = (const char*)sqlite3_column_text(stmt, 1);
        const char *data = (const char*)sqlite3_column_text(stmt, 2);
        fprintf(f, "%lld,%s,\"%s\",0,0,0\n", (long long)ts, src, data ? data : "");
        count++;
    }
    
    sqlite3_finalize(stmt);
    fclose(f);
    printf("[export] market_election.csv: %d rows\n", count);
    sqlite3_close(tdb);
    return count;
}

int main(void) {
    int rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[export] Cannot open %s: %s\n", DB_PATH, sqlite3_errmsg(db));
        return 1;
    }
    
    printf("[export] === Multi-Market Historical Data Export ===\n");
    printf("[export] Source: %s (%.1f GB)\n", DB_PATH, 
           (double)strlen("4.4") * 1024.0 * 1024.0 * 1024.0);
    
    int total = 0;
    total += export_crypto();
    total += export_equity();
    total += export_forex();
    total += export_commodity();
    total += export_bonds();
    total += export_volatility();
    total += export_prediction();
    total += export_sports();
    total += export_weather();
    total += export_election();
    
    sqlite3_close(db);
    printf("[export] === Done: %d total rows exported ===\n", total);
    return 0;
}
