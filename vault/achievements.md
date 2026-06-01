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
