/**
 * room_features_test.c — Test wrapper for room_features module
 * Compile: gcc -O2 -o room_features room_features_test.c -lm
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "types.h"

// Include the module functions
extern void room_features_compute(const MarketTick *tick, FeatureVector *fv, RoomState *s);

int main(void) {
    printf("room_features test: module compiled and linked successfully\n");

    // Create minimal test market tick
    MarketTick tick = {0};
    tick.asset[0] = 'B'; tick.asset[1] = 'T'; tick.asset[2] = 'C'; tick.asset[3] = '\0';
    tick.window_ts = time(NULL);
    tick.market_type = MARKET_CRYPTO;
    tick.open = 50000.0f;
    tick.high = 51000.0f;
    tick.low = 49000.0f;
    tick.close = 50500.0f;
    tick.volume = 1000.0f;
    tick.fear_greed = 55.0f;
    tick.pump_score = 0.2f;
    tick.btc_dominance = 55.0f;
    tick.vix = 18.0f;
    tick.sp500 = 4500.0f;
    tick.btc_30d_volatility = 0.04f;
    tick.btc_30d_mean = 48000.0f;
    tick.btc_30d_high = 55000.0f;
    tick.btc_30d_low = 42000.0f;
    tick.ob_imbalance = 0.55f;
    tick.ob_depth_ratio = 0.52f;
    tick.ob_wall_conc = 0.15f;
    tick.ob_spread_norm = 0.02f;
    tick.cvd_signal = 0.1f;
    tick.funding_signal = 0.01f;
    tick.oi_net_signal = 0.5f;
    tick.ls_ratio_norm = 0.52f;
    tick.liq_ls_ratio_norm = 0.48f;
    tick.stable_inflow_norm = 0.6f;
    tick.whale_activity_norm = 0.3f;
    tick.hash_rate_norm = 0.7f;
    tick.difficulty_norm = 0.65f;
    tick.miner_floor_norm = 0.55f;
    tick.hour_of_day_norm = 0.5f;
    tick.day_of_week_norm = 0.3f;
    tick.iv_skew = 0.4f;
    tick.pcr_volume = 0.8f;
    tick.iv_term_slope = 0.1f;

    // Create minimal room state
    RoomState s = {0};
    s.magic = STATE_MAGIC;
    s.state_version = STATE_VERSION;
    // Initialize history buffers
    for (int m = 0; m < N_FEED_MARKETS; m++) {
        s.price_hist_len[m] = 0;
        s.price_hist_idx[m] = 0;
    }
    s.sp500_hist_len = 0;
    s.sp500_hist_idx = 0;
    s.vix_hist_len = 0;
    s.vix_hist_idx = 0;

    FeatureVector fv = {0};

    printf("Testing room_features_compute...\n");
    room_features_compute(&tick, &fv, &s);

    printf("Features computed: price_delta=%.4f, rsi_7=%.2f, vol_surge=%.4f, ema_cross=%.4f\n",
           fv.price_delta_pct, fv.rsi_7, fv.volume_surge_ratio,
           fv.ema_fast - fv.ema_slow);

    // Check that features are non-zero (meaning computation happened)
    int non_zero = 0;
    #define CHECK_FEAT(name, val) if (fabsf(val) > 1e-6) { printf("  %s: %.6f\n", name, val); non_zero++; }

    CHECK_FEAT("price_delta_pct", fv.price_delta_pct);
    CHECK_FEAT("micro_momentum", fv.micro_momentum);
    CHECK_FEAT("rsi_7", fv.rsi_7);
    CHECK_FEAT("volume_surge_ratio", fv.volume_surge_ratio);
    CHECK_FEAT("ema_fast", fv.ema_fast);
    CHECK_FEAT("ema_slow", fv.ema_slow);
    CHECK_FEAT("macd_hist", fv.macd_hist);
    CHECK_FEAT("bollinger_pct", fv.bollinger_pct);
    CHECK_FEAT("divergence_score", fv.divergence_score);
    CHECK_FEAT("pump_score", fv.pump_score);
    CHECK_FEAT("regime_indicator", fv.regime_indicator);
    CHECK_FEAT("fear_greed_norm", fv.fear_greed_norm);
    CHECK_FEAT("herd_consensus", fv.herd_consensus);
    CHECK_FEAT("ob_imbalance", fv.ob_imbalance);
    CHECK_FEAT("ob_depth_ratio", fv.ob_depth_ratio);
    CHECK_FEAT("cvd_signal", fv.cvd_signal);
    CHECK_FEAT("dft_dominant", fv.dft_dominant);
    CHECK_FEAT("tail_risk_score", fv.tail_risk_score);
    CHECK_FEAT("funding_signal", fv.funding_signal);
    CHECK_FEAT("oi_net_signal", fv.oi_net_signal);
    CHECK_FEAT("ls_ratio_norm", fv.ls_ratio_norm);
    CHECK_FEAT("liq_ls_ratio_norm", fv.liq_ls_ratio_norm);
    CHECK_FEAT("stable_inflow_norm", fv.stable_inflow_norm);
    CHECK_FEAT("whale_activity_norm", fv.whale_activity_norm);
    CHECK_FEAT("hash_rate_norm", fv.hash_rate_norm);
    CHECK_FEAT("difficulty_norm", fv.difficulty_norm);
    CHECK_FEAT("miner_floor_norm", fv.miner_floor_norm);
    CHECK_FEAT("hour_of_day_norm", fv.hour_of_day_norm);
    CHECK_FEAT("day_of_week_norm", fv.day_of_week_norm);
    CHECK_FEAT("iv_skew", fv.iv_skew);
    CHECK_FEAT("pcr_volume", fv.pcr_volume);
    CHECK_FEAT("iv_term_slope", fv.iv_term_slope);

    printf("Non-zero features: %d / %d\n", non_zero, N_FEATURES);

    if (non_zero > 0) {
        printf("✅ room_features test passed\n");
        return 0;
    } else {
        printf("❌ room_features test failed: all features zero\n");
        return 1;
    }
}
