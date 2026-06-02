# STATE — Money Room Walkway

## Current Status
- **Engine:** 210 C files, 10K agent paper trading (2500 active), 17 markets
- **Website:** GH Pages at waefrebeorn.github.io/money-room/, data_server port 9090
- **Battleship:** 365 cells across 9 domains (1 🔴 P0, 73 🟡 P1, 181 ⚪ P3)
- **Rooms:** 16 configured. market_type set per room. 34 features. Warm-start from elite genomes. Daily loss limit (C05). Cycle count persists (A57). Heartbeat/alert (A58). Feature staleness (B37). Epsilon-greedy exploration (A30). Multi-objective Darwin fitness (A40). Elite preservation (A52). Per-cycle metrics JSONL (A56). Input validation G11. MIN_TRADE_STAKE=$5 (C15). Take-profit at room level (C35). Anomaly detection on feeds (D38). State version migration (F11). Room config validation (A54). Flash crash simulation (C09). Data freshness dashboard (D34).
- **Data:** 14 JSON feeds, 25+ collectors, timeline.db backfill, orderbook/CVD live
- **Latest batch:** D34 data freshness script, C09 flash crash scenario, A54 config validation, F11 state migration, D38 anomaly detection, C35 take-profit. P1: 77→73.
- **Structure:** STATE_MAGIC=ROMB (0x524F4D42), STATE_VERSION=3, STATE_CRC active

## Gap Map Available
- `vault/battleship-ultimate.md` — 365 cells (training, features, risk, data, execution, infra, security, website, monetization)
- `vault/homework-list.md` — 65 human tasks in 3 tiers
- `vault/go-mantra.md` — compact pasteback for loop

## Session Progress
- Started: 95 P1, 228 P3 (323 undone)
- Closed: 22 P1, 47 P3 (69 total closed)
- Current: 73 P1, 181 P3 (254 undone)
- Engine compiles clean, no regressions
- 15 code commits + 8 doc commits this session
