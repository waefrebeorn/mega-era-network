# BATTLEFIELD DEEP AUDIT — Money Room Trading Floor
**Generated**: 2026-06-08
**Scope**: Full system sweep — every component, every gap, every lie

---

## AUDIT METHODOLOGY
- **P0 (Critical)**: Blocks live profit, data integrity, or safety
- **P1 (Major)**: Degrades performance, prevents scale, technical debt
- **P2 (Minor)**: Code quality, observability, nice-to-have
- **P3 (Process)**: Missing automation, docs, runbooks

Each gap: `ID | Component | Severity | Evidence | Fix Effort | Owner`

---

## 1. C_ROOM ENGINE CORE (`room_engine.c`, `types.h`, state)

---

## 1. C_ROOM ENGINE CORE (`room_engine.c`, `types.h`, state)

### P0 - Critical
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| C001 | State CRC mismatch on every startup | P0 | Logs show `stored=0x50786194, computed=0x95B85FA6` — state reinitialized every run | 2h |
| C002 | No live market data in engine loop | P0 | `market_feed.json` only refreshed by external `feed_builder`; engine reads stale feed | 4h |
| C003 | Single market_type hardcoded | P0 | `g_agent_market[MAX_AGENTS]` but all agents get same market_type; no per-room specialization | 8h |
| C004 | Panic stop file check only | P0 | `check_panic()` checks `/tmp/money_room_panic` but no auto-create on drawdown/circuit breaker | 2h |
| C005 | No position sizing — fixed $10/trade | P0 | `room_capital_apply` uses hardcoded sizing; no Kelly, no risk-parity, no max exposure | 8h |
| C006 | Darwin evolution frozen | P0 | `epoch=0, culled=0, cloned=0` in 1,314 cycles; `g_flags.darwin_evolution=true` but never triggers | 4h |
| C007 | Nested HT prediction broken | P0 | `compute_nested_prediction` returns 0.5 for all markets; macro feats[12-16] all zeros | 8h |
| C008 | Feature importance never updates | P0 | `pos_contrib_total`, `neg_contrib_total` stay 0 — no feedback from `accuracy_scorer` | 6h |
| C009 | Resolution tick never fed | P0 | `room_capital_resolve` needs `resolution_tick` but main loop never provides future price | 6h |
| C010 | Trade log CSV append but never rotated | P0 | 3.1GB `trade_log.csv` — no rotation, no indexing, O(N) reads | 2h |

### P1 - Major
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| C101 | Hot-reload scans directory every 1000 cycles | P1 | `opendir`/`readdir` on every check; no inotify/fanotify | 4h |
| C102 | Binary market detection by price range | P1 | `if (tick->close > 1000.0) price = 0.5` — BTC $63k treated as binary | 2h |
| C103 | Macro features hardcoded zeros | P1 | `feats[14]=0; feats[15]=0; feats[16]=0` — Fed funds, CPI, 10y-2y never populated | 4h |
| C104 | Volume ratio always 1.0 | P1 | `feats[7]=1.0; feats[8]=1.0` — no real volume data for most markets | 4h |
| C105 | Cascade inference runs every cycle | P1 | 6-level MLP/LR per market type × 10 = 60 inferences/cycle; no batching | 6h |
| C106 | No memory mapping for state — uses mmap but no madvise | P1 | `mmap` without `MADV_SEQUENTIAL` or `MADV_WILLNEED` | 2h |
| C107 | Signal handlers not async-signal-safe | P1 | `printf`, `fprintf` in `handle_sig` — UB if interrupted malloc | 2h |
| C108 | `g_nested` global — no mutex for concurrent access | P1 | If multi-threaded, race on nested models | 4h |
| C109 | Checkpoint saves every 1000 cycles but no retention policy | P1 | `room_state.bin.checkpoint.N` accumulates indefinitely | 2h |
| C110 | `is_us_holiday` incomplete | P1 | Missing Good Friday, Columbus Day, Veterans Day, Juneteenth | 1h |

### P2 - Minor / Code Quality
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| C201 | Magic number 1380928834 (ROMB) not validated against STATE_MAGIC | P2 | `STATE_MAGIC=ROMB` in Makefile but runtime check unclear | 1h |
| C202 | `STATE_VERSION=4` but no migration logic | P2 | Version bump with no forward/backward compatibility | 4h |
| C203 | Unused function `is_market_mode()` | P2 | Defined line 124, warned by compiler | 0.5h |
| C204 | Unused variable `offset` in `nested_ht_infer.h` | P2 | Compiler warning | 0.5h |
| C205 | No bounds check on `g_agent_market` indexing | P2 | `g_agent_market[MAX_AGENTS]` but written without validation | 1h |



