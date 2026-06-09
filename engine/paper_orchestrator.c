/**
 * paper_orchestrator.c — Run all 16 rooms in PAPER_MODE for N cycles
 * 
 * Unlike cycle_all_rooms which runs short-lived LIVE engines,
 * this spawns paper engines as background processes and waits for them
 * to complete their historical replay.
 * 
 * Usage: ./paper_orchestrator [max_cycles_per_room]
 *   max_cycles_per_room: optional, default 1000
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
#include <sys/stat.h>

#define ROOMS_DIR "/home/wubu2/.hermes/pm_logs/rooms"
#define MAX_ROOMS 20

static const char *ROOMS[] = {
    "consensus", "crypto_prices", "economic", "elections", "kalshi",
    "macro", "manifold", "momentum", "options", "polymarket",
    "predictit", "science_tech", "sports", "stocks", "weather", "btc_main",
    NULL
};

static pid_t child_pids[MAX_ROOMS];
static int n_children = 0;
static volatile int g_shutdown = 0;

static void handle_signal(int sig) {
    g_shutdown = 1;
    for (int i = 0; i < n_children; i++) {
        if (child_pids[i] > 0) kill(child_pids[i], SIGTERM);
    }
}

static int run_room(const char *room_name, int max_cycles) {
    char eng_path[512], room_dir[512];
    snprintf(eng_path, sizeof(eng_path), "%s/%s/room_engine_paper", ROOMS_DIR, room_name);
    snprintf(room_dir, sizeof(room_dir), "%s/%s", ROOMS_DIR, room_name);

    struct stat st;
    if (stat(eng_path, &st) != 0 || !(st.st_mode & S_IXUSR)) {
        fprintf(stderr, "[ORCH] No paper engine at %s\n", eng_path);
        return -1;
    }
    if (stat(room_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "[ORCH] No room dir %s\n", room_dir);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }
    if (pid == 0) {
        // Child
        setenv("ROOM_DIR", room_dir, 1);
        if (max_cycles > 0) {
            char cycles_str[32];
            snprintf(cycles_str, sizeof(cycles_str), "%d", max_cycles);
            setenv("PAPER_MAX_CYCLES", cycles_str, 1);
        }
        
        // Suppress output for cleaner orchestration
        FILE *null = fopen("/dev/null", "w");
        if (null) {
            dup2(fileno(null), STDOUT_FILENO);
            dup2(fileno(null), STDERR_FILENO);
            fclose(null);
        }
        
        execl(eng_path, "room_engine_paper", NULL);
        _exit(127);
    }
    
    return pid;
}

int main(int argc, char **argv) {
    int max_cycles = 1000;
    if (argc > 1) {
        max_cycles = atoi(argv[1]);
        if (max_cycles <= 0) max_cycles = 1000;
    }

    printf("[PAPER_ORCH] Starting all rooms with max %d cycles each\n", max_cycles);
    
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Launch all rooms
    for (int i = 0; ROOMS[i]; i++) {
        pid_t pid = run_room(ROOMS[i], max_cycles);
        if (pid > 0) {
            child_pids[n_children++] = pid;
            printf("[ORCH] Started %s (pid=%d)\n", ROOMS[i], pid);
        }
    }

    printf("[ORCH] Launched %d rooms. Waiting for completion...\n", n_children);

    // Wait for all children
    int completed = 0;
    while (completed < n_children && !g_shutdown) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            completed++;
            if (WIFEXITED(status)) {
                printf("[ORCH] Room %d exited with code %d\n", pid, WEXITSTATUS(status));
            } else {
                printf("[ORCH] Room %d terminated by signal %d\n", pid, WTERMSIG(status));
            }
        } else if (pid == 0) {
            // None ready, sleep
            sleep(2);
        } else {
            // Error or no children
            break;
        }
    }

    // If shutdown, wait for remaining
    if (g_shutdown) {
        printf("[ORCH] Shutdown requested, waiting for remaining...\n");
        for (int i = 0; i < n_children; i++) {
            if (child_pids[i] > 0) {
                waitpid(child_pids[i], NULL, 0);
            }
        }
    }

    printf("[ORCH] All %d rooms completed\n", completed);
    return 0;
}