/**
 * room_engine.c — The Room Main Loop
 * L0: Orchestrates all 6 layers, <100ms per cycle.
 * Reads market data → computes features → runs vote → allocates capital → evolves
 */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include <math.h>
#include <dirent.h>
#include "types.h"
#include "nested_ht_infer.h"

// ── v2: Declared in room_vote.c ──
void init_genome_weights(Genome *g);

// ── F05: Graceful shutdown flag ──
static volatile sig_atomic_t g_shutdown_flag = 0;

static void handle_signal(int sig) {
    (void)sig;
    g_shutdown_flag = 1;
}

/* ── F10: CRC-32 (IEEE) — nibble-at-a-time, no external deps ── */
static uint32_t crc32_ieee(const unsigned char *buf, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    static const uint32_t table[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC,
        0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
        0x9B64C2B0, 0x86D3D244, 0xA00AE2E8, 0xBDFBF20C
    };
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        crc = (crc >> 4) ^ table[crc & 0x0F];
        crc = (crc >> 4) ^ table[crc & 0x0F];
    }
    return ~crc;
}

/* Compute CRC over RoomState excluding magic+state_crc (first 8 bytes) */
static void state_compute_crc(RoomState *s) {
    const unsigned char *base = (const unsigned char *)s + 8;
    size_t len = sizeof(RoomState) - 8;
    s->state_crc = crc32_ieee(base, len);
}

/* Verify CRC; returns 0 on match, -1 on mismatch */
static int state_verify_crc(const RoomState *s) {
    /* Compute CRC on a temp copy to avoid const issues */
    unsigned char *base = (unsigned char *)s + 8;
    size_t len = sizeof(RoomState) - 8;
    uint32_t expected = crc32_ieee(base, len);
    if (s->state_crc == expected) return 0;
    fprintf(stderr, "[F10] CRITICAL: state CRC mismatch (stored=0x%08X, computed=0x%08X) — state corrupted\n",
            s->state_crc, expected);
    return -1;
}

/* Write state corruption alert file */
static void state_write_corrupt_alert(const RoomState *s) {
    FILE *af = fopen("/home/wubu2/money-room/data/state_corrupt_alert.json", "w");
    if (!af) { perror("fopen state_corrupt_alert"); return; }
    fprintf(af, "{\n  \"alert\": \"state_corruption\",\n");
    fprintf(af, "  \"magic\": \"0x%08X\",\n", s->magic);
    fprintf(af, "  \"state_crc\": \"0x%08X\",\n", s->state_crc);
    fprintf(af, "  \"cycle\": %d,\n", s->cycle);
    fprintf(af, "  \"trade_count\": %d,\n", s->trade_count);
    fprintf(af, "  \"action\": \"state_reinitialized\"\n}\n");
    fclose(af);
}

// ── Pace control ──
// In paper mode, 5ms between cycles for fast bulk historical runs
// In live mode, 1s between cycles to match real-time data
#define PAPER_PACE_NS    5000000LL   // 5ms for paper mode

// ── F29: Runtime feature flags ──
typedef struct {
    bool darwin_evolution;
    bool sgd_training;
    bool epsilon_exploration;
    bool circuit_breaker;
    bool kelly_sizing;
    bool weekend_slippage;
    bool checkpoint_save;
} FeatureFlags;
static FeatureFlags g_flags = {true,true,true,true,true,true,true};
static void load_feature_flags(void) {
    FILE *f = fopen("../config/feature_flags.conf", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64]; char val[16];
        if (sscanf(line, "%63[^=]=%15s", key, val) != 2) continue;
        bool on = (strcmp(val, "on") == 0 || strcmp(val, "ON") == 0 || strcmp(val, "1") == 0);
        if (strcmp(key, "darwin_evolution") == 0) g_flags.darwin_evolution = on;
        else if (strcmp(key, "sgd_training") == 0) g_flags.sgd_training = on;
        else if (strcmp(key, "epsilon_exploration") == 0) g_flags.epsilon_exploration = on;
        else if (strcmp(key, "circuit_breaker") == 0) g_flags.circuit_breaker = on;
        else if (strcmp(key, "kelly_sizing") == 0) g_flags.kelly_sizing = on;
        else if (strcmp(key, "weekend_slippage") == 0) g_flags.weekend_slippage = on;
        else if (strcmp(key, "checkpoint_save") == 0) g_flags.checkpoint_save = on;
    }
    fclose(f);
}
#define LIVE_PACE_NS     1000000000LL // 1s for live mode
// ── Room paths (runtime-overridable via ROOM_DIR env var) ──
static char g_room_dir[512] = "/home/wubu2/.hermes/pm_logs/c_room";
static char g_state_path[576];
static char g_feed_path[576];
static char g_log_path[576];
static char g_room_mode[16] = "live";
static int is_paper_mode(void) { return strcmp(g_room_mode, "paper") == 0; }
static int is_market_mode(void) { return strcmp(g_room_mode, "market") == 0; }
#define ROOM_DIR g_room_dir
#define STATE_PATH g_state_path
#define FEED_PATH  g_feed_path
#define LOG_PATH   g_log_path
// ── A56: Per-cycle metrics log path (JSON lines, append mode) ──
static char g_cycle_metrics_path[576];
static char g_json_log_path[576];

// ── Init paths at startup ──
static void init_paths(void) {
    const char *env = getenv("ROOM_DIR");
    if (env && env[0]) {
        size_t len = strlen(env);
        if (len < sizeof(g_room_dir)) {
            memcpy(g_room_dir, env, len + 1);
        }
    }
    snprintf(g_state_path, sizeof(g_state_path), "%s/room_state%s.bin", g_room_dir,
             is_paper_mode() ? "_paper" : ""
    );
    snprintf(g_feed_path, sizeof(g_feed_path), "%s/market_feed.json", g_room_dir);
    snprintf(g_log_path, sizeof(g_log_path), "%s/room_log.csv", g_room_dir);
    // ── A56: Per-cycle metrics JSON lines file ──
    snprintf(g_cycle_metrics_path, sizeof(g_cycle_metrics_path), "%s/cycle_metrics.jsonl", g_room_dir);
    snprintf(g_json_log_path, sizeof(g_json_log_path), "%s/engine_log.jsonl", g_room_dir);
}
static RoomState *state = NULL;
static int state_fd = -1;
static volatile int running = 1;

// ── Nested model inference — per-market-type buffers ──
#define NESTED_BUF_SIZE 50
#define NESTED_N_MARKETS 10  // matches N_MARKET_TYPES
static NestedModelCollection *g_nested = NULL;
static double g_nested_price_buf[NESTED_N_MARKETS][NESTED_BUF_SIZE];
static int g_nested_buf_len[NESTED_N_MARKETS];
static int g_nested_buf_idx[NESTED_N_MARKETS];
static double g_prev_volume[NESTED_N_MARKETS];
static double g_nested_prediction[NESTED_N_MARKETS];  // per-market cascade predictions (0-1)

// ── Per-agent market type map ──
static int g_agent_market[MAX_AGENTS];  // market type per agent index (for Darwin evolution)

// ── Hot-reload tracking ──
static time_t g_last_hot_reload_ts = 0;
#define HOT_RELOAD_DIR   "/home/wubu2/money-room/data/multi_market"
#define HOT_RELOAD_CYCLE 1000  // Check every 1000 cycles

// ── C25: Panic stop sentinel file ──
// When this file exists, all trading halts immediately.
// Create: touch /tmp/money_room_panic
// Remove: rm /tmp/money_room_panic  (engine resumes on next cycle)
#define PANIC_FILE "/tmp/money_room_panic"
static inline int check_panic(RoomState *state) {
    int panicking = (access(PANIC_FILE, F_OK) == 0);
    if (panicking && !state->panic_stop) {
        printf("[PANIC] Panic stop file detected — halting all trading!\n");
        state->panic_stop = 1;
    } else if (!panicking && state->panic_stop) {
        printf("[PANIC] Panic stop file removed — resuming trading.\n");
        state->panic_stop = 0;
    }
    return state->panic_stop;
}

// ── A16: Feature importance pruning — decay/zero out features that persistently hurt ──
// Called periodically to prune dead features from agent genomes.
// Importance score = pos_wr - neg_wr where wr = wins/total for each side.
// Negative score = feature hurts more than helps when it pushes signal direction.
static void prune_dead_features(AgentState *agents, int n, FeatureImportance *imp) {
    static int prune_counter = 0;
    prune_counter++;
    if (prune_counter % 100 != 0) return;  // Every 100 cycles
    if (!imp) return;

    for (int f = 0; f < N_FEATURES; f++) {
        int pos_total = imp->pos_contrib_total[f];
        int neg_total = imp->neg_contrib_total[f];
        if (pos_total + neg_total < 20) continue;  // Not enough data

        float pos_wr = pos_total > 0 ? imp->pos_contrib_wins[f] / (float)pos_total : 0.5f;
        float neg_wr = neg_total > 0 ? imp->neg_contrib_wins[f] / (float)neg_total : 0.5f;
        float score = pos_wr - neg_wr;

        if (score < -0.1f) {
            // Feature is hurting — decay its weight across all agents
            for (int a = 0; a < n; a++) {
                if (!agents[a].alive) continue;
                agents[a].genome.feat_weight[f] *= 0.95f;  // 5% decay per prune
                // Also decay regime-specific weights
                for (int r = 0; r < N_REGS; r++) {
                    agents[a].genome.regime_weight[r][f] *= 0.95f;
                }
            }
        }
        if (score < -0.3f && (pos_total + neg_total) > 500) {
            // Strongly hurting with lots of data — zero out completely
            for (int a = 0; a < n; a++) {
                if (!agents[a].alive) continue;
                agents[a].genome.feat_weight[f] *= 0.5f;  // 50% reduction
                for (int r = 0; r < N_REGS; r++) {
                    agents[a].genome.regime_weight[r][f] *= 0.5f;
                }
            }
        }

        // ── A17: Convergence check — detect flat importance over many cycles ──
        #define STAGNANT_THRESHOLD 0.05f   // Max absolute change to consider "flat"
        #define STAGNANT_MAX_CYCLES 1000   // Prune after this many flat cycles
        float imp_change = score - imp->last_importance[f];
        if (imp_change < 0) imp_change = -imp_change;
        if (imp_change < STAGNANT_THRESHOLD) {
            imp->stagnant_cycles[f]++;
        } else {
            imp->stagnant_cycles[f] = 0;
        }
        imp->last_importance[f] = score;

        if (imp->stagnant_cycles[f] >= STAGNANT_MAX_CYCLES && (pos_total + neg_total) > 100) {
            // Feature has been flat for too long — aggressively prune
            for (int a = 0; a < n; a++) {
                if (!agents[a].alive) continue;
                agents[a].genome.feat_weight[f] *= 0.5f;
                for (int r = 0; r < N_REGS; r++) {
                    agents[a].genome.regime_weight[r][f] *= 0.5f;
                }
            }
            if (imp->stagnant_cycles[f] == STAGNANT_MAX_CYCLES) {
                printf("[CONV] Feature %d stagnant for %d cycles — pruning weight\\n",
                       f, imp->stagnant_cycles[f]);
            }
        }
    }
}