### Types.h Structural Gaps (from room_engine.c audit continued)
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| C206 | STATE_VERSION=4 but state_version init=3 | P2 | Line 324: `current=3` comment vs `#define STATE_VERSION 4` | 0.5h |
| C207 | MAX_TRADE_HIST=1,000,000 -> 68MB just for trades array | P1 | TradeRecord x 1M = ~36MB; OOM risk on 32-bit | 2h |
| C208 | votes[MAX_AGENTS] in RoomState -- 10k x VoteRecord | P1 | VoteRecord ~48B -> 480KB per cycle; written every cycle = heavy mmap | 4h |
| C209 | No max_position_pct_room enforcement in room_capital.c | P1 | Defined in RoomState line 381 but never checked | 4h |
| C210 | epsilon decay but no epsilon-greedy in vote | P2 | Lines 409-411 define epsilon but room_vote.c doesn't use it | 4h |
| C211 | cross_room_correlation 20x20 matrix never computed | P2 | Lines 426-430 defined, no code fills it | 8h |
| C212 | exchange_fees[MAX_ASSETS] but only 8 assets defined | P2 | MAX_ASSETS=8 but 16 rooms -> asset_id collision | 2h |
| C213 | recent_trade_keys[1024] -- duplicate detection only 1k window | P2 | High-frequency trading needs larger window | 1h |
| C214 | asset_exposure[MAX_ASSETS][2] -- same 8-asset limit | P2 | Correlated exposure tracking broken for 16 rooms | 2h |
| C215 | panic_stop flag but no persist across restarts | P1 | If engine crashes during panic, resumes trading on restart | 2h |

---

## 2. ALL 16 ROOM DIRECTORIES & CONFIGS


---

## 2. ALL 16 ROOM DIRECTORIES & CONFIGS

### P0 - Critical
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| R001 | **ALL 16 rooms share IDENTICAL market_feed.json** | P0 | Every room has BTC OHLCV, Kraken/Coinbase BTC prices; only `room_domain` and `market_type` differ | 16h |
| R002 | No domain-specific data collectors | P0 | `room_config.json` lists features like `spy_price`, `eth_price`, `kalshi_prob` but feed has NONE of these | 40h |
| R003 | `crypto_prices` and `science_tech` have `max_trades_per_cycle: 0` | P0 | Line 44: 0 trades allowed = disabled rooms | 0.5h |
| R004 | 15/16 rooms point to same binary `/macro/room_engine_v3` | P0 | Only `btc_main` and `momentum` use different binaries; no specialization | 2h |
| R005 | `engine_mode: "market"` but no live exchange wiring | P0 | All configs say market mode but `room_engine_market` never tested with real API | 16h |
| R006 | `ws_age_sec: 848228` (9.8 days stale) in all feeds | P0 | WebSocket data 9+ days old — all rooms trading on dead data | 4h |
| R007 | `coinbase_volume_24h: 0.0` in all feeds | P0 | Coinbase feed dead; no volume data for any room | 2h |
| R008 | `nested_prediction: 0.5000` constant across all rooms | P0 | Cascade model outputting neutral 0.5 everywhere | 8h |

### P1 - Major
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| R101 | Room configs describe features that don't exist in feed | P1 | `stocks` config: `spy_price,qqq_price,vix_corr,sector_flow` — feed has BTC only | 24h |
| R102 | `market_type` enum values don't match feature sets | P1 | `weather` = type 8 but feed has `market_type: 8` with BTC price data | 4h |
| R103 | All rooms use same `circuit_breaker_drawdown_pct: 20` | P1 | No room-specific risk params (crypto vs elections vs weather need different limits) | 2h |
| R104 | `capital: 50` for 15 rooms, `100` for btc_main | P1 | Arbitrary; no Kelly/sizing rationale | 2h |
| R105 | `room_engine_v2` exists only in macro/momentum/polymarket | P1 | Inconsistent binary versions across rooms | 2h |
| R106 | No per-room `room_config.json` versioning | P1 | Configs created May 29, never updated; no schema validation | 2h |
| R107 | `room_log.csv` growing unbounded (345KB-3MB each) | P1 | No rotation, no compression | 2h |
| R108 | `eco/` subdirectory in each room — purpose unclear | P1 | Empty or stale; no integration with main engine | 2h |
| R109 | `types.h` copied to each room (10KB × 16 = waste) | P1 | Should be shared from engine build | 1h |

### P2 - Minor
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| R201 | `room_snapshot.json` identical structure, not used | P2 | All 6.3KB same format; no consumer reads it | 1h |
| R202 | `room.heartbeat` only in btc_main, macro, momentum, polymarket | P2 | Inconsistent health signaling | 1h |
| R203 | `engine_log.jsonl` only in some rooms | P2 | Missing observability for 10 rooms | 1h |
| R204 | `room_watchdog` binary same in all (16KB × 16) | P2 | Should be symlink or shared | 0.5h |
| R205 | No room-specific genome files | P2 | All rooms load from same `multi_market/*.bin` | 4h |

