#ifndef ROOM_TYPES_H
#define ROOM_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// ── History ring buffer constants (shared with room_features.c) ──
#define FEED_HISTORY 50
#define N_FEED_MARKETS 10  // matches N_MARKET_TYPES

// ── Constants ──
#define PHI 1.618033988749895f  // Golden ratio φ
#define SQRT_PHI 1.272019649514069f  // √φ
#define INV_PHI 0.618033988749895f   // 1/φ
#define TWO_PI 6.283185307179586f    // 2π

// ── Market Types for multi-market training ──
typedef enum {
    MARKET_CRYPTO     = 0,
    MARKET_EQUITY     = 1,  // Stock indices: SP500, DOW, NASDAQ, FTSE100, NIKKEI
    MARKET_FOREX      = 2,  // FX pairs: EURUSD, GBPUSD, USDJPY
    MARKET_COMMODITY  = 3,  // GOLD, SILVER, CRUDE_OIL
    MARKET_BOND       = 4,  // DGS10 (10yr yield)
    MARKET_VOLATILITY = 5,  // VIX
    MARKET_PREDICTION = 6,  // Polymarket binary events
    MARKET_SPORTS     = 7,  // Sports binary outcomes
    MARKET_WEATHER    = 8,  // Weather prediction
    MARKET_ELECTION   = 9,  // Election binary outcomes
    N_MARKET_TYPES    = 10
} MarketType;

#define MAX_MARKETS 20

extern const char *MARKET_TYPE_NAMES[];

// Paper proof uses fewer agents for faster evolution per trade cap
#ifdef PAPER_MODE
#define ROOM_AGENTS  2500
#else
#define ROOM_AGENTS  10000
#endif
#define MAX_AGENTS    ROOM_AGENTS
#define N_FEATURES        34
#define N_REGS            3   // Regimes: 0=range, 1=trend, 2=volatile (P22)
#define MAX_ASSETS        8
#define MAX_TRADE_HIST    1000000
#define STATE_MAGIC       0x524F4D37  // "ROM7" — bumped for B23 VIX history in RoomState

// Fee constants (shared across modules)
#define TAKER_FEE    0.001f   // Kraken spot taker 0.1% (paper)
#define MATCH_FEE    0.002f   // Match fee on loser pool (0.2%)
// ── T97: Minimum trade size ──
#define MIN_TRADE_STAKE   1.0f    // Minimum $1 stake per trade (skip smaller)
// ── T20: Slippage model ──
#define SLIPPAGE_BPS          5.0f    // 5 bps = 0.05% baseline slippage
#define SLIPPAGE_VOL_SCALE   5.0f    // Additional bps per $100 of position (market impact)

// Market direction mode fees (for P2P ensemble paper proof)
#ifdef MARKET_MODE
#define MARKET_TAKER_FEE 0.001f  // 0.1% taker fee
#define MARKET_MAKER_FEE 0.000f  // 0% maker fee (limit orders)
#endif

// Contrarian mode: when majority is wrong (anti-edge on multi-stream),
// bet against the majority for >55% WR
#define CONTRARIAN_MODE

// ── Genome: evolves via Darwin ──
typedef struct {
    float position_size;        // 0.01–0.50 fraction of capital
    float conviction_threshold; // 0.05–0.95 min conviction to act
    float risk_tolerance;       // 0.0–1.0
    float lie_sensitivity;      // 0.10–0.98 how much to distrust crony
    float herd_antipathy;       // 0.0–1.0 contrarian bias
    float stop_loss_pct;        // 0.01–0.25
    float take_profit_pct;      // 0.01–0.60
    float min_edge_pct;         // 1.0–100.0 min expected return
    float time_horizon;         // 0.1–10.0 minutes
    float mean_reversion_bias;  // -1.0–1.0
    // ── v2: Learned feature weights ──
    float feat_weight[N_FEATURES];  // Per-feature weight — Darwin evolves these
    float bias;                     // Learned bias term
    float learning_rate;            // SGD step size (0.001–0.1)
    // ── P22: Regime-specific weights ──
    float regime_weight[N_REGS][N_FEATURES];  // Per-regime feature weights
    float regime_bias[N_REGS];                // Per-regime bias term
} Genome;

