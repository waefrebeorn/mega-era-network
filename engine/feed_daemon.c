/**
 * feed_daemon.c — Continuous feed generator for all rooms
 * Runs room_feed_gen for each room every N seconds
 *
 * Compile: gcc -O2 -o feed_daemon feed_daemon.c
 * Usage:   ./feed_daemon [interval_seconds]
 */

#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <time.h>

#define ROOMS_DIR "/home/wubu2/.hermes/pm_logs/rooms"
#define FEED_GEN "/home/wubu2/.hermes/scripts/room_feed_gen"

static const char *ROOMS[] = {
    "consensus", "crypto_prices", "economic", "elections", "kalshi",
    "macro", "manifold", "momentum", "options", "polymarket",
    "predictit", "science_tech", "sports", "stocks", "weather", "btc_main",
    NULL
};

static volatile int g_shutdown = 0;

static void handle_signal(int sig) {
    g_shutdown = 1;
}

static int run_feed_gen(const char *room_name) {
    char room_dir[512];
    snprintf(room_dir, sizeof(room_dir), "%s/%s", ROOMS_DIR, room_name);

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }
    if (pid == 0) {
        setenv("ROOM_DIR", room_dir, 1);
        // Suppress output
        FILE *null = fopen("/dev/null", "w");
        if (null) {
            dup2(fileno(null), STDOUT_FILENO);
            dup2(fileno(null), STDERR_FILENO);
            fclose(null);
        }
        execl(FEED_GEN, "room_feed_gen", NULL);
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int main(int argc, char **argv) {
    int interval = 10;  // default 10 seconds
    if (argc > 1) {
        interval = atoi(argv[1]);
        if (interval < 1) interval = 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("[FEED_DAEMON] Starting feed generation every %ds for %zu rooms\n", interval, sizeof(ROOMS)/sizeof(ROOMS[0]) - 1);

    while (!g_shutdown) {
        time_t start = time(NULL);

        for (int i = 0; ROOMS[i]; i++) {
            if (g_shutdown) break;
            int rc = run_feed_gen(ROOMS[i]);
            if (rc != 0 && rc != -1) {
                fprintf(stderr, "[FEED_DAEMON] %s feed_gen failed: %d\n", ROOMS[i], rc);
            }
        }

        // Sleep for remaining interval
        time_t elapsed = time(NULL) - start;
        int sleep_time = interval - elapsed;
        if (sleep_time > 0) {
            struct timespec ts = { sleep_time, 0 };
            while (sleep_time > 0 && !g_shutdown) {
                sleep_time = nanosleep(&ts, &ts);
            }
        }
    }

    printf("[FEED_DAEMON] Shutdown\n");
    return 0;
}