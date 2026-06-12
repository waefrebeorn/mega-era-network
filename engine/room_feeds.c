/** Room feeds — PAPER_MODE reads historical CSVs, LIVE_MODE reads market_feed.json */
#include "types.h"
#include "paper_feature_bridge.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <jansson.h>

#define MARKET_FEED_PATH "/home/wubu2/.hermes/pm_logs/c_room/market_feed.json"
#define HISTORICAL_DIR   "/home/wubu2/.hermes/pm_logs/historical"

static int64_t g_last_ts = 0;
static int g_first_call = 1;

/* ── CSV candle for paper mode historical data ── */
typedef struct {
    int64_t ts;
    float open, high, low, close, volume;
} Candle;

/* ── Per-market CSV state for paper mode ── */
typedef struct {
    Candle *candles;
    int count;
    int pos;        /* next candle index to read */
    int market_type; /* MarketType */
    int exhausted;  /* 1 when all candles consumed */
} MarketCSV;

static MarketCSV g_market_csvs[N_MARKET_TYPES];
static int g_csvs_loaded = 0;
static int g_round_robin_mt = 0; /* current market type for round-robin */

/* ── Parse one CSV line into a candle ── */
static int parse_candle_line(const char *line, Candle *c) {
    /* Expected format: ts,open,high,low,close,volume[,source] */
    return sscanf(line, "%ld,%f,%f,%f,%f,%f",
                  &c->ts, &c->open, &c->high, &c->low, &c->close, &c->volume);
}

/* ── Load a CSV file into memory ── */
static int load_market_csv(MarketType mt, const char *filename) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", HISTORICAL_DIR, filename);

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "[FEED] WARN: cannot open market CSV %s -- skipping\n", path);
        return -1;
    }

    /* Count lines */
    int n = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) n++;
    rewind(f);

    Candle *candles = calloc(n + 1, sizeof(Candle));
    if (!candles) { fclose(f); return -1; }

    int loaded = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '#') continue;
        if (line[0] == 't' && line[1] == 's') continue; /* skip header */
        Candle c;
        if (parse_candle_line(line, &c) == 6) {
            candles[loaded++] = c;
        }
    }
    fclose(f);

    g_market_csvs[mt].candles = candles;
    g_market_csvs[mt].count = loaded;
    g_market_csvs[mt].pos = 0;
    g_market_csvs[mt].market_type = mt;
    g_market_csvs[mt].exhausted = 0;

    fprintf(stderr, "[FEED] loaded %s: %d candles for market_type=%d\n", filename, loaded, mt);
    return loaded;
}

/* ── Load all market CSVs for paper mode ── */
static int load_all_market_csvs(void) {
    if (g_csvs_loaded) return 0;

    struct { MarketType mt; const char *name; } files[] = {
        { MARKET_CRYPTO,     "market_crypto.csv" },
        { MARKET_EQUITY,     "market_equity.csv" },
        { MARKET_FOREX,      "market_forex.csv" },
        { MARKET_COMMODITY,  "market_commodity.csv" },
        { MARKET_BOND,       "market_bonds.csv" },
        { MARKET_VOLATILITY, "market_volatility.csv" },
        { MARKET_PREDICTION, "market_prediction.csv" },
        { MARKET_SPORTS,     "market_sports.csv" },
        { MARKET_WEATHER,    "market_weather.csv" },
        { MARKET_ELECTION,   "market_election.csv" },
    };

    int loaded = 0;
    for (int i = 0; i < N_MARKET_TYPES; i++) {
        if (load_market_csv(files[i].mt, files[i].name) > 0) loaded++;
    }

    g_csvs_loaded = 1;
    g_round_robin_mt = 0;
    fprintf(stderr, "[FEED] paper mode: loaded %d/%d market CSVs\n", loaded, N_MARKET_TYPES);
    return loaded;
}

static double json_get_num(const char *json, const char *key) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == '\t') p++;
    return atof(p);
}

static const char *json_get_str(const char *json, const char *key, char *out, size_t sz) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char *p = strstr(json, search);
    if (!p) return "";
    p += strlen(search);
    const char *end = strchr(p, '"');
    if (!end) return "";
    size_t len = end - p;
    if (len >= sz) len = sz - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return out;
}

