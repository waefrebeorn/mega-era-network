# STATE — Money Room Walkway

## Current Status
- **Engine:** 210+ C files, 10K agent paper trading (2500 active), 17 markets
- **Website:** GH Pages at waefrebeorn.github.io/money-room/, data_server port 9090
- **Battleship:** 365 cells across 9 domains (1 🔴 P0, 58 🟡 P1, 176 ⚪ P3)
- **Structure:** STATE_MAGIC=ROMD (0x524F4D44), STATE_VERSION=3, STATE_CRC active
- **N_FEATURES:** 34

## Key Systems
- Room engine: paper P2P trading, 34 features, epsilon-greedy, multi-objective Darwin, elite preservation
- Risk: circuit breaker, daily loss limit, take-profit, directional exposure limits, win-rate/capital auto-kill, correlation-based position limits, duplicate trade detection
- Data: 14 JSON feeds, 25+ collectors, timeline.db, orderbook/CVD live, anomaly detection, data quality scoring, cross-source consistency validation
- Infra: Docker, CI/CD (GitHub Actions), systemd service, Telegram alerts, logrotate, data archive/retention, state rollback snapshots
- Tests: test_runner (15 tests, 10 pass), stress_test (5 scenarios), ablation_test, cross_source_check

## Session Progress
- Started: 64 P1, 177 P3 (241 undone)
- Closed: 6 P1, 2 P3 (8 total closed this batch)
  - C07 correlation-based position limits (types.h + room_capital.c)
  - C23 duplicate trade detection (room_capital.c)
  - C24 cross-room correlation tracking (room_capital.c)
  - D35 data quality scorer (data_quality_scorer.c)
  - D36 data consistency validation (room_capital.c)
  - D44 exchange fee table (types.h + room_capital.c)
  - D47 trade history DB (trade_history_db.c)
  - F12 state rollback (state_rollback.c)
- Current: 58 P1, 176 P3 (234 undone)
- Engine compiles clean (room_engine + room_engine_paper), 10/15 tests pass (5 pre-existing failures)
- STATE_MAGIC bumped ROMC→ROMD for new RoomState fields
