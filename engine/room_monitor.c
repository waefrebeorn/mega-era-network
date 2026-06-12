/**
 * room_monitor.c — Room engine monitor (C binary, logs only)
 *
 * Rule: C binaries NEVER send Telegram. They log to file + set exit code.
 * Cron prompt reads logs and decides Telegram delivery.
 *
 * Checks:
 *   1. Engine process alive
 *   2. Feed freshness (market_feed.json age)
 *   3. Room state (capital, WR, drawdown, circuit breaker)
 *   4. Trade log activity
 *   5. Disk space
 *
 * Exit: 0 = all OK, 1 = warnings, 2 = critical
 * Log:  /home/wubu2/money-room/data/room_monitor.log
 *
 * Compile: gcc -O2 -std=c11 -o room_monitor room_monitor.c -lm
 * Usage:   ./room_monitor [--quiet]
 */
#define _GNU_SOURCE
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <math.h>

#define STATE_PATH          "/home/wubu2/money-room/engine/state/room_state.bin"
#define TRADE_LOG           "/home/wubu2/.hermes/pm_logs/c_room/trade_log.csv"
#define FEED_PATH           "/home/wubu2/.hermes/pm_logs/c_room/market_feed.json"
#define LOG_PATH            "/home/wubu2/money-room/data/room_monitor.log"
#define DATA_DIR            "/home/wubu2/.hermes/pm_logs"

#define CAPITAL_WARN        25.0f
#define CAPITAL_DANGER      10.0f
#define CAPITAL_BANKRUPT    1.0f
#define DD_THRESHOLD        0.20f
#define FEED_MAX_AGE        300
#define TRADE_MAX_AGE       3600
#define DISK_MIN_GB         5.0f

typedef struct __attribute__((packed)) {
    int32_t magic;
    int32_t state_crc;
    int32_t cycle;
    int64_t last_ts;
    float   room_capital;
    float   room_capital_peak;
    float   prev_room_capital;
    int32_t room_trades;
    int32_t room_wins;
    int32_t room_losses;
    float   hedge_factor;
    float   max_total_exposure_pct;
    float   current_total_exposure;
    float   peak_total_exposure;
    int32_t vote_count;
    int32_t pdt_room_count;
    int64_t pdt_room_window_start;
    float   slippage_events;
    float   total_slippage_paid;
    int32_t circuit_breaker_tripped;
    float   circuit_breaker_peak;
    int32_t kill_switch_engaged;
    float   daily_pnl;
    int32_t consec_room_losses;
    float   withdrawal_threshold;
    int32_t room_take_profit_triggered;
    float   room_take_profit_pct;
    char    _pad[256];
} RoomState;

static int g_quiet = 0;
static FILE *g_logfile = NULL;

static void log_msg(const char *level, const char *fmt, ...) {
    FILE *f = g_logfile ? g_logfile : stderr;

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);

    fprintf(f, "[%s] %s ", ts, level);

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);

    fprintf(f, "\n");
    if (f != stderr) fflush(f);

    if (!g_quiet) {
        printf("[%s] %s ", ts, level);
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
        printf("\n");
    }
}

static int file_age(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 99999;
    return (int)(time(NULL) - st.st_mtime);
}

static int process_running(const char *name) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "pgrep -x %s > /dev/null 2>&1", name);
    return system(cmd) == 0;
}

