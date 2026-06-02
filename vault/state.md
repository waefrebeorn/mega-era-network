# STATE — Money Room Walkway

## Current Status
- **Engine:** 210 C files, 10K agent paper trading (2500 active), 17 markets
- **Website:** GH Pages at waefrebeorn.github.io/money-room/, data_server port 9090
- **Battleship:** 365 cells across 9 domains (1 🔴 P0, 79 🟡 P1, 183 ⚪ P3)
- **Rooms:** 16 configured. market_type set per room. SIGMA_NORMALIZER fixed (0.001→0.15). 98% agent voting. 34 features. Warm-start from elite genomes. Daily loss limit (C05). Cycle count persists (A57). Heartbeat/alert (A58). Feature staleness (B37). Epsilon-greedy exploration (A30, ε=0.05→0.005). Multi-objective Darwin fitness (A40). Elite preservation (A52). Per-cycle metrics JSONL (A56). Input validation on market_feed.json (G11). MIN_TRADE_STAKE=$5 (C15).
- **Data:** 14 JSON feeds, 25+ collectors, timeline.db backfill, orderbook/CVD live
- **Latest batch:** C15 Polymarket min order ($5), A08 market calibration PORTED. P1: 80→79.

## Gap Map Available
- `vault/battleship-ultimate.md` — 365 cells (training, features, risk, data, execution, infra, security, website, monetization)
- `vault/homework-list.md` — 65 human tasks in 3 tiers
- `vault/go-mantra.md` — compact pasteback for loop

## Top Remaining P1 Gaps (79 total)
- E04: Polymarket CLOB — blocked on $50 USDC (external)
- A24: Transfer learning between market types
- A25: Ensemble prediction across rooms
- A29: Single-training-path bottleneck
- A32: Per-room loss function
- A46: HYBRID paper/live mode
- A54: Room config validation
- B13: On-chain features (MVRV/Puell)
- B32: Feature selection process
- B39: Continuous feature ID system
- C11: Position liquidation model
- C23: Duplicate trade detection
- C24: Market correlation across rooms
- C39: Portfolio-level VaR
- C40: Margin adequacy check
- D06-D11: Data pipelines (Coinbase, SP500, forex, commodity, bond, VIX)
- D12-D19: Economic data pipelines
- D20-D24: Prediction market data (external blocks)
- D26-D33: Alternative data pipelines
| D34-D38: Data quality/consistency
| D42-D44: API limitations
| D47-D48: Trade history/journal
| D52: Multi-timeframe data
| E01-E03: Live exchange integration (external/funding)
| E05-E07: Order types (paper-only limitation)
| E13-E15: Exchange auth/rate limits
| E18/E23/E25/E26: Execution modeling
| E30-E31: Exchange health/min sizes
| F01-F04: Infrastructure (Docker, CI/CD, build env, env management)
| F11-F14: State migration, rollback, monitoring, alerts
| F15: Systemd service
| F19-F20: Test suite, memcheck CI
| F30/F35: Runbook, dependency install
| G01-G04: Key management
| G05-G08: Security hardening
| G09-G11: Network/input validation (G10/G11 done)
| G20: HTTPS for data_server
| G21-G22: Key/SSH rotation
| G27-G29: Git security, 2FA, withdrawal allowlists
| H01-H28: Website/UI gaps (not C code)
| I01-I27: Monetization (external/KYC/website)

## Session Progress
- Started: 95 P1, 228 P3
- Closed: 16 P1, 45 P3 (61 total)
- Current: 79 P1, 183 P3
- Engine compiles clean, no regressions
