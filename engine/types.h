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
#define ROOM_AGENTS  10000
#define MAX_AGENTS    ROOM_AGENTS
#define N_FEATURES        64
#define N_REGS            3   // Regimes: 0=range, 1=trend, 2=volatile (P22)
#define MAX_ASSETS        8
#define MAX_TRADE_HIST    1000000
#define STATE_MAGIC       0x524F4D42  // ROMB — STATE_VERSION=6 for N_FEATURES 34→64
#define STATE_VERSION     7           // Current struct layout version (added PDT persist + withdrawal fields)

// Fee constants (shared across modules)
#define TAKER_FEE    0.0026f  // Kraken spot taker fee
#define GAS_FEE_EST  2.50f    // C14: Avg on-chain gas cost USD (L2 ~$0.50, L1 ~$5.00)
#define MATCH_FEE    0.002f   // Match fee on loser pool (0.2%)
// ── T97/C15: Minimum trade size (raised to $5 for Polymarket 5-share minimum) ──
#define MIN_TRADE_STAKE   0.5f    // D-FIX: Lowered from $5 to $0.50 for paper training
    // $5 minimum was blocking all trades when capital dropped below $5.
    // Paper mode needs continuous trading for Darwin evolution.
    // Live mode will enforce real platform minimums via stake clamping.
#define MAX_EXPOSURE_PCT  0.10f   // C11: Max 10% of room capital per single position

// ── C07: Correlation-based position limits ──
#define MAX_CORRELATION_EXPOSURE_PCT 0.25f  // C07: Max 25% of room capital in correlated asset basket
#define CORRELATION_THRESHOLD        0.80f  // C07: Assets with |corr| > 0.80 are "correlated"

// ── D44: Exchange fee table defaults ──
#define DEFAULT_EXCHANGE_FEE  0.0026f  // Default to Kraken
#define DEFAULT_MIN_ORDER     1.0f    // $1 minimum order size

// ── A19: Mini-batch SGD batch size ──
#define SGD_BATCH_SIZE    64      // Trades per mini-batch gradient update (A01: was 8, too sparse)
// ── A22: Max open positions per agent ──
#define MAX_OPEN_POSITIONS 3      // Max concurrent positions per $50 agent
// ── T20: Slippage model ──
#define SLIPPAGE_BPS          5.0f    // 5 bps = 0.05% baseline slippage
#define SLIPPAGE_VOL_SCALE   5.0f    // Additional bps per $100 of position (market impact)
#define SLIPPAGE_WEEKEND_MUL  2.0f   // C27: 2x slippage on weekends (lower liquidity)
#define OVERNIGHT_GAP_BPS    50.0f   // C26: 50bps overnight gap risk charge for non-crypto markets

// Market direction mode fees (for P2P ensemble paper proof)
#ifdef MARKET_MODE
#define MARKET_TAKER_FEE 0.0026f  // Default to Kraken
#define COINBASE_TAKER_FEE 0.0060f  // Coinbase 0.60%
#define COINBASE_MIN_FEE 0.99f       // Coinbase $0.99 minimum
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

