/**
 * multi_market_paper.c — Per-market historical paper trading (v3)
 *
 * Key fixes in v3:
 *   - double for accumulated PnL (prevents float overflow on 6.8M trades)
 *   - Per-market cycle caps (BTC=50K, others=10K, binary=5K)
 *   - Binary market resolve: close price IS the probability, outcome is binary
 *   - Logit transform for binary market features (map 0-1 → -∞ to +∞)
 *   - Data parity: each market uses its own CSV with correct price format
 *
 * Compile: gcc -O2 -std=c11 -o multi_market_paper multi_market_paper.c -lm
 * Usage:   ./multi_market_paper
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define MAX_CANDLES     8000000
#define MAX_AGENTS      500
#define SEED_CAP        50.0
#define MAX_POS_PCT     0.03
#define MIN_STAKE       1.0
#define TAKER_FEE       0.0026

/* Per-market cycle caps to prevent float overflow */
#define MAX_CYCLES_CRYPTO       50000
#define MAX_CYCLES_TRADITIONAL  10000
#define MAX_CYCLES_BINARY       5000

typedef enum {
    MKT_CRYPTO = 0,     /* BTC 1-min, direction bet */
    MKT_EQUITY,         /* SPY daily, direction bet */
    MKT_FOREX,          /* DXY daily, direction bet */
    MKT_COMMODITY,      /* Gold/Oil daily, direction bet */
    MKT_BOND,           /* TNX daily, direction bet (normalized 0-1) */
    MKT_VOLATILITY,     /* VIX daily, direction bet (normalized 0-1) */
    MKT_PREDICTION,     /* Polymarket/PredictIt/Manifold, binary outcome */
    MKT_SPORTS,         /* Resolved game outcomes, binary */
    MKT_WEATHER,        /* Binary hot/cold, binary outcome */
    MKT_ELECTION,       /* Election outcomes, binary */
    N_MARKETS
} MarketType;

static const char *MARKET_NAMES[] = {
    "crypto","equity","forex","commodity","bond",
    "volatility","prediction","sports","weather","election"
};

static const int MAX_CYCLES[] = {
    MAX_CYCLES_CRYPTO,      /* crypto: 6.8M candles but cap at 50K */
    MAX_CYCLES_TRADITIONAL, /* equity: 29K */
    MAX_CYCLES_TRADITIONAL, /* forex: 14K */
    MAX_CYCLES_TRADITIONAL, /* commodity: 13K */
    MAX_CYCLES_TRADITIONAL, /* bonds: 14K */
    MAX_CYCLES_TRADITIONAL, /* volatility: 9K */
    MAX_CYCLES_BINARY,      /* prediction: 865K but cap at 5K */
    MAX_CYCLES_BINARY,      /* sports: 8K */
    MAX_CYCLES_BINARY,      /* weather: 13K */
    MAX_CYCLES_BINARY       /* election: 2K */
};

/* Is this a binary outcome market? */
static int is_binary_market(MarketType mt) {
    return (mt == MKT_PREDICTION || mt == MKT_SPORTS ||
            mt == MKT_WEATHER || mt == MKT_ELECTION);
}

typedef struct {
    double open, high, low, close, volume;
    int64_t ts;
} Candle;

typedef struct {
    Candle *candles;
    int count;
    int capacity;
    char name[32];
} MarketDB;

static MarketDB g_markets[N_MARKETS];

typedef struct {
    float weights[17];
    float position_size;
    float conviction_threshold;
    float stop_loss_pct;
    float take_profit_pct;
    float momentum_bias;
    float volume_weight;
    float trend_strength;
} Genome;

typedef struct {
    int alive;
    double capital;          /* Use double to prevent overflow */
    double peak_capital;
    double total_pnl;        /* Use double for accumulated PnL */
    int trades;
    int wins;
    int losses;
    float wr;
    Genome genome;
    float features[17];
    int agent_market;
} Agent;

static Agent g_agents[MAX_AGENTS];

/* ── Logit transform: map probability (0,1) → (-∞, +∞) ── */
static double logit(double p) {
    if (p <= 0.001) p = 0.001;
    if (p >= 0.999) p = 0.999;
    return log(p / (1.0 - p));
}