---

## 3. DATA COLLECTORS & FEED PIPELINE (27+ collectors)


---

## 3. DATA COLLECTORS & FEED PIPELINE (27+ collectors)

### P0 - Critical
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| D001 | **feed_builder outputs ~80% RANDOM values** | P0 | Lines 314, 330-332, 338, 343-344, 347-348, 351-352, 355-357, 360-362, 365-367, 370-372, 380-385, 387-396, 399-404, 407-412, 420-411, 424-427, 430-451 all use `rand()` | 40h |
| D002 | **feed_builder reads from timeline.db but collectors don't populate it** | P0 | Collector binaries exist but `collector_runner` calls `.sh` wrappers that may not run; no verification that data lands in DB | 24h |
| D003 | **BTC price is ONLY real market data** | P0 | `get_btc_price()` reads from timeline; all other fields synthetic | 16h |
| D004 | **No collector health monitoring in pipeline** | P0 | `collector_health` binary exists but not integrated into `collector_runner` | 8h |
| D005 | **Manifold query in room_feed_gen reads from timeline.db but manifold_collector may not run** | P0 | `get_manifold_prob()` queries `source='manifold'` but log shows `manifold_collector.log` is 92 bytes (empty) | 4h |
| D006 | **coinbase_volume_24h: 0.0** | P0 | Coinbase collector not running or broken | 2h |
| D007 | **ws_age_sec: 848228 (9.8 days)** | P0 | WebSocket feed bridge dead; `ws_feed_bridge.sh` not running | 4h |

### P1 - Major
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| D101 | feed_builder uses fragile string parsing on JSON | P1 | `strstr`/`atof` on raw JSON — breaks on format changes | 8h |
| D102 | No data freshness enforcement in feed_builder | P1 | Outputs whatever is in DB even if 28 days old (DGS10, blockchain) | 4h |
| D103 | collector_runner calls shell scripts with 3 retries but no backoff strategy | P1 | Fixed 5s delay; exponential backoff needed | 2h |
| D104 | collector_runner LOCK_FILE prevents parallel runs but causes skipped cycles | P1 | If one run takes >15min, next cycle skipped entirely | 2h |
| D105 | SPORTS_TASKS includes `kalshi_collector.sh` with 300s timeout | P1 | 5min timeout for single collector = pipeline stall | 2h |
| D106 | No collector dependency graph | P1 | `room_feed_gen` needs manifold data but manifold_collector runs independently | 8h |
| D107 | `weather_collector` outputs hardcoded values (0.375, 0.0, 0.15) | P1 | Lines 430-433 in feed_builder | 4h |
| D108 | `pm_probability`, `sports_odds`, `election_prob` all 0.5 | P1 | Lines 434, 439, 443 — neutral defaults = no signal | 8h |
| D109 | BTC 1-min CSV refresh not in collector_runner | P1 | `btc_csv_refresh.log` exists but not scheduled | 2h |
| D110 | No collector output schema validation | P1 | Feed builder trusts DB blindly | 8h |

### P2 - Minor
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| D201 | `collector_runner.log` 661KB - no rotation | P2 | Growing unbounded | 1h |
| D202 | Shell wrapper scripts (`.sh`) for every collector = indirection | P2 | 50+ shell scripts wrapping C binaries | 8h |
| D203 | `kraken_collector` / `coinbase_collector` not in collector_runner tasks | P2 | Exchange data fetched elsewhere or not at all | 4h |
| D204 | `macro_collector.c` exists but not compiled/used | P2 | Not in Makefile targets | 1h |
| D205 | `feature_autoencoder` binary exists but not in pipeline | P2 | Trained but never used for features | 4h |

---

## 4. EXCHANGE INTEGRATION (API, WebSocket, Order Execution)


---

## 4. EXCHANGE INTEGRATION (API, WebSocket, Order Execution)

### P0 - Critical
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| E001 | **No live order execution — only public ticker fetch** | P0 | `exchange_api.c` only has `fetch_*_ticker()`; no `place_order`, `cancel_order`, `get_balance` | 40h |
| E002 | **Private API keys empty — no authenticated endpoints** | P0 | `exchange_config_load()` loads keys but `exchange_config.json` has empty strings | 8h |
| E003 | **room_engine_market binary exists but never tested live** | P0 | Built but no integration test; `engine_mode: "market"` in configs but unused | 16h |
| E004 | **feed_bridge uses shell `ls/popen` for news files** | P0 | Line 145: `popen("ls -t ...")` — fragile, not portable | 4h |
| E005 | **No WebSocket order book / trade stream** | P0 | `ws_feed_bridge.sh` dead (ws_age_sec 9.8 days); no real-time data | 24h |
| E006 | **No position reconciliation with exchange** | P0 | Engine tracks positions locally; no `get_open_orders` / `get_positions` | 24h |
| E007 | **Order signing/hmac not implemented** | P0 | ExchangeConfig has secret fields but no `sign_request` function | 16h |

