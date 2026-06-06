# Plan — Money Room Walkway v5.7
June 6, 2026 — Phase: FEATURE UNLOCKS + REVENUE READINESS

## Current Phase: FEATURE UNLOCKS + REVENUE PIPELINE

### COMPLETED THIS SESSION
- ✅ **R1-R3: Regulatory disclaimers** on ALL performance metric pages (7 pages)
- ✅ **T441: SEC EDGAR 13F+Form 4 scraper** — 13f_holdings.c + insider_trades.c built, tested, cron'd daily
- ✅ **R4: 5-layer circuit breaker** — Pre-trade risk controls in room_capital.c
- ✅ **Walkway files updated** — state.md, plan.md, prestige.md, battleship-ultimate.md, index.md, entry.md

### SYSTEM STATE
- timeline.db: 2.4GB, 2M+ rows (17 years: 2011-2028)
- historical.db: 503 SP500, 2.5K daily, 50K multi-exch
- Engine: 229K cycles, 218K trades, 2,500 agents, $124K cap
- 80 features defined: ~28 work, ~52 need data wiring
- Website live at waefrebeorn.github.io/money-room
- Cost: $0/month (all free APIs)
- STATE_VERSION=4, STATE_MAGIC=ROMB

### 🔴 IMMEDIATE P0 (Next Session)
**Priority order by impact/hour:**

| # | Cell | Task | Est Time | Impact |
|---|------|------|----------|--------|
| 1 | **R4** | **DONE** — 5-layer circuit breaker: daily loss, consecutive losses, drawdown, exposure, panic stop | — | ✅ Pre-trade risk controls implemented |
| 2 | R5-R8 | Privacy policy, ToS, GDPR/CCPA consent, scraping ToS | 2h | 🔴 Legal risk |
| 3 | T712 | LemonSqueezy integration + PayPal fallback | 4h | 🔴 First $ revenue |
| 4 | T483-T490 | Kelly sizing + VaR + volatility sizing | 2h | ⏳ Build |

### 🟡 P1 (After P0)
| # | Cell | Task | Est Time | Status |
|---|------|------|----------|--------|
| 5 | T256 | Finnhub earnings calendar | 0.5h | ⏳ Needs API key |
| 6 | T542 | Encrypted secrets vault (libsodium) | 1h | ⏳ Build |
| 7 | T543 | Order management system | 2h | ⏳ Build |
| 8 | T624-T632 | Monitoring dashboard + Telegram heartbeat | 2h | ⏳ Build |
| 9 | T721-T730 | Open-Meteo weather (global) | 1h | ✅ Mostly done |

### 🟢 P2 (After P1)
| # | Cell | Task | Est Time |
|---|------|------|----------|
| 10 | T752-T770 | Test suite — unit tests for core engine | 3h |
| 11 | T842-T876 | Agent architecture upgrades | 4h |

### REGULATORY FIXES (Interleaved with Tech)
| # | Fix | Cell | When |
|---|-----|------|------|
| R5 | Document scraping ToS per source | T1354 | Before any scraping |
| R6 | Implement order audit trail | T569 | Before real money |
| R7 | SEC AI-washing language audit | T724 | Before public launch |
| R8 | GDPR/CCPA consent flow | T723 | Before user accounts |

### RULES
- All C. No Python. libcurl + jansson + sqlite3.
- NO delegation (delegate_task banned permanently).
- Read walkway → pick lowest undone cell → fix → update → push → loop.
- Zero Telegram noise (all cron deliver=local).
- Always show results after tool calls (no silent replies).
- Update battleship after each cell closed.
- Push after each batch (5-10 cells).