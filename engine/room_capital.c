/**
 * room_capital.c — L4: Peer-to-Peer Capital Allocation
 *
 * Zero-sum P2P matching. YES votes matched vs NO votes.
 * Winners split losers' stake. Fees deducted from matched portion only.
 *
 * Flow per cycle:
 *   1. Count YES total stake vs NO total stake
 *   2. Match min(YES, NO) — unmatched portion never leaves agent's capital
 *   3. Deduct matched_stake + fee from each agent's capital
 *   4. On resolution: winners get matched_stake back + share of loser pool
 *                     Losers lose their matched_stake entirely
 */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "types.h"

typedef struct {
    int agent_id;
    float stake;
    float conviction;
} Staker;

// ── C23: Duplicate trade detection ──
// Generate a unique key for each (agent_id, window_ts, direction) triple
static inline int64_t trade_key(int agent_id, int64_t window_ts, bool direction) {
    return ((int64_t)agent_id << 33) | ((window_ts & 0xFFFFFFFFULL) << 1) | (direction ? 1ULL : 0ULL);
}

// ════════════════════════════════════════════════════════
//  R4: CIRCUIT BREAKER — Pre-trade risk controls
//  Trips on: daily loss > max_daily_loss_pct, consecutive losses > max_consecutive_losses,
//            room drawdown > max_drawdown_pct, max exposure exceeded, panic stop
// ════════════════════════════════════════════════════════
static bool check_circuit_breaker_before_trade(RoomState *s, AgentState *agents, int n_agents, int64_t window_ts) {
    // Panic stop — external halt (e.g., /tmp/money_room_panic file)
    if (s->panic_stop) {
        fprintf(stderr, "[R4] CIRCUIT BREAKER: Panic stop active — all trading halted\n");
        return true;
    }

    // Initialize defaults if not set
    if (s->max_daily_loss_pct == 0.0f) s->max_daily_loss_pct = 0.10f;  // 10% daily loss limit
    if (s->max_drawdown_pct == 0.0f) s->max_drawdown_pct = 0.20f;       // 20% room drawdown limit
    if (s->max_consecutive_losses == 0) s->max_consecutive_losses = 6;  // 6 consecutive losses
    if (s->circuit_cooldown_cycles == 0) s->circuit_cooldown_cycles = 10; // 10-cycle cooldown

    // Check if we're in cooldown period
    if (s->circuit_breaker_cycles > 0) {
        s->circuit_breaker_cycles--;
        fprintf(stderr, "[R4] CIRCUIT BREAKER: Cooling down, %d cycles remaining\n", s->circuit_breaker_cycles);
        return true;
    }

    // Reset daily PnL if new day
    int current_day = (int)(window_ts / 86400LL);
    if (s->last_daily_reset_day != current_day) {
        s->daily_pnl = 0.0f;
        s->daily_loss_streak = 0;
        s->last_daily_reset_day = current_day;
    }

    // Calculate current room capital
    float total_cap = 0.0f;
    for (int i = 0; i < n_agents; i++) {
        if (agents[i].alive && agents[i].capital > 0)
            total_cap += agents[i].capital;
    }

    // 1. Daily loss limit check
    if (total_cap > 0 && s->daily_pnl < -s->max_daily_loss_pct * total_cap) {
        fprintf(stderr, "[R4] CIRCUIT BREAKER TRIPPED: Daily loss %.2f%% exceeds %.2f%%\n",
                -s->daily_pnl / total_cap * 100, s->max_daily_loss_pct * 100);
        s->circuit_breaker_cycles = s->circuit_cooldown_cycles;
        s->circuit_breaker_count++;
        s->circuit_breaker_ts = window_ts;
        s->circuit_breaker_peak = total_cap;  // Reset peak at breaker
        return true;
    }

    // 2. Consecutive room losses
    if (s->consec_room_losses >= s->max_consecutive_losses) {
        fprintf(stderr, "[R4] CIRCUIT BREAKER TRIPPED: %d consecutive room losses (max %d)\n",
                s->consec_room_losses, s->max_consecutive_losses);
        s->circuit_breaker_cycles = s->circuit_cooldown_cycles;
        s->circuit_breaker_count++;
        s->circuit_breaker_ts = window_ts;
        s->circuit_breaker_peak = total_cap;
        return true;
    }

    // 3. Room drawdown check
    if (s->circuit_breaker_peak > 0 && total_cap > 0) {
        float dd = (s->circuit_breaker_peak - total_cap) / s->circuit_breaker_peak;
        if (dd > s->max_drawdown_pct) {
            fprintf(stderr, "[R4] CIRCUIT BREAKER TRIPPED: Room drawdown %.2f%% exceeds %.2f%%\n",
                    dd * 100, s->max_drawdown_pct * 100);
            s->circuit_breaker_cycles = s->circuit_cooldown_cycles;
            s->circuit_breaker_count++;
            s->circuit_breaker_ts = window_ts;
            s->circuit_breaker_peak = total_cap;
            return true;
        }
        // Update peak if new high
        if (total_cap > s->circuit_breaker_peak)
            s->circuit_breaker_peak = total_cap;
    }

    // 4. Max total exposure check
    if (s->max_total_exposure_pct > 0 && total_cap > 0) {
        float exposure_pct = s->current_total_exposure / total_cap;
        if (exposure_pct > s->max_total_exposure_pct) {
            fprintf(stderr, "[R4] CIRCUIT BREAKER TRIPPED: Total exposure %.2f%% exceeds %.2f%%\n",
                    exposure_pct * 100, s->max_total_exposure_pct * 100);
            s->circuit_breaker_cycles = s->circuit_cooldown_cycles;
            s->circuit_breaker_count++;
            s->circuit_breaker_ts = window_ts;
            return true;
        }
    }

    // 5. Directional exposure check (C36)
    if (s->max_direction_pct > 0 && total_cap > 0) {
        float yes_pct = s->current_yes_exposure / total_cap;
        float no_pct = s->current_no_exposure / total_cap;
        if (yes_pct > s->max_direction_pct || no_pct > s->max_direction_pct) {
            fprintf(stderr, "[R4] CIRCUIT BREAKER TRIPPED: Directional exposure YES=%.2f%% NO=%.2f%% exceeds max=%.2f%%\n",
                    yes_pct * 100, no_pct * 100, s->max_direction_pct * 100);
            s->circuit_breaker_cycles = s->circuit_cooldown_cycles;
            s->circuit_breaker_count++;
            s->circuit_breaker_ts = window_ts;
            return true;
        }
    }

    return false;  // OK to trade
}

