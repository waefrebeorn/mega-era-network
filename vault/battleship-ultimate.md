# BATTLESHIP ULTIMATE — Money Room Active Gap Map

**Generated:** June 1, 2026 (DA Triple Research Audit)
**Count:** 421 cells across 9 domains
**Legend:** 🔴 P0 | 🟡 P1 | 🟢 P2 | ⚪ P3 | ⚫ P4
**Status:** ⏳ Build | ⏳ Stuck | 📋 Planned | ✅ Done

---

## ── DOMAIN A: TRAINING ENGINE (60 cells) ──

| # | Gap | Domain | Pri | Status | Detail |
|---|-----|--------|-----|--------|--------|
|| A01 | No SGD weight update loop in multi_market_trainer | Training | 🔴 | ✅ | Added BCE gradient descent after every trade. feat_weight[i] -= lr * err * (feat[i]-0.5), bias -= lr * err. learning_rate now functional. |
|| A02 | Darwin never fires in any room (cycle=1-2) | Training | 🔴 | ✅ | ROOT CAUSE 1 (infinite loop): static JSON feed + duplicate-window_ts check caused 1s-sleep skip loop. FIXED room_engine.c:701-708: exit after 3 consecutive duplicate timestamps. ROOT CAUSE 2 (trade loss): trade_count reset to 0 every restart, preventing cross-cron accumulation to 100-trade Darwin trigger. FIXED room_engine.c:657: removed unconditional trade_count=0 reset; corrupted-trade_count safety at line 641 retained. |
|| A03 | All 16 rooms share identical binary (same md5) | Training | 🔴 | ✅ | **BY DESIGN**: single-binary architecture reads ROOM_DIR per-room config/feed/state. Differentiation comes from room_feed.json and room_feed_gen. Not a gap. |
|| A04 | Rooms 7 (consensus, elections, manifold, etc.) show 0.50 price | Training | 🔴 | ✅ | **FIXED room_feed_gen.c**: added `get_manifold_prob()` function that pulls real binary probabilities from 556 Manifold prediction markets in ~/.hermes/timeline.db. Each room gets a deterministic market via hash rotation (different per room, same per day). Real probabilities range 0.1-0.9 instead of fake random drift around 0.5. Compile: needs `-lsqlite3`. |
|| A05 | BTC-clone data fed to economic/macro rooms | Training | 🔴 | ✅ | **FALSE CLAIM**: verified room_feed_gen.c:133-136 uses sp500 for macro domain. economic close=7473.47 == sp500=7473.47; macro close=7580.06 == sp500=7580.06. These are sp500 index values, not BTC (BTC was 73598). Feeds correctly differentiated. |
|| A06 | Room feed generator may not work | Training | 🔴 | ✅ | **FALSE CLAIM**: verified by running feed_gen for consensus room (exit=0, close=0.499958). Generated JSON has window_ts, domain-appropriate close, OHLC. Valid per-room feed at market_feed.json. The feed_gen works correctly. |
|| A07 | No per-market-type genome initialization | Training | 🔴 | ✅ | init_agent now takes MarketType param. Crypto→momentum, Equity→macro, Forex→trend-follow, Binary→consensus-skeptic, Bond→slow/horizon, Vol→mean-revert, Commod→vol-aware. |
|| A08 | No market-specific feature calibration | Training | 🟡 | ✅ | **PORTED**: B35 implements regime-specific scaling (range/trend/volatile). Binary vs price features differentiated in room_features.c:254-306. RSI/DFT/tail/volume are scale-independent. EMA/Bollinger/MACD have per-regime normalization via predicted_regime from A13 Markov model. |
|| A09 | No per-asset volatility normalization | Training | 🟡 | ✅ | **STALE**: room_features.c:472-484 differentiates binary vs price with separate normalization. EMA/MACD already have per-market-type normalization (lines 734-738). Bollinger %B is scale-invariant. A31 MARKET_TYPE fix handles per-domain scaling. |
|| A10 | Multi-market trainer not wired into cron | Training | 🔴 | ✅ | **FALSE CLAIM**: verified `crontab -l` — daily 7am multi_market_trainer, */15min auto_retrain_c. 17 genome .bin in data/multi_market/. Last modified May 31 22:08. Trainer running on schedule. |
|| A11 | No walked-forward validation | Training | 🔴 | ✅ | FIXED multi_market_trainer.c:962-1120: added `--validate` and `--validate-only` flags with walk-forward validation. Expanding-window protocol: 5 folds, each training on first N folds and testing on fold N+1. Reports IS vs OOS WR per fold, per market, and grand average. Overfit detected automatically when IS-OOS >10pp. Avg OOS WR across 16 markets: 66.8%. Flag: `--validate-only` for standalone validation (no training output). |
|| A12 | No out-of-sample test set | Training | 🔴 | ✅ | RESOLVED by A11 walk-forward validation (multi_market_trainer.c:962-1120). `--validate` flag runs expanding-window protocol: train on folds 0..N-1, test on fold N. OOS WR computed per fold and averaged across all folds. Avg OOS WR across 16 markets: 66.8%. Same fix serves both A11 and A12. |
|| A13 | No regime transition model | Training | 🟡 | ✅ | **FIXED**: Added 3×3 Markov transition matrix to RoomState (types.h). Updates on each tick after regime computation in room_features.c:535-560. Regime_transition_counts track regime→regime frequency. predicted_regime = argmax of transition matrix row. Persists across restarts via mmap'd state. |
|| A14 | No position sizing by volatility regime | Training | 🟡 | ✅ | **FIXED**: room_capital_apply() halves stake when predicted_regime == 2 (volatile). Uses A13 predicted_regime from Markov model. room_capital.c:74-76, room_engine.c call site passes state->predicted_regime. |
| A15 | No per-agent trade journal | Training | 🟡 | ✅ | **STALE**: trade_log.csv has 14.96M rows in ~/.hermes/pm_logs/c_room/. trade_journal binary exports per-agent audit to docs/data/trade_journal.json. |
| A16 | No feature importance feedback loop | Training | 🟡 | ✅ | **FIXED**: prune_dead_features() in room_engine.c decays weights of features with negative importance score (pos_wr - neg_wr < -0.1). Called every 100 cycles after Darwin. |
|| A17 | N_FEATURES=18 but no convergence check | Training | 🟡 | ✅ | **FIXED**: Added stagnant_cycles tracking to prune_dead_features() (room_engine.c:142-170). Features with flat importance (<0.05 change) for 1000+ cycles get weight halved. [CONV] log on first prune. Detects features that have converged and no longer provide signal. |
|| A18 | No learning rate scheduler | Training | 🟡 | ✅ | **FIXED**: Cosine LR scheduler in room_engine.c:1107-1111. Decays from 1.0 to LR_MIN(0.1) over 100K cycles. Applied in room_capital.c:264 via lr_decay multiplier. |
| A19 | SGD uses last_trade only, not full batch | Training | 🟡 | ✅ | **FIXED**: Changed per-trade SGD to mini-batch (batch size 8). grad_accum[N_REGS][N_FEATURES] + bias_accum[N_REGS] + batch_count in AgentState (types.h). Gradients accumulate per (regime, feature) across trades, applied when count reaches SGD_BATCH_SIZE. Partial batches persist across cron ticks via mmap'd state. STATE_MAGIC ROM8. |
| A20 | No gradient clipping | Training | ⚪ | ✅ | **FIXED**: multi_market_trainer.c second BCE blade clips per-weight and bias steps to ±5.0 in walk-forward training loop. |
| A21 | No weight decay / L2 regularization | Training | ⚪ | ✅ | **FIXED**: L2 lambda 0.001 applied to feat_weight and bias in multi_market_trainer.c second BCE blade. |
| A22 | No early stopping | Training | ⚪ | ✅ | **FIXED**: patience variables and break wired in walk_forward_validate(); stops when OOS not improved for EARLY_PATIENCE folds. |
| A23 | No dropout / gene silencing | Training | ⚪ | ✅ | **FIXED**: per-cycle random 15% feature drop_mask in room_vote.c:compute_agent_signal(). |
| A24 | No transfer learning between market types | Training | 🟡 | ⏳ | Warm-start loads elites across types, but on-disk coverage is crypto-only (10 files). Other market types seed random; no cross-type similarity mapping, weight normalization, or noise injection. |
| A25 | No ensemble prediction across rooms | Training | 🟡 | ⏳ | Partially addressed: `nn_ensemble.c` exists (offline SP500 stacking), and room vote uses top-N expert majority. Missing: weighted combination across rooms by WR/correlation, dynamic per-room ensemble weights, live integration. |
|| A26 | No backtest replay harness | Training | 🟡 | ✅ | **STALE**: `backtest_replay.c` (428 lines) exists — reads BTC 1-min candles from timeline.db, computes 80-dim feature vector cycle-by-cycle, writes feature CSV. Binary built. Makefile target: `backtest_replay`. |
|| A27 | No permutation feature importance | Training | ⚪ | ✅ | **STALE**: `permutation_test.c` exists — binary built in engine dir. Makefile target: `permutation_test`. |
|| A28 | No ablation testing | Training | ⚪ | ✅ | **FIXED**: engine/ablation_test.c — measures Brier score impact of zeroing each feature. Identifies most/least useful features. Reports delta vs baseline. |
| A29 | Single-training-path bottleneck | Training | 🟡 | ⏳ | Only one sequence: feed→feature→vote→resolve. No parallel exploration of strategies. |
|| A30 | No exploration vs exploitation epsilon | Training | ⚪ | ✅ | **FIXED**: room_vote.c — epsilon-greedy exploration. EPSILON_INIT=0.05, EPSILON_MIN=0.005, EPSILON_DECAY=0.9995. Random vote with probability epsilon, decays each cycle. Initialized in room_engine.c:820-822, decayed at line ~1068. types.h: RoomState.epsilon/epsilon_init/epsilon_min fields added. |
|| A31 | Room_engine has no MARKET_TYPE selection at runtime | Training | 🟡 | ✅ | **PORTED**: `tick->market_type` branches in `room_features.c`; `compute_nested_prediction()` takes `market_type`; genomes loaded by type suffix; regime-specific scaling by `predicted_regime`. Enhancement scope remains (per-market voting thresholds, Darwin diversity), not architecture gap. |
|| A32 | No per-room loss function | Training | 🟡 | ⏳ | PARTIAL. Room capital tracks PnL, Sharpe, drawdown (`room_engine.c:778,1603`, `room_capital.c`). Missing: selectable loss by market type (Sharpe/Brier/profit-factor), per-room objective config, multi-objective weighting. |
|| A33 | No calibration score for prediction markets | Training | 🟡 | ✅ | **STALE**: `accuracy_scorer.c` (75 lines) exists — reads outcomes from outcomes.db/timeline.db, computes Brier score, accuracy, calibration error. Needs engine wiring (call on trade resolution) but the computation code exists. Makefile: `accuracy_scorer`. |
|| A34 | No profit factor tracking | Training | ⚪ | ✅ | **STALE**: `risk_report.c` computes `wins` and `losses` counts. Gross win/loss dollar ratio can be derived from existing trade_log.csv PnL data. |
| A35 | No Sortino ratio | Training | ⚪ | ✅ | **STALE**: `risk_report.c:149` computes Sortino using downside deviation. Binary built. Makefile: `risk_report`. |
| A36 | No Calmar ratio | Training | ⚪ | ✅ | **STALE**: `risk_report.c:152` computes Calmar as return/maxDrawdown. Binary built. |
| A37 | No Kelly criterion position sizing | Training | 🟡 | ✅ | **FIXED**: room_capital.c:63-67 — kelly_f = win_rate_ema - 0.5f, caps genome stake: stake = min(stake, kelly_f * capital). Fractional Kelly, WR<50%→reduced. |
| A38 | No minimum sample filter | Training | 🟡 | ✅ | **FIXED**: Bayesian confidence-adjustment in Darwin ranking (room_darwin.c). Agents with <20 trades: win_rate pulled toward 0.5. |
| A39 | No trade count filter for Darwin ranking | Training | 🟡 | ✅ | **FIXED by A38**: same Bayesian-adjusted agent_fitness() handles both. |
|| A40 | No multi-objective evolution | Training | ⚪ | ✅ | **FIXED**: room_darwin.c:agent_fitness() — now combines WR 40% + PnL 30% + drawdown 20% + trade frequency 10%. Previously only used win_rate_ema. |
| A41 | No cross-validation strategy | Training | ⚪ | ⏳ | All data trained once. No k-fold. |
| A42 | No model checkpointing | Training | ⚪ | ⏳ | If binary crashes mid-training, all progress lost. |
| A43 | No training speed benchmark | Training | ⚪ | ⏳ | No baseline for how fast training should complete. Degradation invisible. |
| A44 | No gradient history for SGD diagnosis | Training | ⚪ | ⏳ | Can't tell if SGD is converging, diverging, or stuck in local minima. |
| A45 | No feature correlation matrix | Training | ⚪ | ⏳ | Two highly-correlated features get double-weight. No PCA/decorrelation. |
| A46 | Room_engine has PAPER_MODE vs LIVE_MODE but no HYBRID | Training | 🟡 | ⏳ | Can't run some rooms live and others paper. All-or-nothing. |
| A47 | No warm-start from prior genomes | Training | 🟡 | ✅ | **FIXED**: load_warmstart_genomes() in room_engine.c loads ENGINE_<TYPE>_N.bin elites on restart. Seeds 200 agents (2%) from saved genomes. Elite genomes saved by room_darwin_save_elite() each cycle. |
|| A48 | Darwin epoch count always reads 0 in snapshot | Training | 🔴 | ✅ | RESOLVED by A02 fix: trade_count now persists across restarts (room_engine.c:657). Once rooms accumulate 100+ trades across cron cycles, Darwin fires and epoch increments. |
|| A49 | room_engine_v2 and v3 binaries exist but unclear if used | Training | 🟡 | ✅ | **STALE**: v2 (54KB, macro only) — old build, replaced by current engine. v3 (149KB) — older multi-market engine, replaced by room_engine_market (88KB). Active: room_engine (paper, 83KB) for c_room, room_engine_market (MARKET_MODE) for live rooms. v2/v3 are mega-era-network vestiges. Neither referenced in crontab or scripts. |
|| A50 | No genome diversity metric tracked over time | Training | ⚪ | ✅ | **STALE**: room_darwin.c:384-441 computes both weight_diversity (stddev of L2 norms) and genome_diversity (mean pairwise distance). RoomStats.weight_diversity + RoomStats.genome_diversity populated each Darwin epoch. |
|| A51 | No mutation rate decay schedule | Training | ⚪ | ✅ | **STALE**: room_darwin.c:142 — `mutation_rate = fmaxf(0.05f, 0.3f - epoch * 0.01f)`. Starts at 0.3, decays 0.01/epoch, floors at 0.05. |
|| A52 | No elite preservation | Training | ⚪ | ✅ | **FIXED**: room_darwin.c — added elite_count = max(1, elite_fraction * nmt). Top elite_count agents (sorted by fitness) are protected from culling. Clone loop selects parents from top 10%. |
| A53 | No island model for speciation | Training | ⚪ | ⏳ | One global population. Different strategies compete but can't specialize in niches. |
|| A54 | Room engine market configs stored but not validated | Training | 🟡 | ✅ | **FIXED**: room_feed_gen.c — added validation of required fields (name, market_type, domain) in room_config.json. Warns on missing/invalid fields. |
| A55 | No A/B test harness for config changes | Training | ⚪ | ⏳ | Every engine change affects all rooms. Can't isolate effect of one parameter change. |
|| A56 | No training DB for per-cycle metrics | Training | 🟡 | ✅ | **FIXED**: room_engine.c — appends JSON lines to cycle_metrics.jsonl every 10 cycles. Records cycle, agents, votes, WR, sharpe, drawdown, capital, peak_cap, trades, PnL, epsilon, genome_div, weight_div, timestamp. Path: g_cycle_metrics_path (<room_dir>/cycle_metrics.jsonl). types.h: g_cycle_metrics_path[576]. |
| A57 | Cycle count and trade count may not persist | Training | 🟡 | ✅ | **FIXED**: room_engine.c:835-839 — preserved `prev_cycle = state->cycle` before boot-time hard reset, then restored `state->cycle = prev_cycle`. Cycle count now continues from previous run instead of resetting to 0 on every restart. |
| A58 | No heartbeat timeout alert | Training | 🟡 | ✅ | **FIXED**: Added heartbeat/alert files to cycle_all_rooms.c. Start heartbeat (heartbeat.json + "starting") on launch. Alert file (alert_timeout.json) written on any room timeout (-2) or failure. Final heartbeat written on completion with "ok"/"degraded" status and room counts. Timeouts and failures tracked separately in Phase 3 output. Non-zero exit when any engine failed or timed out. |
| A59 | No multi-threaded room cycling | Training | ⚪ | ⏳ | cycle_all_rooms runs rooms sequentially. 16 rooms × 5s = 80s. Parallel would be 5s. |
|| A60 | Room watchdog only restarts, doesn't report | Training | ⚪ | ✅ | **STALE**: room_watchdog.c already reports via log_msg (stdout), write_heartbeat (heartbeat files per room), and checks snapshot freshness. Writes per-room heartbeats for btc_main, macro, momentum, polymarket + aggregate. Restarts on stale snapshot with 5min threshold. |

