/**
 * live_trader.c — Paper-to-IRL Bridge: $50/Day Survival Engine
 *
 * PHILOSOPHY:
 * Paper is the gym. Real markets are the game. Never deploy unproven
 * strategies. Loss is death — one bad day kills the trader.
 *
 * ARCHITECTURE:
 *   1. Paper traders run continuously (10K agents, $50 seed each)
 *   2. Survivor queue: top-N paper traders by fitness graduate to "live candidate"
 *   3. Live candidate runs on REAL data (Kraken 1-min) with REAL position sizing
 *   4. Human confirms each trade via Telegram before execution
 *   5. Loss = death: any paper trader with negative PnL is permanently excluded
 *   6. $50/day allocation: only the single best candidate gets funded each day
 *
 * SAFETY RULES:
 *   - Max 1 live trade per 5-min window (Polymarket resolution cadence)
 *   - Max 20% of wallet per trade (Kelly cap)
 *   - $2.50 minimum order (Polymarket minimum)
 *   - Stop-loss: if daily PnL < -10%, halt all trading for 24h
 *   - Circuit breaker: if 3 consecutive losses, return to paper-only mode
 *   - Human confirmation required: no autonomous live trading
 *
 * COMPILE:
 *   gcc -O3 -march=native -std=c11 -D_POSIX_C_SOURCE=199309L \
 *       live_trader.c -o live_trader -lm -lsqlite3 -lcurl
 *
 * USAGE:
 *   ./live_trader --mode paper       # Paper training only (default)
 *   ./live_trader --mode candidate   # Live candidate (real data, no orders)
 *   ./live_trader --mode live        # Live with human confirmation
 *   ./live_trader --status           # Print survivor queue + PnL
 */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include <sqlite3.h>
#include <curl/curl.h>
#include <jansson.h>
#include "types.h"
#include "snowball.h"

// ── Mode ──
typedef enum { MODE_PAPER, MODE_CANDIDATE, MODE_LIVE } TraderMode;

// ── Survivor State ──
typedef struct {
    int     agent_id;           // Which genome from paper engine
    float   paper_pnl;          // Total paper PnL
    float   paper_wr;           // Paper win rate
    int     paper_trades;       // Total paper trades
    float   live_pnl;           // Real-money PnL (0 if not yet live)
    int     live_trades;        // Real-money trade count
    int     consecutive_losses; // Rolling consecutive loss count
    int     alive;              // 1 = active, 0 = dead (loss=death)
    int64_t last_trade_ts;      // Timestamp of last trade
    char    status[16];         // "paper", "candidate", "live", "dead"
} SurvivorEntry;

#define MAX_SURVIVORS 20
#define DAILY_BUDGET 50.0f
#define MAX_DAILY_LOSS_PCT 0.10f
#define CONSECUTIVE_LOSS_LIMIT 3
#define MIN_PAPER_TRADES 50      // Must have 50+ paper trades to graduate
#define MIN_PAPER_WR 0.52        // Must have 52%+ WR to graduate
#define MIN_PAPER_PNL 10.0f      // Must have $10+ paper profit to graduate

static volatile sig_atomic_t g_running = 1;
static void handle_signal(int sig) { (void)sig; g_running = 0; }

// ── HTTP helper (same pattern as exchange_api.c) ──
typedef struct { char *data; size_t len; } http_buf_t;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *ud) {
    size_t total = size * nmemb;
    http_buf_t *buf = (http_buf_t*)ud;
    char *np = realloc(buf->data, buf->len + total + 1);
    if (!np) return 0;
    buf->data = np;
    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

static char *http_get(const char *url, long timeout_sec) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    http_buf_t buf = {NULL, 0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "live-trader/1.0");
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) { free(buf.data); return NULL; }
    return buf.data;
}

// ── Kraken ticker fetch ──
static int fetch_kraken_ticker(float *bid, float *ask, float *last) {
    char *json = http_get("https://api.kraken.com/0/public/Ticker?pair=XXBTZUSD", 10);
    if (!json) return -1;
    json_error_t err;
    json_t *root = json_loads(json, 0, &err);
    free(json);
    if (!root) return -1;

    json_t *result = json_object_get(root, "result");
    if (!result) { json_decref(root); return -1; }

    // Kraken uses "XXBTZUSD" as key
    json_t *pair = json_object_get(result, "XXBTZUSD");
    if (!pair) { json_decref(root); return -1; }

    json_t *b = json_object_get(pair, "b");  // bid
    json_t *a = json_object_get(pair, "a");  // ask
    json_t *c = json_object_get(pair, "c");  // last trade

    if (b && a && c) {
        *bid = atof(json_string_value(json_array_get(b, 0)));
        *ask = atof(json_string_value(json_array_get(a, 0)));
        *last = atof(json_string_value(json_array_get(c, 0)));
        json_decref(root);
        return 0;
    }

    json_decref(root);
    return -1;
}

