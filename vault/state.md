# STATE — Money Room Walkway

## Current Status
- **Engine:** 210 C files, 10K agent paper trading (2500 active), 17 markets
- **Website:** GH Pages at waefrebeorn.github.io/money-room/, data_server port 9090
- **Battleship:** 365 cells across 9 domains (23 🔴 P0, 172 🟡 P1, 170 ⚪ P3)
- **Rooms:** 16 configured. All same binary (same md5). 7 on fake data. Darwin.epoch=0.
- **Data:** 14 JSON feeds serving live, 25+ collectors, timeline.db has 21-33 rows/ticker (D01 verified, D02 backfill added)
- **Latest batch:** A04 real Manifold data (room_feed_gen), D01/D02 backfill, D39 false-claim closed. P0: 23→22.

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
9. 7 rooms on fake 0.50 prices (A04)
10. No live exchange API integration (E01-E04)
11. API keys in plaintext, no encryption (G01)
12. No prompt injection guard (G05)
13. No DA guard on wallet ops (G06)
14. LemonSqueezy KYC blocked (I01)