// Check if this trade key exists in recent history (last 1024 trades)
static bool is_duplicate_trade(RoomState *s, int64_t key) {
    for (int i = 0; i < 1024; i++) {
        if (s->recent_trade_keys[i] == key) return true;
    }
    return false;
}

// Record a trade key in the rolling buffer
static void record_trade_key(RoomState *s, int64_t key) {
    s->recent_trade_keys[s->recent_trade_key_idx] = key;
    s->recent_trade_key_idx = (s->recent_trade_key_idx + 1) % 1024;
}

// ── C07: Correlation-based position limits ──
// Check if adding this stake to the asset-direction bucket would exceed
// the correlated-basket limit. Returns 1 if trade should be blocked.
static int check_correlation_exposure(RoomState *s, int asset_id, bool direction,
                                       float stake, float total_room_cap) {
    if (asset_id < 0 || asset_id >= MAX_ASSETS) return 0;
    if (total_room_cap <= 0) return 0;

    // Calculate what the new exposure would be for this asset-direction
    float new_exposure = s->asset_exposure[asset_id][direction] + stake;

    // Sum exposure across all assets that are correlated with this one
    float basket_exposure = 0;
    for (int a = 0; a < MAX_ASSETS; a++) {
        if (a == asset_id) {
            basket_exposure += new_exposure;
        } else {
            // Check if asset a is correlated with asset_id
            float corr = s->cross_room_correlation[a][asset_id];
            if (corr < 0) corr = -corr;
            if (corr >= CORRELATION_THRESHOLD) {
                // This asset is correlated — include its exposure in basket
                basket_exposure += s->asset_exposure[a][0] + s->asset_exposure[a][1];
            }
        }
    }

    float basket_pct = basket_exposure / total_room_cap;
    if (basket_pct > MAX_CORRELATION_EXPOSURE_PCT) {
        return 1;  // Block: correlated basket would exceed limit
    }

    return 0;  // OK
}

// After a trade is matched, update asset exposure
static void update_asset_exposure(RoomState *s, int asset_id, bool direction, float stake) {
    if (asset_id < 0 || asset_id >= MAX_ASSETS) return;
    s->asset_exposure[asset_id][direction] += stake;
}

// ── D44: Exchange fee lookup ──
// Return the fee rate for a given asset (defaults to TAKER_FEE)
static float get_exchange_fee(RoomState *s, int asset_id) {
    if (asset_id >= 0 && asset_id < MAX_ASSETS && s->exchange_fees[asset_id] > 0.0f) {
        return s->exchange_fees[asset_id];
    }
    return TAKER_FEE;  // default
}

// Return the minimum order size for a given asset
static float get_min_order(RoomState *s, int asset_id) {
    if (asset_id >= 0 && asset_id < MAX_ASSETS && s->exchange_min_orders[asset_id] > 0.0f) {
        return s->exchange_min_orders[asset_id];
    }
    return MIN_TRADE_STAKE;  // default
}

// ════════════════════════════════════════════════════════
//  C11: POSITION LIQUIDATION MODEL
//  Forces closing positions when risk thresholds breached:
//   - Correlation basket limit exceeded
//   - Single position > 10% of agent capital
//   - Portfolio drawdown > MAX_DRAWDOWN_PCT
// ════════════════════════════════════════════════════════

