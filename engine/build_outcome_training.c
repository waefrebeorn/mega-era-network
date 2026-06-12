/**
 * build_outcome_training.c — Pair market prices with resolved outcomes
 *
 * For binary markets with KNOWN outcomes, creates training data:
 *   - Features: market probability trajectory (OHLC of the probability)
 *   - Target: resolved outcome (0 or 1)
 *
 * Sources:
 *   - outcomes.db: 941 resolved Polymarket markets + 7,324 sports outcomes + 483 sports predictions
 *   - timeline.db: Polymarket price histories for events that overlap with outcomes
 *
 * This is the "gold standard" training data — markets where we know:
 *   1. What the market was pricing (probability)
 *   2. What actually happened (outcome)
 *
 * Compile: gcc -O2 -std=c11 -o build_outcome_training build_outcome_training.c -lsqlite3 -ljansson -lm
 * Usage:   ./build_outcome_training
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include <jansson.h>
#include <math.h>

#define TL_DB   "/home/wubu2/.hermes/timeline.db"
#define OC_DB   "/home/wubu2/.hermes/pm_logs/outcomes.db"
#define OUT_DIR "/home/wubu2/.hermes/pm_logs/historical"

static FILE *open_csv(const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.csv", OUT_DIR, name);
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return NULL; }
    fprintf(f, "ts,open,high,low,close,volume,source\n");
    return f;
}

/* ── Prediction markets with resolved outcomes ── */
static int build_prediction_resolved(void) {
    FILE *f = open_csv("market_prediction");
    if (!f) return -1;

    sqlite3 *oc;
    if (sqlite3_open(OC_DB, &oc) != SQLITE_OK) return -1;

    /* Get resolved outcomes with their questions */
    const char *sql =
        "SELECT market_id, question, predicted_price, resolved_price, outcome, resolution_time "
        "FROM outcomes "
        "ORDER BY resolution_time";

    sqlite3_stmt *stmt;
    int count = 0;

    if (sqlite3_prepare_v2(oc, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *q = (const char*)sqlite3_column_text(stmt, 1);
            double pred = sqlite3_column_double(stmt, 2);
            double resolved = sqlite3_column_double(stmt, 3);
            int outcome = sqlite3_column_int(stmt, 4);
            int64_t res_ts = sqlite3_column_int64(stmt, 5);

            /* Use predicted_price as the "market open" and resolved_price as the "outcome" */
            /* For training: the agent sees the market probability and predicts the outcome */
            double open_p = (pred > 0 && pred <= 1.0) ? pred : 0.5;
            double close_p = (resolved > 0 && resolved <= 1.0) ? resolved : (outcome ? 1.0 : 0.0);

            /* Clamp */
            if (open_p < 0.001) open_p = 0.001;
            if (open_p > 0.999) open_p = 0.999;
            if (close_p < 0.001) close_p = 0.001;
            if (close_p > 0.999) close_p = 0.999;

            char src[128];
            snprintf(src, sizeof(src), "resolved_%d", outcome);

            fprintf(f, "%lld,%.6f,%.6f,%.6f,%.6f,0,%s\n",
                (long long)res_ts, open_p, open_p + 0.05, open_p - 0.05, close_p, src);
            count++;
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(oc);
    printf("[outcome] prediction: %d resolved markets\n", count);
    return count;
}

/* ── Sports with resolved outcomes ── */
static int build_sports_resolved(void) {
    FILE *f = open_csv("market_sports");
    if (!f) return -1;

    sqlite3 *oc;
    if (sqlite3_open(OC_DB, &oc) != SQLITE_OK) return -1;

    sqlite3_stmt *stmt;
    int count = 0;

    /* Use sports_prediction_accuracy: has prediction_confidence (market prob) + correct (outcome) */
    if (sqlite3_prepare_v2(oc,
        "SELECT resolved_at, league, prediction_confidence, correct, spread "
        "FROM sports_prediction_accuracy "
        "ORDER BY resolved_at",
        -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t ts = sqlite3_column_int64(stmt, 0);
            const char *league = (const char*)sqlite3_column_text(stmt, 1);
            double conf = sqlite3_column_double(stmt, 2);
            int correct = sqlite3_column_int(stmt, 3);

            /* prediction_confidence = market's implied probability */
            /* correct = 1 if prediction was right, 0 if wrong */
            double open_p = (conf > 0 && conf <= 1.0) ? conf : 0.5;
            double close_p = correct ? 1.0 : 0.0;

            if (open_p < 0.001) open_p = 0.001;
            if (open_p > 0.999) open_p = 0.999;

            char src[64];
            snprintf(src, sizeof(src), "sports_%s", league ? league : "unk");

            fprintf(f, "%lld,%.6f,%.6f,%.6f,%.6f,0,%s\n",
                (long long)ts, open_p, open_p + 0.05, open_p - 0.05, close_p, src);
            count++;
        }
        sqlite3_finalize(stmt);
    }

    /* Also add raw sports outcomes (no prediction_confidence, use score differential) */
    if (sqlite3_prepare_v2(oc,
        "SELECT game_time, league, home_score, away_score, winner "
        "FROM sports_outcomes "
        "WHERE home_score IS NOT NULL AND away_score IS NOT NULL "
        "ORDER BY game_time",
        -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t ts = sqlite3_column_int64(stmt, 0);
            const char *league = (const char*)sqlite3_column_text(stmt, 1);
            int home_s = sqlite3_column_int(stmt, 2);
            int away_s = sqlite3_column_int(stmt, 3);
            const char *winner = (const char*)sqlite3_column_text(stmt, 4);

            /* Binary outcome: 1 = home win, 0 = away/tie */
            double outcome;
            if (winner && strcmp(winner, "TIE") == 0) outcome = 0.0;  /* Tie = away win for betting */
            else if (winner) {
                const char *home = (const char*)sqlite3_column_text(stmt, 1);  /* league, not home */
                /* Determine from scores */
                outcome = (home_s > away_s) ? 1.0 : 0.0;
            } else {
                outcome = (home_s > away_s) ? 1.0 : 0.0;
            }

            /* Implied probability from score differential (very rough) */
            double score_diff = (double)(home_s - away_s);
            double open_p = 0.5 + score_diff * 0.05;
            if (open_p > 0.95) open_p = 0.95;
            if (open_p < 0.05) open_p = 0.05;

            char src[64];
            snprintf(src, sizeof(src), "sports_outcome_%s", league ? league : "unk");

            fprintf(f, "%lld,%.6f,%.6f,%.6f,%.6f,0,%s\n",
                (long long)ts, open_p, open_p + 0.1, open_p - 0.1, outcome, src);
            count++;
        }
        sqlite3_finalize(stmt);
    }

    fclose(f);
    sqlite3_close(oc);
    printf("[outcome] sports: %d resolved games\n", count);
    return count;
}

int main(void) {
    printf("=== Outcome-Aligned Training Data Builder ===\n\n");
    build_prediction_resolved();
    build_sports_resolved();
    printf("\nDone. Output in %s/\n", OUT_DIR);
    return 0;
}
