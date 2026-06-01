/**
 * room_features.c — L2: 13-dim feature vector per tick
 * Combines market data, news sentiment, and agent consensus into features.
 */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "types.h"

// ── History ring buffers — per-market-type ──
// NOTE: These are now persisted in RoomState (types.h) so they survive engine restarts.
// B02: Static arrays removed — use s->price_hist[mt] instead.

// ── RSI ──
static float calc_rsi(const float *prices, int len, int period) {
    if (len < period + 1) return 50.0f;
    int eff = period < len - 1 ? period : len - 1;
    float gains = 0, losses = 0;
    int start = len - eff - 1;
    for (int i = start; i < len - 1; i++) {
        float d = prices[i + 1] - prices[i];
        if (d > 0) gains += d;
        else losses -= d;
    }
    float avg_gain = gains / eff;
    float avg_loss = losses / eff;
    if (avg_loss == 0) return 100.0f;
    float rs = avg_gain / avg_loss;
    return 100.0f - 100.0f / (1.0f + rs);
}

// ── EMA ──
static float calc_ema(const float *prices, int len, int period) {
    if (len < period) return prices[len - 1];
    float k = 2.0f / (period + 1);
    float ema = prices[0];
    for (int i = 1; i < len; i++)
        ema = prices[i] * k + ema * (1.0f - k);
    return ema;
}

// ── MACD ──
static float calc_macd_hist(const float *prices, int len) {
    if (len < 26) return 0;
    float ema12 = calc_ema(prices, len, 12);
    float ema26 = calc_ema(prices, len, 26);
    float macd = ema12 - ema26;
    // Signal line: EMA of MACD (9-period approximation)
    // For 1-min data, signal line = last 9 values of MACD line
    // Simplified: use macd - ema_of_macd
    float signal = calc_ema(prices + (len > 9 ? len - 9 : 0),
                            len > 9 ? 9 : len, 9);
    return macd - signal;
}

// ── Bollinger %B ──
static float calc_bollinger_pct(const float *prices, int len) {
    if (len < 20) return 0.5f;
    float sum = 0, mean = 0;
    int n = len > 20 ? 20 : len;
    const float *p = prices + len - n;
    for (int i = 0; i < n; i++) sum += p[i];
    mean = sum / n;
    float var = 0;
    for (int i = 0; i < n; i++) {
        float d = p[i] - mean;
        var += d * d;
    }
    float std = sqrtf(var / n);
    float last = prices[len - 1];
    float lower = mean - 2 * std;
    float upper = mean + 2 * std;
    if (upper - lower < 0.0001f) return 0.5f;
    return (last - lower) / (upper - lower);
}

// ── Regime detection ──
// 0=range, 1=trend, 2=volatile
static float calc_regime(const float *prices, int len) {
    if (len < 10) return 0;
    float sum = 0, mean = 0;
    for (int i = len - 10; i < len; i++) sum += prices[i];
    mean = sum / 10;
    float var = 0;
    for (int i = len - 10; i < len; i++) {
        float d = prices[i] - mean;
        var += d * d;
    }
    float std = sqrtf(var / 10);
    float range_pct = std / (mean > 0 ? mean : 1);

    // Directional movement over 10 periods
    float net = prices[len - 1] - prices[len - 10];
    float gross = 0;
    for (int i = len - 9; i < len; i++)
        gross += fabsf(prices[i] - prices[i - 1]);

    float efficiency = gross > 0 ? fabsf(net) / gross : 0;

    if (range_pct > 0.005) return 2;       // Volatile
    if (efficiency > 0.6) return 1;         // Trending
    return 0;                                // Ranging
}