// Liquidate one agent's position in one asset-direction bucket
static void liquidate_position(AgentState *a, int asset_id, bool direction, RoomState *s) {
    if (!a || asset_id < 0 || asset_id >= MAX_ASSETS) return;
    float amount = s->asset_exposure[asset_id][direction];
    if (amount <= 0.0f) return;
    // Mark as closed: deduct from exposure; actual PnL written to TradeRecord elsewhere
    s->asset_exposure[asset_id][direction] = 0.0f;
    fprintf(stderr, "[C11] LIQUIDATION: asset=%d dir=%d amount=%.2f\n",
            asset_id, (int)direction, amount);
    // Return stake to agent (conservative liquidation price = 1.0)
    a->capital += amount;
}

static int portfolio_drawdown_breached(RoomState *s, AgentState *agents, int n_agents, float room_peak) {
    if (room_peak <= 0.0f) return 0;
    float min_cap = room_peak;
    for (int i = 0; i < n_agents; i++) {
        if (agents[i].capital > 0 && agents[i].capital < min_cap)
            min_cap = agents[i].capital;
    }
    float dd = (room_peak - min_cap) / room_peak;
    return dd > s->max_drawdown_pct ? 1 : 0;
}

// Run liquidation sweep across all rooms/agents
int run_liquidation_sweep(AgentState *agents, int n_agents, RoomState *room_states, int n_rooms) {
    int events = 0;
    float room_cap = 0.0f;
    for (int a = 0; a < n_agents; a++) room_cap += agents[a].capital;
    if (room_cap <= 0.0f) return 0;

    for (int r = 0; r < n_rooms; r++) {
        RoomState *s = &room_states[r];
        // Check correlation basket
        for (int dir = 0; dir < 2; dir++) {
            for (int asset = 0; asset < MAX_ASSETS; asset++) {
                float exp = s->asset_exposure[asset][dir];
                if (exp <= 0.0f) continue;
                float pct = exp / room_cap;
                if (pct > MAX_CORRELATION_EXPOSURE_PCT) {
                    // Find owner by scanning agents with exposure contribution (simplified)
                    for (int a = 0; a < n_agents; a++) {
                        if (agents[a].capital > 0) {
                            liquidate_position(&agents[a], asset, (bool)dir, s);
                            events++;
                        }
                    }
                }
            }
        }
        // Check drawdown
        if (portfolio_drawdown_breached(s, agents, n_agents, room_cap)) {
            // Liquidate largest exposures first
            for (int dir = 0; dir < 2; dir++) {
                for (int asset = 0; asset < MAX_ASSETS; asset++) {
                    if (s->asset_exposure[asset][dir] > room_cap * MAX_CORRELATION_EXPOSURE_PCT) {
                        for (int a = 0; a < n_agents; a++) {
                            if (agents[a].capital > 0) {
                                liquidate_position(&agents[a], asset, (bool)dir, s);
                                events++;
                            }
                        }
                    }
                }
            }
        }
    }
    return events;
}

