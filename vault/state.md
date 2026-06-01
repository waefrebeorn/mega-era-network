# STATE — Money Room Walkway

## Current Status
- **Engine:** 210 C files, 10K agent paper trading (2500 active), 17 markets
- **Website:** GH Pages at waefrebeorn.github.io/money-room/, data_server port 9090
- **Battleship:** 365 cells across 9 domains (2 🔴 P0, 185 🟡 P1, 170 ⚪ P3)
- **Rooms:** 16 configured. Walk-forward validation live (avg OOS 66.8%). VaR/ES cron'd every 15min.
- **Data:** 14 JSON feeds serving live, 25+ collectors, timeline.db has 21-33 rows/ticker (D01 verified, D02 backfill added)
- **Latest batch:** C01 VaR/ES cron'd. Active P0: 2. P0: 22→14→12→11→2.

## Gap Map Available
- `vault/battleship-ultimate.md` — 365 cells (training, features, risk, data, execution, infra, security, website, monetization)
- `vault/homework-list.md` — 65 human tasks in 3 tiers
- `vault/go-mantra.md` — compact pasteback for loop

## Top 🔴 P0 Killers
1. ~~No SGD weight update loop (A01)~~ — DONE: added BCE gradient descent to multi_market_trainer
2. ~~Darwin never fires in any room (A02)~~ — DONE: dup-timestamp loop exit + trade_count persistence
3. ~~All 16 rooms identical binary~~ — DONE: by design architecture
4. ~~BTC-clone in eco/macro~~ — DONE: false claim, sp500 correct
5. ~~Feed generator may not work~~ — DONE: false claim, works correctly
6. ~~Trainer not wired into cron~~ — DONE: false claim, daily+15min cron
7. ~~Darwin epoch=0~~ — DONE: resolved by A02 fix
8. ~~N_FEATURES<18~~ — DONE: all 18 computed
9. ~~7 rooms on fake 0.50 prices (A04)~~ — DONE: Manifold data wired
10. ~~No walk-forward validation (A11)~~ — DONE: `--validate` flag, 5-fold expanding window
11. ~~No out-of-sample test set (A12)~~ — DONE: resolved by A11 same fix
12. ~~No MARKET_TYPE selection at runtime (A31)~~ — STILL OPEN
13. ~~No VaR computation in engine runtime (C01)~~ — DONE: JSON + cron every 15min
14. ~~Polymarket CLOB (E04)~~ — blocked on $50 USDC deposit (external)
