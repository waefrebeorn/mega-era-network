/**
 * binary_trainer.c — Train classifiers on resolved binary market outcomes
 *
 * For each resolved binary event:
 *   1. Compute features from market probability trajectory
 *   2. Train agents to predict outcome (0 or 1)
 *   3. Darwin evolution on classification accuracy
 *
 * Unlike direction markets (time-series), binary markets are classification:
 *   - Each event is one sample
 *   - Features = probability trajectory statistics
 *   - Target = resolved outcome
 *
 * Data per event: [mean_prob, prob_variance, prob_trend, prob_range, time_span, volume]
 *
 * Compile: gcc -O2 -std=c11 -o binary_trainer binary_trainer.c -lm
 * Usage:   ./binary_trainer <market_csv>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define MAX_EVENTS   10000
#define MAX_AGENTS   500
#define SEED_CAP     50.0
#define MAX_POS_PCT  0.05
#define MIN_STAKE    1.0
#define TAKER_FEE    0.0026
#define N_FEATURES   8

/* A resolved binary event */
typedef struct {
    double mean_prob;       /* Average market probability */
    double prob_variance;   /* Variance of probability */
    double prob_trend;      /* Slope of probability over time */
    double prob_range;      /* Max - Min probability */
    double final_prob;      /* Last known probability */
    double time_span;       /* Duration of market in hours */
    double volume;          /* Total volume */
    int64_t ts;             /* Resolution timestamp */
    int outcome;            /* 0 or 1 */
    char source[64];
} BinaryEvent;

static BinaryEvent g_events[MAX_EVENTS];
static int g_n_events = 0;

/* Agent genome for binary classification */
typedef struct {
    float weights[N_FEATURES];
    float bias;
    float position_size;
    float conviction_threshold;
} BinGenome;

typedef struct {
    int alive;
    double capital;
    double total_pnl;
    int trades;
    int wins;
    int losses;
    float wr;
    BinGenome genome;
} BinAgent;

static BinAgent g_bagents[MAX_AGENTS];

/* Load resolved events from CSV */
static int load_events(const char *filename) {
    char path[512];
    snprintf(path, sizeof(path), "/home/wubu2/.hermes/pm_logs/historical/%s", filename);

    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path); return -1; }

    char buf[512];
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return 0; } /* header */

    /* Group by source to compute per-event features */
    /* For now, each row is treated as an independent event */
    while (fgets(buf, sizeof(buf), f) && g_n_events < MAX_EVENTS) {
        BinaryEvent *e = &g_events[g_n_events];
        double ts, open, high, low, close, volume;
        char src[64] = {0};

        if (sscanf(buf, "%lf,%lf,%lf,%lf,%lf,%lf,%63s",
                   &ts, &open, &high, &low, &close, &volume, src) >= 6) {
            e->ts = (int64_t)ts;
            e->mean_prob = (open + close) / 2.0;
            e->final_prob = close;
            e->prob_range = high - low;
            e->prob_variance = (high - low) * (high - low) / 4.0;
            e->prob_trend = close - open;
            e->volume = volume;
            e->time_span = 1.0;  /* Unknown, default to 1 hour */
            e->outcome = (close > 0.5) ? 1 : 0;
            strncpy(e->source, src, sizeof(e->source) - 1);
            g_n_events++;
        }
    }

    fclose(f);
    printf("[load] %d binary events from %s\n", g_n_events, filename);
    return g_n_events;
}

static void compute_event_features(const BinaryEvent *e, float *feats) {
    feats[0] = (float)e->mean_prob;
    feats[1] = (float)e->prob_variance;
    feats[2] = (float)e->prob_trend;
    feats[3] = (float)e->prob_range;
    feats[4] = (float)e->final_prob;
    feats[5] = (float)log(fmax(1.0, e->volume));
    feats[6] = (float)e->time_span;
    feats[7] = (float)(e->mean_prob * e->mean_prob);  /* Non-linear term */
}

