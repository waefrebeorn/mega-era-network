/**
 * cert_check.c — F22: HTTPS/TLS certificate expiry checker
 *
 * Usage:
 *   ./cert_check [host:port] ...
 *   ./cert_check --file /path/to/cert.pem
 *
 * Defaults:
 *   localhost:9091  (api_server)
 *   localhost:9090  (data_server)
 *
 * Exit codes:
 *   0 = OK or all non-TLS/skipped without critical expiry
 *   1 = at least one cert is expired / expires within threshold
 *   3 = fatal usage error
 *
 * Reports:
 *   - subject / issuer
 *   - notBefore / notAfter
 *   - days remaining
 *   - CRIT/WARN/OK
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#define MAX_TARGETS 32
#define WARN_DAYS 30
#define OK_DAYS   90

typedef struct {
    char host[512];
    int port;
    const char *label;
    char not_before[64];
    char not_after[64];
    long days_left;
    int status; /* -1 unknown, 0 ok, 1 warn, 2 crit */
} Target;

static void die(const char *msg) {
    fprintf(stderr, "cert_check: %s\n", msg);
    exit(3);
}

static int parse_kv(const char *arg, char *out_host, size_t out_host_sz, int *out_port) {
    const char *colon = strrchr(arg, ':');
    if (!colon || colon == arg) return -1;
    size_t len = (size_t)(colon - arg);
    if (len >= out_host_sz) len = out_host_sz - 1;
    memcpy(out_host, arg, len);
    out_host[len] = '\0';
    *out_port = atoi(colon + 1);
    return 0;
}

static const char *status_str(int s) {
    switch (s) {
        case 2: return "CRIT";
        case 1: return "WARN";
        case 0: return "OK";
        default: return "UNKNOWN";
    }
}

static int tls_cert_info(const char *host, int port,
                         char *not_before, size_t nb_sz,
                         char *not_after, size_t na_sz) {
    char cmd[1024];
    /* Use openssl s_client to extract cert dates. 5s timeout. */
    snprintf(cmd, sizeof(cmd),
        "echo | openssl s_client -connect %s:%d -servername %s 2>/dev/null "
        "| openssl x509 -noout -startdate -enddate 2>/dev/null",
        host, port, host);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        snprintf(cmd, sizeof(cmd),
            "echo | openssl s_client -connect %s:%d 2>/dev/null "
            "| openssl x509 -noout -startdate -enddate 2>/dev/null",
            host, port);
        fp = popen(cmd, "r");
        if (!fp) return -1;
    }
    int got_start = 0, got_end = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "notBefore=", 10) == 0) {
            size_t vlen = strlen(line + 10);
            if (vlen >= nb_sz) vlen = nb_sz - 1;
            memcpy(not_before, line + 10, vlen);
            not_before[vlen] = '\0';
            char *nl = strchr(not_before, '\n');
            if (nl) *nl = '\0';
            got_start = 1;
        }
        if (strncmp(line, "notAfter=", 9) == 0) {
            size_t vlen = strlen(line + 9);
            if (vlen >= na_sz) vlen = na_sz - 1;
            memcpy(not_after, line + 9, vlen);
            not_after[vlen] = '\0';
            char *nl = strchr(not_after, '\n');
            if (nl) *nl = '\0';
            got_end = 1;
        }
    }
    int rc = pclose(fp);
    return (got_start && got_end) ? 0 : (rc == 0 ? -1 : -1);
}