// ── Survivor queue management ──
static SurvivorEntry g_survivors[MAX_SURVIVORS];
static int g_n_survivors = 0;

static void load_survivor_queue(void) {
    // Read from paper engine state file
    const char *state_path = "/home/wubu2/.hermes/pm_logs/c_room/room_state_paper.bin";
    FILE *f = fopen(state_path, "rb");
    if (!f) {
        fprintf(stderr, "[LIVE] No paper state at %s — running paper-only\n", state_path);
        return;
    }

    // Read RoomState header to get agent data
    // We need: agent_id, paper_pnl (total_pnl), paper_wr (win_rate_ema),
    //           paper_trades (trades), capital
    // RoomState layout: magic(4) + state_version(4) + state_crc(4) + ...
    // Agents start at offset after RoomState header fields

    // Simplified: read top agents by fitness from the mmap'd state
    // For now, scan the state file for agent data
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < (long)sizeof(RoomState)) {
        fprintf(stderr, "[LIVE] State file too small (%ld bytes)\n", sz);
        fclose(f);
        return;
    }

    RoomState *state = malloc(sizeof(RoomState));
    if (!state) { fclose(f); return; }

    if (fread(state, sizeof(RoomState), 1, f) != 1) {
        fprintf(stderr, "[LIVE] Failed to read state\n");
        free(state); fclose(f); return;
    }
    fclose(f);

    // Collect top agents by fitness = WR * sqrt(trades) * log(capital)
    typedef struct { int id; float fitness; float pnl; float wr; int trades; } AgentRank;
    AgentRank ranks[MAX_AGENTS];
    int n_ranks = 0;

    for (int i = 0; i < MAX_AGENTS; i++) {
        if (!state->agents[i].alive) continue;
        if (state->agents[i].trades < MIN_PAPER_TRADES) continue;

        AgentState *a = &state->agents[i];
        float wr = a->win_rate_ema;
        int tr = a->trades;
        float cap = a->capital;

        if (wr < MIN_PAPER_WR) continue;
        if (a->total_pnl < MIN_PAPER_PNL) continue;

        float fitness = wr * sqrtf((float)tr) * logf(cap + 1.0f);
        ranks[n_ranks].id = i;
        ranks[n_ranks].fitness = fitness;
        ranks[n_ranks].pnl = a->total_pnl;
        ranks[n_ranks].wr = wr;
        ranks[n_ranks].trades = tr;
        n_ranks++;
    }

    // Sort by fitness descending (insertion sort, n is small)
    for (int i = 1; i < n_ranks; i++) {
        AgentRank key = ranks[i];
        int j = i - 1;
        while (j >= 0 && ranks[j].fitness < key.fitness) {
            ranks[j + 1] = ranks[j];
            j--;
        }
        ranks[j + 1] = key;
    }

    // Fill survivor queue with top N
    g_n_survivors = 0;
    for (int i = 0; i < n_ranks && g_n_survivors < MAX_SURVIVORS; i++) {
        SurvivorEntry *s = &g_survivors[g_n_survivors];
        s->agent_id = ranks[i].id;
        s->paper_pnl = ranks[i].pnl;
        s->paper_wr = ranks[i].wr;
        s->paper_trades = ranks[i].trades;
        s->live_pnl = 0.0f;
        s->live_trades = 0;
        s->consecutive_losses = 0;
        s->alive = 1;
        s->last_trade_ts = 0;
        strncpy(s->status, "paper", sizeof(s->status));
        g_n_survivors++;
    }

    printf("[LIVE] Loaded %d survivors from paper engine (scanned %d agents)\n",
           g_n_survivors, n_ranks);
    if (g_n_survivors > 0) {
        printf("[LIVE] Top candidate: agent #%d PnL=$%.2f WR=%.1f%% trades=%d\n",
               g_survivors[0].agent_id, g_survivors[0].paper_pnl,
               g_survivors[0].paper_wr * 100, g_survivors[0].paper_trades);
    }

    free(state);
}

// ── Daily PnL tracking ──
typedef struct {
    int64_t day_ts;         // Start of day timestamp
    float   starting_cap;   // Capital at start of day
    float   current_cap;    // Current capital
    float   daily_pnl;      // PnL today
    int     trades_today;   // Number of trades today
    int     halted;         // 1 = trading halted (loss limit hit)
} DailyState;

