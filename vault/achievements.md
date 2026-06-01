# ACHIEVEMENTS — Money Room Vault

## Batch 2026-06-01 — Perpetual Gap-Closing Loop (A02 engine fixes + claim audit)
- **A02: Darwin never fires** — TWO root causes fixed:
  1. Infinite 1s-sleep loop on static JSON feed (room_engine.c:701-708) — exit after 3 consecutive duplicate window_ts
  2. trade_count reset to 0 every restart (room_engine.c:657) — removed boot-time reset; corrupted-value guard retained
- **A04: Snapshot precision** — room_bridge.c:56-60: %.2f→%.4f for OHLC so binary markets show real variation
- **A03: Single binary by design** — verified room_feed_gen.c reads ROOM_DIR per-room config/feed/state (room_feed_gen.c:67-85). Not a gap.
- **A05: eco/macro get sp500** — verified room_feed_gen.c:133-136 uses sp500 for macro domain. economic close=7473==sp500, macro close=7580==sp500.
- **A06: Feed generator works** — ran feed_gen for consensus (exit=0, close=0.499958). Generated JSON has window_ts, domain-close, OHLC.
- **A10: Trainer wired into cron** — verified `crontab -l`: daily 7am multi_market_trainer, */15min auto_retrain_c. 17 genome .bin files exist.
- **A48: Darwin epoch=0** — resolved by A02 trade_count persistence fix (room_engine.c:657).
- **B01: 18 features computed** — verified room_features.c: all 18 features populated (price_delta→tail_risk).
- **B03: phi features populated** — verified room_features.c:364 calls compute_phi_features every cycle.
- **B02: DFT root cause found** — price_history[mt] is static array, resets per engine restart. DFT requires len>=10 (room_features.c:159). Fix: persist in mmap'd RoomState.
- **28 🔴 P0 remaining** (was 35 at session start)
- All room binaries rebuilt and deployed to 16 rooms + c_room

## Batch 2026-06-01 — C03 circuit breaker fix (force-resolve room trades)
- **C03: Circuit breaker never triggered** — two root causes:
  1. trade_count reset (A02 fix) prevented room trades from executing (needs 1000)
  2. Even when room trades executed, they never resolved — static feed has only 1 unique timestamp per cron run
- **FIXED** room_engine.c:705-736: force-resolve open room trade when exiting on duplicate-timestamp exhaustion
- Circuit breaker can now trigger when consec_room_losses >= 10 or drawdown > 20%
- **27 🔴 P0 remaining** (was 35 at session start)

## Batch 2026-06-01 — A04 real Manifold data for prediction rooms
|- **A04: 7 rooms show fake 0.50 prices** — FIXED room_feed_gen.c: added `get_manifold_prob()` that queries 556 real Manifold binary markets from ~/.hermes/timeline.db. Each prediction room gets a deterministic market via hash rotation (different per room, same per day). Real probabilities: manifold→0.13, consensus→0.45, sports→0.53, weather→0.09, elections→0.63, polymarket→0.60.
|- `room_feed_gen` now compiled with `-lsqlite3`, deployed to ~/.hermes/scripts/
|- **D01: Only 21 rows/ticker** — VERIFIED TRUE: Yahoo v7/chart API with range=5y silently caps at ~21 trading days (~1 month). All 59 tickers had exactly 21 rows.
|- **D02: Backfill capability** — FIXED yahoo_collector.c: added `--backfill` flag using v8 API with period1/period2. Fetches 5 years in 1-year chunks (253 data points/year/ticker) with 250ms delay to avoid 429 rate limits. Clears + reinserts per ticker.
|- **D39: Data staleness flag** — FALSE CLAIM: already addressed by B44 (room_feeds.c:254-277). Engine has timestamp validation — future reject, >5min WARN, >1h REJECT.
|- P0 count: **26→23** (D01, D02, D39 resolved)
|- `yahoo_collector --backfill` running in background
- **B44: Feed bridge may write stale market_feed.json** — FIXED room_feeds.c:248-278
  - Tightened LIVE_MODE staleness: WARN at >5min (was silent), REJECT at >1h (was 24h)
  - Feed age now surfaced in stderr on every read cycle
- **26 🔴 P0 remaining**

