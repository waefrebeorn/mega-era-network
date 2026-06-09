# Plan — Money Room Walkway v6.0
June 8, 2026 — Phase: CRITICAL GAP CLOSURE (405 gaps, 38 critical)

## Current Phase: CRITICAL GAP CLOSURE

### COMPLETED THIS SESSION
- ✅ **Telegram spam fix** — All 108 crons audited, 22 paused, all delivery→local
- ✅ **DA Pass 1-9** — Fee realism, seed capital, epsilon, regime, darwin, pruning
- ✅ **A01: SGD batch 8→64** — Faster convergence
- ✅ **A22: Max 3 open positions/agent** — Risk control + STATE_V5 migration
- ✅ **A14/A15: Checkpointing** — Verified (every 1000 cycles)
- ✅ **C03-C06: Circuit breaker** — Verified (5-layer pre-trade)
- ✅ **C17-C19: Auto-kill** — Verified (6-loss, WR<30%, capital<$1)
- ✅ **500 gap registry** — 405 gaps in 9 domains
- ✅ **Walkway files updated** — state.md, plan.md, prestige.md

### SYSTEM STATE
- Engine: 2.8M+ cycles, 83K+ trades, 2,500 agents, 50.8% WR
- STATE_VERSION=5, STATE_MAGIC=ROMB
- 34 features (17 near-zero, need pruning)
- 60+ C collector binaries
- 86 active cron jobs (22 paused, all silent)
- Third-party: libcurl, jansson, sqlite3 only

### 🔴 CRITICAL GAPS (38 total, 5 done, 33 remaining)

**Training Engine (7 remaining):**
- A04/A05: Walk-forward validation
- A10: Cross-room ensemble
- A59: Feed freshness check

**Features (5 remaining):**
- B01: Prune dead features
- B02/B03: Populate DFT + phi features
- B37: Feature staleness detection
- B44: Reject stale feed

**Risk (6 remaining):**
- C01: Runtime VaR
- C04: Max drawdown enforcement (per-agent)
- C05: Daily loss limit (per-agent)
- C06: Max concentration check (per-agent)
- C31: T-tested edge verification

**Data (4 remaining):**
- D01: Backfill capability
- D03: Real-time BTC verification
- D32: Data freshness dashboard
- D37: Feed age check in engine

**Execution (1 remaining):**
- E01: Live exchange API bridge

**Infrastructure (5 remaining):**
- F02: CI/CD pipeline
- F07: Fix resource_monitor.sh
- F09: Database backup
- F10: State corruption recovery
- F19: Automated test suite

**Security (4 remaining):**
- G01: Encrypt key storage
- G05: Prompt injection guard
- G06: DA guard on wallets
- G14: State file integrity

**Website (2 remaining):**
- H21: Terms of Service
- H22: Privacy Policy

**Monetization (1 remaining):**
- I01: Payment processor (LemonSqueezy)

### RULES
- All C. No Python. libcurl + jansson + sqlite3.
- NO delegation (delegate_task banned permanently).
- Read walkway → pick lowest undone cell → fix → update → push → loop.
- Zero Telegram noise (all cron deliver=local).
- Always show results after tool calls (no silent replies).
- Update battleship after each cell closed.
- Push after each batch (5-10 cells).
- NAME PARITY: New A01-I30 IDs supersede old T### IDs. Old battleship archived.