static float bin_predict(const BinAgent *a, const float *feats) {
    float signal = a->genome.bias;
    for (int i = 0; i < N_FEATURES; i++) {
        signal += a->genome.weights[i] * feats[i];
    }
    return signal;
}

static void init_bin_genome(BinGenome *g, unsigned int *seed) {
    for (int i = 0; i < N_FEATURES; i++) {
        g->weights[i] = ((float)rand_r(seed) / RAND_MAX - 0.5f) * 0.1f;
    }
    g->bias = ((float)rand_r(seed) / RAND_MAX - 0.5f) * 0.1f;
    g->position_size = 0.02f + ((float)rand_r(seed) / RAND_MAX) * 0.03f;
    g->conviction_threshold = 0.01f + ((float)rand_r(seed) / RAND_MAX) * 0.05f;
}

static void bin_darwin(BinAgent *agents, int n, unsigned int *seed) {
    int best = -1;
    float best_wr = 0;
    for (int i = 0; i < n; i++) {
        if (agents[i].alive && agents[i].wr > best_wr && agents[i].trades >= 10) {
            best_wr = agents[i].wr;
            best = i;
        }
    }
    if (best < 0) return;

    for (int i = 0; i < n; i++) {
        if (!agents[i].alive) continue;
        if (agents[i].trades < 10) continue;
        if (agents[i].wr < best_wr * 0.7f) {
            /* Crossover + mutate from best */
            int p2 = best;
            for (int t = 0; t < 3; t++) {
                int r = (int)(rand_r(seed) % n);
                if (agents[r].alive && agents[r].wr > agents[p2].wr) p2 = r;
            }
            for (int w = 0; w < N_FEATURES; w++) {
                agents[i].genome.weights[w] = (rand_r(seed) % 2) ?
                    agents[best].genome.weights[w] : agents[p2].genome.weights[w];
                if ((float)rand_r(seed) / RAND_MAX < 0.1f)
                    agents[i].genome.weights[w] += ((float)rand_r(seed) / RAND_MAX - 0.5f) * 0.05f;
            }
            agents[i].genome.bias = agents[best].genome.bias;
            agents[i].capital = SEED_CAP;
            agents[i].wr = 0.5f;
        }
    }
}