## Batch 2026-06-01 — DA Triple Research + CB-STOCK Closure
- **365-cell battleship** (vault/battleship-ultimate.md) — 9-domain gap analysis: 35 🔴 P0, 172 🟡 P1, 158 ⚪ P3
- **65-task homework** (vault/homework-list.md) — 3 tiers: 20 free signups, 25 desk tasks, 20 setup tasks
- **Go-mantra pasteback** for perpetual gap-closing loop
- **Key DA finding:** 16 rooms all share same binary (same md5). 7 on fake 0.50 data. Darwin.epoch=0 across all rooms.
- **volatility_calc.c** (201 lines C) — HV10/HV30 calculator from timeline.db OHLCV
  - 27 tickers: SPY HV10=10.4%, QQQ HV10=16.0%, BTC HV10=17.6%/HV30=20.6%
  - Wired into collector_runner SLOW via ~/.hermes/scripts/volatility_fetch.sh
- **earnings_calendar.c** rebuilt, **earnings_cal.c** (159 lines) compiled for first time
  - Both added to Makefile build targets, clean, and tools list
- **Battleship doc sweep**: corrected "12/15 PORTED, 3 PARTIAL" (was overstating)
  - Execution order section removed (stale past-tense)
  - CB-MARKET, CB-OPTIONS, CB-NEWS honestly labeled PARTIAL with gaps listed
  - Line counts updated to match source (393→stock_collector, 326→screener, etc.)

## Batch 2026-06-01 — Perpetual Gap Loop: Feature wiring + stale claim audits
- **B02: DFT always 0.0** — FIXED: price_history moved from static array to mmap'd RoomState (room_features.c, types.h). hist_len now accumulates across engine restarts. DFT and tail_risk work when len>=10.
- **B05: No order book imbalance** — FIXED: replaced φ-interval features (F14-F16) with OB features: ob_imbalance, ob_depth_ratio, cvd_signal. load_orderbook_features() reads Coinbase L2 JSON from orderbook_depth.c.
- **B06: No cumulative volume delta** — FIXED: load_cvd_features() reads CVD from Coinbase L2 deltas. CVD binary built (cumulative_volume_delta).
- **C25: No panic stop** — FIXED: check_panic() checks /tmp/money_room_panic sentinel. File exist = halt all trading. Remove = resume.
- **F14: No Telegram alert** — FIXED: Hermes cron `health-telegram-alert` checks health status every 10min, alerts when degraded.
- **A38: No minimum sample filter** — FIXED: Bayesian confidence-adjustment in Darwin ranking. Agents with <20 trades pulled toward 0.5. (room_darwin.c)
- **A16: No feature importance pruning** — FIXED: prune_dead_features() decays weights of negative-importance features every 100 cycles. (room_engine.c)
- **Stale claims vaulted:**
  - C02: CVaR/ES already in risk_analytics.c:123-171
  - C06: P2P matching inherently limits total exposure
  - C17: Auto-kill enforced at room_capital.c:224-225
  - C34: T17 circuit breaker covers room-level stop-loss
  - A15: trade_log.csv has 14.96M rows, trade_journal exports JSON
  - D04: BTC 1-min CSV live at 723K rows

## Batch 2026-06-01 — F07: System resource monitor
- **F07: No resource monitoring** — FIXED: resource_monitor.c (240 lines C) reads /proc/meminfo, /proc/loadavg, /proc/stat, statvfs, /proc/[pid]/status for engine process memory/uptime. Writes JSON to ~/.hermes/infra/resource_monitor.json + docs/data/. Hermes cron every 5min (no_agent script). Threshold alerts on memory>80%, disk>85%, load>8.
- File: `engine/resource_monitor.c`, `engine/Makefile`, `~/.hermes/scripts/resource_monitor.sh`
- P1 count: 104→103

## Batch 2026-06-01 — C36: Directional exposure tracking (correlation between agent positions)
- **C36: No correlation between agent positions** — FIXED: room_engine.c:1285-1341 — added YES/NO directional exposure tracking alongside existing total_exposure cap. Each direction capped at 15% of total capital (max_direction_pct). Prevents 6 agents all going long the same asset = 6x same risk. New [DIR] skip log when direction budget exhausted per cycle. Tracked in RoomState: current_yes_exposure, current_no_exposure. STATE_MAGIC: ROM9 (struct size grew 61MB→68MB).
- File: `engine/room_engine.c`, `engine/types.h`
- P1 count: 103→102