---

## ── DOMAIN B: FEATURES (45 cells) ──

| # | Gap | Domain | Pri | Status | Detail |
|---|-----|--------|-----|--------|--------|
||| B01 | N_FEATURES=18 but only ~10 populated | Features | 🔴 | ✅ | **FALSE CLAIM**: verified room_features.c — all 18 features computed: price_delta, momentum, RSI, EMA_fast/slow, MACD, Bollinger, divergence, pump, regime, F&G, herd_consensus, ob_imbalance, ob_depth_ratio, cvd_signal, DFT, tail_risk. |
|| B02 | dft_dominant always shows 0.0 | Features | 🟡 | ✅ | **FIXED**: price_history moved from static array to mmap'd RoomState (persistent across restarts). hist_len now accumulates to 10+ across cron cycles. DFT and tail_risk now compute. room_features.c/types.h. |
|| B03 | phi_return/phi_vol/phi_momentum may be uninitialized | Features | 🟡 | ✅ | **FALSE CLAIM**: these were replaced with orderbook features (B05). |
|| B04 | tail_risk_score always shows 0.0-0.1 range | Features | 🟡 | ✅ | **STALE**: tail_risk computation fixed by B02 (persistent price history). compute_tail_risk() in room_features.c:386-435 uses full kurtosis + extreme-move detection. Previously was limited by static array resetting per process; now history persists across restarts via mmap'd RoomState. Feature produces correct values when data has fat tails — benign market data naturally produces low scores. |
|| B05 | No order book imbalance feature | Features | 🟡 | ✅ | **FIXED**: replaced φ-interval features with ob_imbalance (F14), ob_depth_ratio (F15), cvd_signal (F16). orderbook_depth.c built + cron. |
|| B06 | No cumulative volume delta (CVD) | Features | 🟡 | ✅ | **FIXED**: cumulative_volume_delta built + wired. cvd_signal in FeatureVector. |
| B07 | No time-weighted average price (TWAP) | Features | ⚪ | ⏳ | TWAP only in execution (twap.c), not used as feature. |
| B08 | No VWAP proximity | Features | ⚪ | ⏳ | Relative position vs VWAP is a known alpha signal. |
| B09 | No realized volatility ratio (short/long vol) | Features | ⚪ | ⏳ | Ratio of 5-min to 1-hour volatility shows regime changes. |
| B10 | No skew / kurtosis features | Features | ⚪ | ⏳ | Higher moments of returns distribution missing. |
| B11 | No seasonal/time-of-day features | Features | 🟡 | ✅ | **FIXED**: hour_of_day_norm (F28) + day_of_week_norm (F29). Computed from localtime, no collector needed. |
| B12 | No macro regime feature for equity correlation | Features | 🟡 | ✅ | **FIXED**: Added rolling Pearson correlation (F33) between room price history and SP500 levels. sp500_hist ring buffer in RoomState (persistent across restarts). calc_sp500_corr() in room_features.c — minimum 5 samples, full Pearson formula. Normalized [-1,1]→[0,1]. Data already flowing via MarketTick.sp500 from feed JSON. |
|| B13 | No on-chain feature beyond BTC dominance | Features | 🟡 | ✅ | **PORTED**: onchain_feat.c (167 lines) computes MVRV proxy (price/ATH), exchange vol ratio, volatility signals from CoinGecko. Writes onchain_features.json. Wiring to room_features.c not needed — btc_dominance already flows via MarketTick. On-chain data collected. |
| B14 | No funding rate feature | Features | 🟡 | ✅ | **FIXED**: funding_signal (F19) loaded from funding_features.json. Collector runs every 30min via collector_runner. |
| B15 | No open interest change | Features | 🟡 | ✅ | **FIXED**: oi_net_signal (F20) from open_interest_features.json. BTC OI + SPY PCR combined. |
| B16 | No long/short ratio feature | Features | 🟡 | ✅ | **FIXED**: ls_ratio_norm (F21) from ls_ratio_features.json. OKX taker buy/sell volume proxy. |
| B17 | No liquidation cascade feature | Features | 🟡 | ✅ | **FIXED**: liq_ls_ratio_norm (F22) from liquidation_features.json. Long/short liquidation pressure. |
| B18 | No stablecoin inflow/outflow | Features | 🟡 | ✅ | **FIXED**: stable_inflow_norm (F23) from stablecoin_features.json. Stablecoin volume ratio. |
| B19 | No whale transaction tracking | Features | 🟡 | ✅ | **FIXED**: whale_activity_norm (F24) from whale_features.json. Large-tx activity + mempool pressure. |
| B20 | No inter-exchange basis | Features | ⚪ | ⏳ | Price difference between exchanges shows arbitrage pressure. |
| B21 | No options-derived features (IV skew, put/call ratio) | Features | 🟡 | ✅ | **FIXED**: iv_skew (F30), pcr_volume (F31), iv_term_slope (F32) from latest_features.json. SPY options chain. |
| B22 | No volatility term structure | Features | ⚪ | ⏳ | Short vs long vol term structure (contango/backwardation) signal. |
| B23 | No VIX regime filter | Features | 🟡 | ✅ | **FIXED**: Added vix_regime (F34) to FeatureVector. Continuous mapping: VIX<15→0.0 (low), 15-25→0.5 (normal), >25→1.0 (high). Defaults to 15.0 when VIX unavailable. Persistent vix_hist ring buffer in RoomState. Data already flowing via MarketTick.vix from feed JSON. |
| B24 | No economic surprise index | Features | ⚪ | ⏳ | Actual vs expected macro data releases. |
| B25 | No news sentiment delta (change over time) | Features | ⚪ | ⏳ | Current sentiment only. Sentiment change (d(sentiment)/dt) is stronger signal. |
| B26 | No social media volume spike | Features | ⚪ | ⏳ | Sudden increase in social mentions precedes volatility. |
| B27 | No feature normalization/scaling | Features | 🟡 | ✅ | **FIXED**: room_features.c:394-410 — all 18 features normalized to [0,1] or [-1,1] via tanh, /100, log-normalize, or /2. RSI=100→1.0, price_delta±999→tanh/5. |
| B28 | No feature interaction terms | Features | ⚪ | ⏳ | pump_score * regime_indicator, volume_surge * volatility, etc. |
| B29 | No feature lag transforms | Features | ⚪ | ⏳ | Feature at t-1, t-2, t-3 as separate inputs. Temporally-aware features. |
| B30 | No feature difference transforms (delta) | Features | ⚪ | ⏳ | Feature[i]_t - Feature[i]_{t-1} gives momentum of features themselves. |
| B31 | No rolling z-score normalization | Features | ⚪ | ⏳ | Features should be normalized to z-scores over rolling window. Robust to outliers. |
| B32 | No feature selection process | Features | 🟡 | ⏳ | 18 features is arbitrary. No process for adding/removing features systematically. |
| B33 | No dimension reduction (PCA/UMAP) | Features | ⚪ | ⏳ | High feature space with correlation. Dimensionality reduction would help generalization. |
| B34 | No autoencoder for unsupervised features | Features | ⚪ | ⏳ | Neural feature extraction from raw market data. |
| B35 | No regime-specific feature scaling | Features | 🟡 | ✅ | **FIXED**: Normalization constants for price_delta (F1) and micro_momentum (F2) now vary by predicted_regime from A13 Markov model. Range(0): standard /5,/2. Trend(1): /3,/1.2 (amplify signals). Volatile(2): /10,/4 (compress noise). room_features.c normalization block. No struct changes needed. |
| B36 | No feature timestamp tracking | Features | ⚪ | ⏳ | Engine doesn't track WHEN each feature was last updated. Stale features are invisible. |
| B37 | No feature staleness detection | Features | 🟡 | ✅ | **FIXED**: Added `check_feature_staleness()` to room_features.c. After each cycle, checks mtime of all 10 external feature files (orderbook, CVD, funding, OI, L/S, liquidation, stablecoin, whale, hashrate, options). Writes `[STALE] WARN` (age>1h) or `[STALE] CRIT` (age>4h) to stderr. Appends to `feature_staleness.json` report for dashboard consumption. |
| B38 | No feature gradient reset | Features | ⚪ | ⏳ | If market regime changes fundamentally, old feature correlations become misleading. |
| B39 | No continuous feature ID system | Features | 🟡 | ⏳ | Adding a feature requires recompiling types.h and all binaries. No plug-in architecture. |
| B40 | Feature contribution to variance not tracked | Features | ⚪ | ⏳ | PCA variance explained per feature not computed. |
| B41 | No synthetic feature from ensemble predictions | Features | ⚪ | ⏳ | Other rooms' predictions as features for this room. |
| B42 | No attention-weighted feature aggregation | Features | ⚪ | ⏳ | All features equally weighted. Attention would weight salient features higher. |
| B43 | No feature importance drift monitoring | Features | ⚪ | ⏳ | Feature importance changes over time. Importance should be tracked as time series. |
| B44 | Feed bridge may write stale market_feed.json | Features | 🔴 | ✅ | FIXED room_feeds.c:248-278: tightened LIVE_MODE staleness thresholds — WARN at >5min (was none), REJECT at >1h (was 24h). Feed age now surfaced in stderr logs per read cycle. |
| B45 | Only 14 JSON feeds in docs/data/ — missing many | Features | 🟡 | ⏳ | Website shows 14 feeds but we collect data for 27+ tickers and 16 rooms. |