/* ── Load CSV ── */
static int load_market_csv(MarketType mt, const char *filename) {
    char path[512];
    snprintf(path, sizeof(path), "/home/wubu2/.hermes/pm_logs/historical/%s", filename);

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "[load] Cannot open %s\n", path);
        return -1;
    }

    MarketDB *m = &g_markets[mt];
    strncpy(m->name, MARKET_NAMES[mt], sizeof(m->name));

    int lines = 0;
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) lines++;
    rewind(f);

    m->candles = malloc(sizeof(Candle) * (size_t)(lines + 1));
    if (!m->candles) { fclose(f); return -1; }
    m->capacity = lines;
    m->count = 0;

    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return 0; } /* skip header */

    while (fgets(buf, sizeof(buf), f)) {
        Candle *c = &m->candles[m->count];
        if (sscanf(buf, "%lld,%lf,%lf,%lf,%lf,%lf",
                   (long long*)&c->ts, &c->open, &c->high, &c->low, &c->close, &c->volume) == 6) {
            m->count++;
        }
    }

    fclose(f);
    printf("[load] %s: %d candles from %s\n", m->name, m->count, filename);
    return m->count;
}

/* ── Compute features from candle history ── */
/* CRITICAL: Only uses candles [0..idx-1] to predict direction of candle idx */
static void compute_features(MarketType mt, int idx, float *feats) {
    MarketDB *m = &g_markets[mt];
    memset(feats, 0, sizeof(float) * 17);

    if (idx < 26) return;

    int binary = is_binary_market(mt);
    double price = m->candles[idx-1].close;

    /* For binary markets, apply logit transform to price */
    double trade_price = binary ? logit(price) : price;

    /* feats[0-4]: returns at 1,3,5,10,20 periods */
    if (idx >= 2) {
        double p = binary ? logit(m->candles[idx-2].close) : m->candles[idx-2].close;
        feats[0] = (float)(trade_price - p);
    }
    if (idx >= 4) {
        double p = binary ? logit(m->candles[idx-4].close) : m->candles[idx-4].close;
        feats[1] = (float)(trade_price - p);
    }
    if (idx >= 6) {
        double p = binary ? logit(m->candles[idx-6].close) : m->candles[idx-6].close;
        feats[2] = (float)(trade_price - p);
    }
    if (idx >= 11) {
        double p = binary ? logit(m->candles[idx-11].close) : m->candles[idx-11].close;
        feats[3] = (float)(trade_price - p);
    }
    if (idx >= 21) {
        double p = binary ? logit(m->candles[idx-21].close) : m->candles[idx-21].close;
        feats[4] = (float)(trade_price - p);
    }

    /* feats[5]: volatility */
    if (idx >= 11) {
        double sum = 0, sum2 = 0;
        for (int i = 0; i < 10; i++) {
            int ci = idx - 1 - i;
            int cp = idx - 2 - i;
            if (cp < 0) continue;
            double p1 = binary ? logit(m->candles[ci].close) : m->candles[ci].close;
            double p2 = binary ? logit(m->candles[cp].close) : m->candles[cp].close;
            double r = p1 - p2;
            sum += r;
            sum2 += r * r;
        }
        double mean = sum / 10;
        feats[5] = (float)sqrt(fmax(0, sum2 / 10 - mean * mean));
    }

    /* feats[6]: distance from 20-period mean */
    if (idx >= 21) {
        double sum = 0;
        for (int i = 1; i <= 20; i++) {
            double p = binary ? logit(m->candles[idx-1-i].close) : m->candles[idx-1-i].close;
            sum += p;
        }
        double mean = sum / 20;
        feats[6] = (float)(trade_price - mean);
    }

    /* feats[7]: volume ratio */
    if (idx >= 21) {
        double vol_sum = 0;
        for (int i = 2; i <= 21; i++) vol_sum += m->candles[idx-1-i].volume;
        double vol_avg = vol_sum / 20;
        double vol_cur = m->candles[idx-1].volume;
        feats[7] = (vol_avg > 0) ? (float)(vol_cur / vol_avg) : 1.0f;
    }

    /* feats[8]: range */
    Candle *prev_c = &m->candles[idx-1];
    if (prev_c->high > prev_c->low) {
        feats[8] = (float)((prev_c->high - prev_c->low) / (prev_c->low > 0 ? prev_c->low : 1.0));
    }

    /* feats[9]: close position in range */
    if (prev_c->high > prev_c->low) {
        feats[9] = (float)((prev_c->close - prev_c->low) / (prev_c->high - prev_c->low));
    } else {
        feats[9] = 0.5f;
    }

    /* feats[10]: gap */
    if (idx >= 2) {
        double prev_prev_close = binary ? logit(m->candles[idx-2].close) : m->candles[idx-2].close;
        feats[10] = (float)(prev_c->open - prev_prev_close);
    }

    /* feats[11]: momentum (SMA slope) */
    if (idx >= 11) {
        double sma5 = 0, sma10 = 0;
        for (int i = 1; i <= 5; i++) {
            double p = binary ? logit(m->candles[idx-1-i].close) : m->candles[idx-1-i].close;
            sma5 += p;
        }
        for (int i = 6; i <= 10; i++) {
            double p = binary ? logit(m->candles[idx-1-i].close) : m->candles[idx-1-i].close;
            sma10 += p;
        }
        sma5 /= 5; sma10 /= 5;
        feats[11] = (float)(sma5 - sma10);
    }

    /* feats[12]: RSI approximation */
    if (idx >= 15) {
        double gains = 0, losses = 0;
        for (int i = 1; i <= 14; i++) {
            double p1 = binary ? logit(m->candles[idx-i].close) : m->candles[idx-i].close;
            double p2 = binary ? logit(m->candles[idx-i-1].close) : m->candles[idx-i-1].close;
            double diff = p1 - p2;
            if (diff > 0) gains += diff; else losses -= diff;
        }
        double avg_gain = gains / 14;
        double avg_loss = losses / 14;
        if (avg_loss < 0.0001) feats[12] = 1.0f;
        else {
            double rs = avg_gain / avg_loss;
            feats[12] = (float)(1.0 - 1.0 / (1.0 + rs));
        }
    }

    /* feats[13]: MACD approx */
    if (idx >= 27) {
        double ema12 = binary ? logit(m->candles[idx-26].close) : m->candles[idx-26].close;
        double ema26 = ema12;
        double k12 = 2.0 / 13.0, k26 = 2.0 / 27.0;
        for (int i = 25; i >= 0; i--) {
            double p = binary ? logit(m->candles[idx-1-i].close) : m->candles[idx-1-i].close;
            ema12 = p * k12 + ema12 * (1 - k12);
            ema26 = p * k26 + ema26 * (1 - k26);
        }
        feats[13] = (float)(ema12 - ema26);
    }

    /* feats[14]: Bollinger position */
    if (idx >= 21) {
        double sum = 0, sum2 = 0;
        for (int i = 1; i <= 20; i++) {
            double p = binary ? logit(m->candles[idx-1-i].close) : m->candles[idx-1-i].close;
            sum += p; sum2 += p * p;
        }
        double mean = sum / 20;
        double std = sqrt(fmax(0, sum2 / 20 - mean * mean));
        if (std > 0.001) feats[14] = (float)((trade_price - mean) / (2 * std));
    }

    /* feats[15]: ATR */
    if (idx >= 15) {
        double atr_sum = 0;
        for (int i = 1; i <= 14; i++) {
            double tr = m->candles[idx-i].high - m->candles[idx-i].low;
            atr_sum += tr;
        }
        double atr = atr_sum / 14;
        if (trade_price > 0.001) feats[15] = (float)(atr / fabs(trade_price));
    }

    /* feats[16]: time of day */
    time_t t = (time_t)m->candles[idx-1].ts;
    struct tm *tm = gmtime(&t);
    feats[16] = (float)(tm->tm_hour + tm->tm_min / 60.0) / 24.0f;
}

