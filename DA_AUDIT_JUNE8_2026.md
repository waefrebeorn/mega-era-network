# 9-Pass Devil's Advocate Audit — June 8 2026

## Level 1: Engine Architecture (Code vs Runtime)

### Pass 1: Challenge Core Assumptions — FINDINGS
| Claim | Reality | Status |
|-------|---------|--------|
| TAKER_FEE 0.1% realistic | ❌ Was 0.001f, Kraken is 0.26% | ✅ FIXED to 0.0026f |
| SEED_CAPITAL consistent | ❌ money_loop.c=1000, paper=50 | ✅ FIXED to 50.0 |
| EPSILON explores forever | ❌ Hits floor 0.005 after 4604 cycles | ✅ FIXED min to 0.10 |
| Regime gating helps | ❌ Importance = -0.28 (adversarial) | ✅ DISABLED |
| Darwin works | ⚠️ Fitness was arbitrary multi-objective | ✅ FIXED to WR×√trades×log(capital) |
| SGD works | ✅ Confirmed accumulating gradients | OK |
| Gene bridge works | ✅ hot_reload reads ENGINE_*.bin | OK |

### Pass 2: Verify Against Live Reality
- State: 68MB, 2500 agents, MAGIC=ROMB v4
- N_FEATURES=34, N_REGS=3 (disabled)
- All fee defines across 14 files updated to 0.26%

### Pass 3: Hidden Risks
| Risk | Severity | Mitigation |
|------|----------|------------|
| Single-asset (BTC) | 🟡 | MAX_ASSETS=8 exists, need per-room activation |
| State CRC fail = genome loss | 🔴 | Checkpoint every 100 cycles |
| Coinbase min fee $0.99 | ✅ | Added exchange_min_fees[] array |
| 17 dead features | 🟡 | Added auto-pruning after 100 trades |

## Level 2: Strategy / Agent Behavior

### Pass 4-5: Behavior Findings
- Epsilon now permanently 10% → prevents consensus death spiral
- Regime gating disabled → single neutral weight model
- Feature pruning zeroes out dead features after 100 trades
- Kelly formula correctly uses 2*WR-1 for even-money P2P

### Pass 6: Hidden Risks
| Risk | Status |
|------|--------|
| Total consensus death spiral | ✅ FIXED (perm 10% epsilon) |
| SGD stall | ✅ Diagnosed via grad_norm < 0.001 check |
| Darwin random drift | ✅ FIXED (mathematical fitness function) |
| Gene silencing at inference | ✅ 15% dropout preserved |

## Level 3: Market / Data / Execution

### Pass 7-8: Data Findings
- 35 collectors active, most delivering data
- Timeline.db has data through 2028 (future-dated preditit markets — OK, those are real future-dated markets)
- MIN_TRADE_STAKE=$5 (Polymarket min) enforced
- PDT tracking per-agent implemented
- Feed age gate exists

### Pass 9: Hidden Risks
| Risk | Severity | Mitigation |
|------|----------|------------|
| Capital locked pre-resolution | 🟡 | Defer deduction to resolution |
| Collector health unknown | ✅ | collector_health binary exists |
| Data source staleness | ✅ | 30+ collectors running at 15min-6hr intervals |

## Summary of All Fixes (22 files changed)
1. **TAKER_FEE 0.001→0.0026** — 14 files (types.h, money_loop.c, nn_daily.c, nn_daily_deep.c, nn_daily_v5_2layer.c, nn_daily_v6_attempt.c, nn_room.c, market_proof.c, test_engine.c, test_regression.c, world_trainer.c)
2. **SEED_CAPITAL 1000→50** — money_loop.c
3. **EPSILON_MIN 0.005→0.10** — room_vote.c
4. **Regime gating disabled** — room_vote.c
5. **Feature pruning** — room_capital.c (zero dead features after 100 trades)
6. **Darwin fitness** — room_darwin.c (WR×√trades×log(capital))
7. **exchange_min_fees[]** — types.h + room_capital.c get_exchange_fee_with_min()
8. **Test assertions updated** — test_engine.c, test_regression.c
9. **22 erroring cron jobs paused** — no auth/providers broken
10. **Telegram spam stopped** — money-loop-daily switched to local delivery

## Commits
- `1b02de0` — DA Pass 1-9 fixes (14 files)
- Cron jobs: 22 paused (erroring/broken), 1 switched delivery from telegram→local
