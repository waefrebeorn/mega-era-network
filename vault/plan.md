# PLAN — Money Room Walkway

## Current Focus
Running perpetual gap-closing loop against vault/battleship-ultimate.md (365 cells).

## Completed
1. ~~A01: No SGD weight update loop~~ — DONE: added BCE gradient descent to multi_market_trainer.c
2. ~~A02: Darwin never fires in any room~~ — DONE: fixed infinite-dup-sleep loop (room_engine.c:701-708) + trade_count cross-cron persistence (room_engine.c:657)
3. ~~A03: All 16 rooms identical binary~~ — DONE: confirmed by-design architecture, not a gap
4. ~~A04 (partial): snapshot display~~ — DONE: room_bridge.c:56-60 %.2f→%.4f precision
5. ~~A05: BTC-clone in eco/macro~~ — DONE: FALSE CLAIM, feeds use sp500 correctly
6. ~~A06: Feed generator may not work~~ — DONE: FALSE CLAIM, feed_gen runs correctly with valid output
7. ~~A10: Trainer not wired into cron~~ — DONE: FALSE CLAIM, verified crontab daily+15min
8. ~~A48: Darwin epoch=0 in snapshot~~ — DONE: resolved by A02 trade_count persistence
9. ~~B01: N_FEATURES=18 ~10 populated~~ — DONE: FALSE CLAIM, all 18 computed
10. ~~B03: phi features uninitialized~~ — DONE: FALSE CLAIM, compute_phi_features called
11. ~~C03: Circuit breaker never triggered~~ — DONE: force-resolve room trades on dup-exit (room_engine.c:705-736)
12. ~~B44: Feed staleness check~~ — DONE: tightened LIVE_MODE thresholds — WARN >5min, REJECT >1h (room_feeds.c:248-278)

## Next Cell
D01: timeline.db only has 21-33 rows per ticker