/* ── Agent prediction ── */
static float agent_predict(Agent *a, const float *feats) {
    float signal = 0;
    for (int i = 0; i < 17; i++) {
        signal += a->genome.weights[i] * feats[i];
    }
    signal += a->genome.momentum_bias * feats[11];
    signal *= (1.0f + a->genome.volume_weight * (feats[7] - 1.0f));
    signal += a->genome.trend_strength * feats[6];
    return signal;
}

/* ── Initialize genome ── */
static void init_genome(Genome *g, unsigned int *seed) {
    for (int i = 0; i < 17; i++) {
        g->weights[i] = ((float)rand_r(seed) / RAND_MAX - 0.5f) * 0.2f;
    }
    g->position_size = 0.01f + ((float)rand_r(seed) / RAND_MAX) * 0.02f;
    g->conviction_threshold = 0.01f + ((float)rand_r(seed) / RAND_MAX) * 0.1f;
    g->stop_loss_pct = 0.01f + ((float)rand_r(seed) / RAND_MAX) * 0.05f;
    g->take_profit_pct = 0.01f + ((float)rand_r(seed) / RAND_MAX) * 0.1f;
    g->momentum_bias = ((float)rand_r(seed) / RAND_MAX - 0.5f) * 0.5f;
    g->volume_weight = ((float)rand_r(seed) / RAND_MAX - 0.5f) * 0.3f;
    g->trend_strength = ((float)rand_r(seed) / RAND_MAX - 0.5f) * 0.5f;
}