static const char *month_names[] = {
    "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};

static int parse_month(const char *m) {
    for (int i = 0; i < 12; i++)
        if (strncmp(m, month_names[i], 3) == 0) return i;
    return -1;
}
static long days_until(const char *iso) {
    int mon=-1, day=-1, hour=-1, min=-1, sec=-1, year=-1;
    char monstr[8] = {0};
    struct tm tm = {0};
    tm.tm_isdst = -1;
    int n = sscanf(iso, "%7s %d %d:%d:%d %d GMT", monstr, &day, &hour, &min, &sec, &year);
    if (n < 6) return -1;
    mon = parse_month(monstr);
    if (mon < 0 || day < 1 || day > 31 || year < 1970) return -1;
    tm.tm_mon = mon;
    tm.tm_mday = day;
    tm.tm_hour = hour >= 0 ? hour : 0;
    tm.tm_min  = min  >= 0 ? min  : 0;
    tm.tm_sec  = sec  >= 0 ? sec  : 0;
    tm.tm_year = year - 1900;
    time_t when = mktime(&tm);
    if (when < 0) return -1;
    time_t now = time(NULL);
    if (now < 0) return -1;
    long diff = (long)((when - now) / 86400L);
    return diff;
}

int main(int argc, char **argv) {
    Target targets[MAX_TARGETS];
    int n = 0;

    const char *def_hosts[] = {"localhost:9091", "localhost:9090"};
    if (argc == 1) {
        for (size_t i = 0; i < sizeof(def_hosts)/sizeof(def_hosts[0]) && n < MAX_TARGETS; i++) {
            snprintf(targets[n].host, sizeof(targets[n].host), "%s", def_hosts[i]);
            targets[n].host[sizeof(targets[n].host)-1] = '\0';
            char tmp[512]; tmp[0]='\0';
            if (parse_kv(targets[n].host, tmp, sizeof(tmp), &targets[n].port) == 0)
                snprintf(targets[n].host, sizeof(targets[n].host), "%s", tmp);
            targets[n].label = targets[n].host;
            targets[n].status = -1;
            targets[n].days_left = -1;
            targets[n].not_before[0] = targets[n].not_after[0] = '\0';
            n++;
        }
    } else {
        for (int i = 1; i < argc && n < MAX_TARGETS; i++) {
            if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
                FILE *f = fopen(argv[++i], "r");
                if (!f) {
                    fprintf(stderr, "cert_check: cannot open %s\n", argv[i]);
                    targets[n].status = 2;
                    snprintf(targets[n].host, sizeof(targets[n].host), "%s", argv[i]);
                    targets[n].port = 0;
                    targets[n].label = targets[n].host;
                    targets[n].days_left = -1;
                    n++;
                    continue;
                }
                char line[256];
                int got = 0;
                while (fgets(line, sizeof(line), f)) {
                    if (strncmp(line, "notBefore=", 10) == 0) {
                        size_t vlen = strlen(line+10);
                        if (vlen >= sizeof(targets[n].not_before)) vlen = sizeof(targets[n].not_before)-1;
                        memcpy(targets[n].not_before, line+10, vlen);
                        targets[n].not_before[vlen] = '\0';
                        char *nl = strchr(targets[n].not_before, '\n');
                        if (nl) *nl = '\0';
                        got |= 1;
                    }
                    if (strncmp(line, "notAfter=", 9) == 0) {
                        size_t vlen = strlen(line+9);
                        if (vlen >= sizeof(targets[n].not_after)) vlen = sizeof(targets[n].not_after)-1;
                        memcpy(targets[n].not_after, line+9, vlen);
                        targets[n].not_after[vlen] = '\0';
                        char *nl = strchr(targets[n].not_after, '\n');
                        if (nl) *nl = '\0';
                        got |= 2;
                    }
                }
                fclose(f);
                snprintf(targets[n].host, sizeof(targets[n].host), "%s", argv[i]);
                targets[n].port = 0;
                targets[n].label = targets[n].host;
                targets[n].days_left = days_until(targets[n].not_after[0] ? targets[n].not_after : "Jan 1 00:00:00 1970 UTC");
                targets[n].status = (targets[n].days_left < 0) ? 2 : (targets[n].days_left <= WARN_DAYS ? 2 : (targets[n].days_left <= OK_DAYS ? 1 : 0));
                n++;
                continue;
            }
            targets[n].host[0] = '\0';
            targets[n].port = 0;
            char tmp[512]; tmp[0]='\0';
            if (parse_kv(argv[i], tmp, sizeof(tmp), &targets[n].port) == 0)
                snprintf(targets[n].host, sizeof(targets[n].host), "%s", tmp);
            else
                snprintf(targets[n].host, sizeof(targets[n].host), "%s", argv[i]);
            targets[n].label = targets[n].host;
            targets[n].status = -1;
            targets[n].days_left = -1;
            targets[n].not_before[0] = targets[n].not_after[0] = '\0';
            n++;
        }
    }

    int worst = 0;
    for (int i = 0; i < n; i++) {
        Target *t = &targets[i];
        printf("[%s] %s:%d => ", t->label, t->host, t->port);
        if (t->port > 0) {
            if (tls_cert_info(t->host, t->port,
                              t->not_before, sizeof(t->not_before),
                              t->not_after, sizeof(t->not_after)) == 0) {
                t->days_left = days_until(t->not_after);
                if (t->days_left < 0) t->status = 2;
                else if (t->days_left <= WARN_DAYS) t->status = 2;
                else if (t->days_left <= OK_DAYS) t->status = 1;
                else t->status = 0;
            } else {
                t->status = -1;
                printf("no-tls/unknown\n");
                continue;
            }
        }
        printf("%s", status_str(t->status));
        if (t->not_after[0])
            printf(" | notAfter=%s | days_left=%ld", t->not_after, t->days_left);
        printf("\n");
        if (t->status > worst) worst = t->status;
    }

    return worst >= 2 ? 1 : 0;
}
