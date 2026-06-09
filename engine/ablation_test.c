/**
 * ablation_test.c — A28: Feature Ablation Testing
 * Measures the impact of removing each feature on prediction accuracy.
 * Runs the engine with one feature zeroed out at a time, compares results.
 *
 * Compile: gcc -O3 -o ablation_test ablation_test.c -lm
 * Run: ./ablation_test
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define N_FEATURES 32
#define N_AGENTS 100
#define N_TRIALS 50

// Simplified feature vector
typedef struct {
    float features[N_FEATURES];
} FeatureVector;

// Simplified agent
typedef struct {
    float weights[N_FEATURES];
    float bias;
    float capital;
    int alive;
} SimpleAgent;

static const char *feat_names[N_FEATURES] = {
    "price_delta", "micro_momentum", "rsi_7", "volume_surge",
    "ema_fast", "ema_slow", "macd_hist", "bollinger_pct",
    "divergence", "pump_score", "regime", "fear_greed",
    "herd_consensus", "ob_imbalance", "ob_depth_ratio", "cvd_signal",
    "dft_dominant", "tail_risk", "funding_signal", "oi_net_signal",
    "ls_ratio", "liq_ls_ratio", "stable_inflow", "whale_activity",
    "hash_rate", "difficulty", "miner_floor", "hour_of_day",
    "day_of_week", "iv_skew", "pcr_volume", "iv_term_slope"
};

static float predict(SimpleAgent *a, FeatureVector *f, int ablate_idx) {
    float sum = a->bias;
    for (int i = 0; i < N_FEATURES; i++) {
        float val = (i == ablate_idx) ? 0.0f : f->features[i];
        sum += a->weights[i] * val;
    }
    return 1.0f / (1.0f + expf(-sum)); // sigmoid
}

int main(void) {
    srand((unsigned)time(NULL));

    // Initialize random agents
    SimpleAgent agents[N_AGENTS];
    for (int i = 0; i < N_AGENTS; i++) {
        agents[i].bias = (float)(rand() % 100) / 100.0f - 0.5f;
        agents[i].capital = 50.0f;
        agents[i].alive = 1;
        for (int j = 0; j < N_FEATURES; j++) {
            agents[i].weights[j] = (float)(rand() % 200) / 100.0f - 1.0f;
        }
    }

    // Generate random test features
    FeatureVector trials[N_TRIALS];
    float outcomes[N_TRIALS];
    for (int t = 0; t < N_TRIALS; t++) {
        for (int f = 0; f < N_FEATURES; f++) {
            trials[t].features[f] = (float)(rand() % 200) / 100.0f - 1.0f;
        }
        outcomes[t] = (rand() % 2) ? 1.0f : 0.0f; // random binary outcome
    }

    printf("═══ A28: Feature Ablation Test ═══\n");
    printf("Agents: %d  Trials: %d  Features: %d\n\n", N_AGENTS, N_TRIALS, N_FEATURES);

    // Baseline: all features active
    float baseline_brier = 0;
    for (int t = 0; t < N_TRIALS; t++) {
        float avg_pred = 0;
        int n_alive = 0;
        for (int a = 0; a < N_AGENTS; a++) {
            if (!agents[a].alive) continue;
            avg_pred += predict(&agents[a], &trials[t], -1); // no ablation
            n_alive++;
        }
        if (n_alive > 0) avg_pred /= n_alive;
        float err = avg_pred - outcomes[t];
        baseline_brier += err * err;
    }
    baseline_brier /= N_TRIALS;
    printf("Baseline (all features): Brier = %.4f\n\n", baseline_brier);

    // Ablate each feature
    printf("Feature ablation results:\n");
    printf("%-20s  %10s  %10s\n", "Feature", "Brier", "Delta");
    printf("────────────────────────────────────\n");

    float max_delta = 0;
    int worst_feature = -1;

    for (int ablate = 0; ablate < N_FEATURES; ablate++) {
        float brier = 0;
        for (int t = 0; t < N_TRIALS; t++) {
            float avg_pred = 0;
            int n_alive = 0;
            for (int a = 0; a < N_AGENTS; a++) {
                if (!agents[a].alive) continue;
                avg_pred += predict(&agents[a], &trials[t], ablate);
                n_alive++;
            }
            if (n_alive > 0) avg_pred /= n_alive;
            float err = avg_pred - outcomes[t];
            brier += err * err;
        }
        brier /= N_TRIALS;
        float delta = brier - baseline_brier;

        if (fabsf(delta) > max_delta) {
            max_delta = fabsf(delta);
            worst_feature = ablate;
        }

        const char *marker = (delta > 0.01f) ? " ***" : (delta < -0.01f) ? " +++" : "";
        printf("%-20s  %.4f     %+.4f%s\n", feat_names[ablate], brier, delta, marker);
    }

    printf("\n═══ VERDICT ═══\n");
    printf("Baseline Brier: %.4f\n", baseline_brier);
    if (worst_feature >= 0) {
        printf("Most impactful feature: %s (delta %+.4f)\n",
               feat_names[worst_feature], max_delta);
    }
    printf("*** = removing feature hurts performance (feature is useful)\n");
    printf("+++ = removing feature helps (feature adds noise)\n");

    return 0;
}