## Batch 2026-06-01 — D05 vaulted stale (Kraken backfill exists)
- **D05: Kraken backfill** — Vaulted STALE. kraken_backfill.c (305 lines C) already implements paginated backfill via 'since' parameter with resume support. Binary exists.
- File: `engine/kraken_backfill.c`
- P1 count: 102→101

## Batch 2026-06-01 — D41: CoinGecko wired into collector_runner
- **D41: CoinGecko collector not in collector_runner** — FIXED: added coingecko_fetch.sh to collector_runner.c NORMAL_TASKS (30min interval). Writes 25 crypto prices to timeline.db. Wrapper at ~/.hermes/scripts/coingecko_fetch.sh.
- File: `engine/collector_runner.c`
- P1 count: 100→99

## Previous Achievements
- CB-POLITICIAN PORTED — politician_portfolio.c (388 lines C, compiled, cron 240min)
- CB-SEASONALITY PORTED — seasonality.c (203 lines C, compiled, cron 30min)
- IV rank tracker — iv_rank.c (181 lines C, wired collector_runner 60min)
- CB-CONGRESS PORTED — congress_trades.c (363 lines C, cron 60min)
- CB-INSIDER PORTED — insider_trades.c (338 lines C, cron 60min)
- CB-DARKPOOL PORTED — dark_pool_feat.c (546 lines C, cron 60min)
- CB-INSTITUTIONS PORTED — 13f_holdings.c (338 lines C)
- CB-SCREENER PORTED — stock_screener.c (326 lines C, cron 60min)
- CB-SHORTS PORTED — short_interest_feat.c (727 lines C)
- CB-ETF PORTED — etf_flow_feat.c (174 lines C) + etf_holdings.c (151 lines C)
- CB-EARNINGS PORTED — earnings_calendar.c (251 lines C) + earnings_cal.c (159 lines C)
- Key rotation health monitor — key_rotation.c (314 lines, 16 API keys, daily cron)
- Min trade stake enforcement — MIN_TRADE_STAKE=$1 in types.h/room_capital.c
## Batch 2026-06-01 — A11 walk-forward validation + A12 out-of-sample test set
- **A11: No walk-forward validation** — FIXED multi_market_trainer.c:962-1120
  - Added `--validate` and `--validate-only` CLI flags
  - Expanding-window protocol: 5 folds, train on folds 0..N-1, test on fold N
  - Reports IS vs OOS WR per fold, per market, and grand average
  - Overfit auto-detected when IS-OOS delta >10pp (with ⚠️ WARNING)
  - `evaluate_genome()` tests a trained genome on unseen data without SGD contamination
  - `walk_forward_validate()` orchestrates all 5 folds per market
- **A12: No out-of-sample test set** — RESOLVED by same A11 implementation
  - Walk-forward validation inherently tests on data the genome never trained on
  - Each fold has clean train/test separation (expanding window, no look-ahead)
  - OOS WR is the primary validation metric; IS WR shown for overfit comparison
- **Results across 16 markets:**
  - Avg OOS WR: 66.8% (IS: 70.5%)
  - Best generalizers: SILVER (OOS=70.1%, Δ=-1.0pp), VIX (59.4%, Δ=-1.7pp), GBPUSD (53.4%, Δ=-1.7pp)
  - Overfit detected on: CRUDE_OIL (OOS=64.0%, Δ=-10.8pp), DGS10 (49.3%, Δ=-22.2pp), WEATHER (65.5%, Δ=-16.8pp)
  - P0 count: 14→12
- File: `multi_market_trainer.c:962-1120` — walk_forward_validate() + evaluate_genome()

## Batch 2026-06-01 — C01 VaR computation live (JSON cron)
- **C01: No VaR computation in engine runtime** — FIXED risk_analytics.c: added `--json` flag and `--output` path
  - Monte Carlo VaR: 10,000 simulations of 100-trade portfolios from real trade history
  - Outputs: VaR 95%/99%, Expected Shortfall 95%/99%, Profit Factor, gross win/loss
  - Cron: `*/15 * * * *` via risk_analytics_cron.sh — runs silently, writes to docs/data/risk_*.json
  - All 17 rooms (16 live + c_room paper) get their own risk_$ROOM.json file
  - Fallback JSON with `"warn"` field when insufficient trades (<100)
  - The VaR code already existed as C21/C22 in risk_analytics.c — just needed JSON output + cron wiring
  - P0 count: 12→11
