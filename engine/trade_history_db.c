/**
 * trade_history_db.c — D47: Trade history beyond room_log.csv
 *
 * Reads trade_log.csv and imports into a SQLite DB for structured querying.
 * Also writes a summary JSON for the dashboard.
 *
 * Compile: gcc -O2 -o trade_history_db trade_history_db.c -lsqlite3 -ljansson -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include <jansson.h>

#define CSV_PATH    "/home/wubu2/.hermes/pm_logs/c_room/trade_log.csv"
#define DB_PATH     "/home/wubu2/.hermes/pm_logs/c_room/trade_history.db"
#define JSON_PATH   "/home/wubu2/money-room/docs/data/trade_history_summary.db"

static sqlite3 *db;

static void init_db(void) {
    char *err = NULL;
    const char *sql =
        "CREATE TABLE IF NOT EXISTS trades ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ts INTEGER,"
        "  agent_id INTEGER,"
        "  direction TEXT,"
        "  size REAL,"
        "  entry_price REAL,"
        "  exit_price REAL,"
        "  won INTEGER,"
        "  pnl_pct REAL,"
        "  resolved_at INTEGER,"
        "  asset TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_agent ON trades(agent_id);"
        "CREATE INDEX IF NOT EXISTS idx_ts ON trades(ts);"
        "CREATE INDEX IF NOT EXISTS idx_won ON trades(won);";

    sqlite3_exec(db, sql, NULL, NULL, &err);
    if (err) {
        fprintf(stderr, "D47: init_db error: %s\n", err);
        sqlite3_free(err);
    }
}

static void import_csv(void) {
    FILE *f = fopen(CSV_PATH, "r");
    if (!f) {
        fprintf(stderr, "D47: Cannot open %s\n", CSV_PATH);
        return;
    }

    char line[1024];
    int imported = 0;
    int skipped = 0;

    /* Skip header */
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return;
    }

    /* Begin transaction */
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    const char *insert_sql =
        "INSERT OR IGNORE INTO trades (ts,agent_id,direction,size,entry_price,exit_price,won,pnl_pct,resolved_at,asset) "
        "VALUES (?,?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);

    while (fgets(line, sizeof(line), f)) {
        int ts, agent_id, won;
        float size, entry_price, exit_price, pnl_pct;
        int64_t resolved_at;
        char direction[8], asset[16];

        if (sscanf(line, "%d,%d,%7[^,],%f,%f,%f,%d,%f,%lld,%15s",
                   &ts, &agent_id, direction, &size, &entry_price,
                   &exit_price, &won, &pnl_pct, (long long *)&resolved_at, asset) < 10) {
            skipped++;
            continue;
        }

        sqlite3_bind_int(stmt, 1, ts);
        sqlite3_bind_int(stmt, 2, agent_id);
        sqlite3_bind_text(stmt, 3, direction, -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 4, size);
        sqlite3_bind_double(stmt, 5, entry_price);
        sqlite3_bind_double(stmt, 6, exit_price);
        sqlite3_bind_int(stmt, 7, won);
        sqlite3_bind_double(stmt, 8, pnl_pct);
        sqlite3_bind_int64(stmt, 9, resolved_at);
        sqlite3_bind_text(stmt, 10, asset, -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) == SQLITE_DONE) imported++;
        sqlite3_reset(stmt);
    }

    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    sqlite3_finalize(stmt);
    fclose(f);

    printf("D47: Imported %d trades, %d skipped\n", imported, skipped);
}

static void write_summary(void) {
    json_t *root = json_object();
    sqlite3_stmt *stmt;

    /* Total trades */
    sqlite3_prepare_v2(db, "SELECT COUNT(*), SUM(won), AVG(pnl_pct) FROM trades", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        json_object_set_new(root, "total_trades", json_integer(sqlite3_column_int(stmt, 0)));
        json_object_set_new(root, "total_wins", json_integer(sqlite3_column_int(stmt, 1)));
        json_object_set_new(root, "avg_pnl_pct", json_real(sqlite3_column_double(stmt, 2)));
    }
    sqlite3_finalize(stmt);

    /* Win rate */
    int total = 0, wins = 0;
    sqlite3_prepare_v2(db, "SELECT COUNT(*), SUM(won) FROM trades WHERE won >= 0", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        total = sqlite3_column_int(stmt, 0);
        wins = sqlite3_column_int(stmt, 1);
    }
    sqlite3_finalize(stmt);
    if (total > 0) {
        json_object_set_new(root, "win_rate", json_real((double)wins / total));
    } else {
        json_object_set_new(root, "win_rate", json_real(0.0));
    }

    /* Top agents by PnL */
    json_t *top_agents = json_array();
    sqlite3_prepare_v2(db,
        "SELECT agent_id, SUM(pnl_pct) as total_pnl, COUNT(*) as trades "
        "FROM trades GROUP BY agent_id ORDER BY total_pnl DESC LIMIT 10",
        -1, &stmt, NULL);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json_t *a = json_object();
        json_object_set_new(a, "agent_id", json_integer(sqlite3_column_int(stmt, 0)));
        json_object_set_new(a, "total_pnl", json_real(sqlite3_column_double(stmt, 1)));
        json_object_set_new(a, "trades", json_integer(sqlite3_column_int(stmt, 2)));
        json_array_append_new(top_agents, a);
    }
    sqlite3_finalize(stmt);
    json_object_set_new(root, "top_agents", top_agents);

    json_object_set_new(root, "generated_at", json_integer(time(NULL)));

    char *json_str = json_dumps(root, JSON_INDENT(2));
    if (json_str) {
        FILE *f = fopen(JSON_PATH, "w");
        if (f) {
            fprintf(f, "%s\n", json_str);
            fclose(f);
            printf("D47: Summary written to %s\n", JSON_PATH);
        }
        free(json_str);
    }
    json_decref(root);
}

int main(void) {
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        fprintf(stderr, "D47: Cannot open DB: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    init_db();
    import_csv();
    write_summary();

    sqlite3_close(db);
    return 0;
}