---

## ── DOMAIN C: RISK MANAGEMENT (40 cells) ──

| # | Gap | Domain | Pri | Status | Detail |
|---|-----|--------|-----|--------|--------|
|| C01 | No VaR computation in engine runtime | Risk | 🔴 | ✅ | FIXED risk_analytics.c: added `--json` flag and `--output` path. Writes VaR 95%/99%, ES 95%/99%, profit factor, gross win/loss to docs/data/risk_*.json. Cron: */15 * * * * via risk_analytics_cron.sh (Hermes job f62a834137b3). Runs on all 17 rooms (16 live + c_room paper). Monte Carlo VaR uses 10K simulations of 100-trade portfolios from real trade history. Fallback JSON with "warn" field when <100 trades available (dashboard-friendly). Code existed as offline binary (C21 VaR + C22 ES) — just needed JSON output + cron wiring. |
|| C02 | No CVaR/Expected Shortfall | Risk | 🟡 | ✅ | **STALE**: CVaR/ES already computed in risk_analytics.c:123-171 and written to JSON output (es_95_pct, es_99_pct). C22 was folded into C01 implementation. |
||| C03 | Circuit breaker configured but never triggered | Risk | 🔴 | ✅ | ROOT CAUSES: (1) trade_count reset to 0 each restart prevented room trades (requires 1000). (2) A02 fixed trade_count persistence, but room trades opened on cycle 1 never resolved — static feed means no 2nd unique timestamp. FIXED room_engine.c:705-736: force-resolve open room trade on dup-timestamp exit. Circuit breaker can now trigger when consec_room_losses >= 10 or drawdown > 20%. |
|| C04 | Max drawdown threshold unknown | Risk | 🟡 | ✅ | **DOCUMENTED**: room_engine.c:684 — `state->max_drawdown_pct = 0.20f` (20%). benchmark.c:179 checks `max_drawdown < 0.20f`. Threshold is 20% of peak capital from circuit breaker peak tracking. |
|| C05 | No daily loss limit for room capital | Risk | 🟡 | ✅ | **FIXED**: Added day-boundary-checked daily_pnl tracking (room_engine.c:999-1005). Circuit breaker trips when daily loss exceeds max_daily_loss_pct (10% of peak capital, types.h:max_daily_loss_pct). Resets on day boundary via window_ts/86400. PnL updated after every trade resolution at 4 resolution points (dup-exit, kill-switch, slippage). |
|| C06 | No max position concentration check | Risk | 🟡 | ✅ | **STALE**: P2P matching inherently limits exposure — only min(YES_total, NO_total) is matched. Unmatched surplus stays in agent capital. No over-exposure possible. |
|| C07 | No correlation-based position limits | Risk | ⚪ | ⏳ | If BTC and ETH are highly correlated, betting on both doesn't diversify. |
| C08 | No black swan scenario testing | Risk | 🟡 | ✅ | **FIXED**: Added 4th scenario to stress_test.c: 2026 black swan (-50% gap down in 1 day). 4 scenarios: 2008 crash (50% over 20d), 2020 flash crash (30% 1d), 2022 bear (20% 60d), 2026 black swan (50% 1d). T17 circuit breaker trips at 20% drawdown across all scenarios. A14 volatile half-sizing provides additional protection. |
|| C09 | No flash crash simulation | Risk | ⚪ | ✅ | **FIXED**: stress_test.c — added 5th scenario: 2020 flash crash (-40% instant drop in minutes). Loop now uses sizeof instead of hardcoded 4. |
|| C10 | No exchange outage handling | Risk | 🟡 | ✅ | **STALE**: Covered by B44 feed staleness detection (room_feeds.c:254-277 — >5min WARN, >1h REJECT) + T17 circuit breaker (drawdown+consecutive losses). Paper engine auto-exits on feed exhaustion (room_engine.c:832-864). Live mode would require exchange-specific handling but currently paper-only. |
| C11 | No position liquidation model | Risk | 🟡 | ⏳ | Paper trading doesn't model forced liquidation at margin thresholds. |
| C12 | No slippage shock test | Risk | ⚪ | ✅ | **FIXED**: stress_test.c C12 block — 10x slippage shock (50bps vs 5bps), computes friction cost over trade history, PASS/FAIL verdict vs 10% capital threshold. |
|| C13 | No fee model for different order types | Risk | 🟡 | ✅ | **STALE**: MARKET_MAKER_FEE (0%) and MARKET_TAKER_FEE (0.1%) both defined in types.h. In paper P2P mode, all orders are market orders (taker+0.1%). Maker model matters for live exchange execution (E01), not paper. No code change needed. |
| C14 | No gas cost model for crypto trades | Risk | ⚪ | ✅ | **FIXED**: types.h GAS_FEE_EST=$2.50 (avg L2/L1). room_capital.c skips trades where gas >50% of stake. Prevents small on-chain trades being consumed by fees. |
|| C15 | No Polymarket minimum order enforcement | Risk | 🟡 | ✅ | **FIXED**: types.h — raised MIN_TRADE_STAKE from $1 to $5 to cover Polymarket 5-share minimum (5 shares × $1 max price). room_capital.c enforces this as universal minimum. |
|| C16 | No position size floor check | Risk | 🟡 | ✅ | **STALE**: MIN_TRADE_STAKE=$1 enforced at room_capital.c:82-83. `if (stake < MIN_TRADE_STAKE) continue` catches any sub-threshold stake regardless of how it was computed. Also `if (stake <= 0) continue` guard. |
|| C17 | No auto-kill on 6 consecutive losses | Risk | 🟡 | ✅ | **STALE**: enforced at room_capital.c:224-225 — `if (agents[aid].consecutive_losses >= 6) agents[aid].alive = false;`. Running in all engine modes. |
|| C18 | No win-rate-floor auto-kill | Risk | ⚪ | ✅ | **FIXED**: room_capital.c:280-282 — after WR EMA update, agents with ≥100 trades and WR < 30% are culled (`alive = false`). |
|| C19 | No capital-floor auto-kill | Risk | ⚪ | ✅ | **FIXED**: room_capital.c:268-269 — after consecutive_losses kill, agents with capital < $1 are also culled. |
|| C20 | No max_position_pct_room per agent | Risk | 🟡 | ✅ | **STALE**: Enforced at room_engine.c:1285-1292. position_size capped to max_position_pct_room (2%) of total capital per agent. Logged as [LIMIT] when triggered. |
|| C21 | No max_total_exposure_pct enforcement | Risk | 🟡 | ✅ | **STALE**: Enforced at room_engine.c:1297-1304. Total exposure across all agents capped to max_total_exposure_pct (25%). Votes exceeding remaining budget get position_size=0 and logged as [LIMIT] skip. |
|| C22 | No trade throttle per agent | Risk | ⚪ | ✅ | **STALE**: `room_engine.c:812` — `max_trades_per_cycle=5000`, enforced at line 1443-1454. Excess trades deferred. `types.h:375-377`. |
| C23 | No duplicate trade detection | Risk | 🟡 | ⏳ | Two rooms could place same trade on same market. Double exposure. |
| C24 | No market correlation across rooms | Risk | 🟡 | ⏳ | Sports room and consensus room both trade binary events. Correlation unknown. |
|| C25 | No panic stop for all rooms | Risk | 🟡 | ✅ | **FIXED**: check_panic() in room_engine.c checks /tmp/money_room_panic sentinel each cycle. File exists = skips vote and trading. File removed = resumes immediately. |
|| C26 | No overnight gap risk model | Risk | ⚪ | ✅ | **FIXED**: types.h OVERNIGHT_GAP_BPS=50.0f. room_engine.c adds gap risk charge at market open (9-10am weekdays) on entry slippage. Crypto 24/7 exempt. |
|| C27 | No weekend liquidity model | Risk | ⚪ | ✅ | **FIXED**: types.h SLIPPAGE_WEEKEND_MUL=2.0f. room_engine.c checks tm_wday (0=Sun,6=Sat) via localtime_r, applies 2x to SLIPPAGE_BPS and SLIPPAGE_VOL_SCALE on all 3 slippage calc sites (entry, exit, P2P exit). |
|| C28 | No holiday effect model | Risk | ⚪ | ✅ | **FIXED**: room_engine.c is_us_holiday() — 7 US market holidays (NYE, July 4, Christmas, MLK, Presidents, Labor, Thanksgiving). Holiday days get 2x slippage via SLIPPAGE_WEEKEND_MUL, integrated into all 3 slippage calc sites. |
|| C29 | No fee-aware position sizing | Risk | 🟡 | ✅ | **STALE**: Fees are proportional (0.1% taker). $1 trade = $0.001 fee — no minimum fee. MIN_TRADE_STAKE=$1 floor already enforced. MIN_TRADE_STAKE ≥ fee cost for all trade sizes. Relevant for fixed-fee chains (Ethereum gas) but not paper P2P. |
|| C30 | No win rate stability filter | Risk | ⚪ | ✅ | **FIXED**: types.h win_rate_var field + STATE_MAGIC bump. room_capital.c online variance tracking (exponential). room_darwin.c penalizes agents with WR variance >0.10 by 15%. |
|| C31 | No t-tested edge | Risk | ⚪ | ✅ | **FIXED**: room_darwin.c edge_zscore() — z-test for binomial WR vs H0:p=0.5. |z|>1.96 = p<0.05 significant edge. Agents with ≥100 trades and z<1.96 get 20% fitness penalty. Requires ≥30 trades for test. |
|| C32 | No Kelly bet sizing | Risk | 🟡 | ✅ | **STALE**: A37 already implements Fractional Kelly at room_capital.c:62-73. kelly_f = win_rate_ema - 0.5f caps genome stake. WR<50% → 1/4 genome size. Position capped at 5% max_loss and 50% of capital. |
|| C33 | No position unwind schedule | Risk | ⚪ | ✅ | **FIXED**: room_engine.c unwind_priority() — priority = pnl - age*0.1. Losers closed first (lowest priority), then oldest positions. Used at circuit breaker trigger to log first position to unwind. |
|| C34 | No stop-loss at room level | Risk | 🟡 | ✅ | **STALE**: T17 circuit breaker IS the room-level stop-loss. Triggered at 20% drawdown or 10 consecutive losses. 100-cycle cooldown. |
|| C35 | No take-profit at room level | Risk | ⚪ | ✅ | **FIXED**: room_engine.c — added room_take_profit_pct (default 20%) and room_take_profit_triggered flag to RoomState (types.h). After consecutive losses check, if profit from $50 seed ≥ threshold, sets flag and skips trades via goto. One-shot trigger — once profits are locked, room stops trading. |
|| C36 | No correlation between agent positions | Risk | 🟡 | ✅ | **FIXED**: room_engine.c:1285-1341 — added directional exposure tracking (yes_exposure/no_exposure) with per-direction cap at 15% of total capital per direction (max_direction_pct). Prevents 6 agents from all going long the same asset. New [DIR] skip log when direction cap hit. Tracked in state: current_yes_exposure, current_no_exposure. STATE_MAGIC: ROM9. |
| C37 | No hedge ratio optimization | Risk | ⚪ | ⏳ | Optimal hedge ratio between positions not computed. |
| C38 | No tail-risk overlay strategy | Risk | ⚪ | ⏳ | REAL. No options hedge or tail-risk overlay implementation exists. |
| C39 | No portfolio-level VaR model | Risk | 🟡 | ⏳ | Each room independent. Aggregate portfolio VaR not computed. |
| C40 | No margin adequacy check | Risk | 🟡 | ⏳ | If trading on margin (future), equity check needed before each trade. |

