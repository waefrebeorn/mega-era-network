/**
 * room_orchestrator.c — Multi-Room Orchestrator for ALL 16 rooms
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#define HB_DIR  "/home/wubu2/.hermes/infra/heartbeats"
#define ROOMS_ROOT "/home/wubu2/.hermes/pm_logs/rooms"
#define OUT     "/home/wubu2/.hermes/pm_logs/c_room/orchestrator_state.json"
#define MAX_AGE_OK    300
#define MAX_AGE_STALE 1800
#define N_ROOMS 16

static const char *ROOMS[16] = {
    "btc_main", "consensus", "crypto_prices", "economic",
    "elections", "kalshi", "macro", "manifold",
    "momentum", "options", "polymarket", "predictit",
    "science_tech", "sports", "stocks", "weather"
};

static const char *ROOM_LABELS[16] = {
    "BTC Main", "Consensus", "Crypto Prices", "Economic",
    "Elections", "Kalshi", "Macro", "Manifold",
    "Momentum", "Options", "Polymarket", "PredictIt",
    "Science/Tech", "Sports", "Stocks", "Weather"
};

static time_t file_mtime(const char *path) {
    struct stat st; if (stat(path, &st) != 0) return 0;
    return st.st_mtime;
}

static time_t read_hb(const char *name) {
    char path[256]; snprintf(path, sizeof(path), "%s/%s.heartbeat", HB_DIR, name);
    FILE *f = fopen(path, "r"); if (!f) return 0;
    long t; if (fscanf(f, "%ld", &t) != 1) t = 0;
    fclose(f); return (time_t)t;
}

int main(void) {
    time_t now = time(NULL);
    printf("[ORCH] Multi-Room Orchestrator (16 rooms)\n");

    const char *statuses[16];
    time_t last_seen[16];
    int all_alive = 1;

    for (int i = 0; i < 16; i++) {
        // Check room snapshot
        char snap_path[512];
        snprintf(snap_path, sizeof(snap_path), "%s/%s/room_snapshot.json", ROOMS_ROOT, ROOMS[i]);
        time_t snap_mtime = file_mtime(snap_path);
        time_t snap_age = snap_mtime > 0 ? now - snap_mtime : 999999;
        last_seen[i] = snap_mtime;

        if (snap_age <= MAX_AGE_OK) statuses[i] = "ALIVE";
        else if (snap_age <= MAX_AGE_STALE) { statuses[i] = "STALE"; all_alive = 0; }
        else { statuses[i] = "DEAD"; all_alive = 0; }

        printf("  %s: %s (snapshot %lds old)\n", ROOM_LABELS[i], statuses[i], (long)snap_age);
    }

    // Build orchestrator JSON
    char json[8192]; int n = 0;
    n += snprintf(json + n, sizeof(json) - n,
        "{\n  \"fetched_at\": %ld,\n  \"all_rooms_alive\": %s,\n  \"rooms\": [\n",
        (long)now, all_alive ? "true" : "false");

    for (int i = 0; i < 16; i++) {
        char tb[32] = "never";
        if (last_seen[i] > 0) strftime(tb, sizeof(tb), "%H:%M:%S", gmtime(&last_seen[i]));
        n += snprintf(json + n, sizeof(json) - n,
            "    {\"name\":\"%s\",\"label\":\"%s\",\"status\":\"%s\","
            "\"last_seen\":%ld,\"last_seen_str\":\"%s\"}%s\n",
            ROOMS[i], ROOM_LABELS[i], statuses[i],
            (long)last_seen[i], tb,
            i < 15 ? "," : "");
    }

    n += snprintf(json + n, sizeof(json) - n,
        "  ]\n}\n");

    mkdir("/home/wubu2/.hermes/pm_logs/c_room", 0755);
    int fd = open(OUT, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd >= 0) { write(fd, json, n); close(fd); }
    fprintf(stdout, "  Orchestrator state written to %s\n", OUT);

    if (!all_alive) fprintf(stdout, "  ⚠ NOT all rooms alive\n");
    return all_alive ? 0 : 1;
}