// ════════════════════════════════════════════════════════
//  MATCH VOTES — pair YES vs NO agents, execute trades
//  CRITICAL: Only matched_stake is deducted from capital.
//  Unmatched surplus was never deducted — so NEVER returned.
//  This preserves zero-sum property.
// ════════════════════════════════════════════════════════
RoomError room_capital_apply(VoteRecord *votes, int count,
                             AgentState *agents, int n_unused,
                             TradeRecord *trades, int start_offset,
                             int *new_count, int64_t window_ts,
                             int predicted_regime,
                             RoomState *s) {
    (void)n_unused;
    *new_count = 0;
    if (count < 2) return ERR_OK;

    // ═══════════════════════════════════════════════════════
    //  R4: CIRCUIT BREAKER — Check before any matching
    // ═══════════════════════════════════════════════════════
    if (check_circuit_breaker_before_trade(s, agents, MAX_AGENTS, window_ts)) {
        return ERR_OK;  // Trading halted by circuit breaker
    }

    int max_new = MAX_TRADE_HIST - start_offset;
    if (max_new <= 0) return ERR_OK;

    Staker *yes = (Staker *)malloc(count * sizeof(Staker));
    Staker *no = (Staker *)malloc(count * sizeof(Staker));
    if (!yes || !no) { free(yes); free(no); return ERR_FILE_READ; }

    int ny = 0, nn = 0;
    float yes_total = 0, no_total = 0;

    // ── C11: Pre-compute total room capital for exposure check ──
    float total_room_cap = 0;
    for (int ac = 0; ac < count; ac++) total_room_cap += agents[ac].capital;

    // ── Pass 1: collect stakes (no capital deduction yet) ──
    for (int i = 0; i < count; i++) {
        int aid = votes[i].agent_id;
        AgentState *a = &agents[aid];
        if (!a->alive || a->capital <= 0) continue;

        float stake = votes[i].position_size * a->capital;
        // ── A37: Kelly criterion cap — prevent over-betting when WR is low ──
        // For even-money P2P bets: Kelly f* = win_rate - (1-win_rate) = 2*WR - 1
        // Use fractional Kelly (half-Kelly for safety): min(genome_size, max(0, WR-0.5))
        if (a->trades >= 20) {
            float kelly_f = a->win_rate_ema - 0.5f;  // Kelly fraction for even-money
            if (kelly_f > 0.0f) {
                float kelly_stake = kelly_f * a->capital;
                if (stake > kelly_stake) {
                    stake = kelly_stake;  // Kelly caps the genome-evolved size
                }
            } else {
                // WR below 50% — Kelly says don't bet at all. Use 1/4 genome size.
                stake *= 0.25f;
            }
        }
        // ── A14: Reduce position sizing in volatile regime ──
        if (predicted_regime == 2) {
            stake *= 0.50f;  // Half position in volatile regime
        }
        float max_loss = a->capital * 0.05f;
        if (stake > max_loss) stake = max_loss;
        if (stake > a->capital * 0.5f) stake = a->capital * 0.5f;
        // ── C11: Max exposure — no single position > 10% of total room capital ──
        { float max_exp = total_room_cap * MAX_EXPOSURE_PCT;
          if (max_exp > 0 && stake > max_exp) stake = max_exp; }
        if (stake < MIN_TRADE_STAKE) continue;  // T97/C15: skip tiny trades (min $5 for Polymarket)
        // ── C14: Gas fee check — skip if gas would eat >50% of stake ──
        if (GAS_FEE_EST > stake * 0.5f) continue;
        if (stake <= 0) continue;

        // ── C23: Duplicate trade detection ──
        {
            int64_t tk = trade_key(aid, window_ts, votes[i].direction);
            if (is_duplicate_trade(s, tk)) {
                s->duplicate_trades_blocked++;
                fprintf(stderr, "[C23] DUPLICATE BLOCKED: agent=%d ts=%lld dir=%d\n",
                        aid, (long long)window_ts, votes[i].direction);
                continue;
            }
        }

        // ── C07: Correlation-based position limits ──
        // Map asset string to index (simple hash of first 2 chars)
        {
            int asset_id = 0;  // Default to asset 0 for room-level trades
            // For room trades, use market_type as asset proxy
            asset_id = (int)(votes[i].direction ? 0 : 1);  // YES=0, NO=1 as asset buckets
            if (asset_id >= MAX_ASSETS) asset_id = 0;

            if (check_correlation_exposure(s, asset_id, votes[i].direction, stake, total_room_cap)) {
                s->correlation_blocked++;
                fprintf(stderr, "[C07] CORR LIMIT BLOCKED: agent=%d asset=%d dir=%d stake=%.2f\n",
                        aid, asset_id, votes[i].direction, stake);
                continue;
            }
        }

        // ── T96: PDT (Pattern Day Trader) enforcement ──
        // SEC Rule: accounts under $25K limited to 3 day trades per rolling 5-day window.
        // All agent trades resolve within 1 cycle → every trade is a day trade.
        if (a->capital < 25000.0f) {
            int64_t now = window_ts;
            if (a->day_trade_roll_ts > 0 &&
                (now - a->day_trade_roll_ts) >= 5 * 86400LL) {
                // Rolling 5-day window expired — reset
                a->day_trades_5d = 0;
                a->day_trade_roll_ts = now;
            }
            if (a->day_trades_5d >= 3) {
                if (a->day_trade_roll_ts == 0)
                    a->day_trade_roll_ts = now;
                continue;  // PDT limit hit — skip this agent
            }
            a->day_trades_5d++;
            if (a->day_trade_roll_ts == 0)
                a->day_trade_roll_ts = now;
        }

        if (votes[i].direction) {
            yes[ny].agent_id = aid;
            yes[ny].stake = stake;
            yes[ny].conviction = votes[i].conviction;
            yes_total += stake;
            ny++;
        } else {
            no[nn].agent_id = aid;
            no[nn].stake = stake;
            no[nn].conviction = votes[i].conviction;
            no_total += stake;
            nn++;
        }
    }

    if (ny == 0 || nn == 0) { free(yes); free(no); return ERR_OK; }
    if (yes_total <= 0 || no_total <= 0) { free(yes); free(no); return ERR_OK; }

    // ── Pass 2: match ──
    float matched = fminf(yes_total, no_total);
    float yes_ratio = matched / yes_total;
    float no_ratio = matched / no_total;

    // ── Pass 3: deduct matched_stake + fee, write trade records ──
    // NOTE: Unmatched surplus stays in agent's capital (never deducted).
    // We only deduct what's actually matched.
    int trade_idx = start_offset;

    for (int i = 0; i < ny && trade_idx < start_offset + max_new; i++) {
        float matched_stake = yes[i].stake * yes_ratio;  // Portion at risk
        // ── D44: Per-asset fee lookup (defaults to TAKER_FEE) ──
        float fee = matched_stake * get_exchange_fee(s, 0);
        // NO surplus return — unmatched portion was never deducted

        AgentState *a = &agents[yes[i].agent_id];
        a->capital -= (matched_stake + fee);
        if (a->capital < 0) a->capital = 0;  // C1: capital floor
        a->trades++;
        a->last_trade_window = (int)window_ts;

        // ── C23: Record trade key for duplicate detection ──
        record_trade_key(s, trade_key(yes[i].agent_id, window_ts, true));
        // ── C07: Update asset exposure ──
        update_asset_exposure(s, 0, true, matched_stake);

        // ── R4: Track directional exposure for circuit breaker ──
        s->current_total_exposure += matched_stake;
        s->current_yes_exposure += matched_stake;

        trades[trade_idx].window_ts = window_ts;
        trades[trade_idx].agent_id = yes[i].agent_id;
        trades[trade_idx].direction = true;
        trades[trade_idx].position_size = matched_stake;
        trades[trade_idx].entry_price = 0.5f;
        trades[trade_idx].exit_price = 0;
        trades[trade_idx].pnl_pct = 0;
        trades[trade_idx].won = false;
        trades[trade_idx].resolved_at = 0;
        strncpy(trades[trade_idx].asset, "ROOM", 7);
        trade_idx++;
    }

    for (int i = 0; i < nn && trade_idx < start_offset + max_new; i++) {
        float matched_stake = no[i].stake * no_ratio;
        // ── D44: Per-asset fee lookup (defaults to TAKER_FEE) ──
        float fee = matched_stake * get_exchange_fee(s, 0);

        AgentState *a = &agents[no[i].agent_id];
        a->capital -= (matched_stake + fee);
        if (a->capital < 0) a->capital = 0;  // C1: capital floor
        a->trades++;
        a->last_trade_window = (int)window_ts;

        // ── C23: Record trade key for duplicate detection ──
        record_trade_key(s, trade_key(no[i].agent_id, window_ts, false));
        // ── C07: Update asset exposure ──
        update_asset_exposure(s, 0, false, matched_stake);

        // ── R4: Track directional exposure for circuit breaker ──
        s->current_total_exposure += matched_stake;
        s->current_no_exposure += matched_stake;

        trades[trade_idx].window_ts = window_ts;
        trades[trade_idx].agent_id = no[i].agent_id;
        trades[trade_idx].direction = false;
        trades[trade_idx].position_size = matched_stake;
        trades[trade_idx].entry_price = 0.5f;
        trades[trade_idx].exit_price = 0;
        trades[trade_idx].pnl_pct = 0;
        trades[trade_idx].won = false;
        trades[trade_idx].resolved_at = 0;
        strncpy(trades[trade_idx].asset, "ROOM", 7);
        trade_idx++;
    }

    *new_count = trade_idx - start_offset;
    free(yes);
    free(no);
    return ERR_OK;
}