static DailyState g_daily = {0};

static void reset_daily_state(float starting_cap) {
    g_daily.day_ts = time(NULL);
    g_daily.starting_cap = starting_cap;
    g_daily.current_cap = starting_cap;
    g_daily.daily_pnl = 0.0f;
    g_daily.trades_today = 0;
    g_daily.halted = 0;
}

static int check_daily_loss_limit(void) {
    if (g_daily.starting_cap <= 0) return 0;
    float loss_pct = -g_daily.daily_pnl / g_daily.starting_cap;
    if (loss_pct > MAX_DAILY_LOSS_PCT) {
        fprintf(stderr, "[LIVE] DAILY LOSS LIMIT HIT: %.1f%% > %.1f%% — HALTING\n",
                loss_pct * 100, MAX_DAILY_LOSS_PCT * 100);
        g_daily.halted = 1;
        return 1;
    }
    return 0;
}

// ── Signal computation (same 7-indicator as pm_live_clob.py) ──
typedef struct {
    float window_delta;     // Price change over window
    float micro_momentum;   // Short-term momentum
    float acceleration;     // Second derivative
    float volume_confirm;   // Volume alignment with price
    float rsi;              // RSI(14)
    float spread;           // Bid-ask spread
    float conviction;       // Agent's conviction (0-1)
} LiveSignal;

static LiveSignal compute_signal(float *prices, int n, float bid, float ask) {
    LiveSignal sig = {0};
    if (n < 14) return sig;

    // Window delta: last price vs price 5 bars ago
    sig.window_delta = (prices[n-1] - prices[n-6]) / prices[n-6];

    // Micro momentum: last 3 bars
    sig.micro_momentum = (prices[n-1] - prices[n-4]) / prices[n-4];

    // Acceleration: change in momentum
    float mom1 = (prices[n-1] - prices[n-3]) / prices[n-3];
    float mom2 = (prices[n-3] - prices[n-5]) / prices[n-5];
    sig.acceleration = mom1 - mom2;

    // RSI(14)
    float gains = 0, losses = 0;
    for (int i = n - 14; i < n; i++) {
        float d = prices[i+1] - prices[i];
        if (d > 0) gains += d; else losses -= d;
    }
    float rs = losses > 0.001f ? gains / losses : 100.0f;
    sig.rsi = 100.0f - 100.0f / (1.0f + rs);

    // Spread
    sig.spread = (ask - bid) / bid;

    // Conviction from agent weights (simplified: use signal strength)
    float raw = sig.window_delta * 0.3f + sig.micro_momentum * 0.3f +
                sig.acceleration * 0.2f + (sig.rsi - 50.0f) / 50.0f * 0.2f;
    sig.conviction = 1.0f / (1.0f + expf(-raw * 5.0f));  // sigmoid

    return sig;
}

// ── Trade decision ──
typedef struct {
    int   direction;    // 1 = YES/UP, 0 = NO/DOWN
    float stake;        // Dollar amount
    float confidence;   // 0-1
    char  reason[128];  // Human-readable reason
} TradeDecision;

static TradeDecision make_decision(LiveSignal *sig, float wallet_usd) {
    TradeDecision d = {0};

    // Direction: UP if momentum + delta positive
    float score = sig->window_delta * 0.3f + sig->micro_momentum * 0.3f +
                  sig->acceleration * 0.2f + (sig->rsi - 50.0f) / 50.0f * 0.2f;
    d.direction = score > 0 ? 1 : 0;
    d.confidence = sig->conviction;

    // Stake: Kelly-inspired, max 20% of wallet, min $2.50
    float kelly = 2.0f * d.confidence - 1.0f;  // f* = 2p - 1 for even-money
    if (kelly < 0) kelly = 0.05f;  // Minimal exploration
    d.stake = wallet_usd * kelly * 0.5f;  // Half-Kelly
    if (d.stake > wallet_usd * 0.20f) d.stake = wallet_usd * 0.20f;
    if (d.stake < 2.50f) d.stake = 2.50f;
    if (d.stake > wallet_usd) d.stake = wallet_usd;

    snprintf(d.reason, sizeof(d.reason),
             "delta=%+.4f mom=%+.4f acc=%+.4f rsi=%.1f conf=%.0f%%",
             sig->window_delta, sig->micro_momentum, sig->acceleration,
             sig->rsi, d.confidence * 100);

    return d;
}