// ── Feature vector (64-dim, multi-asset + weather + microstructure) ──
typedef struct {
    // === F1-F13: Core price/volume ===
    float price_delta_pct;      // F1:  Current vs window open %
    float micro_momentum;       // F2:  Last 2 closes delta %
    float rsi_7;                // F3:  7-period RSI (0-100)
    float volume_surge_ratio;   // F4:  Recent/prior volume ratio
    float ema_fast;             // F5:  3-period EMA
    float ema_slow;             // F6:  8-period EMA
    float macd_hist;            // F7:  MACD histogram value
    float bollinger_pct;        // F8:  %B position (0=lower, 1=upper)
    float divergence_score;     // F9:  Price-RSI divergence (-1..1)
    float pump_score;           // F10: Crony-weighted news sentiment (-1..1)
    float regime_indicator;     // F11: 0=range, 1=trend, 2=volatile
    float fear_greed_norm;      // F12: Normalized F&G (0-1)
    float herd_consensus;       // F13: % agents voting same direction
    // === F14-F16: Order book ===
    float ob_imbalance;         // F14: bid_vol/(bid+ask_vol) top-10 (0-1)
    float ob_depth_ratio;       // F15: bid_depth/(bid+ask_depth) 0.5% band
    float cvd_signal;           // F16: Cumulative volume delta normalized
    // === F17-F23: Advanced signals ===
    float dft_dominant;         // F17: DFT dominant frequency strength (0-1)
    float tail_risk_score;      // F18: Tail risk score (0-1)
    float funding_signal;       // F19: Funding rate deviation from 7d avg (-1..1)
    float oi_net_signal;        // F20: Aggregated OI signal (0-1)
    float ls_ratio_norm;        // F21: L/S taker volume ratio normalized (0-1)
    float liq_ls_ratio_norm;    // F22: Liquidation L/S ratio (0-1)
    float stable_inflow_norm;   // F23: Stablecoin volume ratio (0-1+)
    // === F24-F27: On-chain ===
    float whale_activity_norm;  // F24: Whale transaction activity (0-1)
    float hash_rate_norm;       // F25: BTC hashrate normalized (0-1)
    float difficulty_norm;      // F26: Mining difficulty normalized (0-1)
    float miner_floor_norm;     // F27: Miner cost floor normalized (0-1)
    // === F28-F29: Time-of-day ===
    float hour_of_day_norm;     // F28: Hour of day [0,1)
    float day_of_week_norm;     // F29: Day of week [0,1)
    // === F30-F32: Options ===
    float iv_skew;              // F30: IV skew (0-1+)
    float pcr_volume;           // F31: Put/call ratio by volume (0-1+)
    float iv_term_slope;        // F32: IV term structure slope (0-1+)
    // === F33-F34: Macro correlation ===
    float btc_sp500_corr;       // F33: Rolling BTC-SP500 correlation (-1..1)
    float vix_regime;           // F34: VIX regime filter (0/0.5/1)
    // === F35-F36: Weather (T721 — from weather_collector) ===
    float weather_temp_zscore;  // F35: Temperature z-score vs 30d avg (-3..3)
    float weather_precip_anom;  // F36: Precipitation anomaly vs 30d avg (0-1)
    // === F37: Inter-exchange basis (B47) ===
    float interexchange_basis;  // F37: BTC price spread between exchanges (0-1)
    // === F38: Economic surprise index (B24) ===
    float economic_surprise;    // F38: Actual vs expected macro data (-1..1)
    // === F39: News sentiment delta (B25) ===
    float news_sentiment_delta; // F39: Sentiment change over 24h (-1..1)
    // === F40: Social volume spike (B26) ===
    float social_volume_spike;  // F40: Mention burst z-score (0-1)
    // === F41-F42: Return distribution moments (A46) ===
    float return_skew;          // F41: Return skewness (-1..1)
    float return_kurtosis;      // F42: Return kurtosis excess (0-1)
    // === F43: Realized vol ratio (B08) ===
    float realized_vol_ratio;   // F43: Short/long vol divergence (0-1)
    // === F44: OB imbalance change (B05 delta) ===
    float ob_imbalance_change;  // F44: OB imbalance delta from t-1 (-1..1)
    // === F45: CVD trend (B06 slope) ===
    float cvd_trend;            // F45: Cumulative volume delta trend (-1..1)
    // === F46: Liquidation cascade (B16) ===
    float liq_cascade;          // F46: Liquidation cascade signal (0-1)
    // === F47: Funding rate change (B14 delta) ===
    float funding_rate_change;  // F47: Funding rate change from 7d avg (-1..1)
    // === F48: Open interest change (B15 delta) ===
    float oi_change;            // F48: Open interest change from 7d avg (-1..1)
    // === F49-F50: TWAP/VWAP proximity ===
    float twap_proximity;       // F49: Price proximity to TWAP (0-1)
    float vwap_proximity;       // F50: Price proximity to VWAP (0-1)
    // === F51: Overnight gap risk (C26) ===
    float overnight_gap_risk;   // F51: Overnight gap risk charge (0-1)
    // === F52: Weekend liquidity (C27) ===
    float weekend_slippage;     // F52: Weekend liquidity penalty (0-1)
    // === F53: Cross-room ensemble (A10) ===
    float room_ensemble_signal; // F53: Cross-room consensus signal (0-1)
    // === F54: Data freshness (D37) ===
    float feed_freshness_score; // F54: Data freshness score (0-1, 1=fresh)
    // === F55: Volatility regime change ===
    float vol_regime_change;    // F55: Volatility regime transition prob (0-1)
    // === F56: Correlation breakdown (B11) ===
    float corr_breakdown;       // F56: BTC-SP500 correlation breakdown (0-1)
    // === F57: Options flow (B13 — from options_flow collector) ===
    float options_flow_signal;  // F57: Unusual options flow signal (-1..1)
    // === F58: Dark pool (B12 — from dark_pool_feat collector) ===
    float dark_pool_signal;     // F58: Dark pool print signal (0-1)
    // === F59: Insider trades (B13 — from insider_trades collector) ===
    float insider_trade_signal; // F59: Insider trade sentiment (-1..1)
    // === F60: 13F institutional flow (B14) ===
    float institutional_flow;   // F60: 13F institutional flow (-1..1)
    // === F61: Short interest (B15) ===
    float short_interest_signal;// F61: Short interest change (-1..1)
    // === F62: ETF flow (B16) ===
    float etf_flow_signal;      // F62: ETF flow signal (-1..1)
    // === F63: Seasonality (B17) ===
    float seasonality_signal;   // F63: Calendar seasonality signal (-1..1)
    // === F64: Reserved ===
    float _reserved_64;         // F64: Reserved for future expansion
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
    float    predicted_prob;    // A006: predicted probability for accuracy scoring
    int      regime;            // A006: regime at time of prediction
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
    float    win_rate_var;      // C30: Running variance of WR (for stability filter)
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
    // ── A19: Mini-batch SGD accumulators ──
    float    grad_accum[N_REGS][N_FEATURES];  // Accumulated gradient per (regime, feature)
    float    bias_accum[N_REGS];              // Accumulated bias gradient per regime
    int      batch_count;                     // Trades accumulated in current batch
    // ── T96: PDT (Pattern Day Trader) enforcement ──
    int      day_trades_5d;       // Day trades in rolling 5-business-day window
    int64_t  day_trade_roll_ts;   // Start of rolling 5-day window (timestamp)
    // ── A22: Per-agent open position tracking ──
    int      n_open_positions;    // Number of currently open positions
    // ── A18: Profit factor tracking for Darwin fitness ──
    float    gross_profit;         // Sum of all winning trade PnL
    float    gross_loss;           // Sum of all losing trade PnL (positive value)
    // ── A17: Brier score calibration tracking ──
    float    brier_num;            // Running sum of (predicted - outcome)^2
    int      brier_den;            // Count of resolved predictions with Brier data
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
    uint32_t state_crc;         // F10: CRC-32 checksum over rest of struct (bytes 8..sizeof(RoomState))
    int      state_version;     // F11: struct layout version for migration (current=3)
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
    float    room_take_profit_pct; // C35: take-profit threshold (default 20%)
    int      room_take_profit_triggered; // C35: 1 when take-profit has fired
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
    float    max_direction_pct;       // C36: Max % of total capital per single direction (YES/NO)
    float    current_total_exposure;  // Sum of all open stakes
    float    current_yes_exposure;    // C36: Sum of stakes in YES direction
    float    current_no_exposure;     // C36: Sum of stakes in NO direction
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
    // ── A30: Epsilon-greedy exploration state ──
    float    epsilon;                // Current exploration rate (decays each cycle)
    float    epsilon_init;           // Starting epsilon (default 0.05)
    float    epsilon_min;            // Floor epsilon (default 0.005)

    // ── C07: Correlation-based position limits ──
    // Tracks per-asset-direction exposure to cap correlated positions
    float    asset_exposure[MAX_ASSETS][2];  // [asset_id][dir] = $ exposure
    float    max_correlation_exposure_pct;    // Max % of capital in correlated basket
    int      correlation_blocked;             // C07: count of trades blocked by correlation

    // ── C23: Duplicate trade detection ──
    // Key = (agent_id << 32) | (window_ts % 0xFFFFFFFF) stored per recent entry
    // Simple hash set: last N trade keys to detect duplicates within window
    int64_t  recent_trade_keys[1024];         // Rolling window of trade keys
    int      recent_trade_key_idx;            // Current write index
    int      duplicate_trades_blocked;        // C23: count of duplicate trades blocked

    // ── C24: Cross-room market correlation ──
    float    cross_room_correlation[MAX_ASSETS][MAX_ASSETS]; // Rolling return correlation
    float    cross_room_return[MAX_ASSETS][FEED_HISTORY];    // Per-asset last N returns
    int      cross_room_ret_idx[MAX_ASSETS];                 // Write index per asset
    int      cross_room_ret_len[MAX_ASSETS];                 // Length per asset

    // ── D44: Exchange fee table (per-asset fee lookup) ──
    float    exchange_fees[MAX_ASSETS];      // Fee rate per asset (0.0026 = 0.26% Kraken)
    float    exchange_min_fees[MAX_ASSETS];  // Min fee per asset (e.g., Coinbase $0.99)
    float    exchange_min_orders[MAX_ASSETS]; // Min order size per asset ($)

    // ── I2: PDT tracker persistence (survives engine restarts) ──
    int      pdt_room_count;                 // Room trades in current 5-day window
    int64_t  pdt_room_window_start;          // Epoch when current window started

    // ── C41: Withdrawal schedule ──
    float    withdrawal_threshold;           // Auto-withdraw when room_capital exceeds this
    float    withdrawal_target;              // Amount to withdraw (profit above initial)
    int64_t  last_withdrawal_ts;             // Timestamp of last withdrawal
    float    total_withdrawn;                // Cumulative withdrawals
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
    ERR_DATA_EXHAUSTED = -7,  // PAPER_MODE: historical data exhausted / max cycles reached
} RoomError;

#endif // ROOM_TYPES_H