// ── B05: Load order book features from orderbook_depth.c JSON ──
#define OB_FEAT_PATH "/home/wubu2/.hermes/orderbook_cache/orderbook_features.json"
static int load_orderbook_features(MarketTick *tick) {
    FILE *f = fopen(OB_FEAT_PATH, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 10) { fclose(f); return -1; }
    rewind(f);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t nread = fread(buf, 1, sz, f);
    fclose(f);
    buf[nread] = '\0';

    // Parse normalized features
    const char *p;
    p = strstr(buf, "\"ob_imbalance_norm\"");
    if (p) tick->ob_imbalance = strtof(p + 22, NULL);
    p = strstr(buf, "\"ob_depth_ratio_norm\"");
    if (p) tick->ob_depth_ratio = strtof(p + 25, NULL);
    p = strstr(buf, "\"ob_wall_conc_norm\"");
    if (p) tick->ob_wall_conc = strtof(p + 23, NULL);
    p = strstr(buf, "\"ob_spread_norm\"");
    if (p) tick->ob_spread_norm = strtof(p + 20, NULL);

    free(buf);
    return 0;
}

// ── B06: Load cumulative volume delta features ──
#define CVD_FEAT_PATH "/home/wubu2/.hermes/cvd_cache/cvd_features.json"
static int load_cvd_features(MarketTick *tick) {
    FILE *f = fopen(CVD_FEAT_PATH, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 10) { fclose(f); return -1; }
    rewind(f);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t nread = fread(buf, 1, sz, f);
    fclose(f);
    buf[nread] = '\0';

    const char *p = strstr(buf, "\"cvd_signal_norm\"");
    if (p) tick->cvd_signal = strtof(p + 18, NULL);

    free(buf);
    return 0;
}

// ── B14: Load funding rate features ──
#define FUNDING_FEAT_PATH "/home/wubu2/.hermes/options_cache/funding_features.json"
static int load_funding_features(MarketTick *tick) {
    FILE *f = fopen(FUNDING_FEAT_PATH, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 10) { fclose(f); return -1; }
    rewind(f);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t nread = fread(buf, 1, sz, f);
    fclose(f);
    buf[nread] = '\0';
    const char *p = strstr(buf, "\"funding_signal\"");
    if (p) tick->funding_signal = strtof(p + 16, NULL);
    p = strstr(buf, "\"funding_rate_norm\"");
    if (p && tick->funding_signal == 0.0f) tick->funding_signal = strtof(p + 19, NULL) * 2.0f - 1.0f;
    free(buf);
    return 0;
}

// ── B15: Load open interest features ──
#define OI_FEAT_PATH "/home/wubu2/.hermes/options_cache/open_interest_features.json"
static int load_open_interest_features(MarketTick *tick) {
    FILE *f = fopen(OI_FEAT_PATH, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 10) { fclose(f); return -1; }
    rewind(f);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t nread = fread(buf, 1, sz, f);
    fclose(f);
    buf[nread] = '\0';
    const char *p = strstr(buf, "\"btc_oi_signal\"");
    if (p) tick->oi_net_signal = strtof(p + 16, NULL) * 0.5f + 0.5f;  // [-1,1] → [0,1]
    free(buf);
    return 0;
}

// ── B16: Load L/S ratio features ──
#define LS_FEAT_PATH "/home/wubu2/.hermes/options_cache/ls_ratio_features.json"
static int load_ls_ratio_features(MarketTick *tick) {
    FILE *f = fopen(LS_FEAT_PATH, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 10) { fclose(f); return -1; }
    rewind(f);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t nread = fread(buf, 1, sz, f);
    fclose(f);
    buf[nread] = '\0';
    const char *p = strstr(buf, "\"ls_ratio_norm\"");
    if (p) tick->ls_ratio_norm = strtof(p + 16, NULL);
    if (tick->ls_ratio_norm < 0.01f) {  // fallback
        p = strstr(buf, "\"buy_pct_norm\"");
        if (p) tick->ls_ratio_norm = strtof(p + 15, NULL);
    }
    free(buf);
    return 0;
}