/* ── Darwin evolution ── */
static void darwin_evolve(Agent *agents, int n, MarketType mt, unsigned int *seed) {
    int alive_count = 0;
    for (int i = 0; i < n; i++) {
        if (agents[i].alive && agents[i].agent_market == mt) alive_count++;
    }
    if (alive_count < 10) return;

    int best = -1;
    float best_wr = 0;
    for (int i = 0; i < n; i++) {
        if (agents[i].alive && agents[i].agent_market == mt && agents[i].wr > best_wr) {
            best_wr = agents[i].wr;
            best = i;
        }
    }
    if (best < 0) return;

    for (int i = 0; i < n; i++) {
        if (!agents[i].alive || agents[i].agent_market != mt) continue;
        if (agents[i].trades < 20) continue;

        if (agents[i].wr < best_wr * 0.7f) {
            int p1 = best, p2 = best;
            for (int t = 0; t < 3; t++) {
                int r = (int)(rand_r(seed) % n);
                if (agents[r].alive && agents[r].agent_market == mt && agents[r].wr > agents[p2].wr) {
                    p2 = r;
                }
            }

            for (int w = 0; w < 17; w++) {
                agents[i].genome.weights[w] = (rand_r(seed) % 2) ?
                    agents[p1].genome.weights[w] : agents[p2].genome.weights[w];
                if ((float)rand_r(seed) / RAND_MAX < 0.1f) {
                    agents[i].genome.weights[w] += ((float)rand_r(seed) / RAND_MAX - 0.5f) * 0.05f;
                }
            }
            agents[i].genome.position_size = agents[p1].genome.position_size;
            if ((float)rand_r(seed) / RAND_MAX < 0.1f)
                agents[i].genome.position_size += ((float)rand_r(seed) / RAND_MAX - 0.5f) * 0.005f;
            if (agents[i].genome.position_size < 0.005f) agents[i].genome.position_size = 0.005f;
            if (agents[i].genome.position_size > (float)MAX_POS_PCT) agents[i].genome.position_size = (float)MAX_POS_PCT;

            agents[i].capital = SEED_CAP;
            agents[i].peak_capital = SEED_CAP;
            agents[i].wr = 0.5f;
        }
    }
}

