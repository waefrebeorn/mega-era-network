# PLAN — Money Room Walkway

## Current Focus
Running perpetual gap-closing loop against vault/battleship-ultimate.md (365 cells).

## Completed
1. ~~A01: No SGD weight update loop~~ — DONE: added BCE gradient descent to multi_market_trainer.c
2. ~~A02: Darwin never fires in any room~~ — DONE: fixed infinite-dup-sleep loop (room_engine.c:701-708) + trade_count cross-cron persistence (room_engine.c:657)
3. ~~A03: All 16 rooms identical binary~~ — DONE: confirmed by-design architecture, not a gap
4. ~~A04 (partial): snapshot display~~ — DONE: room_bridge.c:56-60 %.2f→%.4f precision

## Next Cell
A05: BTC-clone data fed to economic/macro rooms
