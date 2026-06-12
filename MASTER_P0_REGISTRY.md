# MASTER P0 GAP REGISTRY - 68 Critical Gaps

Generated from BATTLEFIELD_AUDIT.md

## Summary by Component

| Component | P0 Count | Est Hours |
|-----------|----------|-----------|
| C_ROOM Engine Core | 10 | 50 |
| 16 Room Directories | 8 | 88.5 |
| Data Collectors & Feed | 7 | 98 |
| Exchange Integration | 7 | 132 |
| Genome/Darwin/Nested HT | 6 | 76 |
| Risk/Capital/Position Sizing | 7 | 36 |
| Accuracy/Outcome Scoring | 7 | 34 |
| Cron/Scheduler | 10 | 26.5 |
| Paper/Live Parity | 9 | 64 |
| Monitoring/Alerting | 10 | 34 |
| **TOTAL** | **68** | **~609 hrs** |

## TOP 10 IMMEDIATE ACTIONS (Week 1)

1. S001 - Unpause critical cron jobs (2h)
2. D001 - Replace rand() in feed_builder with real data (40h)
3. G001 - Enable Darwin evolution trigger (8h)
4. C005 - Implement Kelly/position sizing (8h)
5. A001 - Create outcomes.db + predictions schema (4h)
6. E001 - Implement private exchange API + order execution (40h)
7. R003 - Remove/fix PDT enforcement for paper (2h)
8. R001 - Fix Kelly negative at 50% WR (4h)
9. S007 - Add live trading engine cron (2h)
10. M002 - Set TELEGRAM_BOT_TOKEN env var (1h)