static double disk_free_gb(const char *path) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "df -BG %s 2>/dev/null | tail -1 | awk '{print $4}' | tr -d 'G'", path);
    FILE *f = popen(cmd, "r");
    if (!f) return 999;
    int gb = 0;
    fscanf(f, "%d", &gb);
    pclose(f);
    return (double)gb;
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quiet") == 0) g_quiet = 1;
    }

    /* Open log file */
    g_logfile = fopen(LOG_PATH, "a");
    if (!g_logfile) g_logfile = stderr;

    int warnings = 0;
    int criticals = 0;

    log_msg("INFO", "=== Monitor check start ===");

    /* ── 1. Engine process ── */
    int engine_alive = process_running("room_engine") || process_running("room_engine_paper");
    if (!engine_alive) {
        log_msg("CRITICAL", "Engine process NOT running! (room_engine / room_engine_paper not found)");
        criticals++;
    } else {
        log_msg("INFO", "OK: Engine process alive");
    }

    /* ── 2. Feed freshness ── */
    int feed_age = file_age(FEED_PATH);
    if (feed_age > FEED_MAX_AGE) {
        log_msg("WARN", "Feed stale: %ds old (max %d)", feed_age, FEED_MAX_AGE);
        warnings++;
    } else {
        log_msg("INFO", "OK: Feed age %ds", feed_age);
    }

    /* ── 3. Room state ── */
    FILE *sf = fopen(STATE_PATH, "rb");
    if (!sf) {
        log_msg("WARN", "No room state file at %s", STATE_PATH);
        warnings++;
    } else {
        RoomState st;
        if (fread(&st, sizeof(st), 1, sf) != 1) {
            log_msg("ERROR", "Cannot read room state (short read)");
            warnings++;
        } else {
            fclose(sf);
            float cap = st.room_capital;
            float peak = st.room_capital_peak;
            float dd = (peak > 0) ? (peak - cap) / peak : 0;
            float wr = (st.room_trades > 0) ? (float)st.room_wins / st.room_trades * 100 : 0;

            log_msg("INFO", "Room: cap=$%.2f peak=$%.2f dd=%.1f%% trades=%d wr=%.1f%% consec_loss=%d",
                    cap, peak, dd * 100, st.room_trades, wr, st.consec_room_losses);

            if (cap < CAPITAL_BANKRUPT) {
                log_msg("CRITICAL", "Capital BANKRUPT: $%.2f (below $%.2f)", cap, CAPITAL_BANKRUPT);
                criticals++;
            } else if (cap < CAPITAL_DANGER) {
                log_msg("CRITICAL", "Capital DANGER: $%.2f (below $%.2f)", cap, CAPITAL_DANGER);
                criticals++;
            } else if (cap < CAPITAL_WARN) {
                log_msg("WARN", "Capital LOW: $%.2f (below $%.2f)", cap, CAPITAL_WARN);
                warnings++;
            }

            if (dd > DD_THRESHOLD) {
                log_msg("CRITICAL", "Drawdown %.1f%% exceeds %.0f%% threshold", dd * 100, DD_THRESHOLD * 100);
                criticals++;
            } else if (dd > DD_THRESHOLD * 0.75f) {
                log_msg("WARN", "Drawdown %.1f%% approaching %.0f%% threshold", dd * 100, DD_THRESHOLD * 100);
                warnings++;
            }

            if (st.consec_room_losses >= 5) {
                log_msg("WARN", "%d consecutive losses", st.consec_room_losses);
                warnings++;
            }

            if (st.circuit_breaker_tripped) {
                log_msg("CRITICAL", "Circuit breaker TRIPPED");
                criticals++;
            }

            if (st.kill_switch_engaged) {
                log_msg("CRITICAL", "Kill switch ENGAGED");
                criticals++;
            }
        }
        if (sf) fclose(sf);
    }

    /* ── 4. Trade log activity ── */
    int trade_age = file_age(TRADE_LOG);
    if (trade_age > TRADE_MAX_AGE && trade_age < 99999) {
        log_msg("WARN", "No trades in %ds (last trade %d min ago)", trade_age, trade_age / 60);
        warnings++;
    } else if (trade_age < 99999) {
        log_msg("INFO", "OK: Last trade %ds ago", trade_age);
    } else {
        log_msg("INFO", "Trade log does not exist yet (OK if engine just started)");
    }

    /* ── 5. Disk space ── */
    double disk_gb = disk_free_gb(DATA_DIR);
    if (disk_gb < DISK_MIN_GB) {
        log_msg("WARN", "Disk low: %.1f GB free (min %.0f)", disk_gb, DISK_MIN_GB);
        warnings++;
    } else {
        log_msg("INFO", "OK: Disk %.1f GB free", disk_gb);
    }

    /* ── Summary ── */
    if (criticals > 0) {
        log_msg("CRITICAL", "=== %d CRITICAL(s), %d WARN(s) ===", criticals, warnings);
    } else if (warnings > 0) {
        log_msg("WARN", "=== %d WARN(s) ===", warnings);
    } else {
        log_msg("INFO", "=== All checks passed ===");
    }

    log_msg("INFO", "=== Monitor check end ===");

    if (g_logfile && g_logfile != stderr) fclose(g_logfile);

    /* Exit code: 0=OK, 1=warnings, 2=critical */
    if (criticals > 0) return 2;
    if (warnings > 0) return 1;
    return 0;
}