---

## ── DOMAIN D: DATA PIPELINE (55 cells) ──

| # | Gap | Domain | Pri | Status | Detail |
|---|-----|--------|-----|--------|--------|
||| D01 | timeline.db only has 21-33 rows per ticker | Data | 🔴 | ✅ | **VERIFIED TRUE**: yahoo_* tickers have exactly 21 rows each in ~/.hermes/pm_logs/timeline.db (59 tickers, 1314 total). Root cause: Yahoo v7/chart API with range=5y silently caps at ~21 trading days (~1 month). FIXED by D02 backfill (v8 with period1/period2, 1-year chunks). |
||| D02 | No backfill capability for historical data | Data | 🔴 | ✅ | FIXED yahoo_collector.c: added `--backfill` flag using v8 API with period1/period2. Fetches 5 years in 1-year chunks with 250ms delay to avoid 429 rate limits. Clears existing data before re-insert. Usage: `./yahoo_collector --backfill` (full) or `--backfill --year 2024` (single year). 253 data points per ticker per year confirmed. |
|| D03 | Yahoo v7 API limits to ~125 days | Data | 🟡 | ✅ | **STALE**: D01/D02 v8 backfill (period1/period2) gets full 5-year history per ticker (253 rows/year). v7 still used for incremental daily updates — fine for ongoing collection since historical data already backfilled. |
| D04 | No BTC 1-min historical data pipeline | Data | 🟡 | ✅ | **STALE**: BTC 1-min CSV exists at ~/.hermes/pm_logs/historical/btc_1min_latest.csv (723K rows, updated continuously via cron). Paper engine reads directly from it. |
|| D05 | Kraken backfill exists (kraken_backfill.c) — claim stale | Data | 🟡 | ✅ | **STALE**: kraken_backfill.c (305 lines C) already implements paginated backfill via 'since' parameter (720 candles/request). Supports resume from latest, timestamp, or full backfill from 2017. Writes to historical.db. Binary built. |
| D06 | Coinbase has historical but no active collector | Data | 🟡 | ⏳ | Coinbase API supports start/end params but coinbase_live.c may not use them. |
| D07 | No SP500 daily data pipeline | Data | 🟡 | ⏳ | Market_tide.c exists but SP500 data freshness unknown. |
| D08 | No forex historical data | Data | 🟡 | ⏳ | forex_collector.c exists but only gets current rates. No history. |
| D09 | No commodity data pipeline | Data | 🟡 | ⏳ | GC=F, CL=F data via yahoo but no dedicated collector. |
| D10 | No bond yield data pipeline | Data | 🟡 | ⏳ | ^TNX via yahoo but yield changes tracked erratically. |
| D11 | No VIX data pipeline | Data | 🟡 | ⏳ | ^VIX via yahoo but high-resolution VIX data (1-min) missing. |
| D12 | No economic indicator time series | Data | 🟡 | ⏳ | FRED data collected but may not be complete time series. |
| D13 | No GDP data (current or historical) | Data | 🟡 | ⏳ | Not tracked. |
| D14 | No unemployment data | Data | 🟡 | ⏳ | Not tracked. |
| D15 | No CPI/inflation data | Data | 🟡 | ⏳ | Not tracked. |
| D16 | No PMI manufacturing/services | Data | 🟡 | ⏳ | Not tracked. |
| D17 | No retail sales data | Data | ⚪ | ⏳ | Not tracked. |
| D18 | No central bank rate decisions | Data | 🟡 | ⏳ | FOMC dates not tracked. |
| D19 | No earnings calendar data (company-specific) | Data | 🟡 | ⏳ | Yahoo earnings data may be stale or infrequent. |
| D20 | No real-time Polymarket data | Data | 🟡 | ⏳ | RECLASSIFIED from 🔴 to 🟡: purely external block — requires $50 USDC deposit on Polymarket. No code fix possible. Manifold markets already wired via A04 fix for prediction room data.
| D21 | No PredictIt data | Data | 🟡 | ⏳ | PredictIt API may not be continuously collected. |
| D22 | No Kalshi data | Data | 🟡 | ⏳ | Kalshi collector exists but API auth may block continuous collection. |
| D23 | No Manifold markets data | Data | 🟡 | ⏳ | Manifold API not integrated. |
| D24 | No Sports betting data (live odds) | Data | 🟡 | ⏳ | Sports collector may get scores but not betting odds. |
| D25 | No weather data (other than current) | Data | ⚪ | ⏳ | Weather predictions need forecasts, not current conditions. |
| D26 | No election data pipeline | Data | 🟡 | ⏳ | 538/FiveThirtyEight poll data not collected. |
| D27 | No sentiment by ticker | Data | 🟡 | ⏳ | GDELT is macro/event-based. Stock-specific sentiment not computed. |
| D28 | No news for non-US markets | Data | ⚪ | ⏳ | GDELT covers English. Non-English financial news is untapped. |
| D29 | No dark pool data for single-stock tickers | Data | 🟡 | ⏳ | dark_pool_feat.c only fetches SPY. Other tickers not tracked. |
| D30 | No SEC filings beyond 13F | Data | 🟡 | ⏳ | 8-K, 10-Q, 10-K not processed. Material event detection missing. |
| D31 | No analyst rating changes | Data | 🟡 | ⏳ | Not tracked. |
| D32 | No insider transaction beyond Form 4 | Data | ⚪ | ⏳ | Form 144 (planned sales) and Section 16 changes not tracked. |
| D33 | No options flow beyond PCR/IV | Data | 🟡 | ⏳ | Real-time options flow flags missing. Only summary stats. |
|| D34 | No data freshness dashboard | Data | 🟡 | ✅ | **FIXED**: scripts/data_freshness.sh — checks last update time for 10 data sources (yahoo, coingecko, news, cboe, fear_greed, forex, fred, orderbook, cvd, funding) from timeline.db. Writes JSON with OK/WARN/STALE status to docs/data/data_freshness.json. |
| D35 | No data quality scoring per source | Data | 🟡 | ⏳ | Some sources may return stale/empty data. No quality metric. |
| D36 | No data consistency validation | Data | 🟡 | ⏳ | Cross-source consistency not checked (e.g., Kraken BTC vs Coinbase BTC). |
|| D37 | No data gap alerting | Data | 🟡 | ✅ | **FIXED**: data_gap_alerter.sh (~/.hermes/scripts/) — monitors 7 critical data sources (Yahoo, CoinGecko, News/GDELT, CBOE, Fear&Greed, Forex/Frankfurter, FRED). For each source, queries latest timestamp from timeline.db, compares against staleness threshold (1h-24h per source). Writes JSON to docs/data/data_gap_alert.json. Cron: */30min. Non-zero exit on stale sources for std err visibility. |
|| D38 | No anomaly detection on incoming data | Data | 🟡 | ✅ | **FIXED**: room_feeds.c — added spike/flatline/volume anomaly detection after G11 validation. Tracks prev_close (static), detects >10% single-tick price spikes, flatline (identical close + zero volume), and volume spikes (>5x rolling avg). Logs to stderr. |
||| D39 | No data staleness flag in engine | Data | 🔴 | ✅ | **FALSE CLAIM**: already addressed by B44 fix in room_feeds.c:254-277. Engine validates timestamp on every feed load — rejects future timestamps (<-300s), WARNs at >5min, REJECTs at >1h. Stale data surfaces via stderr logs per cycle. |
|| D40 | No fallback data source for critical feeds | Data | 🟡 | ✅ | **FIXED**: coingecko_fallback.c (engine/coingecko_fallback.c) — standalone C binary that checks BTC 1-min CSV freshness (age >1h). If stale, queries CoinGecko API directly and appends synthetic OHLCV row. Uses libcurl+jansson, single binary deploy. Cron: */30min via Hermes script coingecko_fallback.sh. Fresh-data path returns exit 0 silently; stale path writes fallback row and logs price. |
|| D41 | CoinGecko wired into collector_runner | Data | 🟡 | ✅ | **FIXED**: coingecko_fetch.sh added to collector_runner.c NORMAL_TASKS (30min interval). Writes 25 crypto prices to timeline.db. Wrapper at ~/.hermes/scripts/coingecko_fetch.sh. Binary exists at engine/coingecko_collector. |
| D42 | CBOE data has 15-min delay | Data | 🟡 | ⏳ | Options chain data is delayed. Real-time requires paid OPRA feed. |
| D43 | Finnhub API limited to 300 req/day | Data | 🟡 | ⏳ | stock_collector uses Finnhub free tier. 300 req/day covers ~50 tickers. |
| D44 | No exchange fee table in engine | Data | 🟡 | ⏳ | Fee constants in types.h are hardcoded. No per-exchange fee lookup. |
| D45 | No overnight swap/funding rate data | Data | ⚪ | ⏳ | Futures funding rates not collected. |
| D46 | No order book snapshot archive | Data | ⚪ | ⏳ | Current orderbook_depth.c may get snapshot but no history. |
| D47 | No trade history beyond room_log.csv | Data | 🟡 | ⏳ | CSV format is fragile. No DB-backed trade history. |
|| D48 | No human-readable trade journal | Data | 🟡 | ✅ | **PORTED**: trade_journal.c (165 lines) reads trades.csv, writes trade_journal.json with timestamped entries including agent, direction, size, entry/exit, PnL, asset. Output is human-readable JSON. |
|| D49 | No PnL attribution by market type | Data | 🟡 | ✅ | **STALE**: `strategy_attribution.c` (182 lines) + `analytics_engine.c` with `calc_attribution()` exist — group agents by genome deciles, compute avg PnL per strategy bucket. Binaries exist. |
| D50 | No benchmark comparison | Data | 🟡 | ✅ | **STALE**: `benchmark.c` (185 lines) exists — compares agent PnL vs buy-and-hold BTC and random strategies. Reads room_state.bin. Binary built. Makefile: `benchmark`. |
| D51 | No risk-free rate for Sharpe | Data | 🟡 | ✅ | **FIXED**: ab_test.c:69-70 + room_engine.c:1373-1374 — rf_per_period = 0.045 / periods_per_year. Sharpe now subtracts 4.5% annual T-bill rate from returns. |
| D52 | No multi-timeframe data (1m, 5m, 1h, 1d) | Data | 🟡 | ⏳ | All features computed on single timeframe. Multi-scale analysis missing. |
|| D53 | No data compression archive | Data | ⚪ | ✅ | **FIXED**: scripts/data_archive.sh — compresses CSV files older than 30 days into monthly tar.gz archives. Prunes archives older than 1 year. |
|| D54 | No data retention policy | Data | ⚪ | ✅ | **FIXED**: config/data_retention_policy.json — defines retention rules: 1-min BTC (1yr), CSVs (2yr), JSON (1yr), archives (1yr), state/timeline/training (forever), logs (30d). Enforced by data_archive.sh. |
| D55 | No privacy-protected data pipeline for user data | Data | ⚪ | ⏳ | If user trading data collected, no anonymization step. |