// ── Feature vector (17-dim, L2 spec) ──
typedef struct {
    float price_delta_pct;      // Current vs window open %
    float micro_momentum;       // Last 2 closes delta %
    float rsi_7;                // 7-period RSI (0–100)
    float volume_surge_ratio;   // Recent/prior volume ratio
    float ema_fast;             // 3-period EMA
    float ema_slow;             // 8-period EMA
    float macd_hist;            // MACD histogram value
    float bollinger_pct;        // %B position (0=lower, 1=upper)
    float divergence_score;     // Price-RSI divergence (-1..1)
    float pump_score;           // Crony-weighted news sentiment (-1..1)
    float regime_indicator;     // 0=range, 1=trend, 2=volatile
    float fear_greed_norm;      // Normalized F&G (0–1)
    float herd_consensus;       // % agents voting same direction
    // ── B05: Order book features (replaces φ-interval features) ──
    float ob_imbalance;         // F14: bid_vol / (bid+ask_vol) top-10 levels (0-1)
    float ob_depth_ratio;       // F15: bid_depth / (bid+ask_depth) 0.5% band (0-1)
    float cvd_signal;           // F16: Cumulative volume delta normalized (0-1)
    // ── P13: DFT frequency feature ──
    float dft_dominant;         // F17: Dominant frequency strength (0-1)
    // ── P15: Tailslayer tail risk score ──
    float tail_risk_score;      // F18: 0-1: 0=normal, 1=extreme tail risk
    // ── B14-B16: Funding/OI/LS ratio (loaded from collector cache) ──
    float funding_signal;       // F19: funding rate deviation from 7d avg (-1..1)
    float oi_net_signal;        // F20: aggregated OI signal (0-1: 0 = bearish/bullish)
    float ls_ratio_norm;        // F21: L/S taker volume ratio normalized (0-1)
    // ── B17-B19: Liquidation/Stablecoin/Whale ──
    float liq_ls_ratio_norm;    // F22: liquidation long/short ratio (0-1)
    float stable_inflow_norm;   // F23: stablecoin volume ratio (0-1+)
    float whale_activity_norm;  // F24: whale transaction activity (0-1)
    // ── Hashrate / On-chain ──
    float hash_rate_norm;       // F25: BTC hashrate normalized (0-1)
    float difficulty_norm;      // F26: mining difficulty normalized (0-1)
    float miner_floor_norm;     // F27: miner cost floor normalized (0-1)
    // ── B11: Time-of-day features ──
    float hour_of_day_norm;     // F28: hour of day [0,1) (0=midnight)
    float day_of_week_norm;     // F29: day of week [0,1) (0=Mon, 0.857=Sun)
    // ── B21: Options-derived features ──
    float iv_skew;              // F30: IV skew (0-1+, >0.5 = high put demand = bearish)
    float pcr_volume;           // F31: put/call ratio by volume (0-1+)
    float iv_term_slope;        // F32: IV term structure slope (0-1+)
    // ── B12: Macro equity correlation ──
    float btc_sp500_corr;       // F33: Rolling BTC-SP500 correlation (-1..1)
    // ── B23: VIX regime filter ──
    float vix_regime;           // F34: VIX regime (0=low<15, 0.5=normal 15-25, 1=high>25)
} FeatureVector;