// ── Forward decls ──
RoomError room_feeds_load(MarketTick *tick);
RoomError room_features_compute(const MarketTick *tick, FeatureVector *fv, RoomState *s);
RoomError room_vote_run(AgentState *agents, int n,
                        const FeatureVector *fv,
                        const FeatureImportance *imp,
                        const int *agent_market,
                        VoteRecord *votes, int *count, float epsilon);
RoomError room_capital_apply(VoteRecord *votes, int count, AgentState *agents, int n, TradeRecord *trades, int start_offset, int *new_count, int64_t window_ts, int predicted_regime, RoomState *s);
RoomError room_capital_resolve(TradeRecord *trades, int *tcount,
                               const MarketTick *resolution_tick,
                               float prev_close,
                               AgentState *agents,
                               int max_trades,
                               FeatureImportance *importance,
                               float lr_decay,
                               RoomState *s);
RoomError room_darwin_evolve(AgentState *agents, int n, int cycle, DarwinRecord *rec, const int *agent_market);
RoomError room_darwin_compute_diversity(const AgentState *agents, int n, RoomStats *stats);
RoomError room_darwin_save_elite(const AgentState *agents, int n, const int *agent_market);
void       room_bridge_write(RoomState *state);

// ── Nested model: compute 17-dim features and run inference ──
static double compute_nested_prediction(const MarketTick *tick, MarketType market_type) {
    if (!g_nested) return 0.5;
    int mt = (int)market_type;
    if (mt < 0 || mt >= NESTED_N_MARKETS) mt = MARKET_CRYPTO;

    // Determine the "price" to use based on market type
    double price;
    bool is_binary = (market_type == MARKET_SPORTS || market_type == MARKET_WEATHER ||
                      market_type == MARKET_PREDICTION || market_type == MARKET_ELECTION);

    if (is_binary) {
        // Binary markets: clamp to probability 0-1
        price = tick->close;
        if (price < 0.0) price = 0.0;
        if (price > 1.0) price = 1.0;
        // If close is clearly a BTC price (not probability), default to 0.5
        if (tick->close > 1000.0) price = 0.5;
    } else {
        // Price-based markets: use raw BTC close as proxy
        price = tick->close;
    }

    // Push price into per-market ring buffer
    g_nested_price_buf[mt][g_nested_buf_idx[mt]] = price;
    g_nested_buf_idx[mt] = (g_nested_buf_idx[mt] + 1) % NESTED_BUF_SIZE;
    if (g_nested_buf_len[mt] < NESTED_BUF_SIZE) g_nested_buf_len[mt]++;

    if (g_nested_buf_len[mt] < 10) return 0.5; // Need warmup

    // Build linearized price array for this market type
    double px[NESTED_BUF_SIZE];
    for (int i = 0; i < g_nested_buf_len[mt]; i++) {
        int idx = (g_nested_buf_idx[mt] - g_nested_buf_len[mt] + i + NESTED_BUF_SIZE) % NESTED_BUF_SIZE;
        px[i] = g_nested_price_buf[mt][idx];
    }

    // Compute 17-dim feature vector (matches nested_ht training)
    double feats[17] = {0};

    if (is_binary) {
        // ── Binary market features: probability-based ──
        // feats[0-4]: probability changes at 1,3,5,10,20 periods
        if (g_nested_buf_len[mt] >= 2) feats[0] = price - px[g_nested_buf_len[mt]-2];
        if (g_nested_buf_len[mt] >= 4) feats[1] = price - px[g_nested_buf_len[mt]-4];
        if (g_nested_buf_len[mt] >= 6) feats[2] = price - px[g_nested_buf_len[mt]-6];
        if (g_nested_buf_len[mt] >= 11) feats[3] = price - px[g_nested_buf_len[mt]-11];
        if (g_nested_buf_len[mt] >= 21) feats[4] = price - px[g_nested_buf_len[mt]-21];

        // feats[5]: probability volatility (std of last 10)
        double p_mean = 0, p_var = 0;
        int nv = g_nested_buf_len[mt] < 10 ? g_nested_buf_len[mt] : 10;
        for (int i = 0; i < nv; i++) p_mean += px[g_nested_buf_len[mt] - nv + i];
        p_mean /= nv;
        for (int i = 0; i < nv; i++) { double d = px[g_nested_buf_len[mt] - nv + i] - p_mean; p_var += d * d; }
        feats[5] = sqrt(p_var / nv);

        // feats[6]: distance from 0.5 (conviction strength)
        feats[6] = fabs(price - 0.5) * 2.0;  // 0=uncertain, 1=certain

        // feats[7-8]: volume not meaningful for binary
        feats[7] = 1.0;
        feats[8] = 1.0;

        // feats[9]: probability position in recent range
        double p_min = price, p_max = price;
        for (int i = 0; i < nv; i++) {
            double v = px[g_nested_buf_len[mt] - nv + i];
            if (v < p_min) p_min = v;
            if (v > p_max) p_max = v;
        }
        feats[9] = (p_max > p_min) ? (price - p_min) / (p_max - p_min) : 0.5;

        // feats[10]: gap (change from last)
        feats[10] = (g_nested_buf_len[mt] >= 2) ? price - px[g_nested_buf_len[mt]-2] : 0.0;
    } else {
        // ── Price-based features (crypto, equity, forex, commodity, bond, volatility) ──
        // feats[0-4]: returns at 1,3,5,10,20 periods
        if (g_nested_buf_len[mt] >= 2) feats[0] = (price - px[g_nested_buf_len[mt]-2]) / fmax(px[g_nested_buf_len[mt]-2], 0.001);
        if (g_nested_buf_len[mt] >= 4) feats[1] = (price - px[g_nested_buf_len[mt]-4]) / fmax(px[g_nested_buf_len[mt]-4], 0.001);
        if (g_nested_buf_len[mt] >= 6) feats[2] = (price - px[g_nested_buf_len[mt]-6]) / fmax(px[g_nested_buf_len[mt]-6], 0.001);
        if (g_nested_buf_len[mt] >= 11) feats[3] = (price - px[g_nested_buf_len[mt]-11]) / fmax(px[g_nested_buf_len[mt]-11], 0.001);
        if (g_nested_buf_len[mt] >= 21) feats[4] = (price - px[g_nested_buf_len[mt]-21]) / fmax(px[g_nested_buf_len[mt]-21], 0.001);

        // feats[5]: volatility
        feats[5] = tick->btc_30d_volatility / 100.0;

        // feats[6]: HL range / close
        double hl_range = tick->high - tick->low;
        feats[6] = (price > 0.001) ? hl_range / price : 0.0;

        // feats[7]: volume ratio (approximate)
        feats[7] = 1.0;

        // feats[8]: volume momentum
        feats[8] = (g_prev_volume[mt] > 0.001) ? tick->volume / g_prev_volume[mt] : 1.0;
        g_prev_volume[mt] = tick->volume;

        // feats[9]: price position in range
        feats[9] = (hl_range > 0.001) ? (price - tick->low) / hl_range : 0.5;

        // feats[10]: gap
        double prev_close = (g_nested_buf_len[mt] >= 2) ? px[g_nested_buf_len[mt]-2] : price;
        feats[10] = (prev_close > 0.001) ? (tick->open - prev_close) / prev_close : 0.0;
    }

    // feats[11]: cascade (start at 0.5 for independent, will be updated)
    feats[11] = 0.5;

    // feats[12-16]: macro features (shared across all market types)
    feats[12] = tick->sp500 / 1000.0;
    feats[13] = tick->vix;
    feats[14] = 0; // fedfunds
    feats[15] = 0; // cpi
    feats[16] = 0; // t10y2y

    // Run cascade through levels (L0→L1→...→L5)
    double cascade = 0.5;
    for (int l = 0; l < g_nested->n_levels; l++) {
        if (!g_nested->mlp_models[l] && !g_nested->lr_models[l]) continue;

        // Set cascade feature at slot 11
        feats[11] = cascade;

        double pred = 0.5;
        if (g_nested->mlp_models[l]) {
            pred = nested_predict(g_nested, l, feats, cascade);
        } else if (g_nested->lr_models[l]) {
            // Use LR directly
            LRModel *lr = g_nested->lr_models[l];
            double xs[17];
            memcpy(xs, feats, 17 * sizeof(double));
            standardize_x(xs, lr->mean, lr->std, lr->d);
            pred = lr_predict_raw(lr, xs);
        }
        cascade = pred;
    }

    return cascade;
}

// ── Signal handler ──
static volatile int kill_switch_engaged = 0;

// ── C28: US market holiday check (major holidays only) ──
static bool is_us_holiday(struct tm *t) {
    if (t->tm_wday == 0 || t->tm_wday == 6) return false; // weekends handled separately
    int m = t->tm_mon + 1, d = t->tm_mday;
    // Fixed-date holidays
    if (m == 1 && d == 1) return true;   // New Year's
    if (m == 7 && d == 4) return true;   // Independence Day
    if (m == 12 && d == 25) return true; // Christmas
    // MLK Day (3rd Monday Jan)
    if (m == 1 && t->tm_wday == 1 && d >= 15 && d <= 21) return true;
    // Presidents Day (3rd Monday Feb)
    if (m == 2 && t->tm_wday == 1 && d >= 15 && d <= 21) return true;
    // Labor Day (1st Monday Sep)
    if (m == 9 && t->tm_wday == 1 && d <= 7) return true;
    // Thanksgiving (4th Thursday Nov)
    if (m == 11 && t->tm_wday == 4 && d >= 22 && d <= 28) return true;
    return false;
}

// ── C33: Position unwind priority — close losers first, then oldest ──
static float unwind_priority(const TradeRecord *t, int64_t now_ts) {
    if (t->resolved_at > 0) return 1e9f;
    float pnl = t->pnl_pct;
    float age_factor = (float)(now_ts - t->window_ts) / 86400.0f;
    return pnl - age_factor * 0.1f;
}
static void handle_sig(int sig) {
    if (sig == SIGUSR1) {
        kill_switch_engaged = 1;
        fprintf(stderr, "\n[KILL SWITCH] ENGAGED via SIGUSR1 — liquidating all positions and shutting down.\n");
    }
    running = 0;
}