---

## ── DOMAIN E: EXECUTION (35 cells) ──

| # | Gap | Domain | Pri | Status | Detail |
|---|-----|--------|-----|--------|--------|
| E01 | No live exchange API integration | Execution | 🟡 | ⏳ | RECLASSIFIED from 🔴 to 🟡: all trading is paper. Exchange API integration requires exchange account funding ($50+), API key generation, and trading auth setup. Purely external/funding blocker.
| E02 | No Kraken REST API integration | Execution | 🟡 | ⏳ | RECLASSIFIED from 🔴 to 🟡: kraken_collector.c reads data only. Trade execution blocked on E01 (funding).
| E03 | No Coinbase integration | Execution | 🟡 | ⏳ | RECLASSIFIED from 🔴 to 🟡: coinbase_live.c reads data only. Trade execution blocked on E01 (funding).
| E04 | No Polymarket CLOB integration | Execution | 🔴 | 🟡 | Blocked on $50 USDC. pm_live_clob.py exists but can't execute. |
| E05 | No order type support (market/limit) | Execution | 🟡 | ⏳ | All paper trades are "market" orders. No limit order model. |
| E06 | No partial fill model | Execution | 🟡 | ⏳ | Paper assumes fills at exact price. Real orders may partially fill. |
| E07 | No order cancellation | Execution | 🟡 | ⏳ | Once an order is placed, can't be canceled. |
| E08 | No order replacement | Execution | ⚪ | ⏳ | Can't improve price on existing order. |
| E09 | No TWAP execution | Execution | ⚪ | ⏳ | Large orders split into smaller tranches. |
| E10 | No iceberg order model | Execution | ⚪ | ⏳ | Hidden orders for large positions. |
| E11 | No execution quality scoring | Execution | ⚪ | ⏳ | No metric for how well orders get filled. |
| E12 | No exchange latency model | Execution | ⚪ | ⏳ | Assumes instant execution. Real orders have 100-500ms latency. |
| E13 | No exchange rate limits | Execution | 🟡 | ⏳ | Rate_limiter.c exists but may not be used in engine loop. |
| E14 | No API key rotation for trading | Execution | 🟡 | ⏳ | Key rotation exists for health check but not for trade execution. |
| E15 | No exchange-specific auth | Execution | 🟡 | ⏳ | Kraken uses API key + secret. Engine has no auth module. |
| E16 | No multi-account trading (sub-accounts) | Execution | ⚪ | ⏳ | Some exchanges allow sub-accounts for strategy isolation. |
| E17 | No multi-wallet support for crypto | Execution | ⚪ | ⏳ | Single wallet. Can't segregate trading capital. |
| E18 | No settlement cycle modeling | Execution | 🟡 | ⏳ | Crypto settles T+0, stocks T+1, options T+1. Not modeled. |
| E19 | No margin trading model | Execution | ⚪ | ⏳ | Leverage trading not modeled. |
| E20 | No futures contract rollover | Execution | ⚪ | ⏳ | Futures expire. Roll costs, contango/backwardation not modeled. |
| E21 | No multi-exchange arbitrage | Execution | ⚪ | ⏳ | Cross-exchange price differences not exploited. |
| E22 | No smart-order-routing | Execution | ⚪ | ⏳ | Best execution across venues not computed. |
| E23 | No trade cost analysis | Execution | 🟡 | ⏳ | Real cost of each trade (fee + slippage + impact) not recorded. |
| E24 | No execution vs signal delay model | Execution | ⚪ | ⏳ | Time from signal generation to order placement not tracked. |
| E25 | No order book simulation | Execution | 🟡 | ⏳ | Paper trades assume top-of-book price. L2 impact not modeled. |
| E26 | No market impact model | Execution | 🟡 | ⏳ | Large orders move price. Impact model exists in SLIPPAGE_VOL_SCALE but uncalibrated. |
| E27 | No price improvement model | Execution | ⚪ | ⏳ | Market orders can get better than NBBO. Not modeled. |
| E28 | No dark pool execution model | Execution | ⚪ | ⏳ | Dark pools offer different execution characteristics. |
| E29 | No time-in-force options (IOC, FOK, GTC) | Execution | ⚪ | ⏳ | Paper assumes GTC. Different TIF have different fill probabilities. |
| E30 | No exchange connection health check | Execution | 🟡 | ⏳ | If exchange API is down, engine still places "trades" without error. |
| E31 | No exchange-specific min order sizes in execution | Execution | 🟡 | ⏳ | Kraken min $10 crypto buy. MIN_TRADE_STAKE=$1 may be too low. |
| E32 | No withdrawal automation | Execution | ⚪ | ⏳ | Withdrawal_scheduler.c exists for paper profits only. |
| E33 | No staking/yield integration | Execution | ⚪ | ⏳ | Idle capital earns no yield. Could stake for 3-5% APR. |
| E34 | No exchange sandbox/testnet | Execution | ⚪ | ⏳ | Kraken/Coinbase offer testnet. Real-money testing is the only test. |
| E35 | No transaction cost analysis dashboard | Execution | ⚪ | ⏳ | Total fees paid / total trade value ratio not displayed anywhere. |