// ── B17: Load liquidation features ──
#define LIQ_FEAT_PATH "/home/wubu2/.hermes/options_cache/liquidation_features.json"
static int load_liquidation_features(MarketTick *tick) {
    FILE *f = fopen(LIQ_FEAT_PATH, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 10) { fclose(f); return -1; }
    rewind(f);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t nread = fread(buf, 1, sz, f);
    fclose(f);
    buf[nread] = '\0';
    const char *p = strstr(buf, "\"liq_ls_ratio_norm\"");
    if (p) tick->liq_ls_ratio_norm = strtof(p + 19, NULL);
    if (tick->liq_ls_ratio_norm < 0.01f) {
        p = strstr(buf, "\"total_liq_usd\"");
        if (p) tick->liq_ls_ratio_norm = fminf(strtof(p + 14, NULL) / 100000000.0f, 1.0f);
    }
    free(buf);
    return 0;
}

// ── B18: Load stablecoin inflow features ──
#define STABLE_FEAT_PATH "/home/wubu2/.hermes/options_cache/stablecoin_features.json"
static int load_stablecoin_features(MarketTick *tick) {
    FILE *f = fopen(STABLE_FEAT_PATH, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 10) { fclose(f); return -1; }
    rewind(f);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t nread = fread(buf, 1, sz, f);
    fclose(f);
    buf[nread] = '\0';
    const char *p = strstr(buf, "\"stable_vol_ratio\"");
    if (p) tick->stable_inflow_norm = strtof(p + 18, NULL);
    if (tick->stable_inflow_norm > 1.0f) tick->stable_inflow_norm = 1.0f;
    free(buf);
    return 0;
}

// ── B19: Load whale tracking features ──
#define WHALE_FEAT_PATH "/home/wubu2/.hermes/options_cache/whale_features.json"
static int load_whale_features(MarketTick *tick) {
    FILE *f = fopen(WHALE_FEAT_PATH, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 10) { fclose(f); return -1; }
    rewind(f);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t nread = fread(buf, 1, sz, f);
    fclose(f);
    buf[nread] = '\0';
    const char *p = strstr(buf, "\"whale_activity\"");
    if (p) tick->whale_activity_norm = strtof(p + 16, NULL);
    if (tick->whale_activity_norm < 0.01f) {
        p = strstr(buf, "\"acc_signal_norm\"");
        if (p) tick->whale_activity_norm = strtof(p + 17, NULL);
    }
    free(buf);
    return 0;
}

// ── Hashrate: Load BTC hashrate/difficulty/miner-floor features ──
#define HASHRATE_FEAT_PATH "/home/wubu2/.hermes/options_cache/hashrate_features.json"
static int load_hashrate_features(MarketTick *tick) {
    FILE *f = fopen(HASHRATE_FEAT_PATH, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 10) { fclose(f); return -1; }
    rewind(f);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t nread = fread(buf, 1, sz, f);
    fclose(f);
    buf[nread] = '\0';
    const char *p = strstr(buf, "\"hash_rate_norm\"");
    if (p) tick->hash_rate_norm = strtof(p + 16, NULL);
    p = strstr(buf, "\"difficulty_norm\"");
    if (p) tick->difficulty_norm = strtof(p + 17, NULL);
    p = strstr(buf, "\"miner_floor_norm\"");
    if (p) tick->miner_floor_norm = strtof(p + 18, NULL);
    free(buf);
    return 0;
}

// ── B21: Load options-derived features (IV skew, PCR, term structure) ──
#define OPTIONS_FEAT_PATH "/home/wubu2/.hermes/options_cache/latest_features.json"
static int load_options_features(MarketTick *tick) {
    FILE *f = fopen(OPTIONS_FEAT_PATH, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 10) { fclose(f); return -1; }
    rewind(f);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t nread = fread(buf, 1, sz, f);
    fclose(f);
    buf[nread] = '\0';
    const char *p = strstr(buf, "\"iv_skew\"");
    if (p) tick->iv_skew = strtof(p + 10, NULL);
    p = strstr(buf, "\"pcr_vol\"");
    if (p) tick->pcr_volume = strtof(p + 10, NULL);
    p = strstr(buf, "\"iv_term_slope\"");
    if (p) tick->iv_term_slope = strtof(p + 16, NULL);
    free(buf);
    return 0;
}