/* ── Run paper trading for one market ── */
static void run_market(MarketType mt, int n_agents) {
    MarketDB *m = &g_markets[mt];
    int max_cycles = MAX_CYCLES[mt];

    if (m->count < 100) {
        printf("[paper] %s: insufficient data (%d candles), skipping\n", m->name, m->count);
        return;
    }

    if (max_cycles > m->count - 30) max_cycles = m->count - 30;

    printf("\n[paper] === %s: %d candles, %d agents, %d max_cycles, %s ===\n",
           m->name, m->count, n_agents, max_cycles,
           is_binary_market(mt) ? "BINARY" : "DIRECTION");

    unsigned int seed = (unsigned int)(time(NULL) ^ (mt * 12345));

    /* Initialize agents */
    int agents_initd = 0;
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (!g_agents[i].alive) {
            g_agents[i].alive = 1;
            g_agents[i].capital = SEED_CAP;
            g_agents[i].peak_capital = SEED_CAP;
            g_agents[i].trades = 0;
            g_agents[i].wins = 0;
            g_agents[i].losses = 0;
            g_agents[i].total_pnl = 0.0;
            g_agents[i].wr = 0.5f;
            g_agents[i].agent_market = mt;
            init_genome(&g_agents[i].genome, &seed);
            agents_initd++;
            if (agents_initd >= n_agents) break;
        }
    }

    int warmup = 30;
    int darwin_interval = 500;
    int binary = is_binary_market(mt);

    for (int cycle = warmup; cycle < warmup + max_cycles && cycle < m->count; cycle++) {
        Candle *c = &m->candles[cycle];
        Candle *prev = &m->candles[cycle - 1];

        /* Skip flat candles (no price movement = no signal) */
        if (c->open == c->high && c->high == c->low && c->low == c->close) continue;
        if (prev->open == prev->high && prev->high == prev->low && prev->low == prev->close) continue;

        /* For binary markets, skip if close is exactly 0.5 (no outcome) or too extreme */
        if (binary && (c->close == 0.5 || c->close <= 0.01 || c->close >= 0.99)) continue;

        /* Determine outcome based on market type */
        int won;
        if (binary) {
            won = (c->close > 0.5) ? 1 : 0;
        } else {
            won = (c->close >= prev->close) ? 1 : 0;
        }

        float market_pnl = 0;

        for (int i = 0; i < MAX_AGENTS; i++) {
            if (!g_agents[i].alive || g_agents[i].agent_market != mt) continue;
            if (g_agents[i].capital < MIN_STAKE) {
                g_agents[i].alive = 0;
                continue;
            }

            compute_features(mt, cycle, g_agents[i].features);
            float signal = agent_predict(&g_agents[i], g_agents[i].features);
            float conviction = fabsf(signal);
            if (conviction < g_agents[i].genome.conviction_threshold) continue;

            int predict_up = signal > 0;

            /* Position size */
            double pos = g_agents[i].capital * (double)g_agents[i].genome.position_size;
            if (pos > g_agents[i].capital * MAX_POS_PCT) pos = g_agents[i].capital * MAX_POS_PCT;
            if (pos < MIN_STAKE) pos = MIN_STAKE;
            if (pos > g_agents[i].capital) pos = g_agents[i].capital;

            /* Resolve */
            int trade_won;
            if (binary) {
                /* Binary: predict_up means "outcome will be 1" */
                trade_won = (predict_up == won) ? 1 : 0;
            } else {
                /* Traditional: predict_up means "price goes up" */
                trade_won = (predict_up == won) ? 1 : 0;
            }

            double payout;
            if (trade_won) {
                payout = pos * (1.0 - TAKER_FEE);
            } else {
                payout = -pos;
            }

            g_agents[i].capital += payout;
            g_agents[i].total_pnl += payout;
            g_agents[i].trades++;
            if (trade_won) g_agents[i].wins++;
            else g_agents[i].losses++;
            market_pnl += payout;
        }

        /* Post-trade: update stats, cap overflow, Darwin evolution */
        for (int i = 0; i < MAX_AGENTS; i++) {
            if (!g_agents[i].alive || g_agents[i].agent_market != mt) continue;
            if (g_agents[i].trades > 0) {
                g_agents[i].wr = (float)g_agents[i].wins / (float)g_agents[i].trades;
            }
            if (g_agents[i].capital > g_agents[i].peak_capital)
                g_agents[i].peak_capital = g_agents[i].capital;
            /* Cap to prevent overflow in reporting */
            if (g_agents[i].total_pnl > 1e12) g_agents[i].total_pnl = 1e12;
            if (g_agents[i].total_pnl < -1e12) g_agents[i].total_pnl = -1e12;
            if (g_agents[i].capital > 1e10) g_agents[i].capital = 1e10;
            if (g_agents[i].capital < 0) { g_agents[i].capital = 0; g_agents[i].alive = 0; }
        }
        if ((cycle - warmup) > 0 && (cycle - warmup) % darwin_interval == 0) {
            darwin_evolve(g_agents, MAX_AGENTS, mt, &seed);
        }

        /* Progress report */
        if ((cycle - warmup) % 5000 == 0 && cycle > warmup) {
            int alive = 0;
            double avg_wr = 0, avg_cap = 0;
            for (int i = 0; i < MAX_AGENTS; i++) {
                if (g_agents[i].alive && g_agents[i].agent_market == mt) {
                    alive++;
                    avg_wr += g_agents[i].wr;
                    avg_cap += g_agents[i].capital;
                }
            }
            if (alive > 0) {
                avg_wr /= alive;
                avg_cap /= alive;
                printf("[paper] %s cycle=%d alive=%d avg_WR=%.1f%% avg_cap=$%.2f pnl=$%.2f\n",
                       m->name, cycle, alive, avg_wr * 100, avg_cap, market_pnl);
            }
        }
    }

    /* Final results */
    int alive = 0, total_trades = 0, total_wins = 0;
    double total_cap = 0, total_pnl = 0, best_wr = 0;

    for (int i = 0; i < MAX_AGENTS; i++) {
        if (g_agents[i].agent_market == mt) {
            if (g_agents[i].alive) {
                alive++;
                total_trades += g_agents[i].trades;
                total_wins += g_agents[i].wins;
                total_cap += g_agents[i].capital;
                total_pnl += g_agents[i].total_pnl;
                if (g_agents[i].wr > best_wr && g_agents[i].trades >= 20)
                    best_wr = g_agents[i].wr;
            }
        }
    }

    float overall_wr = total_trades > 0 ? (float)total_wins / (float)total_trades * 100 : 0;
    double avg_cap = alive > 0 ? total_cap / alive : 0;

    printf("[paper] %s RESULTS: alive=%d trades=%d WR=%.1f%% PnL=$%.2f avg_cap=$%.2f best_WR=%.1f%%\n",
           m->name, alive, total_trades, overall_wr, total_pnl, avg_cap, best_wr * 100);
}