// ── Market data from Python feed ──
typedef struct {
    char     asset[8];          // "BTC", "ETH", etc.
    int64_t  window_ts;         // Unix timestamp of window start
    MarketType market_type;     // MARKET_CRYPTO, MARKET_EQUITY, etc.
    float    open, high, low, close, volume;
    float    fear_greed;        // 0–100
    float    pump_score;        // -1..1 from crony pipeline
    float    btc_dominance;     // BTC dominance %
    float    vix;               // VIX if available
    float    sp500;             // S&P500 index level (for nested model macro features)
    float    btc_30d_volatility; // 30-day BTC volatility %
    float    btc_30d_mean;     // 30-day BTC mean price
    float    btc_30d_high;     // 30-day BTC high price
    float    btc_30d_low;      // 30-day BTC low price
    // ── B05: Order book features ──
    float    ob_imbalance;      // bid_vol / (bid+ask_vol) top-10 (0-1)
    float    ob_depth_ratio;    // bid_depth / (bid+ask_depth) 0.5% band (0-1)
    float    ob_wall_conc;      // largest level / top-10 total (0-1)
    float    ob_spread_norm;    // spread bps / 100 (0-1)
    // ── B06: Cumulative volume delta ──
    float    cvd_signal;        // bid-ask volume delta normalized (0-1)
    // ── B14-B16: Funding/OI/LS ratio (loaded from collector cache) ──
    float    funding_signal;     // funding rate deviation from 7d avg (-1..1)
    float    oi_net_signal;      // aggregated OI signal (0-1: 0 = bearish/bullish)
    float    ls_ratio_norm;      // L/S taker volume ratio normalized (0-1)
    // ── B17-B19: Liquidation/Stablecoin/Whale ──
    float    liq_ls_ratio_norm;  // liquidation long/short ratio (0-1)
    float    stable_inflow_norm; // stablecoin volume ratio (0-1+)
    float    whale_activity_norm; // whale transaction activity (0-1)
    // ── Hashrate / On-chain (from hashrate_feat collector) ──
    float    hash_rate_norm;     // BTC hashrate normalized (0-1)
    float    difficulty_norm;    // mining difficulty normalized (0-1)
    float miner_floor_norm;     // F27: miner cost floor normalized (0-1)
    // ── B11: Time-of-day features ──
    float hour_of_day_norm;     // F28: hour of day [0,1) (0=midnight)
    float day_of_week_norm;     // F29: day of week [0,1) (0=Mon, 0.857=Sun)
    // ── B21: Options-derived features (from options_feat collector) ──
    float iv_skew;              // F30: IV skew (0-1+, >0.5 = high put demand = bearish)
    float pcr_volume;           // F31: put/call ratio by volume (0-1+)
    float iv_term_slope;        // F32: IV term structure slope (0-1+)
} MarketTick;

// ── Agent vote result ──
typedef struct {
    int      agent_id;
    bool     voted;             // Did agent act this tick?
    bool     direction;         // true=UP/YES, false=DOWN/NO
    float    conviction;        // 0–1 sigmoid activation
    float    position_size;     // Fraction of capital risked
    float    pnl;               // PnL from this trade (0 if not resolved yet)
} VoteRecord;

// ── Agent state (persistent across cycles) ──
typedef struct {
    bool     alive;
    Genome   genome;
    float    capital;           // Current capital
    float    starting_capital;  // Initial capital (for return calc)
    int      trades;            // Total trades executed
    int      wins, losses;
    float    total_pnl;
    float    max_drawdown;
    float    peak_capital;
    int      consecutive_losses;
    float    win_rate_ema;      // EMA of recent win rate (for ranking)
    int      last_trade_window; // Window of last trade (to avoid double-count)
    // ── v2: Recurrent hidden state ──
    float    hidden[4];         // Persists across trades — gives memory
    float    last_conviction;   // Conviction at last vote (for SGD error)
    float    last_features[N_FEATURES];  // Features at last vote (for SGD update)
    // ── C10: Conviction accuracy tracking ──
    float    conv_hi_wins;      // Wins when conviction > 0.7
    float    conv_hi_total;     // Trades when conviction > 0.7
    float    conv_lo_wins;      // Wins when conviction < 0.3
    float    conv_lo_total;     // Trades when conviction < 0.3
    // ── C19: Weight diversity contribution ──
    float    weight_mag;        // ||feat_weight|| L2 norm (for diversity calc)
} AgentState;