// ── P13: Goertzel DFT — extract dominant frequency ──
// Single-frequency DFT using Goertzel algorithm.
// Finds dominant cycle in price history.
static float compute_dft_dominant(const float *px, int len) {
    if (len < 10) return 0.0f;

    // Remove DC component
    float mean = 0;
    for (int i = 0; i < len; i++) mean += px[i];
    mean /= len;

    // Search for dominant frequency in range [2, len/2] periods
    float max_mag = 0;

    for (int k = 2; k <= len / 2; k++) {
        float omega = TWO_PI * k / len;
        float coeff = 2.0f * cosf(omega);
        float s0 = 0, s1 = 0, s2 = 0;

        for (int i = 0; i < len; i++) {
            s0 = (px[i] - mean) + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        float real = s1 - s2 * cosf(omega);
        float imag = s2 * sinf(omega);
        float mag = real * real + imag * imag;

        if (mag > max_mag) {
            max_mag = mag;
        }
    }

    // Normalize: 0 = no dominant cycle, 1 = strong cycle
    float norm = max_mag / (len * len * 0.01f + 1.0f);
    return fminf(norm, 1.0f);
}

// ── P15: Tailslayer tail risk detection ──
// Computes tail risk from excess kurtosis + extreme moves.
// Returns 0-1 where 0=normal gaussian, 1=extreme fat-tail risk.
static float compute_tail_risk(const float *px, int len) {
    if (len < 10) return 0.0f;

    // Compute log returns
    float returns[FEED_HISTORY];
    int n_ret = 0;
    for (int i = 1; i < len; i++) {
        if (px[i - 1] > 0) {
            returns[n_ret++] = logf(px[i] / px[i - 1]);
        }
    }
    if (n_ret < 5) return 0.0f;

    // Mean and std of returns
    float mean = 0.0f;
    for (int i = 0; i < n_ret; i++) mean += returns[i];
    mean /= n_ret;

    float var = 0.0f;
    for (int i = 0; i < n_ret; i++) {
        float d = returns[i] - mean;
        var += d * d;
    }
    float std = sqrtf(var / n_ret);
    if (std < 1e-8f) return 0.0f;

    // ── Kurtosis: E[(X-μ)⁴] / σ⁴ — excess kurtosis > 3 = fat tails
    float m4 = 0.0f;
    float extreme_count = 0.0f;
    for (int i = 0; i < n_ret; i++) {
        float z = (returns[i] - mean) / std;
        float z2 = z * z;
        m4 += z2 * z2;
        // Count extreme moves (>2 sigma)
        if (fabsf(z) > 2.0f) extreme_count += 1.0f;
    }
    float kurtosis = m4 / n_ret;  // Raw kurtosis (excess = kurtosis - 3)
    float extreme_ratio = extreme_count / n_ret;

    // ── Composite score: blend excess kurtosis + extreme move frequency
    // Excess kurtosis: 0=normal, >3=very fat. Normalize via tanh(k/3)
    float kurt_contrib = tanhf(fmaxf(kurtosis - 3.0f, 0.0f) / 3.0f);
    // Extreme ratio: 0-1, expect 0.05 for normal (5% beyond 2σ), scale up
    float extreme_contrib = fminf(extreme_ratio * 10.0f, 1.0f);

    // Blend: 60% kurtosis, 40% extreme frequency
    float score = 0.6f * kurt_contrib + 0.4f * extreme_contrib;
    if (score < 0.0f) score = 0.0f;
    if (score > 1.0f) score = 1.0f;
    return score;
}

// ── B12: Rolling Pearson correlation between two price series ──
// Computes correlation between market price history and SP500 history.
// Returns value in [-1, 1]. Needs minimum window of 5 matching samples.
static float calc_sp500_corr(const float *px, int px_len, const float *spx, int spx_len) {
    int n = px_len < spx_len ? px_len : spx_len;
    if (n < 5) return 0.0f;
    if (n > FEED_HISTORY) n = FEED_HISTORY;
    float sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0, sum_y2 = 0;
    int offset_x = px_len - n;
    int offset_y = spx_len - n;
    for (int i = 0; i < n; i++) {
        float x = px[offset_x + i];
        float y = spx[offset_y + i];
        sum_x  += x;  sum_y  += y;
        sum_xy += x * y;
        sum_x2 += x * x;
        sum_y2 += y * y;
    }
    float num = n * sum_xy - sum_x * sum_y;
    float den = sqrtf((n * sum_x2 - sum_x * sum_x) * (n * sum_y2 - sum_y * sum_y));
    if (den < 1e-10f) return 0.0f;
    return num / den;
}

RoomError room_features_compute(const MarketTick *tick, FeatureVector *fv, RoomState *s) {
    memset(fv, 0, sizeof(FeatureVector));

    // Determine market type for per-buffer indexing
    int mt = (int)tick->market_type;
    if (mt < 0 || mt >= N_FEED_MARKETS) mt = MARKET_CRYPTO;
    bool is_binary = (tick->market_type == MARKET_SPORTS || tick->market_type == MARKET_WEATHER ||
                      tick->market_type == MARKET_PREDICTION || tick->market_type == MARKET_ELECTION);

    // Determine "price" for this market type
    float price_val;
    if (is_binary) {
        // Binary markets: clamp to probability 0-1
        price_val = tick->close;
        if (price_val < 0.0f) price_val = 0.0f;
        if (price_val > 1.0f) price_val = 1.0f;
        if (tick->close > 1000.0f) price_val = 0.5f;  // BTC price, not probability
    } else {
        price_val = tick->close;
    }

    // Push into per-market history buffer (persisted in RoomState)
    s->price_hist[mt][s->price_hist_idx[mt]] = price_val;
    s->volume_hist[mt][s->price_hist_idx[mt]] = tick->volume;
    s->price_hist_idx[mt] = (s->price_hist_idx[mt] + 1) % FEED_HISTORY;
    if (s->price_hist_len[mt] < FEED_HISTORY) s->price_hist_len[mt]++;

    // ── B12: Track SP500 history for equity correlation ──
    s->sp500_hist[s->sp500_hist_idx] = tick->sp500;
    s->sp500_hist_idx = (s->sp500_hist_idx + 1) % FEED_HISTORY;
    if (s->sp500_hist_len < FEED_HISTORY) s->sp500_hist_len++;

    // Need at least 1 data point for initial features
    if (s->price_hist_len[mt] < 1) return ERR_NO_DATA;

    // Build linear price array (oldest to newest) from persistent per-market buffer
    float px[FEED_HISTORY];
    float vol[FEED_HISTORY];
    for (int i = 0; i < s->price_hist_len[mt]; i++) {
        int idx = (s->price_hist_idx[mt] - s->price_hist_len[mt] + i + FEED_HISTORY) % FEED_HISTORY;
        px[i] = s->price_hist[mt][idx];
        vol[i] = s->volume_hist[mt][idx];
    }

    // With only 1 data point, duplicate it for feature computation
    if (s->price_hist_len[mt] == 1) {
        px[1] = px[0];
        vol[1] = vol[0];
    }

    // F1: Price delta (current vs window open)
    if (tick->open > 0) {
        if (is_binary) {
            fv->price_delta_pct = (tick->close - tick->open) * 100.0f;  // Probability delta
        } else {
            fv->price_delta_pct = (tick->close - tick->open) / tick->open * 100.0f;
        }
    }

    // F2: Micro momentum (last 2 closes delta)
    if (s->price_hist_len[mt] >= 3)
        fv->micro_momentum = (px[s->price_hist_len[mt] - 1] - px[s->price_hist_len[mt] - 2]) * (is_binary ? 100.0f : (1.0f / fmax(px[s->price_hist_len[mt] - 2], 0.001f)));
    else if (s->price_hist_len[mt] >= 2)
        fv->micro_momentum = (px[1] - px[0]) * (is_binary ? 100.0f : (1.0f / fmax(px[0], 0.001f)));

    // F3: RSI(7) — meaningful for both price and probability
    fv->rsi_7 = calc_rsi(px, s->price_hist_len[mt], 7);

    // F4: Volume surge ratio
    if (s->price_hist_len[mt] >= 4) {
        float recent = (vol[s->price_hist_len[mt] - 1] + vol[s->price_hist_len[mt] - 2]) / 2.0f;
        float prior = (vol[s->price_hist_len[mt] - 3] + vol[s->price_hist_len[mt] - 4]) / 2.0f;
        fv->volume_surge_ratio = prior > 0 ? recent / prior : 1.0f;
    } else {
        fv->volume_surge_ratio = 1.0f;
    }

    // F5: EMA fast (3)
    fv->ema_fast = calc_ema(px, s->price_hist_len[mt], 3);

    // F6: EMA slow (8)
    fv->ema_slow = calc_ema(px, s->price_hist_len[mt], 8);

    // F7: MACD histogram
    fv->macd_hist = calc_macd_hist(px, s->price_hist_len[mt]);

    // F8: Bollinger %B
    fv->bollinger_pct = calc_bollinger_pct(px, s->price_hist_len[mt]);

    // F9: Divergence score (price vs RSI)
    if (s->price_hist_len[mt] >= 14) {
        float rsi_now = fv->rsi_7;
        float rsi_prev = calc_rsi(px, s->price_hist_len[mt] - 7, 7);
        float px_now = px[s->price_hist_len[mt] - 1];
        float px_prev = px[s->price_hist_len[mt] - 7];
        float px_dir = px_now > px_prev ? 1.0f : -1.0f;
        float rsi_dir = rsi_now > rsi_prev ? 1.0f : -1.0f;
        fv->divergence_score = (rsi_dir - px_dir) / 2.0f;
    }

    // F10: Pump score (from crony-weighted news pipeline)
    fv->pump_score = tick->pump_score;

    // F11: Regime indicator
    fv->regime_indicator = calc_regime(px, s->price_hist_len[mt]);

    // ── A13: Regime transition Markov model ──
    if (s->prev_regime >= 0 && s->prev_regime < N_REGS) {
        int curr_regime = (int)(fv->regime_indicator + 0.5f);
        if (curr_regime >= N_REGS) curr_regime = N_REGS - 1;
        if (curr_regime < 0) curr_regime = 0;
        s->regime_transition_counts[s->prev_regime][curr_regime]++;
        // Compute predicted regime: argmax of transition counts from previous regime
        int best_next = 0;
        int best_count = s->regime_transition_counts[s->prev_regime][0];
        for (int r = 1; r < N_REGS; r++) {
            if (s->regime_transition_counts[s->prev_regime][r] > best_count) {
                best_count = s->regime_transition_counts[s->prev_regime][r];
                best_next = r;
            }
        }
        s->predicted_regime = best_next;
    } else {
        s->predicted_regime = (int)(fv->regime_indicator + 0.5f);
        if (s->predicted_regime >= N_REGS) s->predicted_regime = N_REGS - 1;
        if (s->predicted_regime < 0) s->predicted_regime = 0;
    }
    s->prev_regime = (int)(fv->regime_indicator + 0.5f);
    if (s->prev_regime >= N_REGS) s->prev_regime = N_REGS - 1;
    if (s->prev_regime < 0) s->prev_regime = 0;

    // F12: Fear & Greed normalized
    fv->fear_greed_norm = tick->fear_greed / 100.0f;

    // F13: Herd consensus (what % of agents voted UP last cycle)
    if (s->vote_count > 0) {
        int up = 0;
        for (int i = 0; i < s->vote_count; i++) {
            if (s->votes[i].direction) up++;
        }
        fv->herd_consensus = (float)up / s->vote_count;
    } else {
        fv->herd_consensus = 0.5f;
    }

    // ── B05: Load order book features ──
    load_orderbook_features((MarketTick *)tick);

    // F14: Order book imbalance (0-1, >0.5 = bid-heavy)
    fv->ob_imbalance = tick->ob_imbalance;
    if (fv->ob_imbalance < 0.01f && fv->ob_imbalance > -0.01f) fv->ob_imbalance = 0.5f;  // default neutral

    // F15: Order book depth ratio (0-1, >0.5 = bid-heavy within 0.5% band)
    fv->ob_depth_ratio = tick->ob_depth_ratio;
    if (fv->ob_depth_ratio < 0.01f && fv->ob_depth_ratio > -0.01f) fv->ob_depth_ratio = 0.5f;

    // ── B06: Load cumulative volume delta features ──
    load_cvd_features((MarketTick *)tick);

    // F16: CVD signal normalized (0-1, >0.5 = net buying pressure)
    fv->cvd_signal = tick->cvd_signal;
    if (fv->cvd_signal < 0.01f && fv->cvd_signal > -0.01f) fv->cvd_signal = 0.5f;

    // F17: DFT dominant frequency (P13)
    fv->dft_dominant = compute_dft_dominant(px, s->price_hist_len[mt]);

    // F18: Tailslayer tail risk score (P15)
    fv->tail_risk_score = compute_tail_risk(px, s->price_hist_len[mt]);

    // ── B14-B16: Load funding/OI/LS ratio features ──
    load_funding_features((MarketTick *)tick);
    load_open_interest_features((MarketTick *)tick);
    load_ls_ratio_features((MarketTick *)tick);

    // F19: Funding rate signal (-1..1, <0 = negative funding = bullish perp)
    fv->funding_signal = tick->funding_signal;
    // F20: OI net signal (0-1, >0.5 = bullish OI expansion)
    fv->oi_net_signal = tick->oi_net_signal;
    if (fv->oi_net_signal < 0.01f) fv->oi_net_signal = 0.5f;
    // F21: L/S ratio normalized (0-1, >0.5 = more long buying)
    fv->ls_ratio_norm = tick->ls_ratio_norm;
    if (fv->ls_ratio_norm < 0.01f) fv->ls_ratio_norm = 0.5f;

    // ── B17-B19: Load liquidation/stablecoin/whale features ──
    load_liquidation_features((MarketTick *)tick);
    load_stablecoin_features((MarketTick *)tick);
    load_whale_features((MarketTick *)tick);
    load_hashrate_features((MarketTick *)tick);
    load_options_features((MarketTick *)tick);

    // F22: Liquidation L/S ratio (0-1, >0.5 = more longs being liquidated = bearish)
    fv->liq_ls_ratio_norm = tick->liq_ls_ratio_norm;
    if (fv->liq_ls_ratio_norm < 0.01f) fv->liq_ls_ratio_norm = 0.5f;
    // F23: Stablecoin inflow norm (0-1, >0.5 = high volume activity = bullish exchanges)
    fv->stable_inflow_norm = tick->stable_inflow_norm;
    if (fv->stable_inflow_norm < 0.01f) fv->stable_inflow_norm = 0.5f;
    // F24: Whale activity norm (0-1, >0.5 = high large-tx activity)
    fv->whale_activity_norm = tick->whale_activity_norm;
    if (fv->whale_activity_norm < 0.01f) fv->whale_activity_norm = 0.5f;

    // ── Hashrate features ──
    // F25: Hash rate norm (0-1, higher = more network security)
    fv->hash_rate_norm = tick->hash_rate_norm;
    if (fv->hash_rate_norm < 0.01f) fv->hash_rate_norm = 0.5f;
    // F26: Difficulty norm (0-1)
    fv->difficulty_norm = tick->difficulty_norm;
    if (fv->difficulty_norm < 0.01f) fv->difficulty_norm = 0.5f;
    // F27: Miner floor norm (0-1, higher = higher miner cost floor)
    fv->miner_floor_norm = tick->miner_floor_norm;
    if (fv->miner_floor_norm < 0.01f) fv->miner_floor_norm = 0.5f;

    // ── B11: Time-of-day features ──
    // F28: Hour of day [0,1] — captures intraday seasonality
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    fv->hour_of_day_norm = tm_now->tm_hour / 24.0f + tm_now->tm_min / 1440.0f;
    fv->day_of_week_norm = tm_now->tm_wday / 7.0f;

    // ── B21: Options-derived features ──
    // F30: IV skew (higher = put demand = bearish sentiment)
    fv->iv_skew = tick->iv_skew;
    if (fv->iv_skew < 0.01f) fv->iv_skew = 0.5f;
    // F31: Put/call ratio by volume (higher = bearish hedging)
    fv->pcr_volume = tick->pcr_volume;
    if (fv->pcr_volume < 0.01f) fv->pcr_volume = 0.5f;
    // F32: IV term structure slope (higher = steep contango)
    fv->iv_term_slope = tick->iv_term_slope;
    if (fv->iv_term_slope < 0.01f) fv->iv_term_slope = 0.5f;

    // ── B12: BTC-SP500 equity correlation ──
    // Build linear SP500 array from ring buffer, then compute Pearson correlation
    float spx[FEED_HISTORY];
    for (int i = 0; i < s->sp500_hist_len; i++) {
        int idx = (s->sp500_hist_idx - s->sp500_hist_len + i + FEED_HISTORY) % FEED_HISTORY;
        spx[i] = s->sp500_hist[idx];
    }
    fv->btc_sp500_corr = calc_sp500_corr(px, s->price_hist_len[mt], spx, s->sp500_hist_len);

    // ── B27: Feature normalization — all features to [-1, 1] or [0, 1] ──
    // Without this, RSI(0-100) has 100x the scale of OB features(0-1)
    // F1: price_delta_pct — tanh clamp to [-1, 1]
    fv->price_delta_pct = tanhf(fv->price_delta_pct / 5.0f);
    // F2: micro_momentum — tanh clamp
    fv->micro_momentum = tanhf(fv->micro_momentum / 2.0f);
    // F3: RSI 0-100 → 0-1
    fv->rsi_7 = fv->rsi_7 / 100.0f;
    // F4: volume_surge_ratio — log-normalize
    if (fv->volume_surge_ratio > 0.0f) {
        fv->volume_surge_ratio = logf(fv->volume_surge_ratio) / 3.0f + 0.5f;
    }
    if (fv->volume_surge_ratio < 0.0f) fv->volume_surge_ratio = 0.0f;
    if (fv->volume_surge_ratio > 1.0f) fv->volume_surge_ratio = 1.0f;
    // F5-F7: EMA/MACD normalization — skip for binary (already 0-1)
    if (!is_binary) {
        fv->ema_fast = tanhf((fv->ema_fast / price_val - 1.0f) * 5.0f) * 0.5f + 0.5f;
        fv->ema_slow = tanhf((fv->ema_slow / price_val - 1.0f) * 5.0f) * 0.5f + 0.5f;
        fv->macd_hist = tanhf(fv->macd_hist / (price_val * 0.01f + 0.001f));
    }
    // F9: Divergence score [-1,1] → [0,1]
    fv->divergence_score = fv->divergence_score * 0.5f + 0.5f;
    // F10: Pump score [-1,1] → [0,1]
    fv->pump_score = fv->pump_score * 0.5f + 0.5f;
    // F11: Regime [0,2] → [0,1]
    fv->regime_indicator = fv->regime_indicator / 2.0f;

    // F33: BTC-SP500 correlation [-1,1] → [0,1]
    fv->btc_sp500_corr = fv->btc_sp500_corr * 0.5f + 0.5f;

    return ERR_OK;
}
