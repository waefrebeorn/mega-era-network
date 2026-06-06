# ⚔️ MONEY ROOM BATTLESHIP — 86-Cell Grid (v5.6)
> Generated: June 6, 2026 | C-only | Function-level gaps verified
> PORTED = ≥80%, PARTIAL = 20-80%, REAL GAP = <20% or broken

## Priority
🔴 P0 — Wrong results / crashes
🟡 P1 — System works but unreliable
🟢 P2 — Should have, not blocking
⚪ P3 — Future

---

### 🔴 P0 — CRITICAL (Block Revenue/Launch)

| ID | Status | Description | Source |
|----|--------|-------------|--------|
| R1 | PORTED | Reg disclaimer on index.html (root + docs/) | docs/index.html, index.html |
| R2 | PORTED | Reg disclaimer on rooms.html (live engine stats) | docs/rooms.html |
| R3 | PORTED | Reg disclaimer on paper/live/dashboard/pricing.html | docs/paper.html, live.html, dashboard.html, pricing.html |
| R4 | REAL GAP | Pre-trade circuit breakers (drawdown/daily/consec) | engine/room_capital.c |
| R5 | REAL GAP | Privacy policy page content audit | docs/privacy.html |
| R6 | REAL GAP | Terms of service content audit | docs/terms.html |
| R7 | REAL GAP | SEC AI-washing language audit | docs/* |
| R8 | REAL GAP | GDPR/CCPA consent flow | register.html |

---

### 🟡 P1 — DATA PIPELINE (Unreliable Results)

| ID | Status | Description | Source |
|----|--------|-------------|--------|
| T064 | PARTIAL | health_check 20-component validation | engine/health_check.c |
| T074 | PARTIAL | edgar_collector scheduled 0 */12 * * * | crontab, engine/edgar_collector.c |
| T101 | PARTIAL | room_snapshot.json served every 60s | engine/room_bridge.c, crontab |
| T102 | PARTIAL | 60 compiled collectors, 11 system crons | crontab vs engine/*_collector.c |
| **T441a** | **PORTED** | **13f_holdings.c: SEC 13F scraper → F63,F64** | engine/13f_holdings.c, crontab |
| **T441b** | **PORTED** | **insider_trades.c: SEC Form 4 → 3 features** | engine/insider_trades.c, crontab |

---

### 🟢 P2 — WEBSITE (Visible to Users)

| ID | Status | Description | Source |
|----|--------|-------------|--------|
| T031 | PORTED | dashboard.html: KPI + pipeline + data quality | docs/dashboard.html |
| T032 | PORTED | rooms.html: 16 rooms + SVG charts | docs/rooms.html |
| T033 | PORTED | paper.html: leaderboard + capital dist | docs/paper.html |
| T034 | PORTED | Sign In → register.html login modal | docs/index.html:38-40 |
| T035 | PARTIAL | pricing.html: static, no LemonSqueezy | docs/pricing.html |

---

### ⚪ P3 — ENHANCEMENTS

| ID | Status | Description | Source |
|----|--------|-------------|--------|
| T483-T490 | REAL GAP | Kelly + VaR + vol sizing | engine/room_capital.c |
| T542 | REAL GAP | Encrypted secrets vault (libsodium) | engine/secrets_vault.c |
| T543 | REAL GAP | Order management system | engine/room_capital.c |
| T752-T770 | REAL GAP | Unit test suite | engine/test_*.c |

---

### PORTED COUNT: 74 | REAL GAP: 12 | PARTIAL: 18 | TOTAL: 86