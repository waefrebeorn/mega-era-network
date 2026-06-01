/**
 * resource_monitor.c — F07: System Resource Monitoring
 *
 * Reads /proc for CPU, memory, disk, and engine process metrics.
 * Outputs JSON to docs/data/ for dashboard display.
 * Cron: every 5 minutes.
 *
 * Build: gcc -O2 resource_monitor.c -o resource_monitor -ljansson -lm
 * Usage: ./resource_monitor
 * Output: /home/wubu2/.hermes/infra/resource_monitor.json
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <jansson.h>

#define OUT_DIR  "/home/wubu2/.hermes/infra"
#define OUT_PATH OUT_DIR "/resource_monitor.json"
#define PROC_DIR "/proc"

/* ── Read a double from a /proc key=value line ── */
static double read_proc_val(const char *path, const char *key) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    double val = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, strlen(key)) == 0) {
            const char *p = line + strlen(key);
            while (*p == ' ' || *p == '\t' || *p == ':') p++;
            val = strtod(p, NULL);
            break;
        }
    }
    fclose(f);
    return val;
}

/* ── Memory: read /proc/meminfo ── */
static int read_memory(json_t *out) {
    double total = read_proc_val("/proc/meminfo", "MemTotal");
    double avail = read_proc_val("/proc/meminfo", "MemAvailable");
    double swap_total = read_proc_val("/proc/meminfo", "SwapTotal");
    double swap_free = read_proc_val("/proc/meminfo", "SwapFree");
    if (total <= 0 || avail <= 0) return -1;

    double used = total - avail;
    json_object_set_new(out, "memory_total_mb", json_real(total / 1024.0));
    json_object_set_new(out, "memory_used_mb", json_real(used / 1024.0));
    json_object_set_new(out, "memory_avail_mb", json_real(avail / 1024.0));
    json_object_set_new(out, "memory_used_pct", json_real(used / total * 100.0));
    if (swap_total > 0) {
        json_object_set_new(out, "swap_total_mb", json_real(swap_total / 1024.0));
        json_object_set_new(out, "swap_free_mb", json_real(swap_free / 1024.0));
        json_object_set_new(out, "swap_used_pct",
            json_real((swap_total - swap_free) / swap_total * 100.0));
    }
    return 0;
}

/* ── Load average: read /proc/loadavg ── */
static int read_load(json_t *out) {
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) return -1;
    double l1, l5, l15;
    int running, total, last_pid;
    if (fscanf(f, "%lf %lf %lf %d/%d %d", &l1, &l5, &l15, &running, &total, &last_pid) == 6) {
        json_object_set_new(out, "load_1m", json_real(l1));
        json_object_set_new(out, "load_5m", json_real(l5));
        json_object_set_new(out, "load_15m", json_real(l15));
        json_object_set_new(out, "procs_running", json_integer(running));
        json_object_set_new(out, "procs_total", json_integer(total));
    }
    fclose(f);
    return 0;
}

/* ── CPU: read /proc/stat for user/nice/system/idle/iowait ── */
static int read_cpu(json_t *out) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;
    char line[256];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
    fclose(f);
    unsigned long long user, nice, sys, idle, iowait, irq, softirq, steal;
    if (sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal) < 8)
        return -1;
    unsigned long long total = user + nice + sys + idle + iowait + irq + softirq + steal;
    json_object_set_new(out, "cpu_user_pct", json_real(user * 100.0 / total));
    json_object_set_new(out, "cpu_sys_pct", json_real(sys * 100.0 / total));
    json_object_set_new(out, "cpu_idle_pct", json_real(idle * 100.0 / total));
    json_object_set_new(out, "cpu_iowait_pct", json_real(iowait * 100.0 / total));
    json_object_set_new(out, "cpu_jiffies_total", json_integer((long long)total));
    return 0;
}

/* ── Disk: read root filesystem via statvfs ── */
static int read_disk(json_t *out) {
    struct statvfs buf;
    if (statvfs("/", &buf) != 0) return -1;
    unsigned long long total = (unsigned long long)buf.f_blocks * buf.f_frsize;
    unsigned long long free_b = (unsigned long long)buf.f_bfree * buf.f_frsize;
    unsigned long long avail = (unsigned long long)buf.f_bavail * buf.f_frsize;
    unsigned long long used = total - free_b;
    json_object_set_new(out, "disk_total_gb", json_real(total / 1e9));
    json_object_set_new(out, "disk_used_gb", json_real(used / 1e9));
    json_object_set_new(out, "disk_avail_gb", json_real(avail / 1e9));
    json_object_set_new(out, "disk_used_pct", json_real(used * 100.0 / total));
    return 0;
}