// ── Trade record (for post-hoc analysis) ──
typedef struct {
    int64_t  window_ts;
    char     asset[8];
    int      agent_id;
    bool     direction;
    float    position_size;
    float    entry_price;
    float    exit_price;
    float    pnl_pct;
    bool     won;
    int64_t  resolved_at;
} TradeRecord;

// ── Room-level consensus trade (one per cycle, $50 seed) ──
typedef struct {
    int64_t  window_ts;
    int      yes_votes;           // Count of YES votes
    int      no_votes;            // Count of NO votes
    int      total_votes;         // Total voting agents
    bool     majority_up;         // consensus direction
    float    conviction_spread;   // 0=unanimous, 1=perfect split
    float    stake;               // $ amount risked
    float    entry_price;
    float    exit_price;
    bool     won;
    float    pnl;                 // $ PnL from this trade
    int64_t  resolved_at;
} RoomTrade;

// ── Room stats snapshot ──
typedef struct {
    int      cycle_count;
    int64_t  last_window_ts;
    float    room_pnl_pct;         // Aggregate PnL %
    float    avg_conviction;       // Average conviction of all votes
    float    win_rate;             // Rolling win rate
    float    sharpe_ratio;         // Rolling Sharpe (annualized)
    float    max_drawdown;         // Room-level max drawdown
    float    initial_capital;      // For PnL = (current/initial - 1)*100
    float    cycle_returns[128];   // Ring buffer of per-cycle room returns
    int      return_idx;           // Current write index in ring buffer
    int      return_count;         // Total returns recorded
    int      active_agents;        // Alive agents
    int      voted_this_cycle;     // Agents that voted
    int      trades_total;
    int      trades_won, trades_lost;
    float    capital_peak;
    float    capital_current;
    float    consensus_spread;     // stddev of votes (0=perfect split, 1=unanimous)
    // ── C19: Population diversity metrics ──
    float    weight_diversity;     // Stddev of feat_weight L2 norms across population
    float    genome_diversity;     // Mean pairwise genome distance
    float    conviction_spread_avg;// Rolling avg of conviction spread
    // ── P15: Tailslayer hedging state ──
    float    tail_risk_score;     // Current tail risk (0-1)
    float    hedge_factor;        // Position scaling factor (1.0=normal, <1.0=hedged)
    int      hedge_active_cycles; // Cycles hedge has been active
} RoomStats;

// ── P16: Feature Importance Tracking ──
// Tracks how each feature correlates with win/loss outcome.
// For each resolved trade: if feature contribution (weight * value) is positive,
// increment pos_{wins,total}[i]; if negative, increment neg_{wins,total}[i].
// Feature importance score = pos_wr[i] - neg_wr[i] (positive = helpful).
typedef struct {
    float pos_contrib_wins[N_FEATURES];   // Wins when feature pushed signal direction
    int   pos_contrib_total[N_FEATURES];  // Total trades when feature pushed signal
    float neg_contrib_wins[N_FEATURES];   // Wins when feature opposed signal direction
    int   neg_contrib_total[N_FEATURES];  // Total trades when feature opposed signal
    float last_importance[N_FEATURES];    // A17: Last computed importance score per feature
    int   stagnant_cycles[N_FEATURES];    // A17: Consecutive cycles with flat importance
} FeatureImportance;

// ── Darwin evolution command ──
typedef struct {
    int      epoch;
    int      culled;
    int      cloned;
    float    mutation_rate;        // Current mutation rate (decays)
} DarwinRecord;