int main(void) {
    printf("=== Multi-Market Historical Paper Trading v3 ===\n");
    printf("Payout: 1:1 binary minus %.2f%% taker fee\n", TAKER_FEE * 100);
    printf("Position: 1%%-%.0f%% of capital, min $%.0f\n", MAX_POS_PCT * 100, MIN_STAKE);
    printf("Data: logit transform for binary markets, double PnL, per-market cycle caps\n\n");

    load_market_csv(MKT_CRYPTO, "market_crypto.csv");
    load_market_csv(MKT_EQUITY, "market_equity.csv");
    load_market_csv(MKT_FOREX, "market_forex.csv");
    load_market_csv(MKT_COMMODITY, "market_commodity.csv");
    load_market_csv(MKT_BOND, "market_bonds.csv");
    load_market_csv(MKT_VOLATILITY, "market_volatility.csv");
    load_market_csv(MKT_PREDICTION, "market_prediction.csv");
    load_market_csv(MKT_SPORTS, "market_sports.csv");
    load_market_csv(MKT_WEATHER, "market_weather.csv");
    load_market_csv(MKT_ELECTION, "market_election.csv");

    int n_agents = 200;

    for (int mt = 0; mt < N_MARKETS; mt++) {
        if (g_markets[mt].count > 100) {
            run_market((MarketType)mt, n_agents);
        }
    }

    /* Summary */
    printf("\n=== FINAL SUMMARY ===\n");
    printf("%-12s %8s %8s %8s %12s %12s %6s\n", "Market", "Candles", "Trades", "WR%", "PnL", "AvgCap", "Type");
    printf("--------------------------------------------------------------------\n");

    for (int mt = 0; mt < N_MARKETS; mt++) {
        if (g_markets[mt].count == 0) continue;

        int total_trades = 0, total_wins = 0, alive = 0;
        double total_pnl = 0, total_cap = 0;

        for (int i = 0; i < MAX_AGENTS; i++) {
            if (g_agents[i].agent_market == mt) {
                total_trades += g_agents[i].trades;
                total_wins += g_agents[i].wins;
                total_pnl += g_agents[i].total_pnl;
                if (g_agents[i].alive) {
                    alive++;
                    total_cap += g_agents[i].capital;
                }
            }
        }

        float wr = total_trades > 0 ? (float)total_wins / (float)total_trades * 100 : 0;
        double avg_cap = alive > 0 ? total_cap / alive : 0;

        printf("%-12s %8d %8d %7.1f%% %12.2f %12.2f %6s\n",
               g_markets[mt].name, g_markets[mt].count, total_trades, wr,
               total_pnl, avg_cap, is_binary_market((MarketType)mt) ? "BIN" : "DIR");
    }

    for (int mt = 0; mt < N_MARKETS; mt++) {
        if (g_markets[mt].candles) free(g_markets[mt].candles);
    }

    return 0;
}