// ════════════════════════════════════════════════════════
//  RESOLVE — settle matched trades when market resolves
//  Uses close > prev_close (inter-candle direction).
//  Winners get matched_stake back + share of loser pool.
//  Losers' matched_stake stays gone (already deducted).
//  Zero-sum minus MATCH_FEE on loser pool.
// ════════════════════════════════════════════════════════
RoomError room_capital_resolve(TradeRecord *trades, int *tcount,
                               const MarketTick *resolution_tick,
                               float prev_close,
                               AgentState *agents,
                               int max_trades,
                               FeatureImportance *importance,
                               float lr_decay,
                               RoomState *s) {
    int n = *tcount < max_trades ? *tcount : max_trades;
    if (n == 0) return ERR_OK;

    bool yes_won = resolution_tick->close >= prev_close;

    // Collect unresolved trades from windows before current tick
    float yes_pool = 0, no_pool = 0;
    int yes_count = 0, no_count = 0;

    for (int i = 0; i < n; i++) {
        if (trades[i].resolved_at != 0) continue;
        if (trades[i].window_ts >= resolution_tick->window_ts) continue;

        if (trades[i].direction) {
            yes_pool += trades[i].position_size;
            yes_count++;
        } else {
            no_pool += trades[i].position_size;
            no_count++;
        }
    }

    if (yes_count == 0 && no_count == 0) return ERR_OK;

    float loser_pool = yes_won ? no_pool : yes_pool;
    float winner_pool = yes_won ? yes_pool : no_pool;
    float total_payout = loser_pool * (1.0f - MATCH_FEE);  // Fee on loser pool only

    for (int i = 0; i < n; i++) {
        if (trades[i].resolved_at != 0) continue;
        if (trades[i].window_ts >= resolution_tick->window_ts) continue;

        bool is_winner = trades[i].direction == yes_won;
        int aid = trades[i].agent_id;

        if (aid >= 0 && aid < MAX_AGENTS) {
            if (is_winner) {
                float share = winner_pool > 0 ? trades[i].position_size / winner_pool : 0;
                // Return matched_stake + share of loser pool (net of match fee)
                float payout = trades[i].position_size + total_payout * share;
                agents[aid].capital += payout;
                agents[aid].wins++;
                agents[aid].consecutive_losses = 0;
                // C10: Conviction accuracy tracking
                if (agents[aid].last_conviction > 0.7f) {
                    agents[aid].conv_hi_wins++;
                    agents[aid].conv_hi_total++;
                } else if (agents[aid].last_conviction < 0.3f) {
                    agents[aid].conv_lo_wins++;
                    agents[aid].conv_lo_total++;
                }
                trades[i].won = true;
                trades[i].pnl_pct = total_payout * share / trades[i].position_size;
            } else {
                // Loser's matched_stake stays deducted — transferred to winners
                agents[aid].losses++;
                agents[aid].consecutive_losses++;
                // C10: Conviction accuracy tracking (losses)
                if (agents[aid].last_conviction > 0.7f) {
                    agents[aid].conv_hi_total++;
                } else if (agents[aid].last_conviction < 0.3f) {
                    agents[aid].conv_lo_total++;
                }
                trades[i].won = false;
                trades[i].pnl_pct = -1.0f;
                if (agents[aid].consecutive_losses >= 6)
                    agents[aid].alive = false;
                // ── C19: Capital-floor auto-kill — agent below $1 can't trade ──
                if (agents[aid].capital < 1.0f)
                    agents[aid].alive = false;
            }

            agents[aid].total_pnl += trades[i].pnl_pct * trades[i].position_size;
            if (agents[aid].capital > agents[aid].peak_capital)
                agents[aid].peak_capital = agents[aid].capital;

            float dd = (agents[aid].peak_capital - agents[aid].capital) / agents[aid].peak_capital;
            if (dd > agents[aid].max_drawdown)
                agents[aid].max_drawdown = dd;

            float wr = trades[i].won ? 1.0f : 0.0f;
            float old_ema = agents[aid].win_rate_ema;
            agents[aid].win_rate_ema = old_ema * 0.9f + wr * 0.1f;
            // ── C30: Online variance tracking (exponential) ──
            float delta = wr - old_ema;
            agents[aid].win_rate_var = agents[aid].win_rate_var * 0.9f + delta * (wr - agents[aid].win_rate_ema) * 0.1f;

            // ── C18: Win-rate-floor auto-kill — cull agents below 30% WR over 100+ trades ──
            if (agents[aid].trades >= 100 && agents[aid].win_rate_ema < 0.30f) {
                agents[aid].alive = false;
            }
            // REINFORCE: w += lr * (actual - predicted) * feature
            // Accumulates gradients then applies as batch to reduce variance.
            // actual = 1.0 (won) or 0.0 (lost), predicted = last_conviction
            int sgd_regime = (int)(agents[aid].last_features[11] + 0.5f);
            if (sgd_regime < 0) sgd_regime = 0;
            if (sgd_regime >= N_REGS) sgd_regime = N_REGS - 1;
            if (agents[aid].last_conviction > 0.0f) {
                float error = (trades[i].won ? 1.0f : 0.0f) - agents[aid].last_conviction;
                float lr = agents[aid].genome.learning_rate * lr_decay;  // A18: Cosine LR decay
                // Scale learning rate by importance of this trade
                float importance = trades[i].position_size / (agents[aid].capital + 1.0f);
                float step = lr * error * fmaxf(importance, 0.01f);
                // Clamp step to prevent wild updates
                if (step > 0.1f) step = 0.1f;
                if (step < -0.1f) step = -0.1f;
                // Accumulate gradient for each feature weight
                for (int fi = 0; fi < N_FEATURES; fi++) {
                    agents[aid].grad_accum[sgd_regime][fi] += step * agents[aid].last_features[fi];
                }
                agents[aid].bias_accum[sgd_regime] += step;
                agents[aid].batch_count++;

                // Apply accumulated gradients as mini-batch when batch is full
                if (agents[aid].batch_count >= SGD_BATCH_SIZE) {
                    // ── A44: Compute gradient norm for SGD diagnosis ──
                    float grad_norm_sq = 0.0f;
                    for (int fi = 0; fi < N_FEATURES; fi++)
                        grad_norm_sq += agents[aid].grad_accum[sgd_regime][fi] * agents[aid].grad_accum[sgd_regime][fi];
                    float grad_norm = sqrtf(grad_norm_sq);
                    if (grad_norm < 0.001f && agents[aid].trades >= 100)
                        printf("[A44] SGD stall: agent=%d regime=%d grad_norm=%.6f\n", aid, sgd_regime, grad_norm);
                    float inv_batch = 1.0f / agents[aid].batch_count;
                    for (int fi = 0; fi < N_FEATURES; fi++) {
                        agents[aid].genome.regime_weight[sgd_regime][fi]
                            += agents[aid].grad_accum[sgd_regime][fi] * inv_batch;
                        if (agents[aid].genome.regime_weight[sgd_regime][fi] > 1.0f)
                            agents[aid].genome.regime_weight[sgd_regime][fi] = 1.0f;
                        if (agents[aid].genome.regime_weight[sgd_regime][fi] < -1.0f)
                            agents[aid].genome.regime_weight[sgd_regime][fi] = -1.0f;
                    }
                    agents[aid].genome.regime_bias[sgd_regime]
                        += agents[aid].bias_accum[sgd_regime] * inv_batch;
                    // Reset accumulators for this regime
                    memset(agents[aid].grad_accum[sgd_regime], 0, sizeof(float) * N_FEATURES);
                    agents[aid].bias_accum[sgd_regime] = 0.0f;
                    agents[aid].batch_count = 0;
                    // A21: L2 weight decay on regime weights
                    float l2_lambda = 0.001f;
                    for (int fi = 0; fi < N_FEATURES; fi++) {
                        agents[aid].genome.regime_weight[sgd_regime][fi]
                            -= l2_lambda * agents[aid].genome.regime_weight[sgd_regime][fi];
                    }
                }
            }

            // ── P16: Feature importance tracking ──
            // For each feature, track if its contribution aligned with the signal
            // direction and whether the trade won. Uses per-regime weights (P22).
            if (importance) {
                for (int fi = 0; fi < N_FEATURES; fi++) {
                    float contrib = agents[aid].last_features[fi] * agents[aid].genome.regime_weight[sgd_regime][fi];
                    if (fabsf(contrib) > 1e-6f) {
                        if (contrib > 0) {
                            // Feature pushed signal UP
                            if (trades[i].won)
                                importance->pos_contrib_wins[fi] += 1.0f;
                            importance->pos_contrib_total[fi]++;
                        } else {
                            // Feature pushed signal DOWN
                            if (trades[i].won)
                                importance->neg_contrib_wins[fi] += 1.0f;
                            importance->neg_contrib_total[fi]++;
                        }
                    }
                }
            }
        }

        trades[i].exit_price = resolution_tick->close;
        trades[i].resolved_at = resolution_tick->window_ts;

        // ── T16: Log resolved trade to CSV for post-hoc audit ──
        FILE *tlog = fopen("/home/wubu2/.hermes/pm_logs/c_room/trade_log.csv", "a");
        if (tlog) {
            // Write header on first open (file didn't exist before open in "a" mode)
            // We check file existence by seeing if position after fopen is at start
            // In "a" mode, ftell returns 0 for a new file, >0 for existing
            long pos = ftell(tlog);
            if (pos == 0) {
                fprintf(tlog, "ts,agent_id,direction,size,entry_price,exit_price,won,pnl_pct,resolved_at,asset\n");
            }
            fprintf(tlog, "%ld,%d,%s,%.4f,%.4f,%.4f,%s,%.6f,%ld,%s\n",
                    (long)trades[i].window_ts,
                    trades[i].agent_id,
                    trades[i].direction ? "YES" : "NO",
                    trades[i].position_size,
                    trades[i].entry_price,
                    trades[i].exit_price,
                    trades[i].won ? "WIN" : "LOSS",
                    trades[i].pnl_pct,
                    (long)trades[i].resolved_at,
                    trades[i].asset);
            fclose(tlog);
        }
    }

    // ═══════════════════════════════════════════════════════
    //  R4: CIRCUIT BREAKER STATE UPDATE — Track daily PnL & consec losses
    // ═══════════════════════════════════════════════════════
    // Room-level PnL from this resolution batch
    float room_pnl_this_batch = 0.0f;
    for (int i = 0; i < n; i++) {
        if (trades[i].resolved_at != 0 && trades[i].window_ts < resolution_tick->window_ts) {
            room_pnl_this_batch += trades[i].pnl_pct * trades[i].position_size;
        }
    }
    s->daily_pnl += room_pnl_this_batch;

    // Track consecutive room losses (room trade won/lost)
    if (yes_count + no_count > 0) {
        if (yes_won) {
            s->consec_room_losses = 0;
            s->daily_loss_streak = 0;
        } else {
            s->consec_room_losses++;
            s->daily_loss_streak++;
            fprintf(stderr, "[R4] Room loss streak: %d/%d\n",
                    s->consec_room_losses, s->max_consecutive_losses);
        }
    }

    // Clear exposure tracking for resolved trades
    s->current_total_exposure = 0.0f;
    s->current_yes_exposure = 0.0f;
    s->current_no_exposure = 0.0f;

    return ERR_OK;
}