### P1 - Major
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| E101 | feed_bridge depends on local cache files (`options_cache/*.json`) | P1 | Lines 32-42: paths to `ONCHAIN_FEAT`, `FUNDING_FEAT`, etc. — need collectors to populate | 8h |
| E102 | Yahoo Finance scraping for SPY/QQQ/VIX — no official API | P1 | Lines 52-58: fragile HTML/JSON parsing; rate limits | 8h |
| E103 | Coinbase volume_24h always 0 in market_feed.json | P1 | `fetch_coinbase_ticker` doesn't return 24h volume field | 2h |
| E104 | No request rate limiting / backoff in exchange_api | P1 | `http_get` has 10s timeout but no retry/backoff logic | 4h |
| E105 | feed_bridge makes 15+ sequential HTTP calls per run | P1 | Coinbase, OKX, Frankfurter, Yahoo×7, Blockchain, Fear&Greed — >30s total | 8h |
| E106 | No exchange failover (Kraken→Binance→Coinbase) | P1 | All three fetched but feed uses first available; no priority | 4h |
| E107 | Order book depth not fetched (only ticker) | P1 | `ob_imbalance`, `ob_depth_ratio` in feed are random | 16h |
| E108 | No margin/futures support | P1 | Only spot tickers; Polymarket/Kalshi are binary options, not crypto | 24h |
| E109 | Market controller (`market_controller`) called but not audited | P1 | Line 95-111 in room_feed_bridge calls it for Q-reward | 2h |
| E110 | No exchange-specific fee calculation in order path | P1 | `TAKER_FEE=0.001` in types.h but not used in execution | 4h |

### P2 - Minor
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| E201 | `feed_bridge` uses `malloc` for price array (43200 elements) | P2 | Line 483: `malloc(cap * sizeof(double))` — no free on error path | 1h |
| E202 | `feed_bridge` compiles but `feed_builder` also builds market_feed.json | P2 | Duplicate feed generation — which one runs? | 2h |
| E203 | `exchange_spread_pct` utility but unused in live path | P2 | Defined in exchange_api.c line 320 | 0.5h |

---

## 5. GENOME / DARWIN / NESTED HT TRAINING PIPELINE


---

## 5. GENOME / DARWIN / NESTED HT TRAINING PIPELINE

### P0 - Critical
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| G001 | **Darwin evolution NEVER RUNS** | P0 | `epoch=0, culled=0, cloned=0` in room_snapshot.json after 1,314 cycles; `room_darwin_evolve` never called from main loop | 8h |
| G002 | **Nested HT predictions constant 0.5** | P0 | `nested_prediction: 0.5000` in all market_feed.json; `compute_nested_prediction` returns 0.5 for all markets | 16h |
| G003 | **Genome training pipeline stale** | P0 | Last training May 30 (9 days ago); `trained_genes.bin` 372KB, `training_results.json` shows 10yr backtest but no recent runs | 16h |
| G004 | **Hot-reload directory scanned but genomes never written** | P0 | `room_darwin_save_elite` defined but never called; `ENGINE_<type>_N.bin` files don't exist in `multi_market/` | 4h |
| G005 | **Feature importance tracking disconnected** | P0 | `pos_contrib_total`/`neg_contrib_total` always 0 — `accuracy_scorer` outputs 0 predictions | 8h |
| G006 | **Manifold/Polymarket/Kalshi genomes don't exist** | P0 | `polymarket_genes.bin` only 286KB; no genomes for sports, weather, elections, stocks, etc. | 24h |