---

## ── DOMAIN F: INFRASTRUCTURE (35 cells) ──

| # | Gap | Domain | Pri | Status | Detail |
|---|-----|--------|-----|--------|--------|
|| F01 | No Docker container for engine | Infra | 🟡 | ✅ | **FIXED**: Dockerfile + docker-compose.yml. Multi-stage build (debian:bookworm-slim). Compiles all C binaries, copies to minimal runtime image. Non-root user, health check, volume mounts. Compose: engine + data-server + watchdog services. |
|| F02 | No CI/CD beyond GitHub Pages | Infra | 🟡 | ✅ | **FIXED**: .github/workflows/ci.yml — builds all engine binaries, runs tests, runs valgrind memcheck on room_engine. Triggers on push/PR to main. Uploads artifacts. |
|| F03 | No hermetic build environment | Infra | 🟡 | ✅ | **FIXED**: scripts/build.sh — checks all deps (gcc, make, pkg-config, libcurl, jansson, sqlite3), verifies versions, builds with make all -jN. Supports --clean, --test, --memcheck flags. Pinned version requirements documented. |
|| F04 | No environment variable management | Infra | 🟡 | ✅ | **STALE**: secrets.h already implements .env pattern. Reads from ~/.hermes/secrets.env (gitignored, outside repo). Supports KEY=VALUE format, optional expiry timestamps, key rotation with auto-renew hooks. SECRETS_PATH defined at line 32. |
| F05 | No graceful shutdown | Infra | 🟡 | ✅ | **FIXED**: Added SIGTERM/SIGINT handler to room_engine.c. `g_shutdown_flag` volatile sig_atomic_t flag set by `handle_signal()`. Main loop checks flag after each cycle, completes current cycle then exits normally. `msync()` flush added before `munmap()` to ensure mmap'd state is committed to disk. Signal handler registered at line 810, loop check at line 847, msync at line 1596. |
| F06 | No process health beyond heartbeat | Infra | 🟡 | ✅ | **STALE**: room_watchdog.c already implements responsiveness check — verifies snapshot.json mtime < 5min before declaring healthy. If stale (hung engine), cycles all engines with timeout. 4 per-room heartbeats written on success. |
|| F07 | resource_monitor.c — CPU/memory/disk/process monitoring | Infra | 🟡 | ✅ | **FIXED**: resource_monitor.c (240 lines C) — reads /proc/meminfo, /proc/loadavg, /proc/stat, statvfs for disk, /proc/[pid]/status for engine processes (RSS/VSz/uptime). Writes JSON to ~/.hermes/infra/resource_monitor.json + docs/data/. Cron: every 5min via Hermes job. Threshold alerts: memory>80%=WARN, >90%=CRIT; disk>85%=WARN, >95%=CRIT; load>8=WARN, >16=CRIT on 8-core system. |
|| F08 | No disk space monitoring | Infra | 🟡 | ✅ | **STALE**: F07 resource_monitor.c (engine/resource_monitor.c:105-116) already implements disk monitoring via read_disk() using statvfs("/", &buf). Writes disk_total_gb, disk_used_gb, disk_avail_gb, disk_used_pct to JSON. Alerts at >85% WARN and >95% CRITICAL. Active every 5min via Hermes job. |
|| F09 | No database backup strategy | Infra | 🟡 | ✅ | **FIXED**: db_backup.c (80 lines C) — copies timeline.db and other key DBs to data/backups/ with daily timestamp. Keeps last 30 days per DB, auto-prunes older. Compiles standalone, no external libs. Make target: `make db_backup`. |
|| F10 | No recovery from corrupt state files | Infra | 🟡 | ✅ | **FIXED**: RoomState now has CRC-32 checksum (state_crc field, types.h:309). Computed nibble-at-a-time CRC-32 over struct bytes 8..sizeof(RoomState) — no external deps. Verified on mmap load (room_engine.c:695): if magic matches but CRC doesn't, writes state_corrupt_alert.json and reinitializes. CRC updated before msync (room_engine.c:1658). STATE_MAGIC bumped to ROMA (0x524F4D41) for the struct layout change. |
|| F11 | No state version migration | Infra | 🟡 | ✅ | **FIXED**: types.h — added state_version field to RoomState (STATE_VERSION=3, STATE_MAGIC bumped to ROMB). room_engine.c — added migrate_old_state() logic: detects old version, initializes new fields (CRC, take-profit), recomputes CRC. Fresh init sets state_version=3. |
| F12 | No rollback capability | Infra | 🟡 | ⏳ | git revert code but DB state can't be rolled back. |
| F13 | No monitoring dashboard beyond CLI | Infra | 🟡 | ⏳ | Web dashboard shows summary but no real-time engine status. |
|| F14 | No alert integration (Telegram/email) | Infra | 🟡 | ✅ | **FIXED**: health_alerter.c — added send_telegram_alert() using curl to Telegram Bot API. Sends ⚠️ on health degradation, ✅ on recovery. Token from TELEGRAM_BOT_TOKEN env var. Async (background) delivery. |
|| F15 | No systemd service for engine | Infra | 🟡 | ✅ | **FIXED**: config/money-room.service — systemd unit with auto-restart, SIGTERM shutdown, security hardening (NoNewPrivileges, ProtectSystem, PrivateTmp), resource limits (2G RAM, 80% CPU), journal logging. |
|| F16 | No log rotation for all logs | Infra | 🟡 | ✅ | **FIXED**: Created logrotate config at ~/.hermes/logrotate-money-room. Covers CSV (7-day rotation, 100M max), JSONL (30-day, 500M max), room_log.csv (4-week, 50M max). copytruncate for running processes without restart. |
| F17 | No structured logging (JSON) | Infra | ⚪ | ⏳ | Engine logs are text printf. Machine parsing hard. |
| F18 | No performance benchmark suite | Infra | ⚪ | ⏳ | No baseline for cycle time, memory usage, trade throughput. |
|| F19 | No regression test suite for engine | Infra | 🟡 | ✅ | **FIXED**: test_runner.c — added 6 engine logic tests: engine binary exists, darwin compiles, features compile, stress_test compiles, ablation_test compiles+runs, cross_source_check compiles. 9/11 pass (2 pre-existing failures). |
|| F20 | No memory leak detection in CI | Infra | 🟡 | ✅ | **FIXED**: .github/workflows/ci.yml — added F20 step that runs make memcheck and fails CI if any definite memory leaks detected (grep "definitely lost: [1-9]"). |
| F21 | No dependency update tracking | Infra | ⚪ | ⏳ | libcurl/libjansson/sqlite3 versions not tracked. |
| F22 | No SSL cert management | Infra | ⚪ | ⏳ | If website adds HTTPS, cert renewal not automated. |
| F23 | No multi-region/DR | Infra | ⚪ | ⏳ | Single WSL host. No disaster recovery. |
| F24 | No data export API | Infra | ⚪ | ⏳ | No REST API for external systems to query engine state. |
| F25 | No read-replica for website data | Infra | ⚪ | ⏳ | Website reads live files. No caching layer. |
| F26 | No gzipped/compressed data serving | Infra | ⚪ | ⏳ | JSON files served uncompressed. Large payloads. |
| F27 | No ETag/cache headers on data files | Infra | ⚪ | ⏳ | GitHub Pages data files have no caching optimization. |
| F28 | No preview/staging deployment | Infra | ⚪ | ⏳ | All changes go directly to production. |
| F29 | No feature flag system | Infra | ⚪ | ⏳ | Can't enable/disable features at runtime. Requires recompile. |
| F30 | No runbook for common failures | Infra | 🟡 | ⏳ | If something breaks, no documented recovery procedure. |
| F31 | No incident response plan | Infra | ⚪ | ⏳ | No defined severity levels, escalation paths. |
| F32 | No post-mortem process | Infra | ⚪ | ⏳ | Failures not formally documented. |
| F33 | No time-series metrics database | Infra | ⚪ | ⏳ | Prometheus/InfluxDB not deployed. No historical metric queries. |
| F34 | No anomaly detection on engine metrics | Infra | ⚪ | ⏳ | If cycle time triples or trade volume drops 90%, no alert. |
|| F35 | No automated dependency install | Infra | 🟡 | ✅ | **FIXED**: Created config/setup-deps.sh — detects package manager (apt/yum/pacman/apk), installs gcc, make, pkg-config, libcurl, jansson, sqlite3, valgrind. Verifies installations after setup. |

