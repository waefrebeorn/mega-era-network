# Plan — Money Room Walkway v5.8
June 6, 2026 — Phase: REVENUE READINESS (Regulatory Complete)

## Current Phase: REVENUE PIPELINE (Regulatory Complete ✅)

### COMPLETED THIS SESSION
- ✅ **R1-R3: Regulatory disclaimers** on ALL performance metric pages (7 pages)
- ✅ **T441: SEC EDGAR 13F+Form 4 scraper** — 13f_holdings.c + insider_trades.c built, tested, cron'd daily
- ✅ **R4: 5-layer circuit breaker** — Pre-trade risk controls in room_capital.c
- ✅ **R5: Privacy Policy v2.0** — GDPR/CCPA: legal basis (Art. 6), retention, rights, SCCs, DPO
- ✅ **R6: Terms v2.0** — GDPR/CCPA: IP rights, governing law, rights acknowledgment
- ✅ **R8: Register consent flow** — 4 granular checkboxes, versioning, localStorage + server payload
- ✅ **Cookie consent banner** on all main pages (GDPR Art. 7)
- ✅ **Walkway files updated** — state.md, plan.md, prestige.md, battleship-ultimate.md

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
| 1 | **R7** | SEC AI-washing language audit | 2h | 🔴 Legal risk |
| 2 | **T712** | LemonSqueezy integration + PayPal fallback | 4h | 🔴 First $ revenue |
| 3 | **T483-T490** | Kelly sizing + VaR + volatility sizing | 2h | ⏳ Build |

### 🟡 P1 (After P0)
| # | Cell | Task | Est Time | Status |
|---|------|------|----------|--------|
| 4 | **T256** | Finnhub earnings calendar | 0.5h | ⏳ Needs API key |
| 5 | **T542** | Encrypted secrets vault (libsodium) | 1h | ⏳ Build |
| 6 | **T543** | Order management system | 2h | ⏳ Build |
| 7 | **T624-T632** | Monitoring dashboard + Telegram heartbeat | 2h | ⏳ Build |
| 8 | **T721-T730** | Open-Meteo weather (global) | 1h | ✅ Mostly done |

### 🟢 P2 (After P1)
| # | Cell | Task | Est Time |
|---|------|------|----------|
| 9 | **T752-T770** | Test suite — unit tests for core engine | 3h |
| 10 | **T842-T876** | Agent architecture upgrades | 4h |

### REGULATORY FIXES (Complete)
| # | Fix | Status |
|---|-----|--------|
| R1-R3 | Disclaimers on all metrics | ✅ DONE |
| R4 | Pre-trade circuit breakers | ✅ DONE |
| R5 | Privacy Policy v2.0 (GDPR/CCPA) | ✅ DONE |
| R6 | Terms v2.0 (GDPR/CCPA) | ✅ DONE |
| R7 | SEC AI-washing audit | ❌ PENDING |
| R8 | GDPR/CCPA consent flow | ✅ DONE |

### RULES
- All C. No Python. libcurl + jansson + sqlite3.
- NO delegation (delegate_task banned permanently).
- Read walkway → pick lowest undone cell → fix → update → push → loop.
- Zero Telegram noise (all cron deliver=local).
- Always show results after tool calls (no silent replies).
- Update battleship after each cell closed.
- Push after each batch (5-10 cells).