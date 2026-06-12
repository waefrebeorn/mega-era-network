/**
 * snowball.h — Snowball Fund Architecture
 *
 * Compounding rules for growing $50 → $500 → $2K → $10K
 *
 * TIERS:
 *   Tier 1: $50-$100    — Conservative (max 10% per trade, 1 trade/day)
 *   Tier 2: $100-$500   — Moderate (max 15% per trade, 2 trades/day)
 *   Tier 3: $500-$2000  — Aggressive (max 20% per trade, 3 trades/day)
 *   Tier 4: $2000+       — Full deployment (max 25% per trade, 5 trades/day)
 *
 * RULES:
 *   - Loss is death: any tier drop triggers 24h cooldown
 *   - Withdraw 50% of profits above tier threshold
 *   - Reinvest remaining 50% to compound
 *   - Max daily loss: 10% of current tier floor
 *   - Consecutive loss limit: 3 (return to paper mode)
 *
 * COMPILE: included by live_trader.c (no separate compilation needed)
 */

#ifndef SNOWBALL_H
#define SNOWBALL_H

#include <math.h>

typedef enum {
    TIER_CONSERVATIVE = 0,  // $50-$100
    TIER_MODERATE     = 1,  // $100-$500
    TIER_AGGRESSIVE   = 2,  // $500-$2000
    TIER_FULL         = 3,  // $2000+
    N_TIERS           = 4
} SnowballTier;

typedef struct {
    float tier_floor;       // Minimum capital for this tier
    float tier_cap;         // Maximum capital for this tier
    float max_position_pct; // Max % of capital per trade
    int   max_trades_day;   // Max trades per day
    float max_daily_loss;   // Max daily loss ($)
    float withdraw_pct;     // % of profits to withdraw (vs reinvest)
    const char *name;       // Human-readable name
} TierConfig;

static const TierConfig TIERS[N_TIERS] = {
    {  50.0f,  100.0f, 0.10f, 1,  5.0f, 0.50f, "Conservative" },
    { 100.0f,  500.0f, 0.15f, 2, 10.0f, 0.50f, "Moderate"     },
    { 500.0f, 2000.0f, 0.20f, 3, 50.0f, 0.40f, "Aggressive"   },
    {2000.0f, 99999.0f, 0.25f, 5, 100.0f, 0.30f, "Full"        }
};

typedef struct {
    SnowballTier tier;          // Current tier
    float        capital;       // Current capital
    float        peak_capital;  // All-time peak
    float        total_withdrawn; // Total profits withdrawn
    float        daily_pnl;     // Today's PnL
    int          daily_trades;  // Today's trade count
    int          consec_losses; // Consecutive losses
    int          cooldown;      // Cooldown cycles remaining
    int64_t      day_start;     // Timestamp of current day
    int          tier_drop;     // 1 if dropped to lower tier today
} SnowballState;

static inline SnowballState snowball_init(float starting_capital) {
    SnowballState s = {0};
    s.capital = starting_capital;
    s.peak_capital = starting_capital;
    s.tier = TIER_CONSERVATIVE;
    s.day_start = 0;
    return s;
}

static inline SnowballTier snowball_get_tier(float capital) {
    for (int i = N_TIERS - 1; i >= 0; i--) {
        if (capital >= TIERS[i].tier_floor) return (SnowballTier)i;
    }
    return TIER_CONSERVATIVE;
}

static inline void snowball_new_day(SnowballState *s, int64_t now) {
    int64_t day = now / 86400;
    if (day != s->day_start) {
        s->day_start = day;
        s->daily_pnl = 0.0f;
        s->daily_trades = 0;
        s->tier_drop = 0;
        if (s->cooldown > 0) s->cooldown--;
    }
}

static inline int snowball_can_trade(SnowballState *s) {
    if (s->cooldown > 0) return 0;
    if (s->tier_drop) return 0;  // Dropped tier — cooldown
    if (s->consec_losses >= 3) return 0;
    if (s->daily_trades >= TIERS[s->tier].max_trades_day) return 0;
    if (s->daily_pnl <= -TIERS[s->tier].max_daily_loss) return 0;
    return 1;
}

static inline float snowball_max_stake(SnowballState *s) {
    return s->capital * TIERS[s->tier].max_position_pct;
}

static inline void snowball_record_trade(SnowballState *s, float pnl) {
    s->daily_pnl += pnl;
    s->daily_trades++;
    s->capital += pnl;

    if (s->capital > s->peak_capital) {
        s->peak_capital = s->capital;
    }

    if (pnl < 0) {
        s->consec_losses++;
    } else {
        s->consec_losses = 0;
    }

    // Check for tier change
    SnowballTier new_tier = snowball_get_tier(s->capital);
    if (new_tier < s->tier) {
        // Dropped tier — trigger cooldown
        s->tier_drop = 1;
        s->cooldown = 24;  // 24 hours
    } else if (new_tier > s->tier) {
        // Promoted — withdraw excess
        float excess = s->capital - TIERS[new_tier].tier_floor;
        float withdraw_amt = excess * TIERS[new_tier].withdraw_pct;
        s->capital -= withdraw_amt;
        s->total_withdrawn += withdraw_amt;
    }
    s->tier = new_tier;
}

static inline float snowball_withdraw_profit(SnowballState *s) {
    if (s->capital <= s->peak_capital) return 0.0f;
    float profit = s->capital - s->peak_capital;
    float withdraw_amt = profit * TIERS[s->tier].withdraw_pct;
    s->capital -= withdraw_amt;
    s->total_withdrawn += withdraw_amt;
    return withdraw_amt;
}

#endif /* SNOWBALL_H */
