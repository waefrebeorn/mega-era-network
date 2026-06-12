/**
 * room_features.c — L2: 64-dim feature vector per tick
 *
 * ALL 64 features are computed from actual market data (CSV feed).
 * No external JSON files required — everything comes from:
 *   - Price history (open/high/low/close)
 *   - Volume history
 *   - Time-of-day / calendar
 *   - Derived statistics (RSI, EMA, MACD, Bollinger, etc.)
 *
 * F1-F13:  Core price/volume features (always available)
 * F14-F20: Order book / flow proxies computed from price/volume
 * F21-F27: On-chain proxies computed from price patterns
 * F28-F32: Time / calendar features
 * F33-F34: Cross-asset correlation (SP500, VIX from aux DB)
 * F35-F36: Weather (from aux DB or defaults)
 * F37-F50: Advanced price-derived features
 * F51-F64: Regime / risk / calendar features
 */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include "paper_feature_bridge.h"
#include "types.h"

// ── History ring buffers — per-market-type ──
// NOTE: These are now persisted in RoomState (types.h) so they survive engine restarts.

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
    // Signal line: EMA of last 9 MACD approximations
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

// ── P13: Goertzel DFT — extract dominant frequency ──
static float compute_dft_dominant(const float *px, int len) {
    if (len < 10) return 0.0f;

    float mean = 0;
    for (int i = 0; i < len; i++) mean += px[i];
    mean /= len;

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
        if (mag > max_mag) max_mag = mag;
    }
    float norm = max_mag / (len * len * 0.01f + 1.0f);
    return fminf(norm, 1.0f);
}

// ── P15: Tailslayer tail risk detection ──
static float compute_tail_risk(const float *px, int len) {
    if (len < 10) return 0.0f;

    float returns[FEED_HISTORY];
    int n_ret = 0;
    for (int i = 1; i < len; i++) {
        if (px[i - 1] > 0) {
            returns[n_ret++] = logf(px[i] / px[i - 1]);
        }
    }
    if (n_ret < 5) return 0.0f;

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

    float m4 = 0.0f;
    float extreme_count = 0.0f;
    for (int i = 0; i < n_ret; i++) {
        float z = (returns[i] - mean) / std;
        float z2 = z * z;
        m4 += z2 * z2;
        if (fabsf(z) > 2.0f) extreme_count += 1.0f;
    }
    float kurtosis = m4 / n_ret;
    float extreme_ratio = extreme_count / n_ret;

    float kurt_contrib = tanhf(fmaxf(kurtosis - 3.0f, 0.0f) / 3.0f);
    float extreme_contrib = fminf(extreme_ratio * 10.0f, 1.0f);

    float score = 0.6f * kurt_contrib + 0.4f * extreme_contrib;
    if (score < 0.0f) score = 0.0f;
    if (score > 1.0f) score = 1.0f;
    return score;
}

// ── B12: Rolling Pearson correlation between two price series ──
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

// ── Compute returns array from price history ──
static int calc_returns(const float *px, int len, float *rets) {
    int n = 0;
    for (int i = 1; i < len; i++) {
        if (px[i - 1] > 0)
            rets[n++] = (px[i] - px[i - 1]) / px[i - 1];
        else
            rets[n++] = 0.0f;
    }
    return n;
}

