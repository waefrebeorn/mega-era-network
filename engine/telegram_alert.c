/**
 * telegram_alert.c — T090: Telegram Trade Alert Generator
 *
 * Reads room_log.csv last lines, outputs formatted Telegram summary to stdout.
 * Designed for no_agent cron delivery (output = message text).
 *
 * Build: gcc -O2 -o telegram_alert telegram_alert.c -lm
 * Usage: ./telegram_alert
 *        ./telegram_alert daily    — detailed (once per day)
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#define LOG_FILE "/home/wubu2/.hermes/pm_logs/c_room/room_log.csv"
#define STATE_FILE "/home/wubu2/money-room/engine/room_state_paper.bin"

static int read_last_line(const char *path, char *buf, int max_len) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[4096];
    *buf = '\0';
    while (fgets(line, sizeof(line), f)) {
        /* Skip header */
        if (strstr(line, "cycle,")) continue;
        /* Keep last data line */
        strncpy(buf, line, max_len - 1);
        buf[max_len - 1] = '\0';
        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
    }
    fclose(f);
    return *buf ? 0 : -1;
}

/* Parse field from CSV line (non-reentrant, but fine for sequential use) */
static char *csv_field(const char *line, int idx) {
    char *buf = malloc(256);
    const char *p = line;
    int col = 0;
    while (*p) {
        if (col == idx) {
            const char *start = p;
            const char *end = strchr(p, ',');
            if (!end) end = p + strlen(p);
            int len = (int)(end - start);
            if (len > 255) len = 255;
            strncpy(buf, start, len);
            buf[len] = '\0';
            return buf;
        }
        if (*p == ',') col++;
        p++;
    }
    strcpy(buf, "?");
    return buf;
}

int main(int argc, char **argv) {
    int mode = (argc > 1 && strcmp(argv[1], "daily") == 0) ? 1 : 0;
    char last_line[4096] = {0};

    if (read_last_line(LOG_FILE, last_line, sizeof(last_line)) < 0) {
        printf("⚠️  Engine log unavailable\n");
        return 1;
    }

    char *c_cycle  = csv_field(last_line, 0);
    char *c_wr     = csv_field(last_line, 5);
    int cycle = atoi(c_cycle);
    float wr = atof(c_wr);
    free(c_cycle); free(c_wr);

    char *c_pnl = csv_field(last_line, 9);
    float pnl = atof(c_pnl);
    free(c_pnl);

    char *c_trades = csv_field(last_line, 10);
    int trades = atoi(c_trades);
    free(c_trades);

    char *c_rwr = csv_field(last_line, 11);
    char *c_cap = csv_field(last_line, 12);
    float rwr = atof(c_rwr);
    float cap = atof(c_cap);
    free(c_rwr); free(c_cap);

    /* ── Build message ── */
    if (mode) printf("📊 Daily Summary\n\n");
    else      printf("📊 Hourly Update\n\n");

    printf("🔄 Cycle: %d\n", cycle);
    printf("💰 Capital: $%.2f\n", cap);
    printf("📈 Trades: %d\n", trades);
    printf("🎯 Agent WR: %.2f%%\n", wr * 100.0f);
    printf("🏠 Room WR: %.2f%%\n", rwr * 100.0f);
    printf("📉 PnL: $%.2f\n", pnl);

    /* PnL direction */
    if (fabsf(pnl) < 1.0f) printf("📊 Flat\n");
    else if (pnl > 0)      printf("📈 +$%.2f\n", pnl);
    else                   printf("📉 -$%.2f\n", -pnl);

    if (mode) {
        /* Daily: check survival stats */
        FILE *sf = fopen(STATE_FILE, "rb");
        if (sf) {
            fseek(sf, 0, SEEK_END);
            long sz = ftell(sf);
            fclose(sf);
            printf("🧬 State: %.0f KB\n", sz / 1024.0);
        }
    }

    printf("\n");
    time_t now = time(NULL);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%H:%M UTC", gmtime(&now));
    printf("🕐 %s\n", tbuf);

    return 0;
}
