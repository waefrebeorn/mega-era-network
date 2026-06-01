/**
 * db_backup.c — F09: Database backup utility
 * Copies timeline.db and other critical DBs to backup dir
 * Timestamps each backup for point-in-time recovery
 *
 * Compile: gcc -O2 -Wall -o db_backup db_backup.c
 * Usage:   ./db_backup
 * Cron:    0 6 * * * /home/wubu2/money-room/engine/db_backup
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <libgen.h>
#include <errno.h>

#define MAX_DBS 8
#define MAX_PATH 512
#define BACKUP_DIR "/home/wubu2/money-room/data/backups"
#define MAX_BACKUPS 30  // Keep last 30 days

static const char *DB_PATHS[] = {
    "/home/wubu2/.hermes/pm_logs/timeline.db",
    "/home/wubu2/.hermes/pm_logs/options_trades.db",
    "/home/wubu2/.hermes/pm_logs/sports_events.db",
    "/home/wubu2/.hermes/pm_logs/news_articles.db",
    "/home/wubu2/.hermes/pm_logs/pipeline_state.db",
    NULL
};

static int ts_cmp(const void *a, const void *b) {
    return (*(long*)a - *(long*)b);
}

int main(void) {
    struct stat st;
    if (stat(BACKUP_DIR, &st) != 0) {
        if (mkdir(BACKUP_DIR, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "[BACKUP] Failed to create %s: %s\n", BACKUP_DIR, strerror(errno));
            return 1;
        }
    }

    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    char date_str[32];
    strftime(date_str, sizeof(date_str), "%Y%m%d", tm_now);

    int backed_up = 0, failed = 0;
    for (int i = 0; DB_PATHS[i]; i++) {
        if (stat(DB_PATHS[i], &st) != 0) {
            fprintf(stderr, "[BACKUP] SKIP %s (not found)\n", DB_PATHS[i]);
            continue;
        }
        char tgt[MAX_PATH];
        const char *base = strrchr(DB_PATHS[i], '/');
        base = base ? base + 1 : DB_PATHS[i];
        snprintf(tgt, sizeof(tgt), "%s/%s.%s", BACKUP_DIR, base, date_str);

        // Copy via system cp (preserve through rename)
        char cmd[MAX_PATH + 80];
        snprintf(cmd, sizeof(cmd), "cp \"%s\" \"%s\" 2>/dev/null", DB_PATHS[i], tgt);
        int rc = system(cmd);
        if (rc == 0) {
            backed_up++;
            printf("[BACKUP] OK %s → %s (%lld bytes)\n", base, tgt, (long long)st.st_size);
        } else {
            failed++;
            fprintf(stderr, "[BACKUP] FAIL %s → %s (rc=%d)\n", base, tgt, rc);
        }
    }

    // Prune old backups: keep last MAX_BACKUPS per db
    for (int i = 0; DB_PATHS[i]; i++) {
        const char *base = strrchr(DB_PATHS[i], '/');
        base = base ? base + 1 : DB_PATHS[i];
        char glob[MAX_PATH];
        snprintf(glob, sizeof(glob), "ls -1 %s/%s.* 2>/dev/null", BACKUP_DIR, base);
        FILE *ls = popen(glob, "r");
        if (!ls) continue;
        long *timestamps = NULL;
        int count = 0, cap = 0;
        char line[MAX_PATH];
        while (fgets(line, sizeof(line), ls)) {
            line[strcspn(line, "\n")] = 0;
            const char *dot = strrchr(line, '.');
            if (!dot) continue;
            long ts = atol(dot + 1);
            if (ts < 20250101) continue; // Not a date-stamped backup
            if (count >= cap) {
                cap = cap ? cap * 2 : 16;
                timestamps = realloc(timestamps, cap * sizeof(long));
            }
            timestamps[count++] = ts;
        }
        pclose(ls);

        if (count > MAX_BACKUPS) {
            qsort(timestamps, count, sizeof(long), ts_cmp);
            int to_remove = count - MAX_BACKUPS;
            for (int r = 0; r < to_remove; r++) {
                char rm_cmd[MAX_PATH + 64];
                snprintf(rm_cmd, sizeof(rm_cmd), "rm -f %s/%s.%ld 2>/dev/null", BACKUP_DIR, base, timestamps[r]);
                system(rm_cmd);
            }
            printf("[BACKUP] Pruned %d old %s backups (kept %d)\n", to_remove, base, MAX_BACKUPS);
        }
        free(timestamps);
    }

    printf("[BACKUP] Done: %d backed up, %d failed\n", backed_up, failed);
    return failed > 0 ? 1 : 0;
}