// ── Human confirmation via Telegram ──
static int request_human_confirmation(TradeDecision *d, float wallet_usd) {
    // Write pending trade to file for Telegram bot to pick up
    FILE *f = fopen("/home/wubu2/money-room/data/pending_trade.json", "w");
    if (!f) return -1;

    fprintf(f, "{\n");
    fprintf(f, "  \"action\": \"%s\",\n", d->direction ? "BUY_YES" : "BUY_NO");
    fprintf(f, "  \"stake\": %.2f,\n", d->stake);
    fprintf(f, "  \"confidence\": %.2f,\n", d->confidence);
    fprintf(f, "  \"wallet_usd\": %.2f,\n", wallet_usd);
    fprintf(f, "  \"reason\": \"%s\",\n", d->reason);
    fprintf(f, "  \"timestamp\": %ld,\n", (long)time(NULL));
    fprintf(f, "  \"status\": \"PENDING_CONFIRMATION\"\n");
    fprintf(f, "}\n");
    fclose(f);

    printf("[LIVE] Trade pending confirmation: %s $%.2f (%.0f%% conf) — %s\n",
           d->direction ? "BUY_YES" : "BUY_NO", d->stake,
           d->confidence * 100, d->reason);
    printf("[LIVE] Written to data/pending_trade.json — awaiting human approval\n");

    return 0;
}

// ── Log trade to SQLite ──
static void log_trade(int agent_id, int direction, float stake, float pnl,
                      const char *status) {
    sqlite3 *db;
    if (sqlite3_open("/home/wubu2/money-room/data/live_trades.db", &db) != SQLITE_OK) return;

    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS live_trades ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  agent_id INTEGER,"
        "  direction INTEGER,"  // 1=YES, 0=NO
        "  stake REAL,"
        "  pnl REAL,"
        "  status TEXT,"         // PENDING, FILLED, REJECTED, EXPIRED
        "  created_at INTEGER,"
        "  resolved_at INTEGER"
        ");", NULL, NULL, NULL);

    const char *sql = "INSERT INTO live_trades "
                      "(agent_id, direction, stake, pnl, status, created_at) "
                      "VALUES (?, ?, ?, ?, ?, ?)";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, agent_id);
        sqlite3_bind_int(stmt, 2, direction);
        sqlite3_bind_double(stmt, 3, stake);
        sqlite3_bind_double(stmt, 4, pnl);
        sqlite3_bind_text(stmt, 5, status, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 6, (sqlite3_int64)time(NULL));
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

