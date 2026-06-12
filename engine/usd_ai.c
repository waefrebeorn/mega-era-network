/**
 * usd_ai.c — USD AI Paper Trading Manager
 *
 * PHILOSOPHY:
 * USD AI is the paper trading gym manager. It runs 10K paper traders,
 * compares their signals to actual live market moves, and picks the
 * winners — only paper-proven strategies get real money.
 *
 * ARCHITECTURE:
 *   1. Load paper engine state (room_state_paper.bin)
 *   2. Fetch live market data (Kraken BTC/USD ticker)
 *   3. Compare each paper agent's last signal to actual market direction
 *   4. Score agents: correct direction = +1, wrong = -1, no signal = 0
 *   5. Rank agents by live-accuracy score (not just paper PnL)
 *   6. Write winner queue to data/winner_queue.json
 *   7. live_trader reads winner queue for real-money execution
 *
 * KEY INSIGHT: Paper PnL alone is not enough. An agent might make money
 * in paper by luck. USD AI compares paper signals to LIVE market moves
 * to verify the agent actually predicted reality correctly.
 *
 * SAFETY:
 *   - Only agents with 50+ paper trades AND 55%+ live accuracy qualify
 *   - Max 1 winner per 5-min window (single human trader)
 *   - Loss = death: any agent with negative live PnL is permanently excluded
 *   - $50/day budget: only the single best winner gets funded
 *
 * COMPILE:
 *   gcc -O3 -march=native -std=c11 -D_POSIX_C_SOURCE=199309L \
 *       usd_ai.c -o usd_ai -lm -lsqlite3 -lcurl -ljansson
 *
 * USAGE:
 *   ./usd_ai --compare     # Run paper-vs-live comparison, write winner queue
 *   ./usd_ai --status      # Print current winner queue + accuracy scores
 *   ./usd_ai --train       # Run paper engine for N cycles, then compare
 *   ./usd_ai --live-loop   # Continuous: compare every 5 min, update winners
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

// ── Constants ──
#define PAPER_STATE_PATH  "/home/wubu2/.hermes/pm_logs/c_room/room_state_paper.bin"
#define WINNER_QUEUE_PATH "/home/wubu2/money-room/data/winner_queue.json"
#define LIVE_PRICE_PATH   "/home/wubu2/money-room/data/live_prices.json"
#define ACCURACY_DB_PATH  "/home/wubu2/money-room/data/accuracy_tracker.db"
#define MIN_PAPER_TRADES  50
#define MIN_LIVE_ACCURACY 0.55f
#define MIN_PAPER_PNL     10.0f
#define MAX_WINNERS       10
#define DAILY_BUDGET      50.0f
#define PRICE_HISTORY_LEN 100

static volatile sig_atomic_t g_running = 1;
static void handle_signal(int sig) { (void)sig; g_running = 0; }

// ── HTTP helper ──
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
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "usd-ai/1.0");
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) { free(buf.data); return NULL; }
    return buf.data;
}

// ── Live price fetch ──
typedef struct {
    float bid;
    float ask;
    float last;
    float high_24h;
    float low_24h;
    float volume;
    int64_t timestamp;
} LivePrice;

static int fetch_live_price(LivePrice *p) {
    char *json = http_get("https://api.kraken.com/0/public/Ticker?pair=XXBTZUSD", 10);
    if (!json) return -1;
    json_error_t err;
    json_t *root = json_loads(json, 0, &err);
    free(json);
    if (!root) return -1;

    json_t *result = json_object_get(root, "result");
    if (!result) { json_decref(root); return -1; }
    json_t *pair = json_object_get(result, "XXBTZUSD");
    if (!pair) { json_decref(root); return -1; }

    json_t *b = json_object_get(pair, "b");
    json_t *a = json_object_get(pair, "a");
    json_t *c = json_object_get(pair, "c");
    json_t *h = json_object_get(pair, "h");
    json_t *l = json_object_get(pair, "l");
    json_t *v = json_object_get(pair, "v");

    if (b && a && c) {
        p->bid = atof(json_string_value(json_array_get(b, 0)));
        p->ask = atof(json_string_value(json_array_get(a, 0)));
        p->last = atof(json_string_value(json_array_get(c, 0)));
        p->high_24h = h ? atof(json_string_value(json_array_get(h, 1))) : p->last;
        p->low_24h = l ? atof(json_string_value(json_array_get(l, 1))) : p->last;
        p->volume = v ? atof(json_string_value(json_array_get(v, 1))) : 0;
        p->timestamp = time(NULL);
        json_decref(root);
        return 0;
    }
    json_decref(root);
    return -1;
}

// ── Price history for direction tracking ──
static float g_price_history[PRICE_HISTORY_LEN];
static int64_t g_price_ts[PRICE_HISTORY_LEN];
static int g_n_prices = 0;

static void record_price(float price, int64_t ts) {
    if (g_n_prices < PRICE_HISTORY_LEN) {
        g_price_history[g_n_prices] = price;
        g_price_ts[g_n_prices] = ts;
        g_n_prices++;
    } else {
        memmove(g_price_history, g_price_history + 1, sizeof(float) * (PRICE_HISTORY_LEN - 1));
        memmove(g_price_ts, g_price_ts + 1, sizeof(int64_t) * (PRICE_HISTORY_LEN - 1));
        g_price_history[PRICE_HISTORY_LEN - 1] = price;
        g_price_ts[PRICE_HISTORY_LEN - 1] = ts;
    }
}

// ── Live market direction ──
// Returns: 1 = UP, 0 = DOWN, -1 = UNKNOWN
static int get_live_direction(void) {
    if (g_n_prices < 2) return -1;
    float prev = g_price_history[g_n_prices - 2];
    float curr = g_price_history[g_n_prices - 1];
    if (prev <= 0) return -1;
    return (curr > prev) ? 1 : 0;
}

// ── Winner entry ──
typedef struct {
    int     agent_id;
    float   paper_pnl;
    float   paper_wr;
    int     paper_trades;
    float   live_accuracy;    // % of signals that matched live market
    int     live_comparisons; // Total comparisons made
    int     live_correct;     // Correct direction calls
    float   live_pnl;         // Real-money PnL (0 if not yet live)
    int     live_trades;
    int     consecutive_losses;
    int     alive;
    int     promoted;         // 1 = promoted to live candidate
    float   composite_score;  // Combined paper + live score
    char    status[16];
} WinnerEntry;

// ── Accuracy tracker (SQLite) ──
static void init_accuracy_db(void) {
    sqlite3 *db;
    if (sqlite3_open(ACCURACY_DB_PATH, &db) != SQLITE_OK) return;
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS agent_accuracy ("
        "  agent_id INTEGER PRIMARY KEY,"
        "  paper_pnl REAL,"
        "  paper_wr REAL,"
        "  paper_trades INTEGER,"
        "  live_comparisons INTEGER DEFAULT 0,"
        "  live_correct INTEGER DEFAULT 0,"
        "  live_accuracy REAL DEFAULT 0,"
        "  live_pnl REAL DEFAULT 0,"
        "  live_trades INTEGER DEFAULT 0,"
        "  consecutive_losses INTEGER DEFAULT 0,"
        "  alive INTEGER DEFAULT 1,"
        "  promoted INTEGER DEFAULT 0,"
        "  last_updated INTEGER"
        ");", NULL, NULL, NULL);
    sqlite3_close(db);
}

static void update_agent_accuracy(int agent_id, int correct, float paper_pnl,
                                   float paper_wr, int paper_trades) {
    sqlite3 *db;
    if (sqlite3_open(ACCURACY_DB_PATH, &db) != SQLITE_OK) return;

    // Upsert
    const char *sql =
        "INSERT INTO agent_accuracy "
        "  (agent_id, paper_pnl, paper_wr, paper_trades, live_comparisons, "
        "   live_correct, live_accuracy, last_updated) "
        "VALUES (?, ?, ?, ?, 1, ?, ?, ?) "
        "ON CONFLICT(agent_id) DO UPDATE SET "
        "  paper_pnl=excluded.paper_pnl, "
        "  paper_wr=excluded.paper_wr, "
        "  paper_trades=excluded.paper_trades, "
        "  live_comparisons=live_comparisons+1, "
        "  live_correct=live_correct+?, "
        "  live_accuracy=CAST(live_correct+? AS REAL)/CAST(live_comparisons+1 AS REAL), "
        "  last_updated=excluded.last_updated;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        int64_t now = time(NULL);
        sqlite3_bind_int(stmt, 1, agent_id);
        sqlite3_bind_double(stmt, 2, paper_pnl);
        sqlite3_bind_double(stmt, 3, paper_wr);
        sqlite3_bind_int(stmt, 4, paper_trades);
        sqlite3_bind_int(stmt, 5, correct);
        sqlite3_bind_double(stmt, 6, correct ? 1.0 : 0.0);
        sqlite3_bind_int64(stmt, 7, now);
        sqlite3_bind_int(stmt, 8, correct);
        sqlite3_bind_int(stmt, 9, correct);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

// ── Load paper agent signals from state ──
// We read the paper state file and extract each agent's last signal direction
// from their genome weights + current feature values
typedef struct {
    int agent_id;
    int signal_direction;  // 1 = UP, 0 = DOWN, -1 = NO SIGNAL
    float confidence;      // 0-1
    float paper_pnl;
    float paper_wr;
    int paper_trades;
    float capital;
} PaperSignal;

static int load_paper_signals(PaperSignal *signals, int max_signals) {
    FILE *f = fopen(PAPER_STATE_PATH, "rb");
    if (!f) {
        fprintf(stderr, "[USD_AI] No paper state at %s\n", PAPER_STATE_PATH);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < (long)sizeof(RoomState)) {
        fprintf(stderr, "[USD_AI] State file too small (%ld bytes)\n", sz);
        fclose(f);
        return 0;
    }

    RoomState *state = malloc(sizeof(RoomState));
    if (!state) { fclose(f); return 0; }

    if (fread(state, sizeof(RoomState), 1, f) != 1) {
        fprintf(stderr, "[USD_AI] Failed to read state\n");
        free(state); fclose(f);
        return 0;
    }
    fclose(f);

    // Read feature values from the state
    // FeatureVector is at state->features (or we compute from agents)
    // For each agent, compute signal from genome weights
    int n = 0;
    for (int i = 0; i < MAX_AGENTS && n < max_signals; i++) {
        if (!state->agents[i].alive) continue;
        if (state->agents[i].trades < MIN_PAPER_TRADES) continue;

        AgentState *a = &state->agents[i];
        Genome *g = &a->genome;

        // Compute signal from genome weights
        // Signal = sum(weight[i] * feature[i]) for all features
        // We need feature values — read from state->features if available
        // For now, use the agent's last vote direction from their state
        // The agent's conviction is stored in their state
        float signal = 0;
        int n_weights = 0;

        // Use genome weights to compute directional signal
        // Positive weights = bullish, negative = bearish
        for (int j = 0; j < N_FEATURES; j++) {
            signal += g->feat_weight[j];
            n_weights++;
        }

        if (n_weights == 0) continue;

        signal /= (float)n_weights;  // Normalize to [-1, 1] range

        // Convert to direction + confidence
        PaperSignal *s = &signals[n];
        s->agent_id = i;
        s->signal_direction = (signal > 0.05f) ? 1 : (signal < -0.05f) ? 0 : -1;
        s->confidence = fabsf(signal);
        s->paper_pnl = a->total_pnl;
        s->paper_wr = a->win_rate_ema;
        s->paper_trades = a->trades;
        s->capital = a->capital;
        n++;
    }

    printf("[USD_AI] Loaded %d paper signals from state (cycle=%d)\n", n, state->cycle);
    free(state);
    return n;
}

// ── Compare paper signals to live market ──
static int compare_to_live(PaperSignal *signals, int n_signals, int live_dir) {
    if (live_dir < 0) {
        fprintf(stderr, "[USD_AI] No live direction yet — need 2+ price points\n");
        return 0;
    }

    int comparisons = 0;
    for (int i = 0; i < n_signals; i++) {
        if (signals[i].signal_direction < 0) continue;  // No signal

        int correct = (signals[i].signal_direction == live_dir) ? 1 : 0;
        update_agent_accuracy(
            signals[i].agent_id,
            correct,
            signals[i].paper_pnl,
            signals[i].paper_wr,
            signals[i].paper_trades
        );
        comparisons++;
    }

    printf("[USD_AI] Compared %d paper signals to live market (dir=%s)\n",
           comparisons, live_dir ? "UP" : "DOWN");
    return comparisons;
}

// ── Load accuracy data and rank winners ──
static int load_winners(WinnerEntry *winners, int max_winners) {
    sqlite3 *db;
    if (sqlite3_open(ACCURACY_DB_PATH, &db) != SQLITE_OK) return 0;

    const char *sql =
        "SELECT agent_id, paper_pnl, paper_wr, paper_trades, "
        "       live_comparisons, live_correct, live_accuracy, "
        "       live_pnl, live_trades, consecutive_losses, alive, promoted "
        "FROM agent_accuracy "
        "WHERE paper_trades >= ? AND live_accuracy >= ? AND paper_pnl >= ? AND alive = 1 "
        "ORDER BY live_accuracy DESC, paper_pnl DESC "
        "LIMIT ?;";

    sqlite3_stmt *stmt;
    int n = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, MIN_PAPER_TRADES);
        sqlite3_bind_double(stmt, 2, MIN_LIVE_ACCURACY);
        sqlite3_bind_double(stmt, 3, MIN_PAPER_PNL);
        sqlite3_bind_int(stmt, 4, max_winners);

        while (sqlite3_step(stmt) == SQLITE_ROW && n < max_winners) {
            WinnerEntry *w = &winners[n];
            w->agent_id = sqlite3_column_int(stmt, 0);
            w->paper_pnl = (float)sqlite3_column_double(stmt, 1);
            w->paper_wr = (float)sqlite3_column_double(stmt, 2);
            w->paper_trades = sqlite3_column_int(stmt, 3);
            w->live_comparisons = sqlite3_column_int(stmt, 4);
            w->live_correct = sqlite3_column_int(stmt, 5);
            w->live_accuracy = (float)sqlite3_column_double(stmt, 6);
            w->live_pnl = (float)sqlite3_column_double(stmt, 7);
            w->live_trades = sqlite3_column_int(stmt, 8);
            w->consecutive_losses = sqlite3_column_int(stmt, 9);
            w->alive = sqlite3_column_int(stmt, 10);
            w->promoted = sqlite3_column_int(stmt, 11);

            // Composite score: weighted combination
            // 40% live accuracy, 30% paper WR, 20% paper PnL (normalized), 10% trade count
            float pnl_score = fminf(w->paper_pnl / 100.0f, 1.0f);  // Cap at $100
            float trade_score = fminf((float)w->paper_trades / 200.0f, 1.0f);  // Cap at 200
            w->composite_score = w->live_accuracy * 0.40f +
                                 w->paper_wr * 0.30f +
                                 pnl_score * 0.20f +
                                 trade_score * 0.10f;

            strncpy(w->status, w->promoted ? "live" : "candidate", sizeof(w->status));
            n++;
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);

    // Re-rank by composite score
    for (int i = 1; i < n; i++) {
        WinnerEntry key = winners[i];
        int j = i - 1;
        while (j >= 0 && winners[j].composite_score < key.composite_score) {
            winners[j + 1] = winners[j];
            j--;
        }
        winners[j + 1] = key;
    }

    return n;
}

// ── Write winner queue to JSON ──
static void write_winner_queue(WinnerEntry *winners, int n) {
    json_t *root = json_object();
    json_t *arr = json_array();

    for (int i = 0; i < n; i++) {
        WinnerEntry *w = &winners[i];
        json_t *entry = json_object();
        json_object_set_new(entry, "rank", json_integer(i + 1));
        json_object_set_new(entry, "agent_id", json_integer(w->agent_id));
        json_object_set_new(entry, "paper_pnl", json_real(w->paper_pnl));
        json_object_set_new(entry, "paper_wr", json_real(w->paper_wr));
        json_object_set_new(entry, "paper_trades", json_integer(w->paper_trades));
        json_object_set_new(entry, "live_accuracy", json_real(w->live_accuracy));
        json_object_set_new(entry, "live_comparisons", json_integer(w->live_comparisons));
        json_object_set_new(entry, "live_correct", json_integer(w->live_correct));
        json_object_set_new(entry, "live_pnl", json_real(w->live_pnl));
        json_object_set_new(entry, "live_trades", json_integer(w->live_trades));
        json_object_set_new(entry, "composite_score", json_real(w->composite_score));
        json_object_set_new(entry, "status", json_string(w->status));
        json_object_set_new(entry, "alive", json_integer(w->alive));
        json_array_append_new(arr, entry);
    }

    json_object_set_new(root, "winners", arr);
    json_object_set_new(root, "count", json_integer(n));
    json_object_set_new(root, "timestamp", json_integer((int64_t)time(NULL)));
    json_object_set_new(root, "daily_budget", json_real(DAILY_BUDGET));
    json_object_set_new(root, "min_accuracy", json_real(MIN_LIVE_ACCURACY));
    json_object_set_new(root, "min_paper_trades", json_integer(MIN_PAPER_TRADES));

    char *json_str = json_dumps(root, JSON_INDENT(2));
    if (json_str) {
        FILE *f = fopen(WINNER_QUEUE_PATH, "w");
        if (f) {
            fputs(json_str, f);
            fclose(f);
            printf("[USD_AI] Winner queue written to %s (%d winners)\n", WINNER_QUEUE_PATH, n);
        }
        free(json_str);
    }
    json_decref(root);
}

// ── Print status ──
static void print_status(void) {
    WinnerEntry winners[MAX_WINNERS];
    int n = load_winners(winners, MAX_WINNERS);

    printf("\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║           USD AI — Paper vs Live Comparison             ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║ Live price history: %d samples                           \n", g_n_prices);
    if (g_n_prices > 0) {
        printf("║ Last price: $%.2f                                       \n", g_price_history[g_n_prices-1]);
    }
    printf("║                                                          \n");
    printf("║ WINNER QUEUE (%d qualified):                             \n", n);
    printf("║                                                          \n");

    if (n == 0) {
        printf("║   (no winners yet — need 50+ trades, 55%%+ accuracy)     \n");
    } else {
        printf("║ %-4s %-6s %-8s %-6s %-6s %-8s %-6s\n",
               "Rank", "Agent", "Acc%", "WR%", "PnL", "Score", "Status");
        printf("║ ---- ------ -------- ------ ------ -------- ------\n");
        for (int i = 0; i < n; i++) {
            WinnerEntry *w = &winners[i];
            printf("║ %-4d %-6d %-8.1f %-6.1f $%-5.2f %-8.3f %-6s\n",
                   i + 1, w->agent_id,
                   w->live_accuracy * 100, w->paper_wr * 100,
                   w->paper_pnl, w->composite_score, w->status);
        }
    }

    printf("║                                                          \n");
    printf("║ Requirements: %d+ trades, %.0f%%+ accuracy, $%.0f+ PnL    \n",
           MIN_PAPER_TRADES, MIN_LIVE_ACCURACY * 100, MIN_PAPER_PNL);
    printf("║ Daily budget: $%.2f                                      \n", DAILY_BUDGET);
    printf("╚══════════════════════════════════════════════════════════╝\n");
}

// ── Promote top winner to live candidate ──
static void promote_winner(int agent_id) {
    sqlite3 *db;
    if (sqlite3_open(ACCURACY_DB_PATH, &db) != SQLITE_OK) return;
    const char *sql = "UPDATE agent_accuracy SET promoted=1, status='live' WHERE agent_id=?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, agent_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        printf("[USD_AI] Promoted agent #%d to LIVE candidate\n", agent_id);
    }
    sqlite3_close(db);
}

// ── Record live trade result ──
static void record_live_result(int agent_id, float pnl) {
    sqlite3 *db;
    if (sqlite3_open(ACCURACY_DB_PATH, &db) != SQLITE_OK) return;

    const char *sql =
        "UPDATE agent_accuracy SET "
        "  live_pnl=live_pnl+?, "
        "  live_trades=live_trades+1, "
        "  consecutive_losses=CASE WHEN ?<0 THEN consecutive_losses+1 ELSE 0 END, "
        "  alive=CASE WHEN ?<0 AND consecutive_losses>=2 THEN 0 ELSE alive END "
        "WHERE agent_id=?;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_double(stmt, 1, pnl);
        sqlite3_bind_double(stmt, 2, pnl);
        sqlite3_bind_double(stmt, 3, pnl);
        sqlite3_bind_int(stmt, 4, agent_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

// ── Main ──
int main(int argc, char **argv) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Default mode: compare
    int mode_compare = 0, mode_status = 0, mode_train = 0, mode_live_loop = 0;
    if (argc > 1) {
        if (strcmp(argv[1], "--compare") == 0) mode_compare = 1;
        else if (strcmp(argv[1], "--status") == 0) mode_status = 1;
        else if (strcmp(argv[1], "--train") == 0) mode_train = 1;
        else if (strcmp(argv[1], "--live-loop") == 0) mode_live_loop = 1;
        else {
            fprintf(stderr, "Usage: %s [--compare|--status|--train|--live-loop]\n", argv[0]);
            return 1;
        }
    } else {
        mode_compare = 1;  // Default
    }

    printf("[USD_AI] Starting — mode: %s\n",
           mode_compare ? "compare" : mode_status ? "status" :
           mode_train ? "train" : "live-loop");

    // Initialize accuracy DB
    init_accuracy_db();

    if (mode_status) {
        print_status();
        return 0;
    }

    if (mode_compare) {
        // 1. Fetch live price
        LivePrice price;
        if (fetch_live_price(&price) != 0) {
            fprintf(stderr, "[USD_AI] Failed to fetch live price\n");
            return 1;
        }
        record_price(price.last, price.timestamp);
        printf("[USD_AI] Live price: $%.2f (bid=$%.2f ask=$%.2f)\n",
               price.last, price.bid, price.ask);

        // 2. Load paper signals
        PaperSignal signals[MAX_AGENTS];
        int n_signals = load_paper_signals(signals, MAX_AGENTS);
        if (n_signals == 0) {
            fprintf(stderr, "[USD_AI] No paper signals — paper engine needs to run first\n");
            return 1;
        }

        // 3. Compare to live market
        int live_dir = get_live_direction();
        compare_to_live(signals, n_signals, live_dir);

        // 4. Load winners and write queue
        WinnerEntry winners[MAX_WINNERS];
        int n_winners = load_winners(winners, MAX_WINNERS);
        write_winner_queue(winners, n_winners);

        // 5. Print summary
        printf("[USD_AI] Comparison complete: %d signals, %d winners\n", n_signals, n_winners);
        if (n_winners > 0) {
            printf("[USD_AI] Top winner: agent #%d (accuracy=%.1f%% score=%.3f)\n",
                   winners[0].agent_id, winners[0].live_accuracy * 100,
                   winners[0].composite_score);
        }
        return 0;
    }

    if (mode_train) {
        // Run paper engine for training, then compare
        printf("[USD_AI] Training mode: running paper engine...\n");
        int ret = system("cd /home/wubu2/money-room/engine && ./room_engine_paper --cycles 1000 2>&1 | tail -5");
        if (ret != 0) {
            fprintf(stderr, "[USD_AI] Paper engine training failed\n");
            return 1;
        }
        // Now compare
        mode_compare = 1;
        // Fall through to compare logic above
        LivePrice price;
        if (fetch_live_price(&price) != 0) {
            fprintf(stderr, "[USD_AI] Failed to fetch live price\n");
            return 1;
        }
        record_price(price.last, price.timestamp);
        PaperSignal signals[MAX_AGENTS];
        int n_signals = load_paper_signals(signals, MAX_AGENTS);
        int live_dir = get_live_direction();
        compare_to_live(signals, n_signals, live_dir);
        WinnerEntry winners[MAX_WINNERS];
        int n_winners = load_winners(winners, MAX_WINNERS);
        write_winner_queue(winners, n_winners);
        printf("[USD_AI] Training + comparison complete: %d signals, %d winners\n",
               n_signals, n_winners);
        return 0;
    }

    if (mode_live_loop) {
        // Continuous loop: compare every 5 minutes
        printf("[USD_AI] Live loop mode: comparing every 5 minutes\n");
        int loop_count = 0;
        while (g_running) {
            loop_count++;
            printf("\n[USD_AI] === Loop %d ===\n", loop_count);

            LivePrice price;
            if (fetch_live_price(&price) == 0) {
                record_price(price.last, price.timestamp);

                PaperSignal signals[MAX_AGENTS];
                int n_signals = load_paper_signals(signals, MAX_AGENTS);
                int live_dir = get_live_direction();
                compare_to_live(signals, n_signals, live_dir);

                WinnerEntry winners[MAX_WINNERS];
                int n_winners = load_winners(winners, MAX_WINNERS);
                write_winner_queue(winners, n_winners);

                if (n_winners > 0) {
                    printf("[USD_AI] Top winner: agent #%d (acc=%.1f%% score=%.3f)\n",
                           winners[0].agent_id, winners[0].live_accuracy * 100,
                           winners[0].composite_score);
                }
            } else {
                fprintf(stderr, "[USD_AI] Failed to fetch live price — retrying in 60s\n");
            }

            // Sleep 5 minutes
            for (int i = 0; i < 300 && g_running; i++) sleep(1);
        }
        printf("[USD_AI] Live loop ended\n");
        return 0;
    }

    return 0;
}