/* ── Process: find room_engine processes, report RSS/VmSize ── */
static int read_engine_procs(json_t *out) {
    json_t *procs = json_array();
    DIR *d = opendir(PROC_DIR);
    if (!d) { json_decref(procs); return -1; }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        // Only numeric dirs (PIDs)
        char *end;
        long pid = strtol(de->d_name, &end, 10);
        if (*end != '\0') continue;

        char cmdline[512];
        snprintf(cmdline, sizeof(cmdline), "/proc/%ld/cmdline", pid);
        FILE *cf = fopen(cmdline, "r");
        if (!cf) continue;
        char cmd[256];
        size_t n = fread(cmd, 1, sizeof(cmd) - 1, cf);
        fclose(cf);
        cmd[n] = '\0';

        // Match room_engine or collect processes
        if (!strstr(cmd, "room_engine") && !strstr(cmd, "collect")) {
            continue;
        }

        // Read status for memory
        char status_path[64];
        snprintf(status_path, sizeof(status_path), "/proc/%ld/status", pid);
        double vm_rss_kb = read_proc_val(status_path, "VmRSS");
        double vm_size_kb = read_proc_val(status_path, "VmSize");

        json_t *p = json_object();
        json_object_set_new(p, "pid", json_integer(pid));
        json_object_set_new(p, "name", json_string(cmd));
        if (vm_rss_kb > 0) json_object_set_new(p, "memory_mb", json_real(vm_rss_kb / 1024.0));
        if (vm_size_kb > 0) json_object_set_new(p, "virtual_mb", json_real(vm_size_kb / 1024.0));

        // Read uptime: process start time via /proc/stat boot time
        char stat_path[64];
        snprintf(stat_path, sizeof(stat_path), "/proc/%ld/stat", pid);
        FILE *sf = fopen(stat_path, "r");
        if (sf) {
            char sbuf[512];
            if (fgets(sbuf, sizeof(sbuf), sf)) {
                // Parse field 22 (starttime in jiffies since boot)
                int field = 0;
                char *tok = strtok(sbuf, " ");
                while (tok) {
                    field++;
                    if (field == 22) {
                        long start_jiffies = strtol(tok, NULL, 10);
                        // Read boot time from /proc/stat
                        FILE *bt = fopen("/proc/stat", "r");
                        long btime = 0;
                        if (bt) {
                            char btline[256];
                            while (fgets(btline, sizeof(btline), bt)) {
                                if (strncmp(btline, "btime ", 6) == 0) {
                                    btime = strtol(btline + 6, NULL, 10);
                                    break;
                                }
                            }
                            fclose(bt);
                        }
                        if (btime > 0) {
                            long proc_start_epoch = btime + start_jiffies / 100;
                            long uptime = time(NULL) - proc_start_epoch;
                            if (uptime > 0)
                                json_object_set_new(p, "uptime_sec", json_integer(uptime));
                        }
                        break;
                    }
                    tok = strtok(NULL, " ");
                }
            }
            fclose(sf);
        }

        json_array_append_new(procs, p);
    }
    closedir(d);
    json_object_set_new(out, "engine_processes", procs);
    return 0;
}

/* ── Uptime: read /proc/uptime ── */
static int read_uptime(json_t *out) {
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return -1;
    double uptime_secs, idle_secs;
    if (fscanf(f, "%lf %lf", &uptime_secs, &idle_secs) == 2) {
        json_object_set_new(out, "uptime_sec", json_real(uptime_secs));
        json_object_set_new(out, "uptime_hours", json_real(uptime_secs / 3600.0));
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    json_t *root = json_object();
    json_object_set_new(root, "timestamp", json_integer(time(NULL)));
    json_object_set_new(root, "tool", json_string("resource_monitor.c"));
    json_object_set_new(root, "version", json_integer(1));

    int warn_count = 0;

    if (read_memory(root) != 0) { warn_count++; fprintf(stderr, "[RES] WARN: memory read failed\n"); }
    if (read_load(root) != 0)   { warn_count++; fprintf(stderr, "[RES] WARN: load read failed\n"); }
    if (read_cpu(root) != 0)    { warn_count++; fprintf(stderr, "[RES] WARN: cpu read failed\n"); }
    if (read_disk(root) != 0)   { warn_count++; fprintf(stderr, "[RES] WARN: disk read failed\n"); }
    if (read_uptime(root) != 0) { warn_count++; fprintf(stderr, "[RES] WARN: uptime read failed\n"); }
    read_engine_procs(root);  // Non-fatal

    // Alert thresholds
    int alerts = 0;
    json_t *alerts_arr = json_array();

    double mem_used = json_number_value(json_object_get(root, "memory_used_pct"));
    if (mem_used > 90.0) {
        json_array_append_new(alerts_arr, json_string("CRITICAL: memory > 90%"));
        alerts++;
    } else if (mem_used > 80.0) {
        json_array_append_new(alerts_arr, json_string("WARN: memory > 80%"));
        alerts++;
    }

    double disk_used = json_number_value(json_object_get(root, "disk_used_pct"));
    if (disk_used > 95.0) {
        json_array_append_new(alerts_arr, json_string("CRITICAL: disk > 95%"));
        alerts++;
    } else if (disk_used > 85.0) {
        json_array_append_new(alerts_arr, json_string("WARN: disk > 85%"));
        alerts++;
    }

    double load_1m = json_number_value(json_object_get(root, "load_1m"));
    if (load_1m > 16.0) {  // Assuming 8-core system
        json_array_append_new(alerts_arr, json_string("CRITICAL: load > 16"));
        alerts++;
    } else if (load_1m > 8.0) {
        json_array_append_new(alerts_arr, json_string("WARN: load > 8"));
        alerts++;
    }

    json_object_set_new(root, "alerts", alerts_arr);
    json_object_set_new(root, "alert_count", json_integer(alerts));
    json_object_set_new(root, "warn_count", json_integer(warn_count));

    // ── Write output ──
    mkdir(OUT_DIR, 0755);
    FILE *f = fopen(OUT_PATH, "w");
    if (!f) {
        fprintf(stderr, "[RES] ERROR: can't write %s\n", OUT_PATH);
        json_decref(root);
        return 1;
    }
    json_dumpf(root, f, JSON_INDENT(2) | JSON_SORT_KEYS);
    fclose(f);

    printf("[RES] Wrote %s\n", OUT_PATH);
    printf("[RES] memory: %.0f%% | disk: %.0f%% | load: %.1f | alerts: %d\n",
           mem_used, disk_used, load_1m, alerts);

    json_decref(root);
    return alerts > 0 ? 2 : 0;
}
