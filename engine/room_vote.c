/**
 * room_vote.c — L3: 10K Agent Voting Engine (v2)
 * Each agent is a learned linear model: signal = dot(feat_weight, features) + bias
 * Feature weights evolve via Darwin + online SGD after each trade.
 * Hidden state persists across trades for recurrent memory.
 *
 * v2 changes:
 * - Removed hardcoded feature weights — now per-agent learned weights
 * - Added hidden state (4 floats) per agent — updated each cycle
 * - SGD update happens in room_capital.c after trade resolution
 */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "types.h"

#define SIGMA_NORMALIZER  0.15f  // Match typical bias range [−0.15, 0.15] so bias+features both drive votes
#define SIGMOID_SCALE     2.5f   // Sharper sigmoid for conviction diversity
// ── A30: Epsilon-greedy exploration ──
#define EPSILON_INIT      0.05f  // 5% random exploration at start
#define EPSILON_MIN       0.10f  // Force 10% permanent exploration to prevent consensus death
#define EPSILON_DECAY     0.9995f // Per-cycle decay toward minimum

static inline float sigmoid(float x) {
    if (x < -10.0f) return 0.0f;
    if (x > 10.0f) return 1.0f;
    return 1.0f / (1.0f + expf(-x));
}

// P15: Tailslayer — beam-search scenario evaluation
// When tail risk is elevated, agent conviction must be proportionally higher to trade.
// This simulates evaluating multiple future scenarios and only acting if conviction
// survives the beam width (1+tail_risk). Acts as a beam-search ensemble filter.
static float tailslayer_threshold(float base_conviction_threshold, float tail_risk) {
    // Base threshold is scaled by (1 + tail_risk), so at tail_risk=1.0,
    // effective threshold is 2× normal. At tail_risk=0, threshold is unchanged.
    // Clamps at 0.80 max to prevent total gridlock.
    float threshold = base_conviction_threshold * (1.0f + tail_risk);
    if (threshold > 0.80f) threshold = 0.80f;
    return threshold;
}

/**
 * Apply feature importance gating to signal computation.
 * Features with negative importance are zeroed out; positive features are amplified.
 */
static void apply_importance_gating(const Genome *g, const FeatureVector *fv, 
                                     const FeatureImportance *imp, int regime,
                                     float *features_out) {
    (void)g; (void)regime; // Unused parameters
    float *features = (float*)fv;
    for (int i = 0; i < N_FEATURES; i++) {
        int pos_total = imp->pos_contrib_total[i];
        int neg_total = imp->neg_contrib_total[i];
        float importance = 0.0f;
        if (pos_total + neg_total >= 20) {
            float pos_wr = pos_total > 0 ? imp->pos_contrib_wins[i] / (float)pos_total : 0.5f;
            float neg_wr = neg_total > 0 ? imp->neg_contrib_wins[i] / (float)neg_total : 0.5f;
            importance = pos_wr - neg_wr;
        }
        
        // Gate: zero out features with negative importance, amplify positive
        if (importance < -0.05f) {
            features_out[i] = 0.0f;  // Harmful feature - disable
        } else if (importance > 0.05f) {
            features_out[i] = features[i] * (1.0f + importance);  // Amplify helpful
        } else {
            features_out[i] = features[i];  // Neutral - keep as-is
        }
    }
}

/**
 * Compute agent signal as learned dot product + bias.
 * Each agent has its own feat_weight[N_FEATURES] and bias,
 * evolved by Darwin and refined by online SGD.
 * Applies feature importance gating to disable harmful features.
 */
