/**
 * cycle_all_rooms_parallel.c — Cycle ALL rooms in C with pthread parallelization
 *
 * Phase 1: Per-room feed generation (differentiated feeds by domain) - SEQUENTIAL
 * Phase 2: c_room multi-market engine (main) - SEQUENTIAL
 * Phase 3: All 16 room engines with ROOM_DIR - PARALLEL (pthread)
 *
 * Compile: gcc -O2 -pthread -o cycle_all_rooms_parallel cycle_all_rooms_parallel.c
 * Usage:   ./cycle_all_rooms_parallel [max_threads]
 */
#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>

#define C_ENG    "/home/wubu2/.hermes/pm_logs/c_room/room_engine"
#define ROOMS_DIR "/home/wubu2/.hermes/pm_logs/rooms"
#define FEED_GEN "/home/wubu2/.hermes/scripts/room_feed_gen"
#define HEARTBEAT_FILE "/home/wubu2/.hermes/pm_logs/c_room/heartbeat.json"
#define ALERT_FILE     "/home/wubu2/.hermes/pm_logs/c_room/alert_timeout.json"

static void write_heartbeat(const char *path, int ok, int total, const char *status) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    fprintf(f, "{\"timestamp\":%ld,\"time\":\"%02d:%02d:%02d\","
               "\"status\":\"%s\",\"rooms_ok\":%d,\"rooms_total\":%d}\n",
            (long)now, tm->tm_hour, tm->tm_min, tm->tm_sec,
            status ? status : "ok", ok, total);
    fclose(f);
}
static void write_alert(const char *msg) {
    FILE *f = fopen(ALERT_FILE, "w");
    if (!f) return;
    time_t now = time(NULL);
    fprintf(f, "{\"timestamp\":%ld,\"alert\":\"%s\"}\n", (long)now, msg ? msg : "unknown");
    fclose(f);
}

static const char *ROOMS[] = {
    "consensus", "crypto_prices", "economic", "elections", "kalshi",
    "macro", "manifold", "momentum", "options", "polymarket",
    "predictit", "science_tech", "sports", "stocks", "weather", "btc_main",
    NULL
};

static int run_cmd(const char *bin, const char *room_dir, int timeout_sec) {
    struct stat st;
    if (stat(bin, &st) != 0 || !(st.st_mode & S_IXUSR)) return -1;

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -3; }
    if (pid == 0) {
        if (room_dir) setenv("ROOM_DIR", room_dir, 1);
        FILE *null = fopen("/dev/null", "w");
        if (null) {
            dup2(fileno(null), STDOUT_FILENO);
            dup2(fileno(null), STDERR_FILENO);
            fclose(null);
        }
        execl(bin, bin, NULL);
        _exit(127);
    }
    struct timespec ts = { timeout_sec, 0 };
    int status;
    pid_t result;
    do {
        result = waitpid(pid, &status, WNOHANG);
        if (result == 0) {
            nanosleep(&ts, NULL);
            kill(pid, SIGTERM);
            nanosleep(&(struct timespec){1, 0}, NULL);
            result = waitpid(pid, &status, WNOHANG);
            if (result == 0) { kill(pid, SIGKILL); waitpid(pid, &status, 0); }
            return -2;
        }
    } while (result == 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -3;
}

typedef struct {
    const char *eng_path;
    const char *room_dir;
    int timeout_sec;
    int *result;
} thread_arg_t;

static void *run_room_engine(void *arg) {
    thread_arg_t *t = (thread_arg_t *)arg;
    *(t->result) = run_cmd(t->eng_path, t->room_dir, t->timeout_sec);
    return NULL;
}