### P1 - Major
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| G101 | Darwin fitness uses `rand()` for mutation | P1 | `mutate_genome` uses `rand()` without seeding; non-deterministic evolution | 2h |
| G102 | Nested HT loads BTC candles only ("bitstamp_1min") | P1 | Line 142: `source='bitstamp_1min'` — only crypto data, no equity/forex/commodity | 16h |
| G103 | Nested HT macro features not populated | P1 | `LEVELS[3-5]` have `use_macro=1` but SP500, VIX, DGS10, Fed, CPI all 0 in feed | 8h |
| G104 | Cascade feature always 0.5 | P1 | `feats[11] = 0.5` in `compute_nested_prediction`; no level-below prediction passed | 4h |
| G105 | `room_darwin_evolve` every 100 trades but 0 trades executed | P1 | Line 158: `epoch = cycle / 100` — but `trade_count=0` so epoch never advances | 2h |
| G106 | Elite genomes saved to `ENGINE_<type>_N.bin` but trainer expects `room_genes_*.bin` | P1 | `save_elite` writes `ENGINE_CRYPTO_0.bin` but `multi_market_trainer` loads `room_genes_CRYPTO.bin` | 4h |
| G107 | No genome versioning / compatibility check | P1 | `Genome` struct changes break hot-reload; no schema version in .bin files | 4h |
| G108 | `multi_market_trainer` uses CSV files from May | P1 | `STOCKS_DIR`, `FOREX_DIR` point to historical/raw/ — no recent data | 8h |
| G109 | Nested HT `lr_predict` uses raw features without standardization | P1 | Line 111-115: no `standardize_x` call during inference (only in training) | 2h |
| G110 | No validation split / overfitting detection in training | P1 | 20 epochs on all data; no early stopping, no holdout | 8h |

### P2 - Minor
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| G201 | `rand()` in `nested_ht_train` for weight init | P2 | Line 105: `(rand()/(double)RAND_MAX - 0.5) * 0.1` — no seed | 1h |
| G202 | Elite genome file format: `Genome` + `int mtype` — no metadata | P2 | No timestamp, no fitness score, no generation in .bin | 2h |
| G203 | `room_darwin_compute_diversity` O(n²) pairwise distance | P2 | 100 agents sampled, 500 pairs — called every cycle? | 2h |
| G204 | No checkpoint/resume for 20-epoch training | P2 | Training crashes = restart from epoch 0 | 4h |

---

## 6. RISK / CAPITAL / POSITION SIZING LOGIC


---

## 6. RISK / CAPITAL / POSITION SIZING LOGIC

### P0 - Critical
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| R001 | **Kelly sizing implemented but agents have 50% WR = negative Kelly** | P0 | Lines 340-350: `kelly_f = win_rate_ema - 0.5` → for 50% WR, Kelly = 0 → stake *= 0.25 | 4h |
| R002 | **Circuit breaker has 8 triggers but NEVER fires** | P0 | `check_circuit_breaker_before_trade` called but `trades=0` so daily_pnl=0, consec_losses=0 | 2h |
| R003 | **PDT enforcement limits agents <$25K to 3 trades/5 days** | P0 | All agents have capital=$50 → 3 day trades max in 5 days; trading halted | 4h |
| R004 | **Duplicate trade detection uses 1024-entry ring buffer** | P0 | Line 145: O(1024) scan per vote; high-frequency = O(n²) | 2h |
| R005 | **Correlation exposure uses `cross_room_correlation` which is ALL ZEROS** | P0 | Line 175: `float corr = s->cross_room_correlation[a][asset_id];` — never populated | 8h |
| R006 | **Position liquidation marks exposure=0 but NO actual PnL calculation** | P0 | `liquidate_position` line 229: `s->asset_exposure[asset_id][direction] = 0.0f;` — just zeros out | 4h |
| R007 | **Max exposure limits: 10% room, 25% correlated, but no per-agent cap** | P0 | Agent can bet 50% of own capital (line 358); no per-agent limit enforced | 4h |

### P1 - Major
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| R101 | Kelly criterion uses genome-evolved `position_size` as starting stake | P1 | Line 336: `stake = position_size * a->capital` — then Kelly caps it; backwards | 2h |
| R102 | Half-Kelly in volatile regime but regime_indicator = 0 or 1 (never 2) | P1 | Line 353: `if (predicted_regime == 2)` but regime is 0/1 only | 2h |
| R103 | Gas fee $2.50 blocks trades < $5 — but min trade is $5 | P1 | Line 364: `if (GAS_FEE_EST > stake * 0.5f) continue;` — kills all small trades | 1h |
| R104 | Circuit breaker cooldown decrements by 1 per CALL not per cycle | P1 | Line 54: `s->circuit_breaker_cycles--` called per trade attempt, not per time | 2h |
| R105 | `max_position_pct_room` defined in RoomState line 381 but NEVER checked | P1 | RoomState has field but `room_capital_apply` never reads it | 2h |
| R106 | Directional exposure (C36) tracked but no rebalancing action | P1 | Lines 127-138: tracks yes/no exposure but only triggers circuit breaker | 4h |
| R107 | `exchange_fees[MAX_ASSETS]` and `exchange_min_orders` never populated | P1 | Lines 200-213: defaults to constants; per-asset overrides don't exist | 2h |
| R108 | Room-level trade (`room_trade`) $50 seed but never resolved | P1 | `room_capital.c` doesn't handle `room_trade` resolution; 0 room trades | 2h |
| R109 | Vote conviction drives stake but conviction = sigmoid(signal) with random dropout | P1 | Line 99: `if (rand() < 0.15f) continue;` — 15% features randomly dropped | 2h |
| R110 | Slippage model defined (T20) but not applied in P2P matching | P1 | `SLIPPAGE_BPS=5` in types.h; P2P matching uses 0.5 entry price | 4h |

