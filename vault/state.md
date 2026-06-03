# STATE — Money Room Walkway

## Current Status
- **Engine:** 210+ C files, 10K agent paper trading (2500 active), 17 markets
- **Website:** GH Pages at waefrebeorn.github.io/money-room/, data_server port 9090
- **Battleship:** 365 cells across 9 domains (1 🔴 P0, 64 🟡 P1, 177 ⚪ P3)
- **Structure:** STATE_MAGIC=ROMB (0x524F4D42), STATE_VERSION=3, STATE_CRC active
- **N_FEATURES:** 34

## Key Systems
- Room engine: paper P2P trading, 34 features, epsilon-greedy, multi-objective Darwin, elite preservation
- Risk: circuit breaker, daily loss limit, take-profit, directional exposure limits, win-rate/capital auto-kill
- Data: 14 JSON feeds, 25+ collectors, timeline.db, orderbook/CVD live, anomaly detection
- Infra: Docker, CI/CD (GitHub Actions), systemd service, Telegram alerts, logrotate, data archive/retention
- Tests: test_runner (11 tests, 9 pass), stress_test (5 scenarios), ablation_test, cross_source_check

## Session Progress
- Started: 77 P1, 183 P3 (260 undone)
- Closed: 13 P1, 6 P3 (19 total closed)
- Current: 64 P1, 177 P3 (241 undone)
- Engine compiles clean, no regressions
- Push timing out — repo too large. 8+ commits pending push.