int main(int argc, char **argv) {
    int max_threads = 8;
    if (argc > 1) max_threads = atoi(argv[1]);
    if (max_threads < 1) max_threads = 1;
    if (max_threads > 16) max_threads = 16;

    printf("[ROOMS_PARALLEL] Cycling all engines (max_threads=%d)...\n", max_threads);
    int total = 0, ok = 0;

    write_heartbeat(HEARTBEAT_FILE, 0, 0, "starting");

    // Phase 1: Feed generation (sequential - they're fast)
    if (stat(FEED_GEN, &(struct stat){0}) == 0) {
        for (int i = 0; ROOMS[i]; i++) {
            char room_dir[256];
            snprintf(room_dir, sizeof(room_dir), "%s/%s", ROOMS_DIR, ROOMS[i]);

            struct stat rd;
            if (stat(room_dir, &rd) != 0 || !S_ISDIR(rd.st_mode)) continue;

            int rc = run_cmd(FEED_GEN, room_dir, 5);
            total++;
            if (rc == 0 || rc == -1) ok++;
        }
    }
    printf("[ROOMS_PARALLEL] Phase 1: %d/%d room feeds generated\n", ok, total);

    // Phase 2: Main c_room engine (sequential)
    if (stat(C_ENG, &(struct stat){0}) == 0) {
        int rc = run_cmd(C_ENG, NULL, 30);
        printf("[ROOMS_PARALLEL] Phase 2: main engine %s\n",
               rc == 0 ? "OK" : (rc == -2 ? "TIMEOUT" : "FAILED"));
        if (rc == -2) write_alert("main_engine_timeout");
        else if (rc != 0) write_alert("main_engine_failed");
    }

    // Phase 3: Room engines in PARALLEL
    total = 0; ok = 0;
    int timeouts = 0, failures = 0;

    // Count valid rooms first
    int room_count = 0;
    for (int i = 0; ROOMS[i]; i++) {
        char eng_path[256], room_dir[256];
        snprintf(eng_path, sizeof(eng_path), "%s/%s/room_engine", ROOMS_DIR, ROOMS[i]);
        snprintf(room_dir, sizeof(room_dir), "%s/%s", ROOMS_DIR, ROOMS[i]);
        struct stat rd;
        if (stat(room_dir, &rd) != 0 || !S_ISDIR(rd.st_mode)) continue;
        struct stat es;
        if (stat(eng_path, &es) != 0 || !(es.st_mode & S_IXUSR)) continue;
        room_count++;
    }

    if (room_count > 0) {
        // Allocate arrays
        pthread_t *threads = malloc(room_count * sizeof(pthread_t));
        thread_arg_t *args = malloc(room_count * sizeof(thread_arg_t));
        int *results = malloc(room_count * sizeof(int));
        if (!threads || !args || !results) {
            perror("malloc");
            free(threads); free(args); free(results);
            return 1;
        }

        int idx = 0;
        for (int i = 0; ROOMS[i]; i++) {
            char eng_path[256], room_dir[256];
            snprintf(eng_path, sizeof(eng_path), "%s/%s/room_engine", ROOMS_DIR, ROOMS[i]);
            snprintf(room_dir, sizeof(room_dir), "%s/%s", ROOMS_DIR, ROOMS[i]);

            struct stat rd;
            if (stat(room_dir, &rd) != 0 || !S_ISDIR(rd.st_mode)) continue;
            struct stat es;
            if (stat(eng_path, &es) != 0 || !(es.st_mode & S_IXUSR)) continue;

            args[idx].eng_path = strdup(eng_path);
            args[idx].room_dir = strdup(room_dir);
            args[idx].timeout_sec = 30;
            args[idx].result = &results[idx];
            pthread_create(&threads[idx], NULL, run_room_engine, &args[idx]);
            idx++;
        }

        // Wait for all threads, but limit concurrency
        // Simple approach: batch by max_threads
        int completed = 0;
        while (completed < room_count) {
            int batch_start = completed;
            int batch_end = batch_start + max_threads;
            if (batch_end > room_count) batch_end = room_count;

            for (int j = batch_start; j < batch_end; j++) {
                pthread_join(threads[j], NULL);
            }
            completed = batch_end;
        }

        // Collect results
        for (int j = 0; j < room_count; j++) {
            int rc = results[j];
            total++;
            if (rc == 0 || rc == -1) ok++;
            else if (rc == -2) timeouts++;
            else failures++;
            free((void*)args[j].eng_path);
            free((void*)args[j].room_dir);
        }

        free(threads);
        free(args);
        free(results);
    }

    printf("[ROOMS_PARALLEL] Phase 3: %d/%d rooms cycled (%d timeout, %d failed)\n",
           ok, total, timeouts, failures);
    if (timeouts > 0) write_alert("room_engine_timeout");
    if (failures > 0) write_alert("room_engine_failed");

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    printf("[ROOMS_PARALLEL] %02d:%02d: All engines cycled\n", tm->tm_hour, tm->tm_min);

    const char *status = (timeouts > 0 || failures > 0) ? "degraded" : "ok";
    write_heartbeat(HEARTBEAT_FILE, ok, total, status);

    return timeouts > 0 || failures > 0 ? 1 : 0;
}