### P2 - Minor
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| R201 | `get_min_order` returns `MIN_TRADE_STAKE=5` but per-asset never set | P2 | Line 208-213 | 1h |
| R202 | Liquidation sweep scans ALL agents for EVERY breach | P2 | Lines 248-291: O(n_agents × n_rooms × MAX_ASSETS) | 4h |
| R203 | Trade key uses 33-bit agent shift + 32-bit ts + 1-bit direction | P2 | Line 31: potential collision if agent_id > 2^31 | 1h |

---

## 7. ACCURACY / OUTCOME SCORING LOOP


---

## 7. ACCURACY / OUTCOME SCORING LOOP

### P0 - Critical
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| A001 | **outcomes.db is EMPTY (0 bytes, no tables)** | P0 | `/money-room/data/outcomes.db` = 0B; `accuracy_scorer` outputs 0 predictions | 4h |
| A002 | **No `predictions` table in timeline.db** | P0 | `sqlite3 timeline.db .tables` shows only 6 tables — no predictions | 4h |
| A003 | **Trade resolution exists but outcomes never persisted** | P0 | `room_capital_resolve` updates `won`/`pnl_pct` in TradeRecord but never writes to outcomes DB | 8h |
| A004 | **Feature importance tracking has 0 data** | P0 | `pos_contrib_total` = 0 for all 34 features — no resolved trades feed back | 4h |
| A005 | **Brier score / calibration = 0** | P0 | `accuracy_scorer` runs but finds no data | 2h |
| A006 | **No prediction logging at vote time** | P0 | `room_vote_run` produces conviction but never stores `(agent, prob, outcome)` for scoring | 4h |
| A007 | **Paper engine has 31,965 trades but 0 accuracy score** | P0 | `paper_engine_service.log`: 32.25M cycles, 31,965 trades — `accuracy_scorer` = 0 | 8h |

### P1 - Major
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| A101 | `accuracy_scorer` looks for `outcomes.db` or `timeline.db` with `predictions` table | P1 | Neither exists; schema mismatch | 4h |
| A102 | `room_capital_resolve` does SGD updates but only on resolved trades | P1 | 0 resolved trades = 0 SGD updates | 2h |
| A103 | Conviction accuracy tracking (C10) uses `last_conviction` but never logs | P1 | Lines 575-593: updates `conv_hi_wins` etc. but no persistence | 2h |
| A104 | Feature importance (P16) updated in `room_capital_resolve` line 680 | P1 | Never triggered — needs resolved trades | 2h |
| A105 | `lr_decay` computed but `SGD_BATCH_SIZE=8` never reached | P1 | 0 trades = 0 batch accumulation | 2h |
| A106 | No cross-validation / walk-forward testing | P1 | Training on all data, no holdout | 8h |
| A107 | `win_rate_var` (C30) tracked but never used for agent selection | P1 | Computed line 616 but only used in `agent_fitness` penalty | 2h |

### P2 - Minor
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| A201 | `accuracy_scorer` uses fragile `atof`/`atoi` on SQL results | P2 | No error handling for NULL values | 1h |
| A202 | No prediction confidence bins for calibration curve | P2 | Only binary accuracy and Brier | 2h |
| A203 | `calibration_error = fabs(accuracy - brier)` is not proper calibration | P2 | Should use reliability diagram / isotonic regression | 4h |

---

## 8. CRON / SCHEDULER / ORCHESTRATION LAYER


---

## 8. CRON / SCHEDULER / ORCHESTRATION LAYER

### P0 - Critical
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| S001 | **90% of Hermes cron jobs are PAUSED** | P0 | Jobs.json: 40 jobs, ~35 `enabled: false` / `state: paused` | 2h |
| S002 | **Only 5-6 cron jobs actually running** | P0 | `stipend-tracker`, `gdelt-sentiment`, `heartbeat`, `marketstack-collector`, `twelvedata-collector`, `openmeteo-collector`, `stockdata-collector`, `finnhub-collector`, `changelog-generator`, `risk-analytics`, `resource-monitor` = 11 running | 4h |
| S003 | **Critical collectors PAUSED: monkey-runner, money-loop, poly-collector** | P0 | `monkey-runner` (1715 runs), `pm-money-loop`, `poly-collector`, `monkey-deploy-data`, `register-processor` all paused | 2h |
| S004 | **Stress test, survival stats, param sweep ERRORING** | P0 | `stress-test-run`, `survival-stats-run`, `param-sweep-run` all `last_status: error` | 4h |
| S004b | **Key rotation binary missing** | P0 | `key_rotation` binary doesn't exist; wrapper fails with code 127 | 1h |
| S005 | **Coingecko fallback binary missing** | P0 | `coingecko_fallback` binary doesn't exist; wrapper fails | 1h |
| S006 | **Data gap alerter firing: 7 sources stale** | P0 | `data-gap-alerter` shows CRIT: Yahoo, NewsGDELT, CBOE, FearGreed, Forex, FRED | 4h |
| S007 | **No trading engine cron** | P0 | No cron runs `cycle_all_rooms` or `cycle_all_rooms_parallel` live | 2h |
| S008 | **Paper orchestrator runs hourly but no live loop** | P0 | `paper_orchestrator` cron runs but `paper_engine_service` is separate process | 4h |