static void train_binary(const char *name, int n_agents) {
    printf("\n[binary] === %s: %d events, %d agents ===\n", name, g_n_events, n_agents);

    if (g_n_events < 50) {
        printf("[binary] %s: insufficient events (%d), skipping\n", name, g_n_events);
        return;
    }

    unsigned int seed = (unsigned int)(time(NULL) ^ 42);

    /* Initialize agents */
    int initd = 0;
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (!g_bagents[i].alive) {
            g_bagents[i].alive = 1;
            g_bagents[i].capital = SEED_CAP;
            g_bagents[i].total_pnl = 0;
            g_bagents[i].trades = 0;
            g_bagents[i].wins = 0;
            g_bagents[i].losses = 0;
            g_bagents[i].wr = 0.5f;
            init_bin_genome(&g_bagents[i].genome, &seed);
            initd++;
            if (initd >= n_agents) break;
        }
    }

    /* Split: 70% train, 30% test */
    int n_train = g_n_events * 7 / 10;
    int n_test = g_n_events - n_train;

    /* Training phase */
    for (int epoch = 0; epoch < 5; epoch++) {
        for (int ei = 0; ei < n_train; ei++) {
            BinaryEvent *e = &g_events[ei];
            float feats[N_FEATURES];
            compute_event_features(e, feats);

            for (int i = 0; i < MAX_AGENTS; i++) {
                if (!g_bagents[i].alive) continue;
                if (g_bagents[i].capital < MIN_STAKE) { g_bagents[i].alive = 0; continue; }

                float signal = bin_predict(&g_bagents[i], feats);
                float conviction = fabsf(signal);
                if (conviction < g_bagents[i].genome.conviction_threshold) continue;

                int predict = (signal > 0) ? 1 : 0;
                double pos = g_bagents[i].capital * (double)g_bagents[i].genome.position_size;
                if (pos > g_bagents[i].capital * MAX_POS_PCT) pos = g_bagents[i].capital * MAX_POS_PCT;
                if (pos < MIN_STAKE) pos = MIN_STAKE;
                if (pos > g_bagents[i].capital) pos = g_bagents[i].capital;

                int won = (predict == e->outcome) ? 1 : 0;
                double payout = won ? pos * (1.0 - TAKER_FEE) : -pos;

                g_bagents[i].capital += payout;
                g_bagents[i].total_pnl += payout;
                g_bagents[i].trades++;
                if (won) g_bagents[i].wins++; else g_bagents[i].losses++;
                g_bagents[i].wr = (float)g_bagents[i].wins / (float)g_bagents[i].trades;

                /* Cap */
                if (g_bagents[i].capital > 1e8) g_bagents[i].capital = 1e8;
                if (g_bagents[i].capital < 0) { g_bagents[i].capital = 0; g_bagents[i].alive = 0; }
            }

            /* Darwin every 100 events */
            if (ei % 100 == 0) bin_darwin(g_bagents, MAX_AGENTS, &seed);
        }
    }

    /* Testing phase: only track PnL, don't update weights */
    int test_trades = 0, test_wins = 0;
    double test_pnl = 0;

    for (int ei = n_train; ei < g_n_events; ei++) {
        BinaryEvent *e = &g_events[ei];
        float feats[N_FEATURES];
        compute_event_features(e, feats);

        /* Use best agent */
        int best = -1;
        float best_wr = 0;
        for (int i = 0; i < MAX_AGENTS; i++) {
            if (g_bagents[i].alive && g_bagents[i].wr > best_wr && g_bagents[i].trades >= 10) {
                best_wr = g_bagents[i].wr;
                best = i;
            }
        }
        if (best < 0) continue;

        float signal = bin_predict(&g_bagents[best], feats);
        if (fabsf(signal) < g_bagents[best].genome.conviction_threshold) continue;

        int predict = (signal > 0) ? 1 : 0;
        test_trades++;
        if (predict == e->outcome) { test_wins++; test_pnl += 1.0 * (1.0 - TAKER_FEE); }
        else { test_pnl -= 1.0; }
    }

    /* Results */
    int alive = 0, total_trades = 0, total_wins = 0;
    double total_pnl = 0, best_wr = 0;

    for (int i = 0; i < MAX_AGENTS; i++) {
        if (g_bagents[i].alive && g_bagents[i].trades > 0) {
            alive++;
            total_trades += g_bagents[i].trades;
            total_wins += g_bagents[i].wins;
            total_pnl += g_bagents[i].total_pnl;
            if (g_bagents[i].wr > best_wr) best_wr = g_bagents[i].wr;
        }
    }

    float train_wr = total_trades > 0 ? (float)total_wins / (float)total_trades * 100 : 0;
    float test_wr = test_trades > 0 ? (float)test_wins / (float)test_trades * 100 : 0;

    printf("[binary] %s RESULTS: alive=%d train_WR=%.1f%% (%d trades) test_WR=%.1f%% (%d trades) test_PnL=$%.2f best_WR=%.1f%%\n",
           name, alive, train_wr, total_trades, test_wr, test_trades, test_pnl, best_wr * 100);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <market_s.csv> [market_p.csv] [market_w.csv]\n", argv[0]);
        printf("Trains binary classifiers on resolved outcome data\n");
        return 1;
    }

    printf("=== Binary Market Classifier Training ===\n");

    for (int i = 1; i < argc; i++) {
        /* Reset agents for each market */
        memset(g_bagents, 0, sizeof(g_bagents));
        g_n_events = 0;

        /* Extract market name from filename */
        const char *name = argv[i];
        const char *slash = strrchr(name, '/');
        if (slash) name = slash + 1;

        load_events(name);
        train_binary(name, 200);
    }

    return 0;
}
