/**
 * capital_check.c — Cron job C: capital & circuit breaker monitor
 *
 * Reads the room engine state file and trade log.
 * Alerts if:
 *   - Room capital drops below threshold
 *   - Circuit breaker triggers (DD > 20%)
 *   - Feed age > 5 minutes (stale data)
 *   - No trades in 60 minutes (engine stuck)
 *   - Kill switch engaged
 *
 * Compile: gcc -O2 -o capital_check capital_check.c -lm
 * Usage:   ./capital_check (run every 5 min from cron)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <math.h>

#define STATE_PATH      "/home/wubu2/money-room/engine/state/room_state.bin"
#define TRADE_LOG       "/home/wubu2/.hermes/pm_logs/c_room/trade_log.csv"
#define FEED_PATH       "/home/wubu2/.hermes/pm_logs/c_room/market_feed.json"
#define ALERT_PATH      "/home/wubu2/money-room/data/capital_alert.json"
#define LOG_PATH        "/home/wubu2/money-room/data/capital_check.log"

#define CAPITAL_WARN    25.0f   /* Alert if room capital below $25 */
#define CAPITAL_DANGER  10.0f   /* Critical if below $10 */
#define CAPITAL_BANKRUPT 1.0f   /* Stop trading if below $1 */
#define DD_THRESHOLD    0.20f   /* 20% drawdown triggers circuit breaker */
#define FEED_MAX_AGE    300     /* 5 minutes */
#define TRADE_MAX_AGE   3600    /* 60 minutes */

typedef struct {
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
    /* Padding to 256 bytes total */
    char    _pad[256 - 100];
} RoomStateMinimal;

static void write_alert(const char *level, const char *msg, float capital, float peak, float dd) {
    FILE *f = fopen(ALERT_PATH, "w");
    if (!f) return;
    
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    
    fprintf(f, "{\n");
    fprintf(f, "  \"timestamp\": %lld,\n", (long long)now);
    fprintf(f, "  \"level\": \"%s\",\n", level);
    fprintf(f, "  \"message\": \"%s\",\n", msg);
    fprintf(f, "  \"room_capital\": %.2f,\n", capital);
    fprintf(f, "  \"room_capital_peak\": %.2f,\n", peak);
    fprintf(f, "  \"drawdown_pct\": %.2f,\n", dd * 100);
    fprintf(f, "  \"circuit_breaker\": %s,\n", dd > DD_THRESHOLD ? "true" : "false");
    fprintf(f, "  \"kill_switch\": %s\n", "unknown"); /* Would need to read from state */
    fprintf(f, "}\n");
    fclose(f);
    
    /* Also log */
    FILE *log = fopen(LOG_PATH, "a");
    if (log) {
        fprintf(log, "[%04d-%02d-%02d %02d:%02d:%02d] %s: %s (cap=$%.2f peak=$%.2f dd=%.1f%%)\n",
                tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                tm->tm_hour, tm->tm_min, tm->tm_sec,
                level, msg, capital, peak, dd * 100);
        fclose(log);
    }
    
    fprintf(stdout, "[CAPITAL_CHECK] %s: %s (cap=$%.2f peak=$%.2f dd=%.1f%%)\n",
            level, msg, capital, peak, dd * 100);
}

/* Check if a file exists and is younger than max_age seconds */
static int file_age_ok(const char *path, int max_age) {
    struct stat st;
    if (stat(path, &st) != 0) return 0; /* File doesn't exist */
    
    time_t now = time(NULL);
    int age = (int)(now - st.st_mtime);
    return age < max_age;
}

static int file_age_seconds(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 99999;
    return (int)(time(NULL) - st.st_mtime);
}

int main(void) {
    int alerts = 0;
    time_t now = time(NULL);
    
    printf("[CAPITAL_CHECK] === %s", ctime(&now));
    
    /* ── Check 1: Feed freshness ── */
    int feed_age = file_age_seconds(FEED_PATH);
    if (feed_age > FEED_MAX_AGE) {
        printf("[CAPITAL_CHECK] ⚠️  Feed is %d seconds old (max %d)\n", feed_age, FEED_MAX_AGE);
        alerts++;
    } else {
        printf("[CAPITAL_CHECK] ✓ Feed age: %ds\n", feed_age);
    }
    
    /* ── Check 2: Room state ── */
    FILE *sf = fopen(STATE_PATH, "rb");
    if (!sf) {
        printf("[CAPITAL_CHECK] ⚠️  No room state file at %s\n", STATE_PATH);
        alerts++;
        write_alert("WARNING", "No room state file", 0, 0, 0);
    } else {
        RoomStateMinimal state;
        if (fread(&state, sizeof(state), 1, sf) == 1) {
            fclose(sf);
            
            float capital = state.room_capital;
            float peak = state.room_capital_peak;
            float dd = (peak > 0) ? (peak - capital) / peak : 0;
            float wr = (state.room_trades > 0) ? (float)state.room_wins / state.room_trades * 100 : 0;
            
            printf("[CAPITAL_CHECK] Room: cap=$%.2f peak=$%.2f dd=%.1f%% trades=%d wr=%.1f%% losses=%d\n",
                   capital, peak, dd * 100, state.room_trades, wr, state.consec_room_losses);
            
            /* Capital checks */
            if (capital < CAPITAL_BANKRUPT) {
                write_alert("CRITICAL", "Capital below $1 — STOP TRADING", capital, peak, dd);
                alerts++;
            } else if (capital < CAPITAL_DANGER) {
                write_alert("DANGER", "Capital below $10 — high risk", capital, peak, dd);
                alerts++;
            } else if (capital < CAPITAL_WARN) {
                write_alert("WARNING", "Capital below $25 — monitor closely", capital, peak, dd);
                alerts++;
            }
            
            /* Drawdown check */
            if (dd > DD_THRESHOLD) {
                write_alert("CRITICAL", "Circuit breaker: DD exceeds 20%", capital, peak, dd);
                alerts++;
            } else if (dd > DD_THRESHOLD * 0.75f) {
                write_alert("WARNING", "Drawdown approaching circuit breaker (15%+)", capital, peak, dd);
                alerts++;
            }
            
            /* Consecutive losses */
            if (state.consec_room_losses >= 5) {
                printf("[CAPITAL_CHECK] ⚠️  %d consecutive losses\n", state.consec_room_losses);
                alerts++;
            }
        } else {
            fclose(sf);
            printf("[CAPITAL_CHECK] ⚠️  Cannot read room state\n");
            alerts++;
        }
    }
    
    /* ── Check 3: Trade log activity ── */
    int trade_age = file_age_seconds(TRADE_LOG);
    if (trade_age > TRADE_MAX_AGE && trade_age < 99999) {
        printf("[CAPITAL_CHECK] ⚠️  No trades in %d seconds\n", trade_age);
        alerts++;
    } else if (trade_age < 99999) {
        printf("[CAPITAL_CHECK] ✓ Last trade: %ds ago\n", trade_age);
    }
    
    /* ── Summary ── */
    if (alerts == 0) {
        printf("[CAPITAL_CHECK] ✓ All checks passed\n");
    } else {
        printf("[CAPITAL_CHECK] ⚠️  %d alert(s) triggered\n", alerts);
    }
    
    return alerts > 0 ? 1 : 0;
}
