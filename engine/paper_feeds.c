/**
 * paper_feeds.c — Historical replay feed for PAPER_MODE
 *
 * Per-room market data: reads room_config.json from ROOM_DIR to determine
 * which historical CSV to load for this room's market type.
 *
 * room_config.json format:
 *   {"market_type": 0, "csv_file": "market_crypto.csv"}
 *
 * Falls back to BTC CSV if no config found.
 */

#define _POSIX_C_SOURCE 199309L
#include "types.h"
#include "paper_feature_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define HISTORICAL_DIR   "/home/wubu2/.hermes/pm_logs/historical"

static FILE *g_csv = NULL;
static char g_line[1024];
static int64_t g_current_ts = 0;
static int g_eof = 0;
static int g_initialized = 0;
static int g_cycle_count = 0;
static int g_max_cycles = -1;
static char g_csv_path[576] = {0};
static int g_market_type = 0;

/* ── Parse room_config.json to get market_type and csv_file ── */
static int load_room_config(void) {
    char cfg_path[640];
    const char *room_dir = getenv("ROOM_DIR");
    if (!room_dir || !room_dir[0]) {
        room_dir = "/home/wubu2/.hermes/pm_logs/c_room";
    }
    snprintf(cfg_path, sizeof(cfg_path), "%s/room_config.json", room_dir);

    FILE *f = fopen(cfg_path, "r");
    if (!f) {
        fprintf(stderr, "[PAPER_FEEDS] No room_config.json at %s, falling back to BTC CSV\n", cfg_path);
        snprintf(g_csv_path, sizeof(g_csv_path), "%s/btc_1min_latest.csv", HISTORICAL_DIR);
        g_market_type = MARKET_CRYPTO;
        return -1;
    }

    char json[4096];
    size_t n = fread(json, 1, sizeof(json)-1, f);
    json[n] = '\0';
    fclose(f);

    /* Simple string-parse for market_type and csv_file */
    int mt = -1;
    char csv_file[256] = {0};

    const char *p = strstr(json, "\"market_type\"");
    if (p) {
        p = strchr(p, ':');
        if (p) mt = atoi(p+1);
    }
    p = strstr(json, "\"csv_file\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p = strchr(p, '"');
            if (p) {
                p++;
                const char *end = strchr(p, '"');
                if (end) {
                    size_t len = end - p;
                    if (len >= sizeof(csv_file)) len = sizeof(csv_file)-1;
                    memcpy(csv_file, p, len);
                    csv_file[len] = '\0';
                }
            }
        }
    }

    if (mt >= 0 && mt < N_MARKET_TYPES && csv_file[0]) {
        g_market_type = mt;
        snprintf(g_csv_path, sizeof(g_csv_path), "%s/%s", HISTORICAL_DIR, csv_file);
        fprintf(stderr, "[PAPER_FEEDS] Room config: market_type=%d csv=%s\n", mt, csv_file);
        return 0;
    }

    fprintf(stderr, "[PAPER_FEEDS] Invalid room_config.json, falling back to BTC CSV\n");
    snprintf(g_csv_path, sizeof(g_csv_path), "%s/btc_1min_latest.csv", HISTORICAL_DIR);
    g_market_type = MARKET_CRYPTO;
    return -1;
}

/* ── CSV format: timestamp,open,high,low,close,volume[,source] ── */
static int paper_feeds_parse_line(const char *line, MarketTick *tick) {
    int64_t ts;
    double o, h, l, c, v;
    if (sscanf(line, "%ld,%lf,%lf,%lf,%lf,%lf", &ts, &o, &h, &l, &c, &v) != 6) {
        return -1;
    }
    memset(tick, 0, sizeof(MarketTick));
    tick->window_ts = ts;
    tick->open = (float)o;
    tick->high = (float)h;
    tick->low = (float)l;
    tick->close = (float)c;
    tick->volume = (float)v;
    tick->market_type = (MarketType)g_market_type;

    /* Set asset label based on market type */
    const char *assets[] = {
        "BTC","SPY","EURUSD","GOLD","TNX","VIX",
        "PM","SPORTS","WEATHER","ELECTION"
    };
    int mt = g_market_type;
    if (mt >= 0 && mt < N_MARKET_TYPES) {
        strncpy(tick->asset, assets[mt], sizeof(tick->asset)-1);
    } else {
        strncpy(tick->asset, "BTC", sizeof(tick->asset)-1);
    }
    tick->asset[sizeof(tick->asset)-1] = '\0';

    g_current_ts = ts;
    return 0;
}

static int paper_feeds_advance(MarketTick *tick) {
    if (!g_csv) return -1;

    /* Skip header on first call */
    if (!g_initialized) {
        if (!fgets(g_line, sizeof(g_line), g_csv)) return -1;
        g_initialized = 1;
    }

    /* Read next data line */
    while (fgets(g_line, sizeof(g_line), g_csv)) {
        if (g_line[0] == '\n' || g_line[0] == '\r') continue;
        if (g_line[0] == 't' && g_line[1] == 's') continue; /* skip header "ts,..." */
        if (g_line[0] == 'T') continue; /* skip header "Timestamp,..." */
        if (paper_feeds_parse_line(g_line, tick) == 0) {
            g_cycle_count++;
            if (g_max_cycles > 0 && g_cycle_count >= g_max_cycles) {
                return -2;
            }
            return 0;
        }
    }

    /* EOF — rewind for continuous replay */
    rewind(g_csv);
    g_eof = 1;
    fgets(g_line, sizeof(g_line), g_csv); /* skip header */
    if (fgets(g_line, sizeof(g_line), g_csv)) {
        return paper_feeds_parse_line(g_line, tick);
    }
    return -1;
}

RoomError room_feeds_load(MarketTick *tick) {
    /* Read max cycles from env on first call */
    if (g_max_cycles == -1) {
        const char *env = getenv("PAPER_MAX_CYCLES");
        if (env && *env) {
            g_max_cycles = atoi(env);
            fprintf(stderr, "[PAPER_FEEDS] Max cycles: %d\n", g_max_cycles);
        }
    }

    /* Load room config on first call */
    if (!g_csv) {
        load_room_config();
        g_csv = fopen(g_csv_path, "r");
        if (!g_csv) {
            fprintf(stderr, "[PAPER_FEEDS] FAILED to open %s\n", g_csv_path);
            return ERR_FILE_READ;
        }
        fprintf(stderr, "[PAPER_FEEDS] Opened: %s (market_type=%d)\n", g_csv_path, g_market_type);
    }

    int ret = paper_feeds_advance(tick);
    if (ret == -2) {
        return ERR_DATA_EXHAUSTED;
    }
    if (ret != 0) {
        return ERR_FILE_READ;
    }

    /* Load aux data (SP500, VIX, BTC 30d stats) from timeline.db */
    paper_load_aux(tick);

    return ERR_OK;
}