### P1 - Major
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| S101 | No cron for `feed_builder` / `feed_bridge` | P1 | Market feed never refreshed automatically | 2h |
| S102 | No cron for `room_darwin_save_elite` / genome retrain | P1 | Last training May 30; no automated retrain | 4h |
| S103 | No cron for `accuracy_scorer` / outcome logging | P1 | Outcomes never scored | 2h |
| S104 | `collector_runner` tasks hardcoded shell wrappers | P1 | `.sh` scripts call C binaries; no direct C scheduling | 8h |
| S105 | `timeline-collector` paused but monkey-runner also paused | P1 | Data collection completely stopped | 2h |
| S106 | `btc-wallet-monitor` / `polygon-wallet-monitor` paused | P1 | No wallet monitoring for live trading | 2h |
| S107 | No dependency graph between cron jobs | P1 | `room_feed_gen` needs manifold data but no scheduler awareness | 4h |
| S108 | Cron output logs growing unbounded | P1 | `/home/wubu2/.hermes/cron/output/` — no rotation | 2h |
| S109 | `webhook-subscriptions` not configured | P1 | No event-driven triggers | 4h |
| S110 | `paper_orchestrator` uses `deepseek` model but default is `nvidia/nemotron` | P1 | Inconsistent model config | 1h |

### P2 - Minor
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| S201 | Cron jobs use `model: deepseek/deepseek-v4-flash:free` but banned | P2 | Memory says NEVER use DeepSeek | 1h |
| S202 | `risk-analytics-cron` runs every 15min but risk_report is fast | P2 | Overkill frequency | 1h |
| S203 | `telegram-alert-hourly` reads files but `health-telegram-alert` every 10m | P2 | Redundant alerting | 1h |

---

## 9. PAPER ↔ LIVE PARITY & SYNC


---

## 9. PAPER ↔ LIVE PARITY & SYNC

### P0 - Critical
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| P001 | **Paper engine marked MISSING in pipeline status** | P0 | `pipeline_status.json`: `engine_paper` status=`missing`, healthy=`false` | 4h |
| P002 | **Paper engine runs DIFFERENT binary (`room_engine_paper`)** | P0 | `paper_orchestrator` launches `room_engine_paper`; live uses `room_engine` | 8h |
| P003 | **Paper uses `PAPER_MAX_CYCLES` env var; live has no equivalent** | P0 | Different code paths — paper has cycle limit, live runs forever | 2h |
| P004 | **Paper portfolio (Polymarket) has 101 open positions, $0 realized** | P0 | `paper_portfolio.json`: 101 trades, all `status: open`, `cash: $0.36` | 8h |
| P005 | **Paper engine cycles: 32M vs Live: 1,314 — 24,000x difference** | P0 | Different time scales; paper replays history, live processes real-time | 16h |
| P006 | **No paper→live sync mechanism** | P0 | Genomes trained in paper never deployed to live; no model registry | 16h |
| P007 | **Paper Sharpe = -9.2, Live Sharpe = 0 (no trades)** | P0 | `pipeline_status.json`: paper sharpe=-9.2; live 0 trades | 4h |
| P008 | **Paper uses `PAPER_PACE_NS = 5ms`; Live uses `LIVE_PACE_NS = 1s`** | P0 | Paper runs 200x faster — no temporal parity | 2h |
| P009 | **Paper state file separate: `room_state_paper.bin` vs `room_state.bin`** | P0 | Two independent state files; no convergence | 4h |

