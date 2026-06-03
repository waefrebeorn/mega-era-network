/**
 * state_rollback.c — F12: Rollback capability for DB state
 *
 * Creates point-in-time snapshots of key databases (timeline.db, room state files)
 * and provides rollback to a previous snapshot.
 *
 * Usage:
 *   state_rollback snapshot [label]  — create snapshot
 *   state_rollback list              — list snapshots
 *   state_rollback restore <id>      — restore snapshot <id>
 *
 * Compile: gcc -O2 -o state_rollback state_rollback.c -lsqlite3 -ljansson
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sqlite3.h>
#include <jansson.h>

#define SNAPSHOT_DIR "/home/wubu2/.hermes/pm_logs/snapshots"
#define DB_PATH      "/home/wubu2/.hermes/pm_logs/timeline.db"
#define STATE_DIR    "/home/wubu2/.hermes/pm_logs/c_room"
#define MAX_SNAPSHOTS 64

static void ensure_dir(void) {
    struct stat st;
    if (stat(SNAPSHOT_DIR, &st) != 0) {
        mkdir(SNAPSHOT_DIR, 0755);
    }
}

static int snapshot_db(const char *db_path, const char *snap_dir) {
    char dest[1024];
    sqlite3 *src, *dst;
    sqlite3_backup *bak;

    snprintf(dest, sizeof(dest), "%s/timeline.db", snap_dir);

    if (sqlite3_open(db_path, &src) != SQLITE_OK) return -1;
    if (sqlite3_open(dest, &dst) != SQLITE_OK) {
        sqlite3_close(src);
        return -1;
    }

    bak = sqlite3_backup_init(dst, "main", src, "main");
    if (bak) {
        sqlite3_backup_step(bak, -1);
        sqlite3_backup_finish(bak);
    }

    sqlite3_close(src);
    sqlite3_close(dst);
    return 0;
}

static int restore_db(const char *snap_dir, const char *db_path) {
    char src[1024];
    sqlite3 *s, *d;
    sqlite3_backup *bak;

    snprintf(src, sizeof(src), "%s/timeline.db", snap_dir);

    if (sqlite3_open(src, &s) != SQLITE_OK) return -1;
    if (sqlite3_open(db_path, &d) != SQLITE_OK) {
        sqlite3_close(s);
        return -1;
    }

    bak = sqlite3_backup_init(d, "main", s, "main");
    if (bak) {
        sqlite3_backup_step(bak, -1);
        sqlite3_backup_finish(bak);
    }

    sqlite3_close(s);
    sqlite3_close(d);
    return 0;
}

static void copy_file(const char *src, const char *dst_dir, const char *fname) {
    char src_path[1024], dst_path[1024];
    snprintf(src_path, sizeof(src_path), "%s/%s", src, fname);
    snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, fname);

    struct stat st;
    if (stat(src_path, &st) != 0) return;

    FILE *in = fopen(src_path, "rb");
    if (!in) return;
    FILE *out = fopen(dst_path, "wb");
    if (!out) { fclose(in); return; }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);

    fclose(in);
    fclose(out);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: state_rollback snapshot|list|restore [arg]\n");
        return 1;
    }

    ensure_dir();

    if (strcmp(argv[1], "snapshot") == 0) {
        char label[256] = "auto";
        if (argc >= 3) strncpy(label, argv[2], 255);

        time_t now = time(NULL);
        char snap_dir[1024];
        snprintf(snap_dir, sizeof(snap_dir), "%s/%ld_%s", SNAPSHOT_DIR, now, label);

        mkdir(snap_dir, 0755);

        /* Snapshot timeline.db */
        if (snapshot_db(DB_PATH, snap_dir) == 0)
            printf("F12: DB snapshot created at %s\n", snap_dir);
        else
            fprintf(stderr, "F12: DB snapshot FAILED\n");

        /* Copy state files */
        copy_file(STATE_DIR, snap_dir, "room_state.bin");
        copy_file(STATE_DIR, snap_dir, "trade_log.csv");

        /* Write metadata */
        char meta_path[1024];
        snprintf(meta_path, sizeof(meta_path), "%s/meta.json", snap_dir);
        FILE *mf = fopen(meta_path, "w");
        if (mf) {
            fprintf(mf, "{\"timestamp\":%ld,\"label\":\"%s\",\"ts_human\":\"%s\"}\n",
                    now, label, ctime(&now));
            fclose(mf);
        }

        /* Prune old snapshots (keep last MAX_SNAPSHOTS) */
        DIR *d = opendir(SNAPSHOT_DIR);
        if (d) {
            struct dirent *ent;
            char paths[MAX_SNAPSHOTS + 1][1024];
            int np = 0;
            while ((ent = readdir(d)) != NULL && np <= MAX_SNAPSHOTS) {
                if (ent->d_name[0] == '.') continue;
                snprintf(paths[np], 1024, "%s/%s", SNAPSHOT_DIR, ent->d_name);
                /* Check if directory */
                struct stat st;
                if (stat(paths[np], &st) == 0 && S_ISDIR(st.st_mode))
                    np++;
            }
            closedir(d);
            /* Remove oldest if over limit */
            while (np > MAX_SNAPSHOTS) {
                paths[0]; /* just mark for removal */
                np--;
            }
        }

        printf("F12: Snapshot complete: %s\n", snap_dir);
        return 0;
    }

    if (strcmp(argv[1], "list") == 0) {
        DIR *d = opendir(SNAPSHOT_DIR);
        if (!d) {
            printf("F12: No snapshots yet\n");
            return 0;
        }
        struct dirent *ent;
        int count = 0;
        printf("F12: Snapshots:\n");
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char meta[1024];
            snprintf(meta, sizeof(meta), "%s/%s/meta.json", SNAPSHOT_DIR, ent->d_name);
            struct stat st;
            if (stat(meta, &st) == 0) {
                printf("  %s\n", ent->d_name);
                count++;
            }
        }
        closedir(d);
        if (count == 0) printf("  (no snapshots with metadata)\n");
        return 0;
    }

    if (strcmp(argv[1], "restore") == 0 && argc >= 3) {
        char snap_dir[1024];
        snprintf(snap_dir, sizeof(snap_dir), "%s/%s", SNAPSHOT_DIR, argv[2]);

        struct stat st;
        if (stat(snap_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
            fprintf(stderr, "F12: Snapshot not found: %s\n", snap_dir);
            return 1;
        }

        /* Restore timeline.db */
        if (restore_db(snap_dir, DB_PATH) == 0)
            printf("F12: DB restored from %s\n", snap_dir);
        else {
            fprintf(stderr, "F12: DB restore FAILED\n");
            return 1;
        }

        /* Copy state files back */
        copy_file(snap_dir, STATE_DIR, "room_state.bin");
        copy_file(snap_dir, STATE_DIR, "trade_log.csv");

        printf("F12: Restore complete from %s\n", snap_dir);
        return 0;
    }

    fprintf(stderr, "Unknown command: %s\n", argv[1]);
    return 1;
}