// ── Main room shared state (mmap'd) ──
typedef struct {
    uint32_t magic;             // STATE_MAGIC for validation
    int64_t  last_updated;      // Unix ns
    int      cycle;             // Current cycle number

    // Market currently being processed
    MarketTick current_market;
    FeatureVector features;

    // Vote results for this tick
    int      vote_count;
    VoteRecord votes[MAX_AGENTS];

    // Agent pool
    AgentState agents[MAX_AGENTS];

    // Stats
    RoomStats stats;
    DarwinRecord darwin;
    int trade_count;
    TradeRecord trades[MAX_TRADE_HIST];

    // Room-level trading ($50 seed, one consensus bet per cycle)
    float    room_capital;         // $50 seed
    float    room_capital_peak;
    int      room_trades;          // total room trades
    int      room_wins, room_losses;
    RoomTrade room_trade;          // current active room trade
    float    prev_room_capital;    // for room-level return tracking

    // Lock flag (non-IPC — process owns, Python reader reads atomic)
    volatile int writing;       // 1 while C is writing, 0 when done
    float    nested_prediction;  // Latest cascade prediction from nested HT model (0-1)
    // ── P16: Feature importance tracker ──
    FeatureImportance feat_importance;
    // ── T17: Circuit breaker (risk limits) ──
    int      circuit_breaker_cycles; // >0 = cooling down (cycles remaining)
    int      circuit_breaker_count;  // Total times circuit breaker has triggered
    float    max_drawdown_pct;       // Max drawdown before tripping (e.g. 0.20 = 20%)
    int      daily_loss_streak;      // Consecutive losing room trades today
    float    daily_pnl;              // Net room PnL since last reset
    float    max_daily_loss_pct;     // C05: Max daily loss before circuit breaker trip (0.10 = 10%)
    int      last_daily_reset_day;   // C05: Day number (ts/86400) when daily_pnl was last reset
    // ── A13: Regime transition model (Markov matrix 3×3) ──
    int      regime_transition_counts[N_REGS][N_REGS]; // Transition counts between regimes
    int      prev_regime;            // Previous cycle's regime
    int      predicted_regime;       // Predicted next regime (argmax of current row)
    int64_t  circuit_breaker_ts;     // When breaker last triggered (for cooldown)
    float    circuit_breaker_peak;   // Peak capital at last breaker reset
    int      circuit_cooldown_cycles; // Config: how many cycles to cool down
    int      max_consecutive_losses; // Config: max consecutive losses before trip
    int      consec_room_losses;     // Current consecutive room trade losses
    float    prev_close;             // Previous cycle's close price (persisted across process restarts)
    // ── C25: Panic stop flag ---
    int      panic_stop;             // 1 = all trading halted (set externally via /tmp/money_room_panic)
    // ── T18: Position limits ──
    float    max_position_pct_room;   // Max % of total room capital any single agent can bet
    float    max_total_exposure_pct;  // Max % of total capital all agents can risk combined
    float    current_total_exposure;  // Sum of all open stakes
    float    peak_total_exposure;     // Peak total exposure recorded
    // ── T19: Order queue / trade rate limiting ──
    int      max_trades_per_cycle;    // Max new trades per cycle (0 = unlimited)
    int      trades_deferred;         // Trades deferred due to rate limit
    int      total_trades_deferred;   // Cumulative deferred count
    // ── T20: Slippage model tracking ──
    float    total_slippage_paid;     // Cumulative slippage cost paid ($)
    int      slippage_events;         // Number of trades with slippage applied
    // ── B02: Persistent price/volume history (survives engine restart) ──
    float    price_hist[N_FEED_MARKETS][FEED_HISTORY];
    float    volume_hist[N_FEED_MARKETS][FEED_HISTORY];
    int      price_hist_len[N_FEED_MARKETS];
    int      price_hist_idx[N_FEED_MARKETS];
    // ── B12: SP500 history for equity correlation ──
    float    sp500_hist[FEED_HISTORY];
    int      sp500_hist_len;
    int      sp500_hist_idx;
    // ── B23: VIX history for regime filter ──
    float    vix_hist[FEED_HISTORY];
    int      vix_hist_len;
    int      vix_hist_idx;
} RoomState;

// ── Error codes ──
typedef enum {
    ERR_OK = 0,
    ERR_NO_DATA = -1,
    ERR_MMAP_FAIL = -2,
    ERR_FILE_READ = -3,
    ERR_NO_AGENTS = -4,
    ERR_CAPITAL_EXHAUSTED = -5,
    ERR_MAX_TRADES = -6,
} RoomError;

#endif // ROOM_TYPES_H