### P1 - Major
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| P101 | Paper trades 31,965 but accuracy_scorer = 0 | P1 | Paper trades resolved but never written to outcomes DB | 4h |
| P102 | Paper engine paper_orchestrator runs hourly via cron | P1 | Cron runs `paper_orchestrator 1000` but paper_engine_service runs continuously | 2h |
| P103 | Paper uses `MIN_TRADE_STAKE=5` same as live but no fee/slippage parity | P1 | Paper `TAKER_FEE=0.001` but live exchange fees unknown | 2h |
| P104 | Paper `GAS_FEE_EST=2.50` hardcoded; live on-chain costs variable | P1 | No dynamic gas estimation | 2h |
| P105 | Paper Darwin evolution runs but genomes never promoted to live | P1 | `room_darwin_save_elite` writes to `multi_market/` but live doesn't hot-load | 4h |
| P106 | Paper Tailslayer hedge activates (log shows `HEDGE ACTIVATED`) | P1 | Live never reaches tail risk threshold | 2h |
| P107 | Paper max agents can be configured; live fixed at 10,000 | P1 | Paper_orchestrator accepts max_cycles but not agent count | 1h |

### P2 - Minor
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| P201 | Paper output suppressed (redirected to /dev/null) | P2 | Line 71-76 in paper_orchestrator — no visibility | 1h |
| P202 | Paper init uses `setenv("ROOM_DIR")` but live uses global `g_room_dir` | P2 | Different environment initialization | 1h |
| P203 | No paper→live capital/position reconciliation | P2 | Paper capital $136K vs Live $500K — different scale | 2h |

---

## 10. MONITORING / ALERTING / AUTO-RECOVERY


---

## 10. MONITORING / ALERTING / AUTO-RECOVERY

### P0 - Critical
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| M001 | **health_check checks binaries that don't exist** | P0 | `health_check.c` lines 42-56: checks `btc_csv_refresher`, `pipeline_monitor` — neither binary exists | 2h |
| M002 | **health_alerter Telegram token from ENV but not set** | P0 | Line 36: `getenv(TELEGRAM_BOT_TOKEN_ENV)` — no token in environment | 1h |
| M003 | **health_alerter uses `system()` for curl — async-unsafe** | P0 | Line 51: `system(cmd)` in signal context potentially | 2h |
| M004 | **health_check process check uses `pgrep -x room_engine`** | P0 | Line 86: `pgrep -x` — but process is `room_engine_paper` for paper, `room_engine` for live | 1h |
| M005 | **data_quality_scorer queries `market_data` table — DOESN'T EXIST** | P0 | Line 43: `FROM market_data` — timeline.db has no `market_data` table | 4h |
| M006 | **data_quality sources hardcoded — don't match actual collectors** | P0 | Lines 98-104: 15 sources but actual collectors produce different names | 2h |
| M007 | **No auto-recovery on health failure** | P0 | `health_alerter` only alerts; no restart, no failover, no circuit breaker reset | 8h |
| M008 | **Circuit breaker alerts written but no auto-reset** | P0 | `health_alerter` clears alert on healthy but circuit breaker cycles count down per trade | 2h |
| M009 | **No SLA/SLO definitions or burn-rate alerting** | P0 | No latency budgets, error budgets, or multi-window alerting | 8h |
| M010 | **Resource monitor exists but not wired to auto-scale/kill** | P0 | `resource_monitor` runs but no action on OOM/CPU saturation | 4h |

### P1 - Major
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| M101 | `health_check` writes `docs/data/health.json` but dashboard not verified | P1 | File written but no consumer dashboard shown | 2h |
| M102 | `data_quality` age thresholds: <5min=1.0, <1hr=0.5, <4hr=0.2 | P1 | Arbitrary; no statistical basis | 2h |
| M103 | `resource_monitor.sh` cron runs every 5min but `health_check` every 6min | P1 | Inconsistent cadence | 1h |
| M104 | Alert history log grows unbounded | P1 | `health_alert_history.log` no rotation | 1h |
| M105 | No distributed tracing / correlation IDs | P1 | Can't trace trade from vote→resolve→PnL | 8h |
| M106 | `data_quality_scorer` exit code 1 on issues but cron ignores | P1 | `update` in Makefile runs it but no action on failure | 1h |
| M107 | No canary deployment / shadow traffic for new binaries | P1 | New engine versions deployed directly | 16h |
| M108 | `watchdog.py` cron paused — no process supervision | P1 | Job `cron-health-watchdog` paused | 1h |

### P2 - Minor
| ID | Component | Severity | Evidence | Fix Effort |
|----|-----------|----------|----------|------------|
| M201 | `health_check` checks `room_engine_v3` but live uses `room_engine` | P2 | Version mismatch | 1h |
| M202 | `data_quality` status strings `OK`/`WARN`/`STALE` don't match `health_check` | P2 | Inconsistent taxonomy | 1h |
| M203 | No alert deduplication / grouping | P2 | Each failure = separate alert | 4h |
| M204 | No on-call rotation / escalation policy | P2 | Single Telegram chat ID | 2h |

---

## MASTER GAP REGISTRY (All P0s by Component)