// ── Main loop ──
int main(int argc, char **argv) {
    TraderMode mode = MODE_PAPER;
    if (argc > 1) {
        if (strcmp(argv[1], "--mode") == 0 && argc > 2) {
            if (strcmp(argv[2], "candidate") == 0) mode = MODE_CANDIDATE;
            else if (strcmp(argv[2], "live") == 0) mode = MODE_LIVE;
        } else if (strcmp(argv[1], "--status") == 0) {
            load_survivor_queue();
            printf("\n=== SURVIVOR QUEUE ===\n");
            printf("%-6s %-10s %-10s %-8s %-10s %-8s %-6s\n",
                   "Agent", "Paper$PnL", "Paper WR", "Trades", "Live$PnL", "Status", "Alive");
            for (int i = 0; i < g_n_survivors; i++) {
                SurvivorEntry *s = &g_survivors[i];
                printf("%-6d $%-9.2f %-9.1f%% %-8d $%-9.2f %-8s %s\n",
                       s->agent_id, s->paper_pnl, s->paper_wr * 100,
                       s->paper_trades, s->live_pnl, s->status,
                       s->alive ? "YES" : "DEAD");
            }
            return 0;
        }
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("[LIVE] Starting in mode: %s\n",
           mode == MODE_PAPER ? "PAPER" : mode == MODE_CANDIDATE ? "CANDIDATE" : "LIVE");

    // Load survivor queue from paper engine
    load_survivor_queue();

    if (g_n_survivors == 0) {
        printf("[LIVE] No survivors qualified — paper-only mode\n");
        printf("[LIVE] Requirements: %d+ trades, %.0f%%+ WR, $%.0f+ PnL\n",
               MIN_PAPER_TRADES, MIN_PAPER_WR * 100, MIN_PAPER_PNL);
    }

    // Initialize snowball fund state ($50 seed)
    SnowballState snowball = snowball_init(DAILY_BUDGET);
    printf("[LIVE] Snowball: Tier=%s Capital=$%.2f\n",
           TIERS[snowball.tier].name, snowball.capital);

    // Initialize daily state
    reset_daily_state(DAILY_BUDGET);

    // Price history for signal computation
    float prices[1000];
    int n_prices = 0;

    // Main loop: 5-minute windows
    int64_t last_window = 0;
    const int WINDOW_SEC = 300;  // 5 minutes

    while (g_running) {
        int64_t now = time(NULL);

        // Update snowball day tracking
        snowball_new_day(&snowball, now);

        // Fetch live ticker
        float bid, ask, last;
        if (fetch_kraken_ticker(&bid, &ask, &last) != 0) {
            fprintf(stderr, "[LIVE] Failed to fetch Kraken ticker — retrying in 30s\n");
            sleep(30);
            continue;
        }

        // Update price history
        if (n_prices < 1000) {
            prices[n_prices++] = last;
        } else {
            memmove(prices, prices + 1, sizeof(float) * 999);
            prices[999] = last;
        }

        // Check if we're in a new 5-min window
        int64_t window = now / WINDOW_SEC;
        if (window == last_window) {
            sleep(10);  // Wait for next window
            continue;
        }
        last_window = window;

        // Check snowball trading rules (tier-based limits)
        if (!snowball_can_trade(&snowball)) {
            printf("[LIVE] Snowball halt: tier=%s cooldown=%d consec_losses=%d tier_drop=%d\n",
                   TIERS[snowball.tier].name, snowball.cooldown,
                   snowball.consec_losses, snowball.tier_drop);
            continue;
        }

        // Check daily halt (legacy, snowball supersedes)
        if (g_daily.halted) {
            int64_t day_start = now - (now % 86400);
            if (day_start > g_daily.day_ts) {
                printf("[LIVE] New day — resetting daily state\n");
                reset_daily_state(g_daily.current_cap);
            } else {
                printf("[LIVE] Daily halt active — skipping window %lld\n", (long long)window);
                continue;
            }
        }

        // Compute signal
        LiveSignal sig = compute_signal(prices, n_prices, bid, ask);

        // Make trade decision using snowball tier for position sizing
        TradeDecision d = make_decision(&sig, snowball.capital);

        // Cap stake to snowball tier max
        float max_stake = snowball_max_stake(&snowball);
        if (d.stake > max_stake) d.stake = max_stake;
        if (d.stake < 2.50f) d.stake = 2.50f;  // Polymarket minimum

        // Only trade if confidence > 55%
        if (d.confidence < 0.55f) {
            printf("[LIVE] Low confidence (%.0f%%) — skipping window %lld\n",
                   d.confidence * 100, (long long)window);
            continue;
        }

        if (mode == MODE_PAPER) {
            // Paper mode: simulate the trade, track PnL
            printf("[LIVE-PAPER] Would trade: %s $%.2f (%.0f%%) tier=%s — %s\n",
                   d.direction ? "BUY_YES" : "BUY_NO", d.stake,
                   d.confidence * 100, TIERS[snowball.tier].name, d.reason);
            log_trade(g_n_survivors > 0 ? g_survivors[0].agent_id : -1,
                      d.direction, d.stake, 0, "PAPER_SIM");
        } else if (mode == MODE_CANDIDATE) {
            // Candidate mode: real data, real signal, but no orders
            printf("[LIVE-CANDIDATE] Signal: %s $%.2f (%.0f%%) tier=%s — %s\n",
                   d.direction ? "BUY_YES" : "BUY_NO", d.stake,
                   d.confidence * 100, TIERS[snowball.tier].name, d.reason);
            log_trade(g_n_survivors > 0 ? g_survivors[0].agent_id : -1,
                      d.direction, d.stake, 0, "CANDIDATE");
        } else if (mode == MODE_LIVE) {
            // Live mode: request human confirmation
            if (g_n_survivors == 0) {
                fprintf(stderr, "[LIVE] No survivors qualified — cannot trade live\n");
                continue;
            }

            // Check consecutive losses
            if (g_survivors[0].consecutive_losses >= CONSECUTIVE_LOSS_LIMIT) {
                fprintf(stderr, "[LIVE] %d consecutive losses — returning to paper mode\n",
                        g_survivors[0].consecutive_losses);
                g_survivors[0].alive = 0;
                strncpy(g_survivors[0].status, "dead", sizeof(g_survivors[0].status));
                continue;
            }

            request_human_confirmation(&d, snowball.capital);
            log_trade(g_survivors[0].agent_id, d.direction, d.stake, 0, "PENDING");
        }

        g_daily.trades_today++;
    }

    printf("\n[LIVE] Shutdown. Daily PnL: $%.2f (%d trades)\n",
           g_daily.daily_pnl, g_daily.trades_today);
    printf("[LIVE] Snowball: Tier=%s Capital=$%.2f Peak=$%.2f Withdrawn=$%.2f\n",
           TIERS[snowball.tier].name, snowball.capital,
           snowball.peak_capital, snowball.total_withdrawn);
    return 0;
}