static float compute_agent_signal(const Genome *g, const FeatureVector *fv,
                                   const FeatureImportance *imp, int agent_market) {
    float features[N_FEATURES];
    float *fv_arr = (float*)fv;
    
    // Apply importance gating
    apply_importance_gating(g, fv, imp, (int)(fv->regime_indicator + 0.5f), features);
    
    // P22: REGIME GATING DISABLED (DA audit: regime importance = -0.28 adversarial)
    // Force single regime with neutral weights = 1.0
    int regime = 0;
    
    // A23: gene silencing — fresh random mask each call (15% feature dropout)
    float signal = g->bias;  // Use agent's learned bias
    for (int i = 0; i < N_FEATURES; i++) {
        // Each feature independently has 15% chance of being silenced
        if (((float)rand() / RAND_MAX) < 0.15f) continue;
        float w = g->feat_weight[i];  // Use agent's LEARNED weight (was hardcoded 1.0 = all agents identical)
        signal += w * features[i];
    }

    // Modulation by genome meta-params
    float horizon_w = fmaxf(0.1f, g->time_horizon) / 5.0f;
    signal *= horizon_w;

    // Regime gating DISABLED - no 1.3x/0.7x multipliers

    // Herd contrarian bias
    signal -= (fv->herd_consensus - 0.5f) * g->herd_antipathy * 0.20f;

    return signal;
}

/**
 * Update agent's recurrent hidden state each cycle.
 * Simple RNN step: h = 0.9 * h + 0.1 * tanh(signal)
 * This gives agents memory of past predictions.
 */
static void update_hidden_state(AgentState *agent, float signal) {
    float activation = tanhf(signal);
    for (int i = 0; i < 4; i++) {
        // Each hidden dim mixes prior state with current signal
        agent->hidden[i] = 0.9f * agent->hidden[i] + 0.1f * activation;
    }
}

/**
 * Initialize genome weights to sensible defaults (v1-compatible starting point).
 * Uses feature importance scores from room state to bias initial weights toward
 * helpful features and away from harmful ones.
 */
void init_genome_weights(Genome *g) {
    // Start with the v1 hardcoded weights as initial prior
    // Feature order: price_delta, micro_momentum, rsi_7, volume_surge,
    //                ema_fast, ema_slow, macd_hist, bollinger_pct,
    //                divergence_score, pump_score, regime_indicator,
    //                fear_greed_norm, herd_consensus,
    //                ob_imbalance, ob_depth_ratio, cvd_signal,
    //                dft_dominant, tail_risk_score,
    //                funding_signal, oi_net_signal, ls_ratio_norm,
    //                liq_ls_ratio_norm, stable_inflow_norm, whale_activity_norm,
    //                hash_rate_norm, difficulty_norm, miner_floor_norm,
    //                hour_of_day_norm, day_of_week_norm,
    //                iv_skew, pcr_volume, iv_term_slope,
    //                btc_sp500_corr, vix_regime
    float default_weights[N_FEATURES] = {
        0.15f,   // price_delta_pct
        0.10f,   // micro_momentum
        0.08f,   // rsi_7 (normalized /50)
        0.06f,   // volume_surge_ratio
        0.05f,   // ema_fast
        0.03f,   // ema_slow
        0.02f,   // macd_hist
        0.10f,   // bollinger_pct (applied as 0.5 - pct)
        0.04f,   // divergence_score
        -0.12f,  // pump_score (negative = avoid crony)
        0.00f,   // regime_indicator (gating applied separately)
        -0.05f,  // fear_greed_norm
        0.00f,   // herd_consensus (gating applied separately)
        0.08f,   // ob_imbalance (F14) - positive per importance
        0.00f,   // ob_depth_ratio (F15) - negative importance, zeroed
        0.08f,   // cvd_signal (F16) - positive per importance
        0.06f,   // dft_dominant (F17) - positive per importance
        -0.04f,  // tail_risk_score (F18) - negative importance
        0.04f,   // funding_signal (F19) - positive per importance
        0.05f,   // oi_net_signal (F20) - positive per importance
        -0.02f,  // ls_ratio_norm (F21) - slightly negative
        -0.10f,  // liq_ls_ratio_norm (F22) - negative importance
        0.05f,   // stable_inflow_norm (F23) - positive per importance
        0.08f,   // whale_activity_norm (F24) - positive per importance
        -0.08f,  // hash_rate_norm (F25) - negative importance
        -0.07f,  // difficulty_norm (F26) - negative importance
        0.01f,   // miner_floor_norm (F27) - near zero
        -0.01f,  // hour_of_day_norm (F28) - slightly negative
        -0.15f,  // day_of_week_norm (F29) - negative importance
        -0.03f,  // iv_skew (F30) - negative importance
        0.02f,   // pcr_volume (F31) - positive per importance
        -0.03f,  // iv_term_slope (F32) - negative importance
        0.01f,   // btc_sp500_corr (F33) - near zero
        -0.04f,  // vix_regime (F34) - negative importance
    };
    memcpy(g->feat_weight, default_weights, sizeof(default_weights));
    
    // Also initialize regime-specific weights
    for (int r = 0; r < N_REGS; r++) {
        memcpy(g->regime_weight[r], default_weights, sizeof(default_weights));
        g->regime_bias[r] = 0.0f;
    }
    
    g->bias = 0.0f;
    g->learning_rate = 0.01f;
}