// ════════════════════════════════════════════════════════
// ── C24: Cross-room market correlation tracking ══════════════════════
// Call this after each room cycle to update cross-room return history
// and recompute the correlation matrix.
// asset_id: 0..MAX_ASSETS-1 — maps to each traded asset across rooms
// ret: the simple return for this asset in this cycle (e.g., close/open - 1)
// ════════════════════════════════════════════════════════
void update_cross_room_correlation(RoomState *s, int asset_id, float ret) {
    if (asset_id < 0 || asset_id >= MAX_ASSETS) return;

    // Store return in ring buffer
    int idx = s->cross_room_ret_idx[asset_id];
    s->cross_room_return[asset_id][idx] = ret;
    s->cross_room_ret_idx[asset_id] = (idx + 1) % FEED_HISTORY;
    if (s->cross_room_ret_len[asset_id] < FEED_HISTORY)
        s->cross_room_ret_len[asset_id]++;

    // Recompute correlation between all pairs that have enough data
    int min_len = 10;
    for (int a = 0; a < MAX_ASSETS; a++) {
        if (a == asset_id) { s->cross_room_correlation[a][a] = 1.0f; continue; }
        int n = s->cross_room_ret_len[asset_id] < s->cross_room_ret_len[a]
                ? s->cross_room_ret_len[asset_id] : s->cross_room_ret_len[a];
        if (n < min_len) { s->cross_room_correlation[a][asset_id] = 0.0f; continue; }

        int off_a = s->cross_room_ret_idx[a] - n;
        int off_b = s->cross_room_ret_idx[asset_id] - n;
        float sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0, sum_y2 = 0;
        for (int i = 0; i < n; i++) {
            int ia = (off_a + i + FEED_HISTORY) % FEED_HISTORY;
            int ib = (off_b + i + FEED_HISTORY) % FEED_HISTORY;
            float x = s->cross_room_return[a][ia];
            float y = s->cross_room_return[asset_id][ib];
            sum_x += x; sum_y += y;
            sum_xy += x * y;
            sum_x2 += x * x; sum_y2 += y * y;
        }
        float num = n * sum_xy - sum_x * sum_y;
        float den = sqrtf((n * sum_x2 - sum_x * sum_x) * (n * sum_y2 - sum_y * sum_y));
        float corr = (den > 1e-10f) ? num / den : 0.0f;
        s->cross_room_correlation[a][asset_id] = corr;
        s->cross_room_correlation[asset_id][a] = corr;
    }
}