// ════════════════════════════════════════════════════════
//  MAIN FEATURE COMPUTATION
// ════════════════════════════════════════════════════════
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
        price_val = tick->close;
        if (price_val < 0.0f) price_val = 0.0f;
        if (price_val > 1.0f) price_val = 1.0f;
        if (tick->close > 1000.0f) price_val = 0.5f;
    } else {
        price_val = tick->close;
    }

    // Push into per-market history buffer (persisted in RoomState)
    s->price_hist[mt][s->price_hist_idx[mt]] = price_val;
    s->volume_hist[mt][s->price_hist_idx[mt]] = tick->volume;
    s->price_hist_idx[mt] = (s->price_hist_idx[mt] + 1) % FEED_HISTORY;
    if (s->price_hist_len[mt] < FEED_HISTORY) s->price_hist_len[mt]++;

    // Track SP500 and VIX history for equity correlation
    s->sp500_hist[s->sp500_hist_idx] = tick->sp500;
    s->sp500_hist_idx = (s->sp500_hist_idx + 1) % FEED_HISTORY;
    if (s->sp500_hist_len < FEED_HISTORY) s->sp500_hist_len++;

    s->vix_hist[s->vix_hist_idx] = tick->vix;
    s->vix_hist_idx = (s->vix_hist_idx + 1) % FEED_HISTORY;
    if (s->vix_hist_len < FEED_HISTORY) s->vix_hist_len++;

    if (s->price_hist_len[mt] < 1) return ERR_NO_DATA;

    // Build linear price/volume arrays from persistent per-market buffer
    float px[FEED_HISTORY];
    float vol[FEED_HISTORY];
    for (int i = 0; i < s->price_hist_len[mt]; i++) {
        int idx = (s->price_hist_idx[mt] - s->price_hist_len[mt] + i + FEED_HISTORY) % FEED_HISTORY;
        px[i] = s->price_hist[mt][idx];
        vol[i] = s->volume_hist[mt][idx];
    }

    // With only 1 data point, duplicate for feature computation
    if (s->price_hist_len[mt] == 1) {
        px[1] = px[0];
        vol[1] = vol[0];
    }

    // ════════════════════════════════════════════════════════
    // F1-F13: Core price/volume features (always from real data)
    // ════════════════════════════════════════════════════════

    // F1: Price delta (current vs window open)
    if (tick->open > 0) {
        if (is_binary)
            fv->price_delta_pct = (tick->close - tick->open) * 100.0f;
        else
            fv->price_delta_pct = (tick->close - tick->open) / tick->open * 100.0f;
    }

    // F2: Micro momentum (last 2 closes delta)
    if (s->price_hist_len[mt] >= 3)
        fv->micro_momentum = (px[s->price_hist_len[mt] - 1] - px[s->price_hist_len[mt] - 2]) * (is_binary ? 100.0f : (1.0f / fmax(px[s->price_hist_len[mt] - 2], 0.001f)));
    else if (s->price_hist_len[mt] >= 2)
        fv->micro_momentum = (px[1] - px[0]) * (is_binary ? 100.0f : (1.0f / fmax(px[0], 0.001f)));

    // F3: RSI(7)
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

    // ════════════════════════════════════════════════════════
    // F14-F20: Order book / flow proxies from price/volume
    // These approximate order book features using price/volume dynamics
    // ════════════════════════════════════════════════════════

    // F14: OB imbalance proxy — bid/ask imbalance from price movement + volume
    // When price rises on high volume = bid-heavy (buy pressure)
    if (s->price_hist_len[mt] >= 3 && vol[s->price_hist_len[mt]-1] > 0) {
        float price_dir = px[s->price_hist_len[mt]-1] - px[s->price_hist_len[mt]-2];
        float vol_ratio = vol[s->price_hist_len[mt]-1] / (vol[s->price_hist_len[mt]-2] + 1e-8f);
        // Positive price move + high volume = bid-heavy
        float imbalance = price_dir * vol_ratio;
        fv->ob_imbalance = tanhf(imbalance * (is_binary ? 10.0f : 100.0f)) * 0.5f + 0.5f;
    } else {
        fv->ob_imbalance = 0.5f;
    }

    // F15: OB depth ratio proxy — volume concentration
    if (s->price_hist_len[mt] >= 5) {
        float recent_vol = 0, total_vol = 0;
        for (int i = 0; i < s->price_hist_len[mt]; i++) total_vol += vol[i];
        for (int i = s->price_hist_len[mt] - 3; i < s->price_hist_len[mt]; i++) recent_vol += vol[i];
        fv->ob_depth_ratio = total_vol > 0 ? recent_vol / total_vol : 0.5f;
        // Normalize: expect ~3/N for uniform, higher = concentrated
        float expected = 3.0f / s->price_hist_len[mt];
        fv->ob_depth_ratio = tanhf((fv->ob_depth_ratio - expected) * 5.0f) * 0.5f + 0.5f;
    } else {
        fv->ob_depth_ratio = 0.5f;
    }

    // F16: CVD proxy — cumulative volume delta from price direction
    if (s->price_hist_len[mt] >= 5) {
        float cvd = 0;
        for (int i = s->price_hist_len[mt] - 5; i < s->price_hist_len[mt]; i++) {
            float dir = (i > 0 && px[i] >= px[i-1]) ? 1.0f : -1.0f;
            cvd += dir * vol[i];
        }
        float total_vol = 0;
        for (int i = s->price_hist_len[mt] - 5; i < s->price_hist_len[mt]; i++) total_vol += vol[i];
        fv->cvd_signal = total_vol > 0 ? tanhf(cvd / total_vol) * 0.5f + 0.5f : 0.5f;
    } else {
        fv->cvd_signal = 0.5f;
    }

    // F17: DFT dominant frequency
    fv->dft_dominant = compute_dft_dominant(px, s->price_hist_len[mt]);

    // F18: Tail risk score
    fv->tail_risk_score = compute_tail_risk(px, s->price_hist_len[mt]);

    // F19: Funding rate proxy — momentum of price (perp vs spot basis)
    if (s->price_hist_len[mt] >= 10) {
        float short_mom = px[s->price_hist_len[mt]-1] - px[s->price_hist_len[mt]-3];
        float long_mom = px[s->price_hist_len[mt]-1] - px[s->price_hist_len[mt]-10];
        float basis = short_mom - long_mom;
        fv->funding_signal = tanhf(basis * (is_binary ? 10.0f : 100.0f)) * 0.5f + 0.5f;
    } else {
        fv->funding_signal = 0.5f;
    }

    // F20: OI net signal proxy — volume trend direction
    if (s->price_hist_len[mt] >= 10) {
        float recent_vol = 0, prior_vol = 0;
        for (int i = s->price_hist_len[mt] - 5; i < s->price_hist_len[mt]; i++) recent_vol += vol[i];
        for (int i = s->price_hist_len[mt] - 10; i < s->price_hist_len[mt] - 5; i++) prior_vol += vol[i];
        float oi_trend = prior_vol > 0 ? (recent_vol - prior_vol) / prior_vol : 0;
        fv->oi_net_signal = tanhf(oi_trend) * 0.5f + 0.5f;
    } else {
        fv->oi_net_signal = 0.5f;
    }

    // ════════════════════════════════════════════════════════
    // F21-F27: On-chain proxies from price patterns
    // ════════════════════════════════════════════════════════

    // F21: L/S ratio proxy — buy/sell volume imbalance
    if (s->price_hist_len[mt] >= 5) {
        float buy_vol = 0, sell_vol = 0;
        for (int i = s->price_hist_len[mt] - 5; i < s->price_hist_len[mt]; i++) {
            if (i > 0 && px[i] >= px[i-1])
                buy_vol += vol[i];
            else
                sell_vol += vol[i];
        }
        float total = buy_vol + sell_vol;
        fv->ls_ratio_norm = total > 0 ? buy_vol / total : 0.5f;
    } else {
        fv->ls_ratio_norm = 0.5f;
    }

    // F22: Liquidation cascade proxy — extreme volume + price move
    if (s->price_hist_len[mt] >= 5) {
        float max_vol = 0, avg_vol = 0;
        for (int i = s->price_hist_len[mt] - 5; i < s->price_hist_len[mt]; i++) {
            avg_vol += vol[i];
            if (vol[i] > max_vol) max_vol = vol[i];
        }
        avg_vol /= 5.0f;
        float vol_spike = avg_vol > 0 ? max_vol / avg_vol : 1.0f;
        float price_range = 0;
        if (!is_binary) {
            float h = px[s->price_hist_len[mt]-5], l = h;
            for (int i = s->price_hist_len[mt] - 5; i < s->price_hist_len[mt]; i++) {
                if (px[i] > h) h = px[i];
                if (px[i] < l) l = px[i];
            }
            price_range = (h - l) / (l + 1e-8f);
        } else {
            price_range = fabsf(px[s->price_hist_len[mt]-1] - px[s->price_hist_len[mt]-5]);
        }
        fv->liq_ls_ratio_norm = tanhf((vol_spike - 1.0f) * price_range * 10.0f) * 0.5f + 0.5f;
    } else {
        fv->liq_ls_ratio_norm = 0.5f;
    }

    // F23: Stablecoin inflow proxy — volume acceleration
    if (s->price_hist_len[mt] >= 10) {
        float vol_accel = 0;
        for (int i = s->price_hist_len[mt] - 5; i < s->price_hist_len[mt]; i++) {
            float prev = (i > s->price_hist_len[mt] - 5) ? vol[i-1] : vol[i];
            vol_accel += (vol[i] - prev);
        }
        float avg_vol = 0;
        for (int i = 0; i < s->price_hist_len[mt]; i++) avg_vol += vol[i];
        avg_vol /= s->price_hist_len[mt];
        fv->stable_inflow_norm = tanhf(vol_accel / (avg_vol + 1e-8f)) * 0.5f + 0.5f;
    } else {
        fv->stable_inflow_norm = 0.5f;
    }

    // F24: Whale activity proxy — large volume spikes
    if (s->price_hist_len[mt] >= 10) {
        float avg_vol = 0;
        for (int i = 0; i < s->price_hist_len[mt]; i++) avg_vol += vol[i];
        avg_vol /= s->price_hist_len[mt];
        float max_spike = 0;
        for (int i = s->price_hist_len[mt] - 5; i < s->price_hist_len[mt]; i++) {
            float spike = avg_vol > 0 ? vol[i] / avg_vol : 1.0f;
            if (spike > max_spike) max_spike = spike;
        }
        fv->whale_activity_norm = tanhf((max_spike - 1.0f) * 0.5f) * 0.5f + 0.5f;
    } else {
        fv->whale_activity_norm = 0.5f;
    }

    // F25: Hash rate proxy — price momentum stability (network health)
    if (s->price_hist_len[mt] >= 20) {
        // Stable upward trend = healthy network
        float returns[FEED_HISTORY];
        int n_ret = calc_returns(px, s->price_hist_len[mt], returns);
        float pos_ret = 0, neg_ret = 0;
        for (int i = 0; i < n_ret; i++) {
            if (returns[i] > 0) pos_ret += returns[i];
            else neg_ret += fabsf(returns[i]);
        }
        float total = pos_ret + neg_ret;
        fv->hash_rate_norm = total > 0 ? pos_ret / total : 0.5f;
    } else {
        fv->hash_rate_norm = 0.5f;
    }

    // F26: Difficulty proxy — price position in recent range
    if (s->price_hist_len[mt] >= 10) {
        float h = px[s->price_hist_len[mt]-10], l = h;
        for (int i = s->price_hist_len[mt] - 10; i < s->price_hist_len[mt]; i++) {
            if (px[i] > h) h = px[i];
            if (px[i] < l) l = px[i];
        }
        float range = h - l;
        fv->difficulty_norm = range > 0 ? (px[s->price_hist_len[mt]-1] - l) / range : 0.5f;
    } else {
        fv->difficulty_norm = 0.5f;
    }

    // F27: Miner floor proxy — price vs long-term average
    if (s->price_hist_len[mt] >= 20) {
        float avg = 0;
        for (int i = 0; i < s->price_hist_len[mt]; i++) avg += px[i];
        avg /= s->price_hist_len[mt];
        fv->miner_floor_norm = tanhf((px[s->price_hist_len[mt]-1] - avg) / (avg + 1e-8f) * 5.0f) * 0.5f + 0.5f;
    } else {
        fv->miner_floor_norm = 0.5f;
    }

    // ════════════════════════════════════════════════════════
    // F28-F32: Time / calendar features
    // ════════════════════════════════════════════════════════

    // F28: Hour of day [0,1]
    {
        time_t ts = (time_t)tick->window_ts;
        struct tm *tm_info = localtime(&ts);
        fv->hour_of_day_norm = tm_info->tm_hour / 24.0f + tm_info->tm_min / 1440.0f;
    }

    // F29: Day of week [0,1]
    {
        time_t ts = (time_t)tick->window_ts;
        struct tm *tm_info = localtime(&ts);
        fv->day_of_week_norm = tm_info->tm_wday / 7.0f;
    }

    // F30: IV skew proxy — price volatility asymmetry
    if (s->price_hist_len[mt] >= 10) {
        float returns[FEED_HISTORY];
        int n_ret = calc_returns(px, s->price_hist_len[mt], returns);
        float pos_var = 0, neg_var = 0;
        for (int i = 0; i < n_ret; i++) {
            if (returns[i] > 0) pos_var += returns[i] * returns[i];
            else neg_var += returns[i] * returns[i];
        }
        float total_var = pos_var + neg_var;
        // IV skew: put demand = more negative variance
        fv->iv_skew = total_var > 0 ? neg_var / total_var : 0.5f;
    } else {
        fv->iv_skew = 0.5f;
    }

    // F31: PCR volume proxy — down-volume / total-volume ratio
    if (s->price_hist_len[mt] >= 5) {
        float down_vol = 0, total_vol = 0;
        for (int i = s->price_hist_len[mt] - 5; i < s->price_hist_len[mt]; i++) {
            total_vol += vol[i];
            if (i > 0 && px[i] < px[i-1]) down_vol += vol[i];
        }
        fv->pcr_volume = total_vol > 0 ? down_vol / total_vol : 0.5f;
    } else {
        fv->pcr_volume = 0.5f;
    }

    // F32: IV term structure slope — short-term vs long-term volatility
    if (s->price_hist_len[mt] >= 20) {
        float returns[FEED_HISTORY];
        int n_ret = calc_returns(px, s->price_hist_len[mt], returns);
        float short_var = 0, long_var = 0;
        int short_n = 5;
        int long_n = n_ret;
        for (int i = 0; i < long_n; i++) {
            long_var += returns[i] * returns[i];
        }
        for (int i = long_n - short_n; i < long_n; i++) {
            short_var += returns[i] * returns[i];
        }
        long_var /= long_n;
        short_var /= short_n;
        float slope = long_var > 0 ? short_var / long_var : 1.0f;
        fv->iv_term_slope = tanhf((slope - 1.0f) * 2.0f) * 0.5f + 0.5f;
    } else {
        fv->iv_term_slope = 0.5f;
    }

    // ════════════════════════════════════════════════════════
    // F33-F34: Cross-asset correlation (SP500, VIX from aux DB)
    // ════════════════════════════════════════════════════════

    // F33: BTC-SP500 correlation
    {
        float spx[FEED_HISTORY];
        for (int i = 0; i < s->sp500_hist_len; i++) {
            int idx = (s->sp500_hist_idx - s->sp500_hist_len + i + FEED_HISTORY) % FEED_HISTORY;
            spx[i] = s->sp500_hist[idx];
        }
        fv->btc_sp500_corr = calc_sp500_corr(px, s->price_hist_len[mt], spx, s->sp500_hist_len);
    }

    // F34: VIX regime
    {
        float vix_val = tick->vix;
        if (vix_val < 0.1f) vix_val = 15.0f;
        fv->vix_regime = (vix_val - 10.0f) / 30.0f;
        if (fv->vix_regime < 0.0f) fv->vix_regime = 0.0f;
        if (fv->vix_regime > 1.0f) fv->vix_regime = 1.0f;
    }

    // ════════════════════════════════════════════════════════
    // F35-F36: Weather (from aux DB via paper_load_aux, or defaults)
    // ════════════════════════════════════════════════════════
    // These are set by paper_load_aux() which reads from historical.db
    // We use tick fields that aux data populates
    fv->weather_temp_zscore = 0.5f;  // Default neutral; aux data overrides
    fv->weather_precip_anom = 0.5f;

    // ════════════════════════════════════════════════════════
    // F37-F50: Advanced price-derived features
    // ════════════════════════════════════════════════════════

    // F37: Inter-exchange basis proxy — price oscillation intensity
    if (s->price_hist_len[mt] >= 5) {
        float oscillation = 0;
        for (int i = s->price_hist_len[mt] - 5; i < s->price_hist_len[mt]; i++) {
            if (i > 0) oscillation += fabsf(px[i] - px[i-1]);
        }
        float avg_px = 0;
        for (int i = 0; i < s->price_hist_len[mt]; i++) avg_px += px[i];
        avg_px /= s->price_hist_len[mt];
        fv->interexchange_basis = tanhf(oscillation / (avg_px + 1e-8f) * 10.0f) * 0.5f + 0.5f;
    } else {
        fv->interexchange_basis = 0.5f;
    }

    // F38: Economic surprise proxy — unexpected price movement
    if (s->price_hist_len[mt] >= 10) {
        float expected_move = 0;
        for (int i = s->price_hist_len[mt] - 10; i < s->price_hist_len[mt] - 1; i++) {
            expected_move += fabsf(px[i+1] - px[i]);
        }
        expected_move /= 9.0f;
        float actual_move = fabsf(px[s->price_hist_len[mt]-1] - px[s->price_hist_len[mt]-2]);
        float surprise = expected_move > 0 ? (actual_move - expected_move) / expected_move : 0;
        fv->economic_surprise = tanhf(surprise) * 0.5f + 0.5f;
    } else {
        fv->economic_surprise = 0.5f;
    }

    // F39: News sentiment delta — price acceleration (news drives acceleration)
    if (s->price_hist_len[mt] >= 5) {
        float accel = (px[s->price_hist_len[mt]-1] - px[s->price_hist_len[mt]-2]) -
                      (px[s->price_hist_len[mt]-2] - px[s->price_hist_len[mt]-3]);
        fv->news_sentiment_delta = tanhf(accel * (is_binary ? 10.0f : 100.0f)) * 0.5f + 0.5f;
    } else {
        fv->news_sentiment_delta = 0.5f;
    }

    // F40: Social volume spike — volume spike z-score
    if (s->price_hist_len[mt] >= 10) {
        float avg_vol = 0, std_vol = 0;
        for (int i = 0; i < s->price_hist_len[mt]; i++) avg_vol += vol[i];
        avg_vol /= s->price_hist_len[mt];
        for (int i = 0; i < s->price_hist_len[mt]; i++) {
            float d = vol[i] - avg_vol;
            std_vol += d * d;
        }
        std_vol = sqrtf(std_vol / s->price_hist_len[mt]);
        float current_vol = vol[s->price_hist_len[mt]-1];
        float z_score = std_vol > 0 ? (current_vol - avg_vol) / std_vol : 0;
        fv->social_volume_spike = tanhf(z_score * 0.5f) * 0.5f + 0.5f;
    } else {
        fv->social_volume_spike = 0.5f;
    }

    // F41-F42: Return skewness and kurtosis
    if (s->price_hist_len[mt] >= 10) {
        float returns[FEED_HISTORY];
        int n_ret = calc_returns(px, s->price_hist_len[mt], returns);
        float sum = 0;
        for (int i = 0; i < n_ret; i++) sum += returns[i];
        float mean = sum / n_ret;
        float m2 = 0, m3 = 0, m4 = 0;
        for (int i = 0; i < n_ret; i++) {
            float d = returns[i] - mean;
            m2 += d * d;
            m3 += d * d * d;
            m4 += d * d * d * d;
        }
        float var = m2 / n_ret;
        float std = sqrtf(var > 0.0f ? var : 1e-8f);
        float skew = (m3 / n_ret) / (std * std * std + 1e-8f);
        fv->return_skew = tanhf(skew * 2.0f) * 0.5f + 0.5f;
        float kurt = (m4 / n_ret) / (var * var + 1e-8f) - 3.0f;
        fv->return_kurtosis = tanhf(kurt * 0.5f) * 0.5f + 0.5f;
    } else {
        fv->return_skew = 0.5f;
        fv->return_kurtosis = 0.5f;
    }

    // F43: Realized vol ratio (short-term / long-term)
    if (s->price_hist_len[mt] >= 20) {
        float returns[FEED_HISTORY];
        int n_ret = calc_returns(px, s->price_hist_len[mt], returns);
        float short_var = 0, long_var = 0;
        float short_mean = 0, long_mean = 0;
        int short_n = 5;
        int long_n = n_ret;
        for (int i = 0; i < long_n; i++) long_mean += returns[i];
        long_mean /= long_n;
        for (int i = 0; i < long_n; i++) { float d = returns[i] - long_mean; long_var += d * d; }
        long_var /= long_n;
        for (int i = long_n - short_n; i < long_n; i++) short_mean += returns[i];
        short_mean /= short_n;
        for (int i = long_n - short_n; i < long_n; i++) { float d = returns[i] - short_mean; short_var += d * d; }
        short_var /= short_n;
        float vol_ratio = (long_var > 1e-8f) ? sqrtf(short_var / long_var) : 1.0f;
        fv->realized_vol_ratio = tanhf((vol_ratio - 1.0f) * 2.0f) * 0.5f + 0.5f;
    } else {
        fv->realized_vol_ratio = 0.5f;
    }

    // F44: OB imbalance change — delta of F14
    // (computed from current and previous OB imbalance proxy)
    // For now, use price acceleration as proxy
    if (s->price_hist_len[mt] >= 3) {
        float curr_dir = px[s->price_hist_len[mt]-1] - px[s->price_hist_len[mt]-2];
        float prev_dir = px[s->price_hist_len[mt]-2] - px[s->price_hist_len[mt]-3];
        float delta = curr_dir - prev_dir;
        fv->ob_imbalance_change = tanhf(delta * (is_binary ? 10.0f : 100.0f)) * 0.5f + 0.5f;
    } else {
        fv->ob_imbalance_change = 0.5f;
    }

    // F45: CVD trend — slope of F16
    if (s->price_hist_len[mt] >= 10) {
        float cvd_recent = 0, cvd_prior = 0;
        for (int i = s->price_hist_len[mt] - 3; i < s->price_hist_len[mt]; i++) {
            if (i > 0 && px[i] >= px[i-1]) cvd_recent += vol[i]; else cvd_recent -= vol[i];
        }
        for (int i = s->price_hist_len[mt] - 6; i < s->price_hist_len[mt] - 3; i++) {
            if (i > 0 && px[i] >= px[i-1]) cvd_prior += vol[i]; else cvd_prior -= vol[i];
        }
        float trend = cvd_recent - cvd_prior;
        fv->cvd_trend = tanhf(trend / (vol[s->price_hist_len[mt]-1] + 1e-8f)) * 0.5f + 0.5f;
    } else {
        fv->cvd_trend = 0.5f;
    }

    // F46: Liquidation cascade — F22 (already computed above)
    fv->liq_cascade = fv->liq_ls_ratio_norm;

    // F47: Funding rate change — delta of F19
    if (s->price_hist_len[mt] >= 15) {
        float short_mom_recent = px[s->price_hist_len[mt]-1] - px[s->price_hist_len[mt]-3];
        float short_mom_prior = px[s->price_hist_len[mt]-5] - px[s->price_hist_len[mt]-7];
        float long_mom = px[s->price_hist_len[mt]-1] - px[s->price_hist_len[mt]-10];
        float basis_recent = short_mom_recent - long_mom;
        float basis_prior = short_mom_prior - long_mom;
        float change = basis_recent - basis_prior;
        fv->funding_rate_change = tanhf(change * 100.0f) * 0.5f + 0.5f;
    } else {
        fv->funding_rate_change = 0.5f;
    }

    // F48: OI change — delta of F20
    if (s->price_hist_len[mt] >= 10) {
        float vol_recent = 0, vol_prior = 0;
        for (int i = s->price_hist_len[mt] - 3; i < s->price_hist_len[mt]; i++) vol_recent += vol[i];
        for (int i = s->price_hist_len[mt] - 6; i < s->price_hist_len[mt] - 3; i++) vol_prior += vol[i];
        float change = vol_recent - vol_prior;
        float avg = (vol_recent + vol_prior) / 2.0f;
        fv->oi_change = tanhf(change / (avg + 1e-8f)) * 0.5f + 0.5f;
    } else {
        fv->oi_change = 0.5f;
    }

    // F49-F50: TWAP/VWAP proximity
    if (s->price_hist_len[mt] >= 5 && price_val > 0.0f) {
        float twap = 0;
        for (int i = 0; i < s->price_hist_len[mt]; i++) twap += px[i];
        twap /= s->price_hist_len[mt];
        fv->twap_proximity = 1.0f - fabsf(price_val - twap) / (price_val + 1e-8f);
        if (fv->twap_proximity < 0.0f) fv->twap_proximity = 0.0f;
        fv->vwap_proximity = fv->twap_proximity;
    } else {
        fv->twap_proximity = 0.5f;
        fv->vwap_proximity = 0.5f;
    }

    // ════════════════════════════════════════════════════════
    // F51-F64: Regime / risk / calendar features
    // ════════════════════════════════════════════════════════

    // F51: Overnight gap risk (non-crypto markets)
    if (tick->market_type != MARKET_CRYPTO && tick->market_type != MARKET_PREDICTION &&
        s->prev_close > 0) {
        float gap = fabsf(tick->open - s->prev_close) / s->prev_close;
        fv->overnight_gap_risk = tanhf(gap * 10.0f);
    } else {
        fv->overnight_gap_risk = 0.0f;
    }

    // F52: Weekend slippage
    {
        time_t ts = (time_t)tick->window_ts;
        struct tm *tm_info = localtime(&ts);
        int wday = tm_info->tm_wday;
        fv->weekend_slippage = (wday == 0 || wday == 6) ? 1.0f : 0.0f;
    }

    // F53: Room ensemble signal — nested prediction (from nested HT model)
    fv->room_ensemble_signal = 0.5f;  // Default; nested model overrides if loaded

    // F54: Feed freshness score — 1.0 = fresh, degrades with time
    {
        time_t now = time(NULL);
        int age = (int)(now - (time_t)tick->window_ts);
        if (age < 60) fv->feed_freshness_score = 1.0f;
        else if (age < 300) fv->feed_freshness_score = 0.8f;
        else if (age < 900) fv->feed_freshness_score = 0.5f;
        else if (age < 3600) fv->feed_freshness_score = 0.2f;
        else fv->feed_freshness_score = 0.0f;
    }

    // F55: Volatility regime change probability
    fv->vol_regime_change = fabsf(fv->regime_indicator * 2.0f - 1.0f);

    // F56: Correlation breakdown
    if (s->sp500_hist_len >= 10) {
        float recent_corr = fv->btc_sp500_corr * 2.0f - 1.0f;
        fv->corr_breakdown = (fv->vix_regime > 0.7f && recent_corr > 0.3f) ? 0.8f : 0.1f;
    } else {
        fv->corr_breakdown = 0.1f;
    }

    // F57: Options flow signal — F30 (IV skew) momentum
    fv->options_flow_signal = fv->iv_skew;

    // F58: Dark pool signal — F24 (whale activity) proxy
    fv->dark_pool_signal = fv->whale_activity_norm;

    // F59: Insider trade signal — F39 (news sentiment) proxy
    fv->insider_trade_signal = fv->news_sentiment_delta;

    // F60: Institutional flow — F20 (OI net signal) proxy
    fv->institutional_flow = fv->oi_net_signal;

    // F61: Short interest signal — F21 (L/S ratio) proxy
    fv->short_interest_signal = fv->ls_ratio_norm;

    // F62: ETF flow signal — F23 (stablecoin inflow) proxy
    fv->etf_flow_signal = fv->stable_inflow_norm;

    // F63: Seasonality signal
    {
        time_t ts = (time_t)tick->window_ts;
        struct tm *tm_info = localtime(&ts);
        int mday = tm_info->tm_mday;
        int month = tm_info->tm_mon;
        float eom = (mday >= 25) ? 0.7f : 0.5f;
        float jan = (month == 0) ? 0.7f : 0.5f;
        fv->seasonality_signal = (eom + jan) * 0.5f;
    }

    // F64: Reserved
    fv->_reserved_64 = 0.0f;

    // ════════════════════════════════════════════════════════
    // NORMALIZATION — all features to [0,1] or [-1,1] → [0,1]
    // ════════════════════════════════════════════════════════
    float delta_scale = 5.0f;
    float mom_scale = 2.0f;
    if (s->predicted_regime == 1) { delta_scale = 3.0f; mom_scale = 1.2f; }
    else if (s->predicted_regime == 2) { delta_scale = 10.0f; mom_scale = 4.0f; }

    fv->price_delta_pct = tanhf(fv->price_delta_pct / delta_scale);
    fv->micro_momentum = tanhf(fv->micro_momentum / mom_scale);
    fv->rsi_7 = fv->rsi_7 / 100.0f;
    if (fv->volume_surge_ratio > 0.0f) {
        fv->volume_surge_ratio = logf(fv->volume_surge_ratio) / 3.0f + 0.5f;
    }
    if (fv->volume_surge_ratio < 0.0f) fv->volume_surge_ratio = 0.0f;
    if (fv->volume_surge_ratio > 1.0f) fv->volume_surge_ratio = 1.0f;
    if (!is_binary) {
        fv->ema_fast = tanhf((fv->ema_fast / price_val - 1.0f) * 5.0f) * 0.5f + 0.5f;
        fv->ema_slow = tanhf((fv->ema_slow / price_val - 1.0f) * 5.0f) * 0.5f + 0.5f;
        fv->macd_hist = tanhf(fv->macd_hist / (price_val * 0.01f + 0.001f));
    }
    fv->divergence_score = fv->divergence_score * 0.5f + 0.5f;
    fv->pump_score = fv->pump_score * 0.5f + 0.5f;
    fv->regime_indicator = fv->regime_indicator / 2.0f;
    fv->btc_sp500_corr = fv->btc_sp500_corr * 0.5f + 0.5f;

    return ERR_OK;
}
