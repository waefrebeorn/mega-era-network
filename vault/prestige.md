# PRESTIGE — Full Context Resume for New Session
June 8 2026 — STATE_VERSION=5, STATE_MAGIC=ROMB, Walkway v6.0

## MISSION
Paper trading system trained on historical data → picks best real-world winners → live profit.
All C. Zero Python. Zero delegation. 405 gaps cataloged, 38 critical (🔴) in TODO.

## PRIORITY QUEUE (Next Session Pick Order)

### 🔴 P0 — IMMEDIATE (Capital/Revenue Blocking)
| # | Gap | Task | Impact |
|---|-----|------|--------|
| 1 | A01 | ✅ SGD batch 8→64 done | Faster convergence |
| 2 | A22 | ✅ Max 3 open positions/agent done | Risk control |
| 3 | A14/A15 | ✅ Checkpointing + elite save verified | State survives crash |
| 4 | C03-C06 | ✅ Circuit breaker verified | Pre-trade risk controls |
| 5 | C17-C19 | ✅ Auto-kill verified (6-loss, WR<30%, capital<$1) | Agent death |
| 6 | B01 | ⏳ Prune 17 dead features (N_FEATURES 34→17) | Reduce noise |
| 7 | B02/B03 | ⏳ Populate DFT + phi-interval features | Better signals |
| 8 | C01 | ⏳ Runtime VaR computation | Risk visibility |
| 9 | F09 | ⏳ Automate database backup | Data safety |
| 10 | F10 | ⏳ State file corruption recovery | Crash recovery |
| 11 | A59/D37 | ⏳ Feed freshness check in engine | No stale trading |
| 12 | D32 | ⏳ Data freshness dashboard | Visibility |
| 13 | G01 | ⏳ Encrypt key storage | Security |
| 14 | G05/G06 | ⏳ Prompt injection + DA guard | Wallet safety |
| 15 | H21/H22 | ⏳ Terms + Privacy Policy | Legal |

### 🟡 P1 — FEATURE UNLOCKS
| # | Gap | Task |
|---|-----|------|
| 16 | A04/A05 | Walk-forward validation (80/20 split) |
| 17 | A10 | Cross-room ensemble prediction |
| 18 | C31 | T-tested edge verification per agent |
| 19 | D01 | Backfill capability for missing history |
| 20 | D03 | Real-time BTC data verification |
| 21 | F02 | CI/CD pipeline (GitHub Actions) |
| 22 | F07 | Fix resource_monitor.sh |
| 23 | F19 | Automated regression test suite |
| 24 | G14 | State file integrity verification |
| 25 | I01 | Payment processor (LemonSqueezy) |

### 🔴 CRITICAL DA FINDINGS (This Session)
1. **Telegram spam** — All 108 cron jobs audited, 22 paused, all `origin`/`telegram` delivery switched to `local`
2. **Fee realism** — TAKER_FEE was 0.1% (14 files), fixed to 0.26% (Kraken)
3. **SEED_CAPITAL** — Was $1000, fixed to $50 (paper reality)
4. **Regime gating** — Was adversarial (-0.28 importance), disabled
5. **Epsilon** — Was decaying to 0.005, fixed to permanent 0.10
6. **Darwin fitness** — Rewritten to WR×√trades×log(capital)
7. **Feature pruning** — Dead features auto-zeroed after 100 trades
8. **Position limits** — Max 3 open positions per agent
9. **State version** — Bumped to v5, migration handles v4→v5
10. **500 gaps** — 405 cataloged in 9 domains, 38 critical

### NAME PARITY ISSUE
- Old battleship used T### IDs (T064, T074, T101, T441a/b) — 86 cells
- New registry uses A01-I30 IDs — 405 cells
- **Resolution**: New registry supersedes old. Old T### IDs are historical.
- **Third-party plugins**: libcurl, jansson, sqlite3 only. No exotic deps.
- All gaps are valid and can be made in C code.

## SYSTEM STATE SNAPSHOT
| Component | Version/Status | Path |
|-----------|----------------|------|
| Engine | v6.0, STATE_V5 | engine/room_engine.c |
| Darwin | Epoch 6,410 | room_state.bin (v4, migrates to v5 on load) |
| Cycles | 2.8M+ | room_state.bin |
| Trades | 83K+ | room_state.bin |
| WR | 50.8% | room_state.bin |
| Sharpe | -82.56 (BUG: ring buffer not sliding) | room_state.bin |
| Agents | 2,500 | room_state.bin |
| Features | 34 (17 near-zero) | engine/types.h |
| Collectors | 60+ C binaries | engine/*_collector.c |
| Cron jobs | 108 total, 22 paused | ~/.hermes/cron/ |
| Telegram spam | STOPPED | All delivery→local |

## FILES CREATED/SESSION
- `DA_AUDIT_JUNE8_2026.md` — 9-pass DA audit findings
- `500_GAP_REGISTRY.md` — 405 gaps across 9 domains
- `engine/cycle_all_rooms_parallel.c` — Parallel room cycling
- 12 shell scripts for no_agent cron jobs

## COMMITS THIS SESSION
- `1b02de0` — DA Pass 1-9: fee, seed, epsilon, regime, darwin, pruning
- `8cc8f9a` — A01 + A22 + STATE_V5 migration
- `05a771c` — DA audit doc
- `500_GAP_REGISTRY.md` commit

## NEXT SESSION
Pick up from B01 (prune dead features) and continue the 🔴 critical gap loop.