// ── Hot-reload genomes from multi-market trainer ──
// Called every HOT_RELOAD_CYCLE cycles. Scans HOT_RELOAD_DIR for .bin files
// newer than the last reload. When found, injects trained genomes into ALL
// agents for that market type. Bottom 50% get full genome replacement + noise;
// top 50% get noise-only mutation to preserve successful weights.
static void hot_reload_genomes(AgentState *agents, int n) {
    const char *mm_dir = HOT_RELOAD_DIR;
    DIR *mm_d = opendir(mm_dir);
    if (!mm_d) return;  // No dir yet — trainer hasn't run

    // Scan for files newer than last reload
    struct dirent *mm_e;
    int found_new = 0;
    while ((mm_e = readdir(mm_d)) != NULL) {
        size_t nlen = strlen(mm_e->d_name);
        if (nlen < 5 || strcmp(mm_e->d_name + nlen - 4, ".bin") != 0) continue;
        if (strcmp(mm_e->d_name, ".") == 0 || strcmp(mm_e->d_name, "..") == 0) continue;

        char mm_path[512];
        snprintf(mm_path, sizeof(mm_path), "%s/%s", mm_dir, mm_e->d_name);
        struct stat st;
        if (stat(mm_path, &st) == 0) {
            if (st.st_mtime > g_last_hot_reload_ts) {
                found_new = 1;
            }
        }
    }
    if (!found_new) { closedir(mm_d); return; }

    printf("\n[HOT] New genomes detected. Reloading...\n");
    time_t now = time(NULL);
    g_last_hot_reload_ts = now;

    rewinddir(mm_d);

    int total_injected = 0;
    while ((mm_e = readdir(mm_d)) != NULL) {
        size_t nlen = strlen(mm_e->d_name);
        if (nlen < 5 || strcmp(mm_e->d_name + nlen - 4, ".bin") != 0) continue;
        if (strcmp(mm_e->d_name, ".") == 0 || strcmp(mm_e->d_name, "..") == 0) continue;

        char mm_path[512];
        snprintf(mm_path, sizeof(mm_path), "%s/%s", mm_dir, mm_e->d_name);

        FILE *mm_f = fopen(mm_path, "rb");
        if (!mm_f) continue;

        Genome trained_genome;
        int market_type = MARKET_CRYPTO;
        if (fread(&trained_genome, sizeof(Genome), 1, mm_f) != 1) {
            fclose(mm_f);
            continue;
        }
        // Try to read market type suffix
        size_t mt_read = fread(&market_type, sizeof(int), 1, mm_f);
        if (mt_read != 1) market_type = MARKET_CRYPTO;
        else if (market_type < 0 || market_type >= N_MARKET_TYPES) market_type = MARKET_CRYPTO;
        fclose(mm_f);

        // Collect agents of this market type, sorted by win_rate_ema ascending (worst first)
        int mt_agents[MAX_AGENTS];
        int mt_count = 0;
        for (int i = 0; i < n; i++) {
            if (!agents[i].alive) {
                // Dead agents are always candidates
                mt_agents[mt_count++] = i;
            } else {
                int amt = g_agent_market[i] < N_MARKET_TYPES ? g_agent_market[i] : 0;
                if (amt == market_type) {
                    mt_agents[mt_count++] = i;
                }
            }
        }

        if (mt_count < 10) continue;  // Skip tiny groups

        // Sort by win_rate_ema ascending via bubble sort (small set)
        for (int i = 0; i < mt_count - 1; i++) {
            for (int j = i + 1; j < mt_count; j++) {
                if (agents[mt_agents[j]].win_rate_ema < agents[mt_agents[i]].win_rate_ema) {
                    int tmp = mt_agents[i]; mt_agents[i] = mt_agents[j]; mt_agents[j] = tmp;
                }
            }
        }

        // Replace bottom 50% with trained genome + noise
        int n_replace = mt_count / 2;
        if (n_replace < 1) n_replace = 1;
        if (n_replace > mt_count) n_replace = mt_count;

        for (int r = 0; r < mt_count; r++) {
            int aid = mt_agents[r];
            if (r < n_replace) {
                // Bottom 50%: full genome replacement + noise
                memcpy(&agents[aid].genome, &trained_genome, sizeof(Genome));
                // T104: Reduced noise from ±0.1 to ±0.01 — preserves trained SGD weights
                for (int w = 0; w < N_FEATURES; w++) {
                    float noise = ((float)(rand() % 201 - 100)) / 10000.0f;
                    agents[aid].genome.feat_weight[w] += noise;
                    if (agents[aid].genome.feat_weight[w] > 1.0f) agents[aid].genome.feat_weight[w] = 1.0f;
                    if (agents[aid].genome.feat_weight[w] < -1.0f) agents[aid].genome.feat_weight[w] = -1.0f;
                }
                agents[aid].genome.bias += ((float)(rand() % 201 - 100)) / 10000.0f;
                if (agents[aid].genome.bias > 1.0f) agents[aid].genome.bias = 1.0f;
                if (agents[aid].genome.bias < -1.0f) agents[aid].genome.bias = -1.0f;
                agents[aid].capital = 50.0f;
                agents[aid].alive = true;
                agents[aid].trades = 0;
                agents[aid].wins = 0;
                agents[aid].losses = 0;
                agents[aid].total_pnl = 0.0f;
                agents[aid].win_rate_ema = 0.5f;
                agents[aid].peak_capital = 50.0f;
                agents[aid].max_drawdown = 0.0f;
                agents[aid].consecutive_losses = 0;
                agents[aid].starting_capital = 50.0f;
                agents[aid].last_trade_window = -1;
            } else {
                // Top 50%: noise-only mutation on existing genome
                for (int w = 0; w < N_FEATURES; w++) {
                    float noise = ((float)(rand() % 2001 - 1000)) / 20000.0f;
                    agents[aid].genome.feat_weight[w] += noise;
                    if (agents[aid].genome.feat_weight[w] > 1.0f) agents[aid].genome.feat_weight[w] = 1.0f;
                    if (agents[aid].genome.feat_weight[w] < -1.0f) agents[aid].genome.feat_weight[w] = -1.0f;
                }
                agents[aid].genome.bias += ((float)(rand() % 2001 - 1000)) / 20000.0f;
                if (agents[aid].genome.bias > 1.0f) agents[aid].genome.bias = 1.0f;
                if (agents[aid].genome.bias < -1.0f) agents[aid].genome.bias = -1.0f;
            }
            g_agent_market[aid] = market_type;
        }
        printf("[HOT]   %s: %d agents injected for market_type=%d\n", mm_e->d_name, n_replace, market_type);
        total_injected += n_replace;
    }
    closedir(mm_d);
    printf("[HOT] Total agents injected: %d\n\n", total_injected);
}

// ── Init agents with random genomes ──
// Tries to warm-start from saved elite genomes first.
#define N_WARMSTART 200  // seed 2% of 10K agents from elites
static const char *warmstart_type_names[] = {
    "CRYPTO", "EQUITY", "FOREX", "COMMODITY", "BOND",
    "VOLATILITY", "PREDICTION", "SPORTS", "WEATHER", "ELECTION"
};
static int load_warmstart_genomes(AgentState *agents, int n, int max_warm) {
    int seeded = 0;
    for (int mt = 0; mt < N_MARKET_TYPES && seeded < max_warm; mt++) {
        for (int r = 0; r < 10 && seeded < max_warm; r++) {
            char path[512];
            snprintf(path, sizeof(path), "%s/ENGINE_%s_%d.bin",
                     "/home/wubu2/money-room/data/multi_market",
                     warmstart_type_names[mt], r);
            FILE *fp = fopen(path, "rb");
            if (!fp) break;  // No more elites for this type
            Genome g;
            if (fread(&g, sizeof(Genome), 1, fp) != 1) {
                fclose(fp); break;
            }
            int file_mt;
            if (fread(&file_mt, sizeof(int), 1, fp) != 1) {
                fclose(fp); break;
            }
            fclose(fp);
            // ── A24: normalize cross-type transfer via market similarity + noise ──
            {
                static const float market_similarity[N_MARKET_TYPES] = {
                    1.00f, // CRYPTO    -> CRYPTO
                    0.55f, // CRYPTO    -> EQUITY
                    0.55f, // CRYPTO    -> FOREX
                    0.50f, // CRYPTO    -> COMMODITY
                    0.45f, // CRYPTO    -> BOND
                    0.60f, // CRYPTO    -> VOLATILITY
                    0.70f, // CRYPTO    -> PREDICTION/BINARY
                    0.55f, // CRYPTO    -> SPORTS
                    0.50f, // CRYPTO    -> WEATHER
                    0.70f  // CRYPTO    -> ELECTION
                };
                float scale = 0.9f + market_similarity[mt] * 0.2f;
                float noise_scale = (1.0f - market_similarity[mt]) * 0.10f;
                int dim = N_FEATURES < N_REGS * N_FEATURES ? N_FEATURES : N_REGS * N_FEATURES;
                for (int w = 0; w < dim; w++) {
                    float base;
                    if (w < N_FEATURES) {
                        base = g.feat_weight[w];
                    } else {
                        base = g.regime_weight[w / N_FEATURES][w % N_FEATURES];
                    }
                    float noisy = base * scale + ((float)(rand() % 2001 - 1000)) / 20000.0f * noise_scale;
                    if (noisy > 1.0f) noisy = 1.0f;
                    if (noisy < -1.0f) noisy = -1.0f;
                    if (w < N_FEATURES) {
                        g.feat_weight[w] = noisy;
                    }
                    g.regime_weight[w / N_FEATURES][w % N_FEATURES] = noisy;
                }
                g.bias += ((float)(rand() % 2001 - 1000)) / 20000.0f * noise_scale;
            }
            // Seed agent i with this genome
            agents[seeded].alive = true;
            agents[seeded].capital = 50.0f;
            agents[seeded].starting_capital = 50.0f;
            agents[seeded].trades = 0;
            agents[seeded].wins = 0;
            agents[seeded].losses = 0;
            agents[seeded].total_pnl = 0.0f;
            agents[seeded].max_drawdown = 0.0f;
            agents[seeded].peak_capital = 50.0f;
            agents[seeded].consecutive_losses = 0;
            agents[seeded].win_rate_ema = 0.5f;
            agents[seeded].last_trade_window = -1;
            agents[seeded].conv_hi_wins = 0;
            agents[seeded].conv_hi_total = 0;
            agents[seeded].conv_lo_wins = 0;
            agents[seeded].conv_lo_total = 0;
            agents[seeded].weight_mag = 0;
            memcpy(&agents[seeded].genome, &g, sizeof(Genome));
            memset(agents[seeded].hidden, 0, sizeof(agents[seeded].hidden));
            agents[seeded].last_conviction = 0.0f;
            memset(agents[seeded].last_features, 0, sizeof(agents[seeded].last_features));
            // ── T96: PDT enforcement — clean window for warm-started agents ──
            agents[seeded].day_trades_5d = 0;
            agents[seeded].day_trade_roll_ts = 0;
            g_agent_market[seeded] = mt;
            seeded++;
        }
    }
    if (seeded > 0)
        printf("[WARM] Loaded %d elite genomes — warm-start from previous run\n", seeded);
    return seeded;
}