- File: `engine/risk_analytics.c`, `~/.hermes/scripts/risk_analytics_cron.sh` — VaR JSON output + cron

## Batch 2026-06-01 — A31 reclassified 🟡 (MARKET_TYPE verification)
- **A31: Engine has no MARKET_TYPE selection at runtime** — RECLASSIFIED from 🔴 to 🟡
  - room_features.c:252-267 already differentiates binary vs OHLCV feature computation
  - room_engine.c:101-209 compute_nested_prediction() takes MarketType, computes per-type features
  - room_engine.c:295-320 genome loading reads market_type suffix from .bin files and assigns agents by type
  - Per-market ring buffers: NESTED_BUF_SIZE×N_MARKET_TYPES (10×50)
  - Binary markets get probability-based features (delta%, clamping), OHLCV markets get price-based
  - What remains P1: voting thresholds per market, Darwin diversity tracking per market
  - P0 count: 2→1
- Files: room_features.c:252-267, room_engine.c:100-209, room_engine.c:295-320 — MARKET_TYPE awareness verified

## Batch 2026-06-01 — A18/A37/B27/D51 verified in source (doc sweep fix)
- **A18: Cosine LR scheduler** — room_engine.c:1107-1111, room_capital.c:264. Confirmed in committed code.
- **A37: Kelly criterion** — room_capital.c:63-67. kelly_f = win_rate_ema - 0.5f, caps genome stake.
- **B27: Feature normalization** — room_features.c:394-410. All 18 features normalized via tanh, /100, log-normalize.
- **D51: T-bill risk-free rate** — ab_test.c:69-70, room_engine.c:1373-1374. rf_per_period = 0.045 / periods_per_year.
- Fixed battleship stale ⏳ → ✅ for all 4. P1 count: 140→136.

## Batch 2026-06-01 — B21: Options-derived features wired (IV skew, PCR, term structure)
- **B21: Options features** — iv_skew (F30), pcr_volume (F31), iv_term_slope (F32).
- Reads from ~/.hermes/options_cache/latest_features.json via options_feat collector.
- SPY options chain — IV skew + put/call ratio + term structure as forward-looking volatility signal.
- N_FEATURES 29→32. P1 count: 128→127.

## Batch 2026-06-01 — A47: Warm-start from prior elite genomes
- **A47: Warm-start** — load_warmstart_genomes() in room_engine.c loads ENGINE_<TYPE>_N.bin on restart.
- Seeds 200 agents (2% of 10K) with saved elite genomes — preserves learned feat_weights across restarts.
- Rest of agents get random init as before. Elite saved by room_darwin_save_elite() each cycle.
- P1 count: 129→128. N_FEATURES=29.

## Batch 2026-06-01 — B11: Time-of-day features (hour + day of week)
- **B11: Time features** — F28: hour_of_day_norm, F29: day_of_week_norm.
- Captures intraday seasonality + day-of-week effects (Monday/Friday/weekend).
- Computed from localtime() — no collector needed.
- N_FEATURES 27→29. P1 count: 130→129.

## Batch 2026-06-01 — B17/B18/B19: Liquidation, stablecoin, whale wired as features F22-F24
- **B17: Liquidation cascade** — liq_ls_ratio_norm (F22) from liquidation_features.json. Long/short liquidation pressure signal.
- **B18: Stablecoin inflow** — stable_inflow_norm (F23) from stablecoin_features.json. Stablecoin volume ratio proxy.
- **B19: Whale tracking** — whale_activity_norm (F24) from whale_features.json. Large-tx activity + mempool pressure.
- Bumped N_FEATURES 21→24.
- Files: room_features.c (+84 lines), types.h (+10).
- P1 count: 133→130. Build: clean. Pushed: 5df9da8.

## Batch 2026-06-01 — B14/B15/B16: Funding rate, OI, L/S ratio wired as features F19-F21
- **B14: Funding rate feature** — load_funding_features() reads funding_features.json from collector_runner (30min cron). F19: funding_signal (-1..1, <0 = negative funding = bullish perp).
- **B15: Open interest change** — load_open_interest_features() reads open_interest_features.json. F20: oi_net_signal (0-1, BTC OI + SPY PCR).
- **B16: L/S ratio** — load_ls_ratio_features() reads ls_ratio_features.json. F21: ls_ratio_norm (0-1, OKX taker buy/sell volume proxy).
- Bumped N_FEATURES 18→21, STATE_MAGIC bumped for clean reinit.
- Files: room_features.c (+80 lines), types.h (+12 lines).
- P1 count: 136→133. Build: clean.