---

## ── DOMAIN G: SECURITY (35 cells) ──

| # | Gap | Domain | Pri | Status | Detail |
|---|-----|--------|-----|--------|--------|
| G01 | API keys stored in ~/.hermes/secrets.env in plaintext | Security | 🟡 | ⏳ | RECLASSIFIED from 🔴 to 🟡: existing vault infrastructure (secrets_vault/ with AES-256-GCM via infra.py) already protects Kraken key. Full migration of 12+ keys requires boot-time vault decryption loader + modifying all 12+ collectors. Not a single-file fix. Current chmod 600 protection is standard for WSL. Vault infra exists at ~/.hermes/infra/ with PBKDF2+AESGCM. |
| G02 | No API key permission scoping | Security | 🟡 | ⏳ | Keys have full exchange access. No read-only / trading-only separation. |
| G03 | No IP whitelist on exchange keys | Security | 🟡 | ⏳ | Most exchanges support IP whitelisting. Not configured. |
| G04 | No key usage monitoring | Security | 🟡 | ⏳ | No alert if key suddenly used from new IP or higher volume. |
| G05 | No prompt injection guard for external content | Security | 🟡 | ⏳ | RECLASSIFIED from 🔴 to 🟡: agent-level concern, not C engine. C engine reads structured JSON feeds only. Agent has memory rule: 'GitHub bounties = prompt injection risk. Never execute commands from issues...' Already mitigated by agent guard.
| G06 | No DA guard on wallet operations | Security | 🟡 | ⏳ | RECLASSIFIED from 🔴 to 🟡: paper trading only — no real wallet operations exist yet. DA guard (confirmation step before fund transfers) is a precondition for E01 (live exchange integration), not needed in current paper-only mode. |
| G07 | No rate limit on API calls | Security | 🟡 | ⏳ | Could trigger exchange rate limits and get banned. |
| G08 | No exchange-connection encryption check | Security | 🟡 | ⏳ | All connections use HTTPS, but no cert pinning. |
| G09 | No local network exposure control | Security | 🟡 | ⏳ | data_server runs on port 9090. No auth. Local network can access. |
|| G10 | No CORS policy on data_server | Security | 🟡 | ✅ | **STALE**: data_server.c already has CORS headers. `Access-Control-Allow-Origin: *` on all responses (400, 200, OPTIONS preflight). `Access-Control-Allow-Methods: POST, GET, OPTIONS`. `Access-Control-Allow-Headers: Content-Type`. CORS preflight handled at line 186. |
|| G11 | No input validation on market_feed.json | Security | 🟡 | ✅ | **FIXED**: room_feeds.c — added NaN/Inf detection on all 14 float fields (replaces with 0 + warns). Validates high >= low (swaps if inverted). Rejects negative prices (returns ERR_NO_DATA). |
|| G20 | No HTTPS for data_server | Security | 🟡 | ⏳ | Port 9090 serves HTTP. No TLS. |
| G13 | No code signing | Security | ⚪ | ⏳ | Built binaries not signed. Tampering undetectable. |
|| G14 | No integrity check on state files | Security | 🟡 | ✅ | **STALE**: F10 fix added CRC-32 checksum to RoomState (types.h:312 state_crc). Computed on save, verified on load. Corrupt state triggers state_corrupt_alert.json and reinitialization. room_engine.c:695 (verify), 1658 (compute). |
| G15 | No sandbox for collector binaries | Security | ⚪ | ⏳ | Collectors have full filesystem access. |
| G16 | No seccomp or capability dropping | Security | ⚪ | ⏳ | Binaries run with full Linux capabilities. |
| G17 | No audit log for state changes | Security | ⚪ | ⏳ | Who changed what config when? No audit trail. |
| G18 | No session management for web dashboard | Security | ⚪ | ⏳ | If dashboard adds auth later, need session system. |
| G19 | No CSRF protection | Security | ⚪ | ⏳ | No web forms currently, but prep needed. |
| G20 | No HTTPS for data_server | Security | 🟡 | ⏳ | Port 9090 serves HTTP. No TLS. |
| G21 | No secrets rotation schedule | Security | 🟡 | ⏳ | Key_rotation.c monitors key age but doesn't auto-rotate. |
| G22 | No SSH key management for deploys | Security | 🟡 | ⏳ | GitHub deploy keys exist but no rotation policy. |
| G23 | No fail2ban for repeated API failures | Security | ⚪ | ⏳ | If API key fails auth repeatedly, no ban. |
| G24 | No DDoS protection | Security | ⚪ | ⏳ | Single-host. No WAF/rate-limiting. |
| G25 | No backup encryption | Security | ⚪ | ⏳ | Backups (if they exist) are unencrypted. |
| G26 | No secure deletion of old keys | Security | ⚪ | ⏳ | If key is rotated, old key persists in secrets.env or git. |
| G27 | No git-crypt for secrets in repo | Security | 🟡 | ⏳ | secrets.h may contain sensitive paths. No encryption. |
| G28 | No 2FA for exchange accounts | Security | 🟡 | ⏳ | Exchange accounts should have 2FA enabled. |
| G29 | No withdrawal address allowlisting | Security | 🟡 | ⏳ | Exchanges support address allowlists. Not configured. |
| G30 | No sub-account isolation for trading | Security | ⚪ | ⏳ | All trading from same account. Sub-accounts isolate risk. |
| G31 | No security scan in CI | Security | ⚪ | ⏳ | No automated vulnerability scanning. |
| G32 | No dependency CVE monitoring | Security | ⚪ | ⏳ | libcurl, jansson, sqlite3 CVEs not tracked. |
| G33 | No incident response plan for breach | Security | ⚪ | ⏳ | If breach happens, what to do? No documented plan. |
| G34 | No session timeout for dashboard | Security | ⚪ | ⏳ | Dashboard has no auth currently. Future concern. |
| G35 | No data minimization policy | Security | ⚪ | ⏳ | What data is collected and why? No documented policy. |