static void init_agents(AgentState *agents, int n) {
    srand(42); // Deterministic seed for reproducibility
    float start_cap = 50.0f; // Each agent gets $50

    // ── A47: Warm-start from saved elite genomes (up to N_WARMSTART agents) ──
    int warm = load_warmstart_genomes(agents, n, N_WARMSTART);

    // Fill remaining agents with random init
    for (int i = warm; i < n; i++) {
        agents[i].alive = true;
        agents[i].capital = start_cap;
        agents[i].starting_capital = start_cap;
        agents[i].trades = 0;
        agents[i].wins = 0;
        agents[i].losses = 0;
        agents[i].total_pnl = 0.0f;
        agents[i].max_drawdown = 0.0f;
        agents[i].peak_capital = start_cap;
        agents[i].consecutive_losses = 0;
        agents[i].win_rate_ema = 0.5f;
        agents[i].last_trade_window = -1;
        // C10: Initialize conviction tracking
        agents[i].conv_hi_wins = 0;
        agents[i].conv_hi_total = 0;
        agents[i].conv_lo_wins = 0;
        agents[i].conv_lo_total = 0;
        // C19: Initialize weight diversity
        agents[i].weight_mag = 0;
        // A19: Clear mini-batch accumulators
        memset(agents[i].grad_accum, 0, sizeof(agents[i].grad_accum));
        memset(agents[i].bias_accum, 0, sizeof(agents[i].bias_accum));
        agents[i].batch_count = 0;
        // ── T96: PDT enforcement — start with clean window ──
        agents[i].day_trades_5d = 0;
        agents[i].day_trade_roll_ts = 0;

        // Random genome within bounds
        agents[i].genome.position_size     = 0.01f + (float)rand() / RAND_MAX * 0.49f;
        agents[i].genome.conviction_threshold = 0.01f + (float)rand() / RAND_MAX * 0.29f;  // Lower initial threshold
        agents[i].genome.risk_tolerance    = (float)rand() / RAND_MAX;
        agents[i].genome.lie_sensitivity   = 0.10f + (float)rand() / RAND_MAX * 0.88f;
        agents[i].genome.herd_antipathy    = (float)rand() / RAND_MAX;
        agents[i].genome.stop_loss_pct     = 0.01f + (float)rand() / RAND_MAX * 0.24f;
        agents[i].genome.take_profit_pct   = 0.01f + (float)rand() / RAND_MAX * 0.59f;
        agents[i].genome.min_edge_pct      = 0.5f + (float)rand() / RAND_MAX * 49.5f;
        agents[i].genome.time_horizon      = 0.1f + (float)rand() / RAND_MAX * 9.9f;
        agents[i].genome.mean_reversion_bias = -1.0f + (float)rand() / RAND_MAX * 2.0f;
        // ── v2: Initialize learned weights ──
        init_genome_weights(&agents[i].genome);
        // Aggressive sign diversity: each weight gets randomly flipped per agent
        for (int w = 0; w < N_FEATURES; w++) {
            float base = agents[i].genome.feat_weight[w];
            // 40% chance to flip sign — creates strong directional diversity
            if ((float)rand() / RAND_MAX < 0.4f) base = -base;
            // Further perturb by random magnitude
            agents[i].genome.feat_weight[w] = base + ((float)rand() / RAND_MAX - 0.5f) * 0.2f;
        }
        agents[i].genome.bias = ((float)rand() / RAND_MAX - 0.5f) * 0.3f;
        agents[i].genome.learning_rate = 0.005f + (float)rand() / RAND_MAX * 0.015f;
        // ── P22: Initialize regime-specific weights ──
        for (int r = 0; r < N_REGS; r++) {
            for (int w = 0; w < N_FEATURES; w++) {
                float base = agents[i].genome.feat_weight[w];
                if ((float)rand() / RAND_MAX < 0.4f) base = -base;
                agents[i].genome.regime_weight[r][w] = base + ((float)rand() / RAND_MAX - 0.5f) * 0.2f;
            }
            agents[i].genome.regime_bias[r] = agents[i].genome.bias + ((float)rand() / RAND_MAX - 0.5f) * 0.2f;
        }
        // ── v2: Initialize hidden state to zero ──
        memset(agents[i].hidden, 0, sizeof(agents[i].hidden));
        agents[i].last_conviction = 0.0f;
        memset(agents[i].last_features, 0, sizeof(agents[i].last_features));
        g_agent_market[i] = MARKET_CRYPTO;  // Default market type
    }
}

// ── Load state from mmap or create fresh ──
static RoomError load_or_init_state(void) {
    // Ensure dir exists
    mkdir(ROOM_DIR, 0755);

    state_fd = open(STATE_PATH, O_RDWR | O_CREAT, 0644);
    if (state_fd < 0) {
        perror("open state");
        return ERR_MMAP_FAIL;
    }

    // Size the file
    size_t sz = sizeof(RoomState);
    if (ftruncate(state_fd, sz) < 0) {
        perror("ftruncate");
        close(state_fd);
        return ERR_MMAP_FAIL;
    }

    state = (RoomState *)mmap(NULL, sz, PROT_READ | PROT_WRITE,
                               MAP_SHARED, state_fd, 0);
    if (state == MAP_FAILED) {
        perror("mmap");
        close(state_fd);
        return ERR_MMAP_FAIL;
    }

    // Check if already initialized with valid CRC
    // F10: If magic matches but CRC doesn't, state is corrupted
    // F11: If version is old, migrate in-place (check version first since CRC may fail across versions)
    bool crc_good = (state->magic == STATE_MAGIC && state_verify_crc(state) == 0);
    // F11: Version migration — handle old state formats (before CRC check since layout changed)
    if (state->magic == STATE_MAGIC && state->state_version > 0 && state->state_version < STATE_VERSION && state->state_version != STATE_VERSION) {
        fprintf(stderr, "[F11] Migrating state from v%d to v%d\n", state->state_version, STATE_VERSION);
        if (state->state_version < 2) {
            state->state_crc = 0;
        }
        if (state->state_version < 3) {
            state->room_take_profit_pct = 0.20f;
            state->room_take_profit_triggered = 0;
        }
        if (state->state_version < 5) {
            // A22: v4→v5 — zero out new per-agent position counters
            for (int i = 0; i < MAX_AGENTS; i++) {
                state->agents[i].n_open_positions = 0;
            }
        }
        state->state_version = STATE_VERSION;
        state_compute_crc(state);
        crc_good = true;
        fprintf(stderr, "[F11] Migration complete — state now v%d\n", STATE_VERSION);
    }
    if (!crc_good) {
        // Corruption detected: magic matches but CRC doesn't (and not a version mismatch)
        if (state->magic == STATE_MAGIC) {
            fprintf(stderr, "[F10] CRITICAL: state corruption detected via CRC mismatch — reinitializing\n");
            state_write_corrupt_alert(state);
        }
        memset(state, 0, sz);
        state->magic = STATE_MAGIC;
        init_agents(state->agents, MAX_AGENTS);

        // ── Load multi-market trained genomes if available ──
        // Seeds a subset of agents with genomes optimized for different market types
        const char *mm_dir = "/home/wubu2/money-room/data/multi_market";
        DIR *mm_d = opendir(mm_dir);
        if (mm_d) {
            struct dirent *mm_e;
            int m_idx = 0;
            int agents_per_market = MAX_AGENTS / 20;  // ~500 agents per market type
            if (agents_per_market < 1) agents_per_market = 1;

            while ((mm_e = readdir(mm_d)) != NULL && m_idx < 20) {
                // Match *.bin files
                size_t nlen = strlen(mm_e->d_name);
                if (nlen < 5 || strcmp(mm_e->d_name + nlen - 4, ".bin") != 0) continue;
                if (strcmp(mm_e->d_name, ".") == 0 || strcmp(mm_e->d_name, "..") == 0) continue;

                char mm_path[512];
                snprintf(mm_path, sizeof(mm_path), "%s/%s", mm_dir, mm_e->d_name);

                FILE *mm_f = fopen(mm_path, "rb");
                if (!mm_f) {
                    fprintf(stderr, "[ROOM] WARN: cannot open genome %s — skipping\n", mm_e->d_name);
                    continue;
                }

                Genome trained_genome;
                int market_type = MARKET_CRYPTO;
                if (fread(&trained_genome, sizeof(Genome), 1, mm_f) == 1) {
                    // Try to read market type suffix (old files may lack it)
                    size_t mt_read = fread(&market_type, sizeof(int), 1, mm_f);
                    if (mt_read != 1) {
                        market_type = MARKET_CRYPTO;
                        fprintf(stderr, "[ROOM] WARN: %s has no market_type suffix, defaulting to CRYPTO\n", mm_e->d_name);
                    } else if (market_type < 0 || market_type >= N_MARKET_TYPES) {
                        fprintf(stderr, "[ROOM] WARN: %s has invalid market_type=%d, defaulting to CRYPTO\n", mm_e->d_name, market_type);
                        market_type = MARKET_CRYPTO;
                    }

                    // Seed agents with this genome
                    int start = m_idx * agents_per_market;
                    int end = start + agents_per_market;
                    if (end > MAX_AGENTS) end = MAX_AGENTS;

                    for (int i = start; i < end; i++) {
                        // Copy the trained genome
                        memcpy(&state->agents[i].genome, &trained_genome, sizeof(Genome));
                        // Set market type for Darwin evolution filtering
                        g_agent_market[i] = market_type;
                        // Add some noise for diversity
                        for (int w = 0; w < N_FEATURES; w++) {
                            float noise = ((float)(rand() % 2001 - 1000)) / 10000.0f;
                            state->agents[i].genome.feat_weight[w] += noise;
                        }
                        state->agents[i].genome.bias += ((float)(rand() % 2001 - 1000)) / 10000.0f;
                    }
                    printf("[ROOM] Loaded %s genome into agents %d-%d (market_type=%d)\n",
                           mm_e->d_name, start, end - 1, market_type);
                }
                fclose(mm_f);
                m_idx++;
            }
            closedir(mm_d);
            printf("[ROOM] Multi-market genomes loaded: %d types\n", m_idx);
        }
        state->stats.active_agents = MAX_AGENTS;
        state->stats.capital_current = 50.0f * MAX_AGENTS;
        state->stats.capital_peak = 50.0f * MAX_AGENTS;
        state->stats.sharpe_ratio = 0.0f;
        state->stats.win_rate = 0.5f;
        state->stats.max_drawdown = 0.0f;
        state->stats.return_count = 0;
        state->stats.return_idx = 0;
        state->room_capital = 50.0f;  // Real $50 seed
        state->room_capital_peak = 50.0f;
        state->room_take_profit_pct = 0.20f; // C35: 20% profit target
        state->room_take_profit_triggered = 0;
        state->state_version = STATE_VERSION; // F11: mark current version
        state->prev_room_capital = 50.0f;
        state->room_trade.resolved_at = -1; // Mark as init
        // ── T17: Circuit breaker defaults ──
        state->circuit_breaker_cycles = 0;
        state->circuit_breaker_count = 0;
        state->max_drawdown_pct = 0.20f;       // 20% max drawdown
        state->circuit_cooldown_cycles = 100;   // Cool down for 100 cycles
        state->max_consecutive_losses = 10;     // Trip after 10 consecutive losses
        state->consec_room_losses = 0;
        state->circuit_breaker_peak = 50.0f;
        state->daily_pnl = 0.0f;
        state->max_daily_loss_pct = 0.10f;  // C05: Trip after 10% daily loss
        state->last_daily_reset_day = 0;
        state->daily_loss_streak = 0;
        // ── A13: Regime transition model init ──
        memset(state->regime_transition_counts, 0, sizeof(state->regime_transition_counts));
        state->prev_regime = -1;
        state->predicted_regime = 0;
        // ── T18: Position limits defaults ──
        state->max_position_pct_room = 0.02f;      // Max 2% of room total per agent
        state->max_total_exposure_pct = 0.25f;      // Max 25% of total capital at risk
        state->max_direction_pct = 0.15f;           // C36: Max 15% per direction (YES/NO)
        state->current_total_exposure = 0.0f;
        state->current_yes_exposure = 0.0f;
        state->current_no_exposure = 0.0f;
        state->peak_total_exposure = 0.0f;
        // ── T19: Trade rate limit defaults ──
        state->max_trades_per_cycle = 0;      // 0 = unlimited
        state->trades_deferred = 0;
        state->total_trades_deferred = 0;
        // ── T20: Slippage tracking defaults ──
        state->total_slippage_paid = 0.0f;
        state->slippage_events = 0;
        // ── C25: Panic stop ──
        state->panic_stop = 0;
        // ── A30: Epsilon-greedy exploration ──
        state->epsilon = 0.05f;          // 5% initial exploration
        state->epsilon_init = 0.05f;
        state->epsilon_min = 0.005f;     // 0.5% floor
        printf("[ROOM] Initialized %d agents, $50 room seed\n", MAX_AGENTS);
        printf("[ROOM] CB: consec_room_losses=%d max_consecutive_losses=%d circuit_breaker_cycles=%d\n",
               state->consec_room_losses, state->max_consecutive_losses, state->circuit_breaker_cycles);
    } else {
        printf("[ROOM] Restored %d agents from state\n", state->stats.active_agents);
        // ── T001: Validate state on restore — reset if corrupted ──
        if (state->trade_count > MAX_TRADE_HIST || state->trade_count < 0) {
            printf("[ROOM] Corrupted trade_count=%d, resetting to 0\n", state->trade_count);
            state->trade_count = 0;
        }
        if (state->consec_room_losses > 10000) {
            state->consec_room_losses = 0;
        }
    }

    // F10: Compute initial CRC for fresh or restored state
    state_compute_crc(state);

    return ERR_OK;
}

