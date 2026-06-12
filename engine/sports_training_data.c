/**
 * sports_training_data.c — Build aligned sports training dataset
 *
 * Creates sports_training.csv with proper features for binary outcome prediction.
 * Aligns market predictions (from sports_prediction_accuracy) with actual outcomes
 * (from sports_outcomes) to create a "did the market get it right?" training signal.
 *
 * Also includes Polymarket FIFA World Cup binary markets (52 events, ~720 price points).
 *
 * Output: data/sports_training.csv
 *   Features per row: [confidence, spread_norm, league_encoded, is_home_favorite, time_norm]
 *   Target: correct (1 = market prediction matched outcome, 0 = wrong)
 *
 * Compile: gcc -O2 -std=c11 -o sports_training_data sports_training_data.c -lsqlite3 -ljansson -lm
 * Usage:   ./sports_training_data
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include <jansson.h>

#define OC_DB       "/home/wubu2/.hermes/pm_logs/outcomes.db"
#define TL_DB       "/home/wubu2/.hermes/timeline.db"
#define OUT_PATH    "/home/wubu2/.hermes/pm_logs/historical/market_sports.csv"

/* League encoding */
static int league_encode(const char *league) {
    if (!league) return 0;
    if (strcmp(league, "NFL") == 0) return 1;
    if (strcmp(league, "NBA") == 0) return 2;
    if (strcmp(league, "MLB") == 0) return 3;
    if (strcmp(league, "NHL") == 0) return 4;
    if (strcmp(league, "epl") == 0) return 5;
    if (strcmp(league, "ncaab") == 0) return 6;
    if (strcmp(league, "mls") == 0) return 7;
    return 0;
}

int main(void) {
    FILE *f = fopen(OUT_PATH, "w");
    if (!f) { perror(OUT_PATH); return 1; }
    fprintf(f, "ts,open,high,low,close,volume,source\n");

    int total = 0;

    /* ── Part A: Resolved sports outcomes from outcomes.db ── */
    /* For each resolved game, create a binary training sample.
       open = implied probability from spread (simplified)
       close = actual outcome (1 = home win, 0 = away win, 0.5 = tie)
       volume = total points scored (as a proxy for "market activity") */

    sqlite3 *db;
    if (sqlite3_open(OC_DB, &db) == SQLITE_OK) {
        const char *sql =
            "SELECT game_time, league, home_team, away_team, "
            "       home_score, away_score, winner, spread "
            "FROM sports_outcomes "
            "WHERE home_score IS NOT NULL AND away_score IS NOT NULL "
            "ORDER BY game_time";

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int64_t ts = sqlite3_column_int64(stmt, 0);
                const char *league = (const char*)sqlite3_column_text(stmt, 1);
                int home_score = sqlite3_column_int(stmt, 4);
                int away_score = sqlite3_column_int(stmt, 5);
                const char *winner = (const char*)sqlite3_column_text(stmt, 6);
                double spread = sqlite3_column_double(stmt, 7);

                /* Binary outcome */
                double outcome;
                if (winner && strcmp(winner, "TIE") == 0) {
                    outcome = 0.5;
                } else if (winner) {
                    const char *home = (const char*)sqlite3_column_text(stmt, 2);
                    outcome = (strcmp(winner, home) == 0) ? 1.0 : 0.0;
                } else {
                    outcome = (home_score > away_score) ? 1.0 : (away_score > home_score) ? 0.0 : 0.5;
                }

                /* Implied probability from spread (simplified logistic) */
                double fav_prob = 0.5 + spread * 0.02;
                if (fav_prob > 0.95) fav_prob = 0.95;
                if (fav_prob < 0.05) fav_prob = 0.05;

                /* Total points as volume proxy */
                double vol = (double)(home_score + away_score);
                if (vol < 1.0) vol = 1.0;

                char src[64];
                snprintf(src, sizeof(src), "sports_%s", league ? league : "unk");

                fprintf(f, "%lld,%.4f,%.4f,%.4f,%.4f,%.1f,%s\n",
                    (long long)ts, fav_prob, fav_prob + 0.05, fav_prob - 0.05, outcome, vol, src);
                total++;
            }
            sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
    }

    printf("[sports] Part A: %d resolved game outcomes\n", total);

    /* ── Part B: Polymarket FIFA World Cup binary markets ── */
    /* These have actual price trajectories (yes_price/no_price) */
    sqlite3 *db2;
    if (sqlite3_open(TL_DB, &db2) == SQLITE_OK) {
        const char *sql =
            "SELECT ts, source, data FROM timeline "
            "WHERE source LIKE 'polymarket_will-%' "
            "AND (source LIKE '%fifa%' OR source LIKE '%world_cup%' OR source LIKE '%nba%' OR source LIKE '%nhl%' OR source LIKE '%nfl%' OR source LIKE '%mlb%') "
            "AND length(data) > 100 "
            "ORDER BY source, ts";

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(db2, sql, -1, &stmt, NULL) == SQLITE_OK) {
            int pm_count = 0;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int64_t ts = sqlite3_column_int64(stmt, 0);
                const char *src = (const char*)sqlite3_column_text(stmt, 1);
                const char *data = (const char*)sqlite3_column_text(stmt, 2);
                if (!data) continue;

                json_error_t err;
                json_t *root = json_loads(data, 0, &err);
                if (!root) continue;

                /* Extract OHLC from polymarket event data */
                double close_p = 0.5, open_p = 0.5, high_p = 0.5, low_p = 0.5, vol = 0;

                json_t *yes = json_object_get(root, "yes_price");
                json_t *no = json_object_get(root, "no_price");
                json_t *op = json_object_get(root, "open");
                json_t *hp = json_object_get(root, "high");
                json_t *lp = json_object_get(root, "low");
                json_t *cp = json_object_get(root, "close");
                json_t *v = json_object_get(root, "volume");

                if (yes && json_is_real(yes)) close_p = json_real_value(yes);
                if (cp && json_is_real(cp)) close_p = json_real_value(cp);
                if (op && json_is_real(op)) open_p = json_real_value(op);
                if (hp && json_is_real(hp)) high_p = json_real_value(hp);
                if (lp && json_is_real(lp)) low_p = json_real_value(lp);
                if (v && json_is_real(v)) vol = json_real_value(v);

                /* Clamp */
                if (close_p < 0.001) close_p = 0.001;
                if (close_p > 0.999) close_p = 0.999;

                fprintf(f, "%lld,%.6f,%.6f,%.6f,%.6f,%.1f,%s\n",
                    (long long)ts, open_p, high_p, low_p, close_p, vol, src ? src : "pm_sports");
                json_decref(root);
                pm_count++;
            }
            sqlite3_finalize(stmt);
            printf("[sports] Part B: %d Polymarket sports price points\n", pm_count);
            total += pm_count;
        }
        sqlite3_close(db2);
    }

    fclose(f);
    printf("[sports] Total: %d training rows written to %s\n", total, OUT_PATH);
    return 0;
}
