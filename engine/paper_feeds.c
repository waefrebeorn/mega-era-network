/**
 * paper_feeds.c — Historical replay feed for PAPER_MODE
 * 
 * Replaces static market_feed.json with advancing historical data from:
 * - btc_1min_latest.csv (BTC 1-min OHLCV candles)
 * - timeline.db (aux data: SP500, VIX, BTC 30d stats, Manifold probs)
 * 
 * Each engine cycle advances one candle. Supports max cycles via
 * PAPER_MAX_CYCLES env var (default: unlimited = infinite loop).
 */

#define _POSIX_C_SOURCE 199309L
#include "types.h"
#include "paper_feature_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define BTC_CSV_PATH "/home/wubu2/.hermes/pm_logs/historical/btc_1min_latest.csv"

static FILE *g_csv = NULL;
static char g_line[1024];
static int64_t g_current_ts = 0;
static int g_eof = 0;
static int g_initialized = 0;
static int g_cycle_count = 0;
static int g_max_cycles = -1;  // -1 = unlimited

// ── CSV format: timestamp,open,high,low,close,volume
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
    strcpy(tick->asset, "BTC");
    tick->market_type = MARKET_CRYPTO;
    g_current_ts = ts;
    return 0;
}

static int paper_feeds_advance(MarketTick *tick) {
    if (!g_csv) return -1;
    
    // Skip header on first call
    if (!g_initialized) {
        if (!fgets(g_line, sizeof(g_line), g_csv)) return -1;  // skip header
        g_initialized = 1;
    }
    
    // Read next data line
    while (fgets(g_line, sizeof(g_line), g_csv)) {
        if (g_line[0] == '#' || g_line[0] == '\n' || g_line[0] == '\r') continue;
        if (paper_feeds_parse_line(g_line, tick) == 0) {
            g_cycle_count++;
            // Check max cycles
            if (g_max_cycles > 0 && g_cycle_count >= g_max_cycles) {
                return -2;  // Signal to stop
            }
            return 0;
        }
    }
    
    // EOF - rewind to start for continuous replay (unless max_cycles hit)
    rewind(g_csv);
    g_eof = 1;
    fgets(g_line, sizeof(g_line), g_csv);  // skip header
    if (fgets(g_line, sizeof(g_line), g_csv)) {
        return paper_feeds_parse_line(g_line, tick);
    }
    return -1;
}

RoomError room_feeds_load(MarketTick *tick) {
    // Read max cycles from env on first call
    if (g_max_cycles == -1) {
        const char *env = getenv("PAPER_MAX_CYCLES");
        if (env && *env) {
            g_max_cycles = atoi(env);
            fprintf(stderr, "[PAPER_FEEDS] Max cycles: %d\n", g_max_cycles);
        }
    }
    
    if (!g_csv) {
        g_csv = fopen(BTC_CSV_PATH, "r");
        if (!g_csv) {
            fprintf(stderr, "[PAPER_FEEDS] Failed to open %s\n", BTC_CSV_PATH);
            return ERR_FILE_READ;
        }
        fprintf(stderr, "[PAPER_FEEDS] Opened historical CSV: %s\n", BTC_CSV_PATH);
    }
    
    int ret = paper_feeds_advance(tick);
    if (ret == -2) {
        // Max cycles reached - graceful exit signal
        return ERR_DATA_EXHAUSTED;  // New error code for clean shutdown
    }
    if (ret != 0) {
        return ERR_FILE_READ;
    }
    
    // Load aux data (SP500, VIX, BTC 30d stats) from timeline.db
    paper_load_aux(tick);
    
    return ERR_OK;
}