// ── Nanosecond clock ──
static int64_t ns_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// ════════════════════════════════════════════════════════
//  MAIN LOOP
// ════════════════════════════════════════════════════════
int main(void) {
    init_paths();
    load_feature_flags();
    printf("[ROOM] Starting... sizeof(RoomState)=%zu expected_file=%zu\n",
           sizeof(RoomState), sizeof(RoomState));
    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);
    signal(SIGUSR1, handle_sig);

    RoomError err = load_or_init_state();
    if (err != ERR_OK) return 1;

    // ── Load nested model weights ──
    const char *weights_path = "/home/wubu2/.hermes/pm_logs/nested_ht/weights.json";
    g_nested = load_nested_weights(weights_path);
    if (g_nested) {
        printf("[ROOM] Nested models loaded: %d levels\n", g_nested->n_levels);
        for (int i = 0; i < g_nested->n_levels; i++) {
            if (g_nested->mlp_models[i])
                printf("  L%d: %d-min MLP (d=%d,h=%d)\n", i, g_nested->res_minutes[i],
                       g_nested->mlp_models[i]->d, g_nested->mlp_models[i]->h);
            if (g_nested->lr_models[i])
                printf("  L%d: %d-min LR (d=%d)\n", i, g_nested->res_minutes[i],
                       g_nested->lr_models[i]->d);
        }
    } else {
        printf("[ROOM] WARN: No nested weights at %s (room will run without)\n", weights_path);
    }

    printf("[ROOM] Engine starting. Target: <100ms/cycle\n");

    // ── F05: Register graceful shutdown handlers ──
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);
    printf("[ROOM] Graceful shutdown handler installed (SIGTERM/SIGINT)\n");

    // ── Safety: Force-clear circuit breaker on startup ──
    state->circuit_breaker_cycles = 0;
    state->consec_room_losses = 0;
    state->circuit_breaker_count = 0;
    // ── Safety: Force-clear trade_count if corrupted ──
    if (state->trade_count > MAX_TRADE_HIST || state->trade_count < 0) {
        printf("[ROOM] CORRUPTED trade_count=%d, FORCE RESET to 0\n", state->trade_count);
        state->trade_count = 0;
    }

    // ── Main loop ──
    FILE *log = fopen(LOG_PATH, "a");
    if (log) {
        fputs("cycle,window_ts,asset,votes,active,win_rate,sharpe,dd_pct,consensus_spread,room_pnl_pct,room_trades,room_wr,room_cap,slippage$\n", log);
        fclose(log);
    }

    // ── Boot-time hard reset of corruptable fields ──
    // NOTE: trade_count MUST persist across restarts for Darwin trigger (needs 100)
    // NOTE: A57: cycle IS preserved from previous run for continuity
    int64_t prev_cycle = state->cycle;  // Save before reset
    state->cycle = 0;
    state->cycle = prev_cycle;  // Restore to continue count from previous run
    state->vote_count = 0;
    state->consec_room_losses = 0;
    state->circuit_breaker_cycles = 0;
    state->circuit_breaker_count = 0;

    int idle_cycles = 0;
    int dup_cycles = 0;  // A02: Consecutive duplicate timestamps (LIVE_MODE static feed)
    float prev_close = state->prev_close;  // Track for inter-candle comparison (persisted from last process)

    while (running) {
        // ── F05: Check graceful shutdown flag ──
        if (g_shutdown_flag) {
            printf("[ROOM] Shutdown signal received — completing cycle then exiting\n");
            running = 0;
        }

        int64_t cycle_start = ns_now();

        // ── L1: Load market feed ──
        MarketTick tick;
        err = room_feeds_load(&tick);
        if (err != ERR_OK) {
            // Check for graceful data exhaustion (PAPER_MODE max cycles)
            if (err == ERR_DATA_EXHAUSTED) {
                printf("[ROOM] Paper mode: reached cycle %d. Shutting down gracefully.\n", state->cycle);
                break;
            }
            // Retry once immediately — feed bridge may be mid-write
            struct timespec retry_ts = { .tv_sec = 0, .tv_nsec = 100000000 }; // 100ms
            nanosleep(&retry_ts, NULL);
            err = room_feeds_load(&tick);
        }
        if (err != ERR_OK) {
            printf("[FEED] Load err=%d ts=%ld\n", err, (long)tick.window_ts);
            idle_cycles++;
            if (idle_cycles % 60 == 0) {
                printf("[ROOM] No data for %d cycles...\n", idle_cycles);
            }
            // In paper mode: after 100 idle cycles, assume data exhausted and exit
            if (idle_cycles > 100) {
                printf("[ROOM] Data exhausted (idle). Shutting down.\n");
                break;
            }
            // PAPER_MODE: short sleep, LIVE_MODE: 1s
#ifdef PAPER_MODE
            struct timespec ts = { .tv_sec = 0, .tv_nsec = PAPER_PACE_NS };
#else
            struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
#endif
            nanosleep(&ts, NULL);
            continue;
        }
        idle_cycles = 0;

        // Skip if we already processed this window
        if (tick.window_ts == state->stats.last_window_ts) {
            dup_cycles++;
            // LIVE_MODE: wait for new data instead of exiting
            // Feed bridge updates market_feed.json periodically (every 10-60s via cron)
            // Sleep and retry instead of exiting after 3 duplicates
            // Room will process new data when feed_bridge writes fresh timestamp
            if (dup_cycles % 10 == 0) {
                printf("[ROOM] Waiting for new feed data (dup_cycles=%d)...\n", dup_cycles);
            }
            // Sleep longer in live mode to avoid busy loop
            struct timespec ts = { .tv_sec = 2, .tv_nsec = 0 };
            nanosleep(&ts, NULL);
            continue;
        }
        dup_cycles = 0;  // Reset on new unique timestamp
    #ifdef MARKET_MODE
RoomError room_market_apply(VoteRecord *votes, int count,
                            AgentState *agents, int n,
                            TradeRecord *trades, int start_offset,
                            int *new_count, int64_t window_ts);
RoomError room_market_resolve(TradeRecord *trades, int *tcount,
                              const MarketTick *tick,
                              float prev_close,
                              AgentState *agents,
                              int max_trades);
void room_market_stats(RoomState *state);
#endif

    // ── Lock state for writing ──
        state->writing = 1;
        state->current_market = tick;

        // ── L2: Compute features ──
        err = room_features_compute(&tick, &state->features, state);
        if (err != ERR_OK) {
            state->writing = 0;
            continue;
        }

        // ── L2b: Compute nested cascade prediction per market type ──
        for (int mt = 0; mt < NESTED_N_MARKETS; mt++) {
            g_nested_prediction[mt] = compute_nested_prediction(&tick, (MarketType)mt);
        }
        state->nested_prediction = (float)g_nested_prediction[tick.market_type];
        // ── A42: Model checkpointing — save state snapshot every 1000 cycles ──
        if (state->cycle > 0 && state->cycle % 1000 == 0) {
            msync(state, sizeof(RoomState), MS_ASYNC);
            char ckpt_path[600];
            snprintf(ckpt_path, sizeof(ckpt_path), "%s.checkpoint.%d", STATE_PATH, state->cycle);
            FILE *src = fopen(STATE_PATH, "rb");
            FILE *dst = fopen(ckpt_path, "wb");
            if (src && dst) {
                char buf[8192]; size_t n;
                while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
                    fwrite(buf, 1, n, dst);
                fclose(dst); fclose(src);
            } else { if (src) fclose(src); if (dst) fclose(dst); }
            if (state->cycle % 5000 == 0)
                printf("[CKPT] Saved checkpoint: %s\n", ckpt_path);
        }

        if (state->cycle % 100 == 0 && g_nested) {
            double signal = (g_nested_prediction[tick.market_type] - 0.5) * 2.0;
            printf("[NESTED] cycle=%d pred=%.4f signal=%+.4f\n",
                   state->cycle, g_nested_prediction[tick.market_type], signal);
        }

        // ── C25: Check panic stop before voting/trading ──
        if (check_panic(state)) {
            // Panic mode: skip vote, capital allocation, and trading.
            // Features are still computed for monitoring, but no trades placed.
            if (state->cycle % 100 == 0)
                printf("[PANIC] cycle=%d — halted (no trades)\n", state->cycle);
            state->writing = 0;
            goto skip_trading;
        }

        // ── L3: Run vote ──
        int vote_count = 0;
        err = room_vote_run(state->agents, MAX_AGENTS, &state->features,
                            &state->feat_importance, g_agent_market,
                            state->votes, &vote_count, state->epsilon);
        state->vote_count = vote_count;

        // ── A30: Decay epsilon after each cycle ──
        if (state->epsilon > state->epsilon_min) {
            state->epsilon *= 0.9995f;
            if (state->epsilon < state->epsilon_min) state->epsilon = state->epsilon_min;
        }

        // ── L3b: P15 Tailslayer hedging — detect tail risk, scale exposure ──
        {
            float tail = state->features.tail_risk_score;
            float prev_hedge = state->stats.hedge_factor;

            // Compute hedge factor: 1.0 when tail_risk < 0.3, scales down to 0.3 at tail_risk=1.0
            float hf;
            if (tail < 0.3f) {
                hf = 1.0f;  // Normal — no hedging
            } else if (tail < 0.7f) {
                hf = 1.0f - (tail - 0.3f) * 0.75f;  // Gradual: 1.0 -> 0.7
            } else {
                hf = 0.7f - (tail - 0.7f) * 1.33f;  // Aggressive: 0.7 -> 0.3
                if (hf < 0.3f) hf = 0.3f;
            }

            state->stats.tail_risk_score = tail;
            state->stats.hedge_factor = hf;

            if (hf < 1.0f && prev_hedge >= 1.0f) {
                printf("[TAIL] HEDGE ACTIVATED: tail=%.3f hedge=%.3f scaling positions by %.0f%%\n",
                       tail, hf, hf * 100.0f);
                state->stats.hedge_active_cycles = 0;
            } else if (hf >= 1.0f && prev_hedge < 1.0f) {
                printf("[TAIL] HEDGE DEACTIVATED: tail=%.3f normal trading resumed\n", tail);
            }

            if (hf < 1.0f && vote_count > 0) {
                state->stats.hedge_active_cycles++;
                // Beam-search ensemble: scale down all vote position_sizes by hedge_factor
                for (int i = 0; i < vote_count; i++) {
                    state->votes[i].position_size *= hf;
                }
            }
        }

        // ── P23: Volatility scaling — scale position sizes inversely with 30d BTC vol ──
        {
            float vol_pct = state->current_market.btc_30d_volatility;
            if (vol_pct < 1.0f) vol_pct = 50.0f;  // Default if no data
            // BTC typical 30d vol ~50-80%. Scale: 1.0 at 65%, 2.0 at 25%, 0.3 at 150%
            float vol_scalar = 65.0f / fmaxf(vol_pct, 10.0f);
            if (vol_scalar > 2.0f) vol_scalar = 2.0f;  // Max 2x in low vol
            if (vol_scalar < 0.3f) vol_scalar = 0.3f;  // Min 0.3x in high vol
            if (vote_count > 0 && fabsf(vol_scalar - 1.0f) > 0.05f) {
                for (int i = 0; i < vote_count; i++) {
                    state->votes[i].position_size *= vol_scalar;
                }
                if (state->cycle % 100 == 0)
                    printf("[VOL] vol=%.1f%% scalar=%.2f scaling %d votes\n",
                           vol_pct, vol_scalar, vote_count);
            }
        }

        // ── T17: Circuit breaker check ──
        // If in cooldown, skip all trading
        if (state->circuit_breaker_cycles > 0) {
            state->circuit_breaker_cycles--;
            if (state->circuit_breaker_cycles == 0) {
                // Cooldown complete — reset
                state->circuit_breaker_peak = state->room_capital;
                state->consec_room_losses = 0;
                printf("[CB] Cooldown complete. Resuming trading at peak=%.2f\n",
                       state->circuit_breaker_peak);
            } else if (state->circuit_breaker_cycles % 20 == 0) {
                printf("[CB] Cooling down: %d cycles remaining\n",
                       state->circuit_breaker_cycles);
            }
            // Update prev_close for the bridge but skip trading
            prev_close = tick.close;
            goto skip_trading;
        }

        // ── C05: Day-boundary reset for daily_pnl ──
        {
            int current_day = (int)(tick.window_ts / 86400);
            if (current_day != state->last_daily_reset_day) {
                state->daily_pnl = 0.0f;
                state->daily_loss_streak = 0;
                state->last_daily_reset_day = current_day;
            }
        }

        // Check drawdown: if TOTAL AGENT capital dropped > max_drawdown_pct from peak
        float total_agent_cap = 0.0f;
        for (int i = 0; i < MAX_AGENTS; i++) {
            if (state->agents[i].alive && state->agents[i].capital > 0)
                total_agent_cap += state->agents[i].capital;
        }
        if (total_agent_cap > state->circuit_breaker_peak) {
            state->circuit_breaker_peak = total_agent_cap;
        }
        float drawdown = state->circuit_breaker_peak > 0
            ? (state->circuit_breaker_peak - total_agent_cap) / state->circuit_breaker_peak
            : 0.0f;
        if (drawdown > state->max_drawdown_pct && state->circuit_breaker_cycles == 0) {
            state->circuit_breaker_cycles = state->circuit_cooldown_cycles;
            state->circuit_breaker_count++;
            state->circuit_breaker_ts = tick.window_ts;
            // F17: Structured JSON log for circuit breaker
            { FILE *jl = fopen(g_json_log_path, "a");
              if (jl) { fprintf(jl, "{\"ts\":%ld,\"event\":\"circuit_breaker\",\"dd_pct\":%.1f,\"cap\":%.2f,\"peak\":%.2f}\n",
                         (long)tick.window_ts, drawdown*100, total_agent_cap, state->circuit_breaker_peak);
                fclose(jl); } }
            printf("[CB] TRIGGERED! Drawdown=%.1f%% max=%.1f%%. Agent cap $%.2f from peak $%.2f. Cooldown=%d cycles.\n",
                   drawdown * 100, state->max_drawdown_pct * 100,
                   total_agent_cap, state->circuit_breaker_peak,
                   state->circuit_cooldown_cycles);
            // C33: Log unwind priority for open positions
            { float best_pri = 1e9f; int best_idx = -1;
              for (int ui = 0; ui < state->trade_count && ui < MAX_TRADE_HIST; ui++) {
                  float pri = unwind_priority(&state->trades[ui], tick.window_ts);
                  if (pri < best_pri) { best_pri = pri; best_idx = ui; }
              }
              if (best_idx >= 0)
                  printf("[CB] Unwind first: trade[%d] agent=%d pnl=%.2f%%\n",
                         best_idx, state->trades[best_idx].agent_id,
                         state->trades[best_idx].pnl_pct * 100);
            }
            goto skip_trading;
        }

        // C05: Daily loss limit — use total agent capital as baseline
        float total_agent_cap_dd = 0.0f;
        for (int i = 0; i < MAX_AGENTS; i++) {
            if (state->agents[i].alive && state->agents[i].capital > 0)
                total_agent_cap_dd += state->agents[i].capital;
        }
        if (state->daily_pnl < 0 && state->circuit_breaker_cycles == 0 && total_agent_cap_dd > 0) {
            float daily_loss_pct = -state->daily_pnl / total_agent_cap_dd;
            if (daily_loss_pct > state->max_daily_loss_pct) {
                state->circuit_breaker_cycles = state->circuit_cooldown_cycles;
                state->circuit_breaker_count++;
                printf("[CB] TRIGGERED! Daily loss $%.2f (%.1f%% of agent cap). "
                       "Max daily loss=%.0f%%. Cooldown=%d cycles.\n",
                       state->daily_pnl, daily_loss_pct * 100,
                       state->max_daily_loss_pct * 100,
                       state->circuit_cooldown_cycles);
                goto skip_trading;
            }
        }

        // Check consecutive losses — guard: must have at least 1 loss
        if (state->consec_room_losses > 0 && state->consec_room_losses >= state->max_consecutive_losses) {
            state->circuit_breaker_cycles = state->circuit_cooldown_cycles / 2;
            state->circuit_breaker_count++;
            printf("[CB] TRIGGERED! %d consecutive losses. Cooling down %d cycles.\n",
                   state->consec_room_losses, state->circuit_cooldown_cycles / 2);
            goto skip_trading;
        }

        // ── C35: Take-profit at room level ──
        if (!state->room_take_profit_triggered && state->room_capital_peak > 0) {
            float room_profit_pct = (state->room_capital - 50.0f) / 50.0f; // profit from $50 seed
            if (room_profit_pct >= state->room_take_profit_pct) {
                state->room_take_profit_triggered = 1;
                printf("[TAKE_PROFIT] Room hit %.0f%% profit target! Capital=$%.2f (profit=$%.2f) — locking profits, skipping trades\n",
                       state->room_take_profit_pct * 100, state->room_capital, state->room_capital - 50.0f);
                goto skip_trading;
            }
        }

        // ── Kill switch check (SIGUSR1) ──
        if (kill_switch_engaged) {
            printf("[KILL SWITCH] Liquidating %d open positions...\n", state->vote_count);
            // Close all open room trades at current price
            if (state->room_trade.resolved_at < 0) {
                // Force-resolve any open room trade as loss (emergency)
                float exit_px = tick.close;
                float entry_px = state->room_trade.entry_price;
                if (entry_px > 0) {
                    float move_pct = (exit_px - entry_px) / entry_px;
                    state->room_trade.won = (move_pct > 0) == state->room_trade.majority_up;
                    state->room_trade.pnl = state->room_trade.won ? state->room_trade.stake * 0.01f : -state->room_trade.stake * 0.01f;
                    state->room_capital += state->room_trade.pnl;
                    state->daily_pnl += state->room_trade.pnl;
                    state->room_trade.exit_price = exit_px;
                    state->room_trade.resolved_at = tick.window_ts;
                    printf("[KILL SWITCH] Room trade liquidated: PnL=$%.2f\n", state->room_trade.pnl);
                }
            }
            printf("[KILL SWITCH] All positions closed. Shutting down.\n");
            break;
        }

        // ── Room Trade Execution (one per cycle, $50 seed) ──
        // Skip first 1K P2P trades for evolution warm-up (lowered from 10K for live mode).
        // Uses multi-stream expert selection: pick top 100 agents by WR,
        // their votes are diverse across different data streams.
        if (state->trade_count >= 1000 && vote_count > 0) {
            // Use top 100 agents' votes (diverse experts per stream)
            int top_n = 100;
            if (top_n > vote_count) top_n = vote_count;
            int step = vote_count / top_n;
            if (step < 1) step = 1;

            int yv = 0, nv = 0;
            for (int i = 0; i < vote_count; i += step) {
                if (state->votes[i].direction) yv++; else nv++;
            }

            if (yv != nv) {
                bool majority_up = yv > nv;
                bool room_direction = majority_up;

                // ── Nested cascade bias ──
                // Override room direction when nested model signal is confident enough
                // Model is trained to 55.7% WR on 4-hr BTC — overrides noisy 1-min agent votes
                float confidence = (float)(yv > nv ? yv : nv) / (float)(yv + nv);
                confidence = (confidence - 0.5f) * 2.0f;
                if (g_nested) {
                    double nest_signal = (g_nested_prediction[MARKET_CRYPTO] - 0.5) * 2.0;  // -1 to 1
                    if (fabs(nest_signal) > 0.20) {  // threshold: model must be >60% confident
                        bool nest_up = g_nested_prediction[MARKET_CRYPTO] > 0.5;
                        if (nest_up != room_direction) {
                            room_direction = nest_up;
                            confidence = (float)fabs(nest_signal);
                            if (confidence > 1.0f) confidence = 1.0f;
                        }
                    }
                }

                float stake = state->room_capital * (0.01f + confidence * 0.04f) * state->stats.hedge_factor;
                if (stake > state->room_capital * 0.05f) stake = state->room_capital * 0.05f;
                if (stake < 0.01f) stake = 0.01f;
                state->room_capital -= stake;
                // ── T20: Entry slippage on room trade ──
                // ── C27: Widen slippage on weekends (lower liquidity) ──
                float slip_cost;
                { struct tm tm_wk; time_t wt = (time_t)tick.window_ts;
                  localtime_r(&wt, &tm_wk);
                  float wk_mul = (tm_wk.tm_wday == 0 || tm_wk.tm_wday == 6 || is_us_holiday(&tm_wk)) ? SLIPPAGE_WEEKEND_MUL : 1.0f;
                  slip_cost = stake * (SLIPPAGE_BPS * wk_mul + stake * SLIPPAGE_VOL_SCALE * wk_mul) / 10000.0f;
                  // ── C26: Overnight gap risk — add gap charge for non-crypto at market open ──
                  if (tm_wk.tm_hour >= 9 && tm_wk.tm_hour < 10 && tm_wk.tm_wday >= 1 && tm_wk.tm_wday <= 5)
                      slip_cost += stake * OVERNIGHT_GAP_BPS / 10000.0f; }
                if (slip_cost > state->room_capital * 0.5f) slip_cost = state->room_capital * 0.5f;
                if (slip_cost > 0.001f) {
                    state->room_capital -= slip_cost;
                    state->total_slippage_paid += slip_cost;
                    state->slippage_events++;
                }
                state->room_trades++;
                state->room_trade.window_ts = tick.window_ts;
                state->room_trade.yes_votes = yv;
                state->room_trade.no_votes = nv;
                state->room_trade.total_votes = vote_count;
                state->room_trade.majority_up = room_direction;
                state->room_trade.conviction_spread = 1.0f - confidence;
                state->room_trade.stake = stake;
                state->room_trade.entry_price = tick.close;
                state->room_trade.exit_price = 0;
                state->room_trade.won = false;
                state->room_trade.pnl = 0;
                state->room_trade.resolved_at = 0;
            }
        }

        // ── L4a: Resolve room trade (if active from previous cycle) ──
        if (state->room_trade.resolved_at == 0 && prev_close > 0) {
            // Room trade resolves: exit when close > prev_close = yes_won
            bool up = state->room_trade.majority_up;
            bool room_won = (tick.close >= prev_close) == up;

            if (room_won) {
                // Winner: get stake back + profit (binary: 1:1 payout minus taker fee)
                float profit = state->room_trade.stake * (1.0f - TAKER_FEE);
                float gross_ret = state->room_trade.stake + profit;
                // ── T20: Exit slippage on room trade winner ──
                // ── C27: Weekend slippage widening ──
                float exit_slip;
                { struct tm tm_wk2; time_t wt2 = (time_t)tick.window_ts;
                  localtime_r(&wt2, &tm_wk2);
                  float wk2 = (tm_wk2.tm_wday == 0 || tm_wk2.tm_wday == 6 || is_us_holiday(&tm_wk2)) ? SLIPPAGE_WEEKEND_MUL : 1.0f;
                  exit_slip = gross_ret * (SLIPPAGE_BPS * wk2 + gross_ret * SLIPPAGE_VOL_SCALE * wk2) / 10000.0f; }
                state->room_capital += gross_ret - exit_slip;
                state->total_slippage_paid += exit_slip;
                state->slippage_events++;
                state->room_wins++;
                state->room_trade.won = true;
                state->room_trade.pnl = profit;
                state->daily_pnl += state->room_trade.pnl;
                state->consec_room_losses = 0;  // Reset on win
            } else {
                // Loser: lose stake + fee
                state->room_losses++;
                state->room_trade.won = false;
                state->room_trade.pnl = -(state->room_trade.stake * (1.0f + TAKER_FEE));
                state->room_capital += state->room_trade.pnl;  // capital already deducted
                state->daily_pnl += state->room_trade.pnl;
                state->consec_room_losses++;  // Track consecutive losses
            }
            state->room_trade.exit_price = tick.close;
            state->room_trade.resolved_at = tick.window_ts;

            if (state->room_capital > state->room_capital_peak)
                state->room_capital_peak = state->room_capital;
        }

        // ── P27: Concept drift detection — rolling WR on room trades ──
        {
            static int drift_buf[100];  // Ring buffer: 1=win, 0=loss
            static int drift_idx = 0, drift_count = 0;
            if (state->room_trade.won) drift_buf[drift_idx] = 1;
            else drift_buf[drift_idx] = 0;
            drift_idx = (drift_idx + 1) % 100;
            if (drift_count < 100) drift_count++;
            if (drift_count >= 50 && drift_count % 10 == 0) {
                int wins = 0;
                for (int i = 0; i < drift_count; i++) wins += drift_buf[i];
                float rolling_wr = (float)wins / drift_count;
                // Expected WR ~55%. If rolling WR drops below 40% over 50+ trades, flag drift
                if (rolling_wr < 0.40f && state->cycle % 100 == 0) {
                    printf("[DRIFT] Rolling WR=%.1f%% over %d trades — concept drift possible\n",
                           rolling_wr * 100, drift_count);
                } else if (rolling_wr > 0.60f && state->cycle % 100 == 0) {
                    printf("[DRIFT] Rolling WR=%.1f%% over %d trades — regime shift positive\n",
                           rolling_wr * 100, drift_count);
                }
            }
        }

        // ── A18: Cosine learning rate decay ──
        // Decays from 1.0 to LR_MIN over ~100K cycles
        #define LR_CYCLE_DECAY 100000
        #define LR_MIN 0.1f
        #define LR_PI 3.14159265f
        float lr_decay = LR_MIN + (1.0f - LR_MIN) * 0.5f *
                         (1.0f + cosf(LR_PI * (float)(state->cycle % LR_CYCLE_DECAY) / (float)LR_CYCLE_DECAY));

        // ── L4a old: Resolve previous window's P2P agent trades ──
        {
            int prev_tcount = state->trade_count;
            // Only resolve if we have a previous close to compare against
            if (prev_close > 0) {
                room_capital_resolve(state->trades, &prev_tcount, &tick,
                                     prev_close,
                                     state->agents, MAX_TRADE_HIST,
                                     &state->feat_importance,
                                     lr_decay,
                                     state);  // R4: Pass RoomState for circuit breaker
            }
            // ── T20: P2P exit slippage — deduct from resolved winners ──
            if (prev_close > 0) {
                for (int i = 0; i < state->trade_count && i < MAX_TRADE_HIST; i++) {
                    if (state->trades[i].resolved_at == tick.window_ts && state->trades[i].won) {
                        float payout = state->trades[i].position_size * (1.0f + state->trades[i].pnl_pct);
                        if (payout <= 0) continue;
                        // ── C27: Weekend slippage widening ──
                        float slip_pct_wk;
                        { struct tm tm_wk3; time_t wt3 = (time_t)tick.window_ts;
                          localtime_r(&wt3, &tm_wk3);
                          float wk3 = (tm_wk3.tm_wday == 0 || tm_wk3.tm_wday == 6 || is_us_holiday(&tm_wk3)) ? SLIPPAGE_WEEKEND_MUL : 1.0f;
                          slip_pct_wk = (SLIPPAGE_BPS * wk3 + payout * SLIPPAGE_VOL_SCALE * wk3) / 10000.0f; }
                        float slip_cost = payout * slip_pct_wk;
                        if (slip_cost < 0.001f) continue;
                        int aid = state->trades[i].agent_id;
                        if (aid >= 0 && aid < MAX_AGENTS && state->agents[aid].capital >= slip_cost) {
                            state->agents[aid].capital -= slip_cost;
                            state->total_slippage_paid += slip_cost;
                            state->slippage_events++;
                        }
                    }
                }
            }
        }

        // ── L4b: Apply capital allocation for NEW trades ──
        // ── T18: Position limit enforcement ──
        // Compute total capital of alive agents for global position limits
        float total_alive_cap = 0.0f;
        for (int i = 0; i < MAX_AGENTS; i++) {
            if (state->agents[i].alive)
                total_alive_cap += state->agents[i].capital;
        }
        float total_exposure = 0.0f;
        float yes_exposure = 0.0f;   // C36: Directional exposure tracking
        float no_exposure = 0.0f;
        for (int i = 0; i < vote_count; i++) {
            int aid = state->votes[i].agent_id;
            float agent_cap = state->agents[aid].capital;
            if (agent_cap <= 0) continue;

            // Computed stake
            float stake = state->votes[i].position_size * agent_cap;
            // Cap per-agent position to max_position_pct_room of total capital
            float max_stake = total_alive_cap * state->max_position_pct_room;
            if (stake > max_stake) {
                float new_pct = max_stake / agent_cap;
                printf("[LIMIT] Agent %d: stake $%.2f capped to $%.2f (%.2f%% of room)\n",
                       aid, stake, max_stake, state->max_position_pct_room * 100);
                state->votes[i].position_size = new_pct;
                stake = max_stake;
            }

            // C36: Cap per-direction exposure (prevent YES/NO concentration)
            bool vote_yes = state->votes[i].direction;
            float dir_exposure = vote_yes ? yes_exposure : no_exposure;
            float max_dir = total_alive_cap * state->max_direction_pct;
            if (dir_exposure + stake > max_dir) {
                float dir_remaining = max_dir - dir_exposure;
                if (dir_remaining <= 0) {
                    state->votes[i].position_size = 0;
                    printf("[DIR] Agent %d: skipped (%s direction at max %.1f%%)\n",
                           aid, vote_yes ? "YES" : "NO", state->max_direction_pct * 100);
                    continue;
                }
                float new_pct = dir_remaining / agent_cap;
                state->votes[i].position_size = new_pct;
                stake = dir_remaining;
            }

            // Cap total exposure across all agents
            float new_exposure = total_exposure + stake;
            float max_exposure = total_alive_cap * state->max_total_exposure_pct;
            if (new_exposure > max_exposure) {
                float remaining = max_exposure - total_exposure;
                if (remaining <= 0) {
                    state->votes[i].position_size = 0; // Skip this vote
                    printf("[LIMIT] Agent %d: skipped (total exposure capped at %.1f%%)\n",
                           aid, state->max_total_exposure_pct * 100);
                    continue;
                }
                float new_pct = remaining / agent_cap;
                state->votes[i].position_size = new_pct;
                stake = remaining;
            }
            total_exposure += stake;
            if (vote_yes) yes_exposure += stake; else no_exposure += stake;
        }
        state->current_total_exposure = total_exposure;
        state->current_yes_exposure = yes_exposure;
        state->current_no_exposure = no_exposure;
        if (total_exposure > state->peak_total_exposure)
            state->peak_total_exposure = total_exposure;

        int new_trades = 0;
        err = room_capital_apply(state->votes, vote_count, state->agents, MAX_AGENTS,
                                 state->trades, state->trade_count, &new_trades,
                                 tick.window_ts, state->predicted_regime, state);
        // ── T19: Trade rate limiting ──
        if (state->max_trades_per_cycle > 0 && new_trades > state->max_trades_per_cycle) {
            int deferred = new_trades - state->max_trades_per_cycle;
            state->trades_deferred = deferred;
            state->total_trades_deferred += deferred;
            // Roll back deferred trades: return capital to agents whose trades were deferred
            for (int i = state->trade_count + state->max_trades_per_cycle;
                 i < state->trade_count + new_trades && i < MAX_TRADE_HIST; i++) {
                int aid = state->trades[i].agent_id;
                state->agents[aid].capital += state->trades[i].position_size;
                state->agents[aid].trades--;
            }
            new_trades = state->max_trades_per_cycle;
            printf("[QUEUE] %d trades deferred (max %d/cycle). Total deferred: %d\n",
                   deferred, state->max_trades_per_cycle, state->total_trades_deferred);
        } else {
            state->trades_deferred = 0;
        }
        if (new_trades > 0) state->trade_count += new_trades;

        // ── T20: P2P entry slippage — deduct from each new trade's agent capital ──
        {
            int start = state->trade_count - new_trades;
            if (start < 0) start = 0;
            for (int i = start; i < state->trade_count && i < MAX_TRADE_HIST; i++) {
                float stake = state->trades[i].position_size;
                float slip_pct = (SLIPPAGE_BPS + stake * SLIPPAGE_VOL_SCALE) / 10000.0f;
                float slip_cost = stake * slip_pct;
                if (slip_cost < 0.001f) continue;
                int aid = state->trades[i].agent_id;
                if (aid >= 0 && aid < MAX_AGENTS && state->agents[aid].capital >= slip_cost) {
                    state->agents[aid].capital -= slip_cost;
                    state->total_slippage_paid += slip_cost;
                    state->slippage_events++;
                }
            }
        }

        // ── Save close for next cycle's resolution ──
        prev_close = tick.close;
        state->prev_close = prev_close;  // Persist across process restarts

        // ── L5: Darwin evolution (every 100 trades) ──
        if (g_flags.darwin_evolution && state->trade_count > 0 && state->trade_count % 100 == 0) {
            room_darwin_evolve(state->agents, MAX_AGENTS, state->cycle, &state->darwin, g_agent_market);
            // A16: Prune dead features using tracked importance
            prune_dead_features(state->agents, MAX_AGENTS, &state->feat_importance);
            // C19: Compute diversity metrics after evolution
            room_darwin_compute_diversity(state->agents, MAX_AGENTS, &state->stats);
            // ── Loss feedback: save elite engine genomes for trainer hot-start ──
            room_darwin_save_elite(state->agents, MAX_AGENTS, g_agent_market);

            // ── C39: Size scaling — compound position sizes based on agent WR ──
            int scaled = 0;
            for (int i = 0; i < MAX_AGENTS; i++) {
                if (!state->agents[i].alive) continue;
                if (state->agents[i].trades < 20) continue;
                float wr = (float)state->agents[i].wins / state->agents[i].trades;
                float old = state->agents[i].genome.position_size;
                if (wr > 0.55f) {
                    // Winning agent: grow position 10%, cap at 0.50
                    state->agents[i].genome.position_size *= 1.10f;
                    if (state->agents[i].genome.position_size > 0.50f)
                        state->agents[i].genome.position_size = 0.50f;
                } else if (wr < 0.45f) {
                    // Losing agent: shrink position 10%, floor at 0.01
                    state->agents[i].genome.position_size *= 0.90f;
                    if (state->agents[i].genome.position_size < 0.01f)
                        state->agents[i].genome.position_size = 0.01f;
                }
                if (fabsf(state->agents[i].genome.position_size - old) > 0.001f)
                    scaled++;
            }
            if (scaled > 0)
                printf("[SCALE] Size-scaling active: %d/%d agents adjusted\n",
                       scaled, state->stats.active_agents);
        }

        // ── Hot-reload genomes from multi-market trainer (every 1000 cycles) ──
        if (state->cycle > 0 && state->cycle % HOT_RELOAD_CYCLE == 0) {
            hot_reload_genomes(state->agents, MAX_AGENTS);
        }

skip_trading:
        // ── Update stats ──
        state->cycle++;
        state->stats.last_window_ts = tick.window_ts;
        state->last_updated = ns_now();

        // Compute aggregate stats
        RoomStats *s = &state->stats;
        s->cycle_count = state->cycle;

        // Set initial capital on first cycle
        if (s->initial_capital <= 0) {
            s->initial_capital = 0;
            for (int i = 0; i < MAX_AGENTS; i++)
                s->initial_capital += state->agents[i].starting_capital;
        }

        float total_cap = 0.0f;
        float conv_sum = 0.0f;
        float peak = 0.0f;
        int alive_agents = 0;
        for (int i = 0; i < MAX_AGENTS; i++) {
            total_cap += state->agents[i].capital;  // ALL agents, dead or alive
            if (state->agents[i].alive) {
                alive_agents++;
                if (state->agents[i].capital > peak)
                    peak = state->agents[i].capital;
            }
        }
        s->capital_current = total_cap;
        s->active_agents = alive_agents;
        // Track room-level PnL (the real seed money)
        float room_pnl_pct = state->room_capital_peak > 0 ?
            ((state->room_capital - 50.0f) / 50.0f) * 100.0f : 0;
        s->room_pnl_pct = room_pnl_pct;

        // Track per-cycle room return for Sharpe (based on room_capital)
        if (state->prev_room_capital > 0 && state->cycle > 1 && state->room_trades > 0) {
            float cycle_return = (state->room_capital - state->prev_room_capital) / state->prev_room_capital;
            s->cycle_returns[s->return_idx] = cycle_return;
            s->return_idx = (s->return_idx + 1) % 128;
            if (s->return_count < 128) s->return_count++;
        }
        state->prev_room_capital = state->room_capital;

        // Conviction sum for spread calculation
        for (int i = 0; i < vote_count && i < MAX_AGENTS; i++) {
            conv_sum += state->votes[i].conviction;
        }

        if (total_cap > s->capital_peak) s->capital_peak = total_cap;
        if (s->capital_peak > 0)
            s->max_drawdown = (s->capital_peak - total_cap) / s->capital_peak;

        // Win rate from aggregate
        int total_w = 0, total_l = 0;
        for (int i = 0; i < state->trade_count && i < MAX_TRADE_HIST; i++) {
            if (state->trades[i].won) total_w++;
            else total_l++;
        }
        s->trades_total = total_w + total_l;
        s->trades_won = total_w;
        s->trades_lost = total_l;
        if (s->trades_total > 0) s->win_rate = (float)total_w / s->trades_total;

        // Conviction spread
        if (vote_count > 1) {
            float mean = conv_sum / vote_count;
            float var = 0;
            for (int i = 0; i < vote_count; i++) {
                float d = state->votes[i].conviction - mean;
                var += d * d;
            }
            s->consensus_spread = sqrtf(var / vote_count);
        }
        s->voted_this_cycle = vote_count;
        s->avg_conviction = vote_count > 0 ? conv_sum / vote_count : 0;

        // Compute Sharpe ratio from cycle returns (annualized)
        if (s->return_count >= 3) {
            float mean_r = 0, var_r = 0;
            int n = s->return_count < 128 ? s->return_count : 128;
            int base = s->return_count >= 128 ? s->return_idx : 0;
            for (int i = 0; i < n; i++) {
                int idx = (base + i) % 128;
                mean_r += s->cycle_returns[idx];
            }
            mean_r /= n;
            for (int i = 0; i < n; i++) {
                int idx = (base + i) % 128;
                float d = s->cycle_returns[idx] - mean_r;
                var_r += d * d;
            }
            float std_r = sqrtf(var_r / n);
            if (std_r > 1e-10f) {
                // Annualized for 1-min data: 525600 cycles/year
                float periods_per_year = 525600.0f;
                // D51: Subtract risk-free rate (T-bill ~4.5% annualized)
                float rf_per_period = 0.045f / periods_per_year;
                s->sharpe_ratio = ((mean_r - rf_per_period) / std_r) * sqrtf(periods_per_year);
            }
        }

        // ── L6: Write to bridge ──
        state->writing = 0;
        room_bridge_write(state);

        // ── Log to CSV ──
        FILE *log = fopen(LOG_PATH, "a");
        if (log) {
            float room_wr = state->room_wins + state->room_losses > 0 ?
                (float)state->room_wins / (state->room_wins + state->room_losses) : 0;
            fprintf(log, "%d,%ld,%s,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%.4f,%.2f,%.4f\n",
                    state->cycle, tick.window_ts, tick.asset,
                    state->vote_count, s->active_agents,
                    s->win_rate, s->sharpe_ratio, s->max_drawdown,
                    s->consensus_spread, s->room_pnl_pct,
                    state->room_trades, room_wr, state->room_capital,
                    state->total_slippage_paid);
            fclose(log);
        }

        // ── Check timing ──
        int64_t elapsed = ns_now() - cycle_start;
        // ── A43: Training speed benchmark — track cycle latency degradation ──
        { static int64_t sum_elapsed = 0; static int elapsed_samples = 0;
          sum_elapsed += elapsed; elapsed_samples++;
          if (elapsed_samples == 1000) {
              float avg_ms = (float)sum_elapsed / elapsed_samples / 1e6f;
              if (avg_ms > 50.0f)
                  printf("[A43] WARN: avg cycle time %.1fms over 1000 cycles (degrading)\n", avg_ms);
              sum_elapsed = 0; elapsed_samples = 0;
          }
        }
        if (elapsed > 100000000LL) { // >100ms
            printf("[ROOM] WARN: cycle %d took %.1fms (>100ms target)\n",
                   state->cycle, elapsed / 1e6);
        } else if (state->cycle % 100 == 0) {
            printf("[ROOM] cycle=%d agents=%d votes=%d win_rate=%.1f%% cap=$%.4f time=%.1fms\n",
                   state->cycle, s->active_agents, vote_count,
                   s->win_rate * 100, total_cap, elapsed / 1e6);
        }

                // ── A56: Append per-cycle metrics to JSON lines file ──
        if (state->cycle % 10 == 0) {
            FILE *cm = fopen(g_cycle_metrics_path, "a");
            if (cm) {
                fprintf(cm,
                    "{\"cycle\":%d,"
                    "\"agents\":%d,"
                    "\"votes\":%d,"
                    "\"wr\":%.4f,"
                    "\"sharpe\":%.4f,"
                    "\"dd\":%.4f,"
                    "\"cap\":%.2f,"
                    "\"peak_cap\":%.2f,"
                    "\"trades\":%d,"
                    "\"pnl\":%.2f,"
                    "\"epsilon\":%.4f,"
                    "\"genome_div\":%.4f,"
                    "\"weight_div\":%.4f,"
                    "\"ts\":%ld}\n",
                    state->cycle, s->active_agents, vote_count,
                    s->win_rate, s->sharpe_ratio, s->max_drawdown,
                    total_cap, s->capital_peak,
                    state->trade_count, s->room_pnl_pct,
                    state->epsilon, s->genome_diversity, s->weight_diversity,
                    (long)time(NULL));
                fclose(cm);
            }
        }


        // ── Pace: faster for paper mode ──
        int64_t sleep_ns = is_paper_mode() ? (PAPER_PACE_NS - elapsed) : (LIVE_PACE_NS - elapsed);
        if (sleep_ns < 0) sleep_ns = 0;
        if (sleep_ns > 0) {
            struct timespec ts = {
                .tv_sec = sleep_ns / 1000000000LL,
                .tv_nsec = sleep_ns % 1000000000LL
            };
            nanosleep(&ts, NULL);
        }
    }

    printf("\n[ROOM] Shutdown. %d cycles run, %d trades\n",
           state->cycle, state->trade_count);

    // ── F10: Update state CRC before syncing to disk ──
    state_compute_crc(state);

    // ── F05: Flush mmap'd state to disk before unmapping ──
    if (msync(state, sizeof(RoomState), MS_SYNC) != 0) {
        perror("msync");
    } else {
        printf("[ROOM] State synced to disk\n");
    }

    if (g_nested) {
        nested_free(g_nested);
        g_nested = NULL;
        printf("[ROOM] Nested models freed\n");
    }

    munmap(state, sizeof(RoomState));
    close(state_fd);
    return 0;
}