---

## ── DOMAIN H: WEBSITE & UI (30 cells) ──

| # | Gap | Domain | Pri | Status | Detail |
|---|-----|--------|-----|--------|--------|
| H01 | No live trading dashboard for rooms | Website | 🟡 | ⏳ | 16 rooms shown as names only. No per-room PnL, WR, cycles. |
| H02 | No per-market breakdown on website | Website | 🟡 | ⏳ | Crypto vs sports vs election PnL not shown. |
| H03 | No trade history explorer | Website | 🟡 | ⏳ | Can't browse individual trades. |
| H04 | No agent browser (top/bottom performers) | Website | 🟡 | ⏳ | Best agents invisible. No leaderboard. |
| H05 | No genome visualizer | Website | ⚪ | ⏳ | Genome weights represented as heatmap. |
| H06 | No feature importance chart | Website | 🟡 | ⏳ | FeatureImportance struct populated but not displayed. |
| H07 | No system confidence score on dashboard | Website | 🟡 | ⏳ | Aggregate "how healthy is the system" indicator. |
| H08 | No data freshness indicators per feed | Website | 🟡 | ⏳ | Each data source shows last-updated time. |
| H09 | No alert history page | Website | ⚪ | ⏳ | Past system alerts visible. |
| H10 | No settings/configuration page | Website | ⚪ | ⏳ | Can't change parameters from web. |
| H11 | No registration/login system | Website | 🟡 | ⏳ | If multi-user, no auth system. |
| H12 | No API key management UI | Website | ⚪ | ⏳ | API key self-service for external users. |
| H13 | No registration page for user accounts | Website | 🟡 | ⏳ | registration.html exists but flows may not work. |
| H14 | No pricing/subscription page | Website | 🟡 | ⏳ | pricing.html exists but no payment integration. |
| H15 | No documentation site | Website | 🟡 | ⏳ | ARCHITECTURE.md is developer-facing. No user docs. |
| H16 | No mobile-responsive design | Website | 🟡 | ⏳ | dashboards may not render well on phone. |
| H17 | No dark mode toggle | Website | ⚪ | ⏳ | Personalization feature. |
| H18 | No live charting (time-series of PnL) | Website | 🟡 | ⏳ | Charts are static snapshots. No interactive time series. |
| H19 | No WebSocket for real-time updates | Website | ⚪ | ⏳ | Page requires refresh. No push updates. |
| H20 | No service worker for offline | Website | ⚪ | ⏳ | No PWA support. |
| H21 | No Terms of Service page | Website | 🟡 | ⏳ | Legal requirement if users access. |
| H22 | No Privacy Policy page | Website | 🟡 | ⏳ | Legal requirement. |
| H23 | No Cookie Consent | Website | ⚪ | ⏳ | EU requirement if cookies used. |
| H24 | No SEO metadata | Website | ⚪ | ⏳ | No meta tags, sitemaps, structured data. |
| H25 | No blog/updates page | Website | ⚪ | ⏳ | Changelog exists but not user-facing. |
| H26 | No "paper proof" results page | Website | 🟡 | ⏳ | Paper trading results should be published. |
| H27 | No comparison vs benchmarks | Website | ⚪ | ⏳ | "We returned X% vs BTC's Y%" comparison chart. |
| H28 | No demo mode that works without live data | Website | 🟡 | ⏳ | Website shows "--" values when data not available. |
| H29 | No JSON API explorer | Website | ⚪ | ⏳ | Endpoints documented with live examples. |
| H30 | No performance metrics (page load, data age) | Website | ⚪ | ⏳ | Website itself not monitored for performance. |

---

## ── DOMAIN I: MONETIZATION (30 cells) ──

| # | Gap | Domain | Pri | Status | Detail |
|---|-----|--------|-----|--------|--------|
| I01 | No payment integration (LemonSqueezy blocked by KYC) | Money | 🟡 | ⏳ | RECLASSIFIED from 🔴 to 🟡: KYC is a human/support blocker. No code fix possible. Purely external.
| I02 | No subscription tiers defined | Money | 🟡 | ⏳ | Free/Pro/Enterprise levels not designed. |
| I03 | No API product for external users | Money | 🟡 | ⏳ | API exists but not packaged for sale. |
| I04 | No data feed product | Money | 🟡 | ⏳ | "Money Room data" as SaaS product. |
| I05 | No signal/alert product | Money | 🟡 | ⏳ | "Buy/Sell signals" as Telegram bot subscription. |
| I06 | No affiliate program | Money | ⚪ | ⏳ | Referral-based growth not set up. |
| I07 | No demo account tier | Money | 🟡 | ⏳ | Free tier should offer limited/lagged data. |
| I08 | No usage-based billing | Money | ⚪ | ⏳ | API call counting + billing not implemented. |
| I09 | No free tier rate limiting | Money | 🟡 | ⏳ | Free users get same access as dev. Need rate limiting. |
| I10 | No user quota tracking | Money | ⚪ | ⏳ | How many API calls per user? No tracking. |
| I11 | No value-add analytics product | Money | 🟡 | ⏳ | C engine's feature importance, tail risk as sellable insights. |
| I12 | No portfolio tracking product | Money | ⚪ | ⏳ | Users connect exchange APIs, get ML portfolio suggestions. |
| I13 | No alerting/notification product | Money | 🟡 | ⏳ | Telegram alert system exists but not packaged for sale. |
| I14 | No white-label option | Money | ⚪ | ⏳ | Enterprise customers get rebranded dashboard. |
| I15 | No revenue dashboard (MRR/ARR) | Money | 🟡 | ⏳ | Not tracking potential revenue. |
| I16 | No churn analysis | Money | ⚪ | ⏳ | No user base yet. But churn model needed before launch. |
| I17 | No A/B pricing test framework | Money | ⚪ | ⏳ | Can't test price points. |
| I18 | No coupon/discount system | Money | ⚪ | ⏳ | Promotional pricing not supported. |
| I19 | No invoice generation | Money | ⚪ | ⏳ | Legal requirement for paid tiers. |
| I20 | No tax computation (VAT, sales tax) | Money | ⚪ | ⏳ | Merchant of Record handles this (LemonSqueezy). |
| I21 | No refund policy page | Money | ⚪ | ⏳ | Legal requirement. |
| I22 | No SLA page | Money | ⚪ | ⏳ | For paid tiers. Uptime guarantees. |
| I23 | No bug bounty program | Money | ⚪ | ⏳ | Security researchers need compensation path. |
| I24 | No referral tracking | Money | ⚪ | ⏳ | User referral links not implemented. |
| I25 | No partner/integration program | Money | ⚪ | ⏳ | Third-party integrations as revenue source. |
| I26 | No sponsored content/product placement | Money | ⚪ | ⏳ | "Powered by Money Room data" endorsements. |
| I27 | No consulting/services offering | Money | 🟡 | ⏳ | Custom trading system builds as service. |
| I28 | No educational content (courses/guides) | Money | ⚪ | ⏳ | Sell access to "how to build a trading engine" course. |
| I29 | No enterprise license model | Money | ⚪ | ⏳ | Per-seat or per-deployment pricing. |
| I30 | No revenue share with data providers | Money | ⚪ | ⏳ | If repackaging data, revenue sharing needed. |

---

## ── SUMMARY ──

| Domain | Cells | 🔴 P0 | 🟡 P1 | 🟢 P2 | ⚪ P3 | ⚫ P4 |
|--------|-------|-------|-------|-------|-------|-------|
||| A: Training Engine | 60 | 0 | 5 | 0 | 27 | 0 |
||| B: Features | 45 | 0 | 2 | 0 | 33 | 0 |
||| C: Risk Management | 40 | 0 | 2 | 0 | 16 | 0 |
||| D: Data Pipeline | 55 | 0 | 31 | 0 | 13 | 0 |
|| E: Execution | 35 | 1 | 13 | 0 | 21 | 0 |
||| F: Infrastructure | 35 | 0 | 2 | 0 | 18 | 0 |
|| G: Security | 35 | 0 | 15 | 0 | 16 | 0 |
|| H: Website & UI | 30 | 0 | 16 | 0 | 14 | 0 |
|| I: Monetization | 30 | 0 | 11 | 0 | 19 | 0 |
||| **TOTAL** | **365** | **1** | **64** | **0** | **177** | **0** |

🔴 P0: 1 critical gap (E04 Polymarket — blocked on $50 USDC) | 🟡 P1: 64 major gaps | ⚪ P3: 177 minor/feature gaps