RoomError room_vote_run(AgentState *agents, int n,
                        const FeatureVector *fv,
                        const FeatureImportance *imp,
                        const int *agent_market,
                        VoteRecord *votes, int *count, float epsilon) {
    *count = 0;
    if (!agents || !fv || !votes) return ERR_NO_AGENTS;

    for (int i = 0; i < n; i++) {
        if (!agents[i].alive) continue;

        const Genome *g = &agents[i].genome;
        int market = agent_market ? agent_market[i] : 0;
        float raw = compute_agent_signal(g, fv, imp, market);
        float z = raw / SIGMA_NORMALIZER;
        float conviction = sigmoid(z * SIGMOID_SCALE);
        bool direction = conviction >= 0.5f;

        // A30: Epsilon-greedy exploration — random vote with probability epsilon
        bool explore = ((float)rand() / RAND_MAX) < epsilon;
        if (explore) {
            conviction = (float)rand() / (float)RAND_MAX;
            direction = conviction >= 0.5f;
        }

        // Update hidden state
        update_hidden_state(&agents[i], raw);

        float conv_strength = fabsf(conviction - 0.5f) * 2.0f;
        // P15: Tailslayer beam-search gating — scale threshold by tail risk
        float effective_threshold = tailslayer_threshold(g->conviction_threshold, fv->tail_risk_score);
        if (conv_strength < effective_threshold) continue;

        float edge = direction ? conviction : (1.0f - conviction);
        float edge_pct = (edge - 0.5f) * 200.0f;
        if (edge_pct < g->min_edge_pct) continue;

        // Store features + conviction for SGD update at resolution time
        memcpy(agents[i].last_features, (float*)fv, sizeof(float) * N_FEATURES);
        agents[i].last_conviction = conviction;
        agents[i].last_trade_window = -1;  // Will be set when trade is recorded

        // A006: Log prediction to outcomes DB for accuracy scoring
        // Note: window_ts and asset need to be passed in or stored in agent
        // For now, store in VoteRecord for logging in room_capital.c

        VoteRecord *v = &votes[*count];
        v->agent_id = i;
        v->voted = true;
        v->direction = direction;
        v->conviction = conviction;
        v->position_size = g->position_size * g->risk_tolerance;
        // A006: Store predicted probability and regime for accuracy scoring
        v->predicted_prob = direction ? conviction : (1.0f - conviction);
        v->regime = (int)(fv->regime_indicator + 0.5f);

        // P24: Kelly criterion override — optimal bet sizing from win rate
        {
            float wr = agents[i].win_rate_ema;
            // Full Kelly for 1:1 payout: f* = 2*WR - 1
            float kelly_raw = 2.0f * wr - 1.0f;
            if (kelly_raw < 0.0f) kelly_raw = 0.0f;
            // Fractional Kelly (0.25) for safety — use genome's risk_tolerance as fraction
            float kelly_frac = g->risk_tolerance * 0.25f;
            float kelly_pos = kelly_raw * kelly_frac;
            // Cap at genome's max position size
            float max_pos = g->position_size * 0.5f;
            if (kelly_pos > max_pos) kelly_pos = max_pos;
            // Blend: use Kelly fraction when agent has enough trades for reliable WR
            if (agents[i].trades >= 20) {
                v->position_size = kelly_pos;
            }
        }

        (*count)++;
    }

    return ERR_OK;
}
