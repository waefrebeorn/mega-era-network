# F30: Runbook — Common Failure Recovery

## Engine won't start
- **Symptom**: `room_engine` exits immediately
- **Check**: `ls -la room_state.bin` — must exist, size = sizeof(RoomState)
- **Fix**: `make room_engine` then `touch room_state.bin && truncate -s $((1024*1024*20)) room_state.bin`
- **Root cause**: Corrupt state file (CRC mismatch) → delete state, engine reinitializes

## Circuit breaker stuck on
- **Symptom**: `[CB] Cooldown` spam in logs, no trades
- **Check**: `grep circuit_breaker_cycles room_log.csv | tail`
- **Fix**: `rm /tmp/money_room_panic` (if panic stop triggered) or wait 100 cycles for cooldown
- **Override**: Edit `config/feature_flags.conf`: `circuit_breaker=off` then restart

## Feed staleness
- **Symptom**: `[FEED] REJECT: stale by Ns` in logs
- **Check**: `ls -la data/prices.json` — `stat -c %Y` should be recent
- **Fix**: Run collector manually: `./collector_runner --once`
- **Root cause**: Collector cron not running → check `hermes cron list`

## Darwin not evolving (epoch=0)
- **Symptom**: `darwin.epoch` stays 0 after 100+ trades
- **Check**: `gdb -batch -ex 'p state.darwin.epoch' ./room_engine`
- **Fix**: Requires 100+ trades total. If agent count low, check `state.stats.active_agents`
- **Root cause**: trade_count not persisting → STATE_MAGIC mismatch → delete state, restart

## Memory leak
- **Symptom**: RSS grows unbounded over hours
- **Check**: `make memcheck` → look for "definitely lost"
- **Fix**: Known: feed_bridge json_array unfreed (fixed), room_engine 0 leaks across 125 allocs
- **Debug**: `valgrind --leak-check=full ./room_engine 2>&1 | grep "definitely lost"`

## Build fails
- **Symptom**: `make: *** [room_engine.o] Error 1`
- **Check**: `gcc --version` — must be ≥ 11. `dpkg -l libcurl4-openssl-dev libjansson-dev libsqlite3-dev`
- **Fix**: `sudo apt install build-essential libcurl4-openssl-dev libjansson-dev libsqlite3-dev`
- **Alt**: Use `config/setup-deps.sh`

## Test failures
- **Symptom**: test_runner shows FAIL
- **Known**: health_check (exit 127 = binary not in PATH), withdrawal_scheduler (non-critical)
- **Fix**: `cp engine/* /usr/local/bin/` or run from engine/ directory

## Disk full
- **Symptom**: `No space left on device` writing room_state.bin
- **Check**: `df -h .` and `du -sh data/`
- **Fix**: `./config/data_archive.sh` (compress old CSVs) and `./config/data_retention_policy.json`
- **Prevent**: logrotate config at `~/.hermes/logrotate-money-room`