RoomError room_feeds_load(MarketTick *tick) {
    memset(tick, 0, sizeof(MarketTick));

#ifdef PAPER_MODE
    /* ── PAPER MODE: read from historical CSVs round-robin across markets ── */
    if (!g_csvs_loaded) {
        int n = load_all_market_csvs();
        if (n == 0) {
            fprintf(stderr, "[FEED] FATAL: no market CSVs loaded for paper mode\n");
            return ERR_FILE_READ;
        }
    }

    /* Round-robin: try each market type in order, skip exhausted ones */
    Candle c;
    int found = 0;
    int attempts = 0;

    while (attempts < N_MARKET_TYPES) {
        MarketType mt = (MarketType)g_round_robin_mt;
        MarketCSV *csv = &g_market_csvs[mt];

        if (!csv->exhausted && csv->candles && csv->pos < csv->count) {
            c = csv->candles[csv->pos++];
            if (csv->pos >= csv->count) csv->exhausted = 1;

            tick->window_ts = c.ts;
            tick->open = c.open;
            tick->high = c.high;
            tick->low = c.low;
            tick->close = c.close;
            tick->volume = c.volume;
            tick->market_type = mt;

            /* Set asset label */
            const char *assets[] = {"BTC","SPY","EURUSD","GOLD","TNX","VIX",
                                    "PM","SPORTS","WEATHER","ELECTION"};
            strncpy(tick->asset, assets[mt < 10 ? mt : 0], sizeof(tick->asset) - 1);
            tick->asset[sizeof(tick->asset) - 1] = '\0';

            /* Advance round-robin pointer */
            g_round_robin_mt = (g_round_robin_mt + 1) % N_MARKET_TYPES;

            g_last_ts = tick->window_ts;
            g_first_call = 0;
            found = 1;
            break;
        }

        /* This market is exhausted, try next */
        g_round_robin_mt = (g_round_robin_mt + 1) % N_MARKET_TYPES;
        attempts++;
    }

    if (!found) {
        fprintf(stderr, "[FEED] paper mode: all market CSVs exhausted\n");
        return ERR_DATA_EXHAUSTED;
    }

    return ERR_OK;

#else
    /* ── LIVE MODE: read from market_feed.json ── */
    FILE *f = fopen(MARKET_FEED_PATH, "r");
    if (!f) return ERR_FILE_READ;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 1024*1024) { fclose(f); return ERR_FILE_READ; }

    char *json = malloc(len + 1);
    if (!json) { fclose(f); return ERR_FILE_READ; }
    fread(json, 1, len, f);
    json[len] = '\0';
    fclose(f);

    tick->window_ts = (int64_t)json_get_num(json, "window_ts");
    json_get_str(json, "asset", tick->asset, sizeof(tick->asset));
    tick->open = (float)json_get_num(json, "open");
    tick->high = (float)json_get_num(json, "high");
    tick->low = (float)json_get_num(json, "low");
    tick->close = (float)json_get_num(json, "close");
    tick->volume = (float)json_get_num(json, "volume");
    tick->fear_greed = (float)json_get_num(json, "fear_greed");
    tick->pump_score = (float)json_get_num(json, "pump_score");
    tick->btc_dominance = (float)json_get_num(json, "btc_dominance");
    tick->vix = (float)json_get_num(json, "vix");
    tick->sp500 = (float)json_get_num(json, "sp500");
    tick->btc_30d_volatility = (float)json_get_num(json, "btc_30d_volatility");
    tick->btc_30d_mean = (float)json_get_num(json, "btc_30d_mean");
    tick->btc_30d_high = (float)json_get_num(json, "btc_30d_high");
    tick->btc_30d_low = (float)json_get_num(json, "btc_30d_low");

    g_last_ts = tick->window_ts;

    double mt = json_get_num(json, "market_type");
    if (mt >= 0) tick->market_type = (MarketType)((int)mt);
    else tick->market_type = MARKET_CRYPTO;

    free(json);
    return ERR_OK;
#endif
}
