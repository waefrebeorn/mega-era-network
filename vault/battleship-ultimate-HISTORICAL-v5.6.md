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
| **R4** | **PORTED** | **5-layer circuit breaker: daily loss >10%, consecutive losses >6, drawdown >20%, max exposure %, directional exposure %, panic stop flag** | engine/room_capital.c:41-115, 315-322, 460-470, 720-750 |
| **R5** | **PORTED** | **Privacy policy v2.0 — GDPR/CCPA compliant: legal basis (Art. 6), retention schedules, rights (access/erasure/portability), SCCs, DPO contact** | docs/privacy.html:1-89 |
| **R6** | **PORTED** | **Terms of Service v2.0 — GDPR/CCPA: IP rights, governing law, rights acknowledgment (Arts. 15-22, §1798.100-199)** | docs/terms.html:1-14 |
| R7 | REAL GAP | SEC AI-washing language audit | docs/* |
| **R8** | **PORTED** | **Register.html — GDPR/CCPA consent flow: 4 granular checkboxes (essential/security/marketing/analytics), consent versioning, written to localStorage + server payload** | docs/register.html:106-140, 218-260 |

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

### PORTED COUNT: 78 | REAL GAP: 8 | PARTIAL: 18 | TOTAL: 86