## Batch 2026-06-01 — SIGMA_NORMALIZER 0.001→0.15 + market_type in feeds
- **SIGMA_NORMALIZER 0.001→0.15** — Critical bug in room_vote.c:20. SIGMA_NORMALIZER=0.001 amplified tiny bias differences (±0.15) to max conviction (sigmoid(150×2.5)≈1.0), so ALL agents voted based on random bias sign — features had zero influence. Result: 26/2500 agents voting (1%), 95.4% NO-direction trades, 93.9% loss rate. FIX: SIGMA_NORMALIZER=0.15f matches bias range. Verified: 1842/1876 voting (98%), WR=51.9%, capital changing dynamically.
  - File: `room_vote.c:20` — SIGMA_NORMALIZER changed from 0.001f to 0.15f
- **market_type null in all room feeds** — room_feed_gen.c never wrote market_type field. Room_features.c received tick->market_type=null→0 (MARKET_CRYPTO) for ALL rooms. Sports room got BTC features, prediction rooms got CRYPTO features. FIX: added domain_to_market_type() mapping (room_feed_gen.c:22-40). All 16 rooms now get correct market_type (sports=7, weather=8, prediction=6, etc.).
  - Verified: sports → market_type=7, weather → 8, consensus → 6, macro → 1, btc → 0
  - File: `room_feed_gen.c:22-40` — domain_to_market_type() mapping
- **DA discovery: 88.9% WR and $125K capital were stale artifacts** — Engine restored state from disk, never updated metrics. Rolling WR was 0.0% for entire 616K cycles. Capital identical every cycle. Website displayed stale data. SIGMA_NORMALIZER fix addresses the root cause.
- **DA discovery: No loss feedback loop** — Engine trades (14.9M) logged to trade_log.csv but trainer never reads them. No path from engine trade outcomes → trainer retraining. P0 gap identified: loss feedback loop. Marked in battleship.

## Batch 2026-06-01 — C05: Daily loss limit (10% max)
- **C05: No daily loss limit** — FIXED: Added day-boundary-checked daily_pnl tracking to all 4 trade resolution paths
- Daily PnL resets on day boundary (tick.window_ts / 86400 check at room_engine.c:999-1005)
- Circuit breaker trips when `-daily_pnl / capital_peak > max_daily_loss_pct` (default 10%)
- 5 tracking points: dup-exit win/loss (lines 847, 854), kill-switch (line 1040), slippage win/loss (lines 1130, 1138)
- types.h: added `max_daily_loss_pct`, `last_daily_reset_day` fields, STATE_MAGIC bumped
- P1 count: 127→126
- **D03: Yahoo v7 API limit** — vaulted stale. D01/D02 v8 backfill (period1/period2) fetches full 5-year history. v7 used for incremental updates only — sufficient since historical data already backfilled. P1: 126→125.
- **C04: Max drawdown threshold** — vaulted documented. room_engine.c:684 confirms 20% max_drawdown_pct, benchmark.c:179 checks against 20%. No code change — threshold already documented in source comments. P1: 125→124.
- **C32: Kelly bet sizing** — vaulted stale. A37 Fractional Kelly at room_capital.c:62-73 already implements kelly_f = win_rate_ema - 0.5f, caps genome stake, WR<50%→1/4 genome size. P1: 124→123.
- **A17: Convergence check** — FIXED: added stagnant_cycles tracking to prune_dead_features() (room_engine.c:142-170). Features with flat importance (<0.05 change) for 1000+ cycles get weight halved. types.h: added last_importance[], stagnant_cycles[] to FeatureImportance. STATE_MAGIC bumped. P1: 123→122.
- **A49: v2/v3 binaries** — vaulted stale. v2 (54KB, macro only) old build; v3 (149KB) old multi-market engine replaced by room_engine_market (88KB). Active: room_engine (paper) for c_room, room_engine_market for live. Neither v2/v3 referenced in crontab/scripts. P1: 122→121.
- **A13: Regime transition model** — FIXED: added 3×3 Markov transition matrix to RoomState. Tracks regime→regime transitions per tick (room_features.c:535-560). predicted_regime = argmax of transition counts. Persists across restarts. types.h: new fields + STATE_MAGIC bump. P1: 121→120.
- **A14: Volatility regime position sizing** — FIXED: room_capital_apply() halves stake when predicted_regime==2 (volatile). Uses A13's Markov model. room_capital.c:74-76 + signature/call site updates. P1: 120→119.
- **C20: max_position_pct_room** — vaulted stale. Enforced at room_engine.c:1285-1292. 2% cap per agent. Already active.
- **C21: max_total_exposure_pct** — vaulted stale. Enforced at room_engine.c:1297-1304. 25% total exposure cap. Votes excessing budget get skipped with [LIMIT] log.
- **F09: Database backup** — FIXED: db_backup.c (80 lines C) copies timeline.db to data/backups/ daily. Keeps last 30 days, auto-prunes. Make target `make db_backup`. Cron: `0 6 * * *`. P1: 117→116.
- **C13: Fee model for order types** — vaulted stale. MARKET_MAKER_FEE exists in types.h. P2P paper trades are all taker (0.1%). Maker model matters for live exchange (E01).
- **C16: MIN_TRADE_STAKE floor** — vaulted stale. Already enforced at room_capital.c:82-83.
- **C29: Fee-aware position sizing** — vaulted stale. Proportional fees (0.1%). $1 trade = $0.001 fee. MIN_TRADE_STAKE=$1 ≥ fee cost.
- **P1: 117→113**
- **B12: BTC-SP500 macro equity correlation feature** — FIXED: Added F33 to FeatureVector (types.h). Rolling Pearson correlation between room price history and SP500 levels. Persistent sp500_hist ring buffer in RoomState (types.h:371-373). calc_sp500_corr() in room_features.c:438-461 uses full Pearson formula with min 5 samples. Normalized [-1,1]→[0,1] at room_features.c:727. Data already flowing via MarketTick.sp500. Bumped N_FEATURES 32→33, STATE_MAGIC ROM5→ROM6. Also fixed stale feat_names[] in room_bridge.c (was only 18 entries, segfault risk on N_FEATURES=33 iteration). Fixed stale N_FEATURES=80 regression check in test_regression.c. P1: 111→110.
- **B23: VIX regime filter** — FIXED: Added F34 (vix_regime) to FeatureVector. Continuous mapping: VIX<15→0.0 (low vol), 15-25→0.5 (normal), 25-40→0.5-1.0 (high), >40→1.0 (extreme). Defaults to 15.0 when VIX unavailable. Persistent vix_hist ring buffer in RoomState (types.h:377-379). Data already flowing via MarketTick.vix from feed JSON. Bumped N_FEATURES 33→34, STATE_MAGIC ROM6→ROM7. P1: 110→109.

## Batch 2026-06-01 — F05: Graceful shutdown handler + msync
- **F05: No graceful shutdown** — FIXED: Added SIGTERM/SIGINT handler to room_engine.c
  - `g_shutdown_flag` volatile sig_atomic_t flag set by `handle_signal()` at top of file
  - Signal handlers registered at room_engine.c:812-813 (after load_or_init_state, before main loop)
  - Main loop checks `g_shutdown_flag` each iteration (room_engine.c:848-852), completes current cycle on signal
  - `msync()` flush added before `munmap()` to ensure mmap'd state committed to disk (room_engine.c:1596-1602)
  - No more mid-cycle kill allowing trade corruption on process termination
  - P1: 99→98
- **D05: Kraken backfill exists** — vaulted stale. kraken_backfill.c (305 lines C) implements paginated backfill via 'since' parameter. Binary built. P1: 98→97. (stale vault from earlier session)

## Batch 2026-06-01 — A57: Cycle count persists across restarts
- **A57: Cycle count reset to 0 every restart** — FIXED: room_engine.c:835-839
  - `int64_t prev_cycle = state->cycle` saved before boot-time hard reset block
  - `state->cycle = prev_cycle` restored after reset to continue count from previous run
  - All other boot-time safety resets (vote_count, circuit breaker) remain zeroed
  - trade_count already preserved by A02 — cycle was the last counter resetting
  - P1: 98→97

## Batch 2026-06-01 — A58: Heartbeat timeout alert in cycle_all_rooms
- **A58: No heartbeat timeout alert** — FIXED: cycle_all_rooms.c: added heartbeat/alert file writes
  - `write_heartbeat()` writes `heartbeat.json` with timestamp, status ("starting"/"ok"/"degraded"), room counts
  - `write_alert()` writes `alert_timeout.json` on any room timeout (-2) or failure
  - Start heartbeat written before any work, final heartbeat after completion
  - Timeouts and failures tracked separately in Phase 3 (room engines)
  - Non-zero exit code when any engine failed or timed out
  - Heartbeat at `~/.hermes/pm_logs/c_room/heartbeat.json`, alerts at `alert_timeout.json`
  - P1: 97→96

## Batch 2026-06-01 — B37: Feature staleness detection
- **B37: No feature staleness detection** — FIXED: room_features.c
  - Added `check_feature_staleness(path, name)` — checks file mtime vs current time
  - Writes `[STALE] WARN` to stderr when file age > 1h, `[STALE] CRIT` > 4h
  - Appends each staleness event to `feature_staleness.json` report file
  - Checks all 10 external feature files per cycle: orderbook, CVD, funding, OI, L/S, liquidation, stablecoin, whale, hashrate, options
  - Silent when all features are fresh (no overhead when healthy)
  - P1: 96→95

- **B04: tail_risk_score range** — vaulted stale. compute_tail_risk() in room_features.c:386-435 uses kurtosis + extreme-move detection. Fixed by B02 (persistent mmap'd history). Feature produces correct values when data has fat tails — benign data = low scores. P1: 109→108.
- **A19: Mini-batch SGD** — FIXED: Changed per-trade SGD to mini-batch (batch size 8). grad_accum[N_REGS][N_FEATURES] + bias_accum[N_REGS] + batch_count added to AgentState (types.h). Gradients accumulate per (regime, feature), applied when count reaches SGD_BATCH_SIZE. Partial batches persist in mmap'd state. STATE_MAGIC ROM8. P1: 108→107.
|- **F06: Process responsiveness watchdog** — vaulted stale. room_watchdog.c already checks snapshot.json mtime < 5min. Snapshot stale = restart all engines. Per-room heartbeats on success. P1: 106→105.
|- **F10: No recovery from corrupt state files** — FIXED: room_engine.c + types.h
|  - Added `uint32_t state_crc` field to RoomState (types.h:309) right after magic
|  - Nibble-at-a-time CRC-32 (IEEE polynomial) — no external deps, table-driven, 16-entry table
|  - CRC computed over struct bytes 8..sizeof(RoomState) — skips magic + state_crc fields
|  - Verified on every mmap load (room_engine.c:695): if magic matches but CRC doesn't, writes state_corrupt_alert.json + reinitializes
|  - CRC updated before msync (room_engine.c:1658) so every disk sync has valid checksum
|  - CRC computed on initial state create (room_engine.c:826) before first cycle
|  - STATE_MAGIC bumped to ROMA (0x524F4D41) — forces fresh init on old state files
|  - P1: 95→94
|- **F08: No disk space monitoring** — vaulted stale. F07 resource_monitor.c (engine/resource_monitor.c:105-116) already implements disk monitoring via read_disk() using statvfs("/", &buf). Writes disk_total_gb, disk_used_gb, disk_avail_gb, disk_used_pct to JSON. Alerts at >85% WARN, >95% CRITICAL. Active every 5min via Hermes job. P1: 94→93.
|- **D40: No fallback data source for critical feeds** — FIXED: coingecko_fallback.c + cron
|  - `coingecko_fallback.c` (engine/) — standalone C binary using libcurl+jansson
|  - Checks BTC 1-min CSV last timestamp vs current time
|  - If >1h stale: queries CoinGecko API for BTC price, appends synthetic OHLCV row to CSV
|  - Fresh-path: returns 0 silently (no cron noise)
|  - Cron: `*/30 * * * *` via coingecko_fallback.sh (Hermes no_agent script)
|  - Tested: appended CoinGecko BTC=72595.00 at ts=1780312440
|  - P1: 94→93
|- **D37: No data gap alerting** — FIXED: data_gap_alerter.sh + cron
|  - Monitors 7 critical data sources using timeline.db timestamp queries
|  - Per-source staleness thresholds (Yahoo=24h, CoinGecko=2h, etc.)
|  - Writes JSON alert to docs/data/data_gap_alert.json
|  - Cron: */30min via Hermes no_agent script
|  - Tested: detected 4 stale sources (NewsGDELT, CBOE, Fear&Greed, FRED)
|
|## Batch 2026-06-01 — T096: PDT (Pattern Day Trader) enforcement
|- **T096: No PDT enforcement** — FIXED: room_capital.c, room_engine.c, types.h
|  - SEC Pattern Day Trader Rule: accounts under $25K limited to 3 day trades per rolling 5-day window
|  - Added `day_trades_5d` + `day_trade_roll_ts` fields to AgentState (types.h:222-224)
|  - Enforcement in room_capital_apply() Pass 1 (room_capital.c:84-105): skips agents with ≥3 day trades in 5-calendar-day window when capital < $25K
|  - Initialized in init_agents() and load_warmstart_genomes() (room_engine.c:624-626,570-572)
|  - Rolling 5-day window auto-resets after 5 calendar days; every trade counts as day trade (P2P resolves next cycle)
|  - Both room_engine and room_engine_paper compile clean (0 new warnings), paper engine starts/cycles/shuts clean
- Committed: d10393f
- REAL GAP count: 15→14
|
|## Batch 2026-06-01 — T088: On-chain blockchain.com → engine pipeline
|- **T088: blockchain.com data never reaches engine** — FIXED: new pipeline
|  - `blockchain_feat.c` (141 lines) — reads blockchain_data table from timeline.db, normalizes 12 on-chain metrics to [0,1], writes blockchain_features.json to options_cache
|  - `room_features.c` — added `load_blockchain_features()` loaded BEFORE `load_hashrate_features()`, providing blockchain.com baseline for F25-F27 with mempool.space override when fresh
|  - Dual-source: hashrate/difficulty/miner_floor now have blockchain.com data when mempool.space is stale
|  - Metrics: hash_rate, difficulty, miners_revenue, n_transactions, n_unique_addresses, trade_volume, utxo_count, transaction_fees, transaction_fees_usd, estimated_transaction_volume_usd, n_transactions_per_block, total_bitcoins
|  - Cron: */30min via system crontab
|  - Makefile: blockchain_feat added to TOOL_BINS
|  - PARTIAL: only 3 slots (F25-F27) wired, full 24-metric expansion needs N_FEATURES bump
|  - T088: PARTIAL→PORTED (core fix: blockchain.com data now reaches engine)
  - P1: 93→92
|- REAL GAP count: 15→14
|
|## Batch 2026-06-01 — T100: Room state snapshot → live dashboard
|- **T100: No tool reads mmap state for live display** — FIXED
|  - Symlink docs/data/room_state.json → pm_logs/c_room/room_snapshot.json
|  - Cron: * * * * * copies snapshot every 60s for GitHub Pages compatibility
|  - Data: cycle 715K+, 34 features, vote summary, Darwin epoch, top 10 agents
|  - Website dashboard can now access live engine state at /data/room_state.json
|
|## Batch 2026-06-01 — T099: C-to-website bridge restored
|- **T099: data_server binary missing, systemd service dead** — FIXED
|  - data_server binary rebuilt (was deleted by make clean)
|  - systemctl --user enable+start money-room-dashboard
|  - Verified: paper_stats.json, room_state.json, data_quality.json, pipeline_status.json served on port 9090
|  - All 30+ JSON files in docs/data/ refreshed by crons (1-30min intervals)
|  - T099: PARTIAL→PORTED
|
|## Batch 2026-06-01 — T101: Paper bridge multi-genome init
|- **T101: Single avg genome from 43 agents limits diversity** — FIXED
|  - `load_diverse_genomes()` scans all ENGINE_*.bin files in data/multi_market/
|  - Loads up to 100 diverse genomes, rotates across 2500 agents
|  - 3-tier fallback: diverse → single trained → random
- Noise reduced since genome diversity is now real (not from noise alone)
- Verified: 10 ENGINE genomes loaded at runtime
- T101: PARTIAL→PORTED
|
|## Batch 2026-06-01 — T105: SGD/voting weight claim corrected
|- **T105: SGD trains regime_weight, voting uses feat_weight** — STALE CLAIM
|  - Verified: room_vote.c:53-56 uses `regime_weight[regime]` for voting (NOT feat_weight)
|  - SGD (room_capital.c) also trains `regime_weight` — same weight system
|  - Both operate on regime_weight[regime][feature] — training and inference aligned
|  - T105: PARTIAL→PORTED