// ════════════════════════════════════════════════════════
// ── D35: Data quality scoring per source ═════════════════════════════
// ════════════════════════════════════════════════════════
typedef struct {
    char name[64];
    float score;
    int   age_seconds;
    int   fields_present;
    int   fields_expected;
    int   range_errors;
} DataQualityScore;

void score_data_source(DataQualityScore *qs, const char *name, int age_s,
                       int present, int expected, int range_err) {
    strncpy(qs->name, name, 63);
    qs->name[63] = '\0';
    qs->age_seconds = age_s;
    qs->fields_present = present;
    qs->fields_expected = expected;
    qs->range_errors = range_err;

    float recency;
    if (age_s < 300) recency = 1.0f;
    else if (age_s < 3600) recency = 0.5f;
    else if (age_s < 14400) recency = 0.2f;
    else recency = 0.0f;

    float completeness = (expected > 0) ? (float)present / expected : 0.0f;
    if (completeness > 1.0f) completeness = 1.0f;

    float consistency = (present > 0)
        ? 1.0f - fminf((float)range_err / present, 1.0f)
        : 0.0f;

    qs->score = recency * 0.4f + completeness * 0.3f + consistency * 0.3f;
}

// ════════════════════════════════════════════════════════
// ── D36: Data consistency validation ═════════════════════════════════
// ════════════════════════════════════════════════════════
int check_cross_source_consistency(float val_a, float val_b, float threshold_pct) {
    if (val_a == 0.0f && val_b == 0.0f) return 0;
    float avg = (fabsf(val_a) + fabsf(val_b)) * 0.5f;
    if (avg < 1e-10f) return 0;
    float diff_pct = fabsf(val_a - val_b) / avg * 100.0f;
    return (diff_pct > threshold_pct) ? 1 : 0;
}

typedef struct {
    char metric[64];
    char source_a[32];
    char source_b[32];
    float val_a;
    float val_b;
    float diff_pct;
    int   inconsistent;
} ConsistencyCheck;

void check_consistency_report(ConsistencyCheck *cc, const char *metric,
                               const char *src_a, const char *src_b,
                               float val_a, float val_b, float threshold_pct) {
    strncpy(cc->metric, metric, 63); cc->metric[63] = '\0';
    strncpy(cc->source_a, src_a, 31); cc->source_a[31] = '\0';
    strncpy(cc->source_b, src_b, 31); cc->source_b[31] = '\0';
    cc->val_a = val_a; cc->val_b = val_b;

    float avg = (fabsf(val_a) + fabsf(val_b)) * 0.5f;
    cc->diff_pct = (avg > 1e-10f) ? fabsf(val_a - val_b) / avg * 100.0f : 0.0f;
    cc->inconsistent = (cc->diff_pct > threshold_pct) ? 1 : 0;
}
