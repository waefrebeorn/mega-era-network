# STATE — Money Room Walkway

## Current Status
- **Engine:** 210 C files, 10K agent paper trading (2500 active), 17 markets
- **Website:** GH Pages at waefrebeorn.github.io/money-room/, data_server port 9090
- **Battleship:** 365 cells across 9 domains (1 🔴 P0, 129 🟡 P1, 226 ⚪ P3)
- **Rooms:** 16 configured. market_type set per room. SIGMA_NORMALIZER fixed (0.001→0.15). 98% agent voting. OB + CVD + 7 derived + time features wired. N_FEATURES=29.
- **Data:** 14 JSON feeds serving live, 25+ collectors, timeline.db backfill underway (33K+ rows), orderbook_depth/cumulative_volume_delta live
- **Latest batch:** B02 (persistent price_history for DFT), B05 (orderbook features), B06 (CVD), C25 (panic stop), F14 (Telegram health alerts), A38 (min sample filter), A16 (feature pruning). 12 stale claims vaulted. 46 P1 gaps closed.

## Gap Map Available
- `vault/battleship-ultimate.md` — 365 cells (training, features, risk, data, execution, infra, security, website, monetization)
- `vault/homework-list.md` — 65 human tasks in 3 tiers
- `vault/go-mantra.md` — compact pasteback for loop

## Top 🔴 P0 Killers
1. ~~No SGD weight update loop (A01)~~ — DONE
2. ~~Darwin never fires (A02)~~ — DONE
3. ~~Identical binary~~ — DONE: by design
4. ~~BTC-clone in eco/macro~~ — DONE
5. ~~Feed generator works~~ — DONE
6. ~~Trainer wired into cron~~ — DONE
7. ~~Darwin epoch=0~~ — DONE
8. ~~N_FEATURES<18~~ — DONE
9. ~~Fake prices (A04)~~ — DONE: Manifold data
10. ~~Walk-forward (A11)~~ — DONE: `--validate`
11. ~~OOS test set (A12)~~ — DONE
12. ~~MARKET_TYPE (A31)~~ — DONE: already in engine
13. ~~VaR (C01)~~ — DONE: JSON + cron
14. ~~SIGMA_NORMALIZER 0.001→0.15~~ — DONE: agents now feature-driven
15. ~~market_type null in feeds~~ — DONE: market_type set per room
16. **No loss feedback loop** — Engine trades not fed back to trainer
17. Polymarket CLOB (E04) — blocked on $50 USDC
