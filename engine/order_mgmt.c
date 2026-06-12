/**
 * order_mgmt.c — T543: Order Management System
 *
 * Tracks order lifecycle: NEW -> PENDING -> FILLED/CANCELLED/EXPIRED
 * Uses SQLite for persistent order journal.
 *
 * Compile: gcc -O3 -march=native order_mgmt.c -o order_mgmt -lsqlite3 -lm
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include <math.h>
#include <stdint.h>

#define ORDER_DB    "/home/wubu2/money-room/data/orders.db"
#define TTL_SECONDS (86400 * 7)

typedef enum {
    ORDER_NEW=0, ORDER_PENDING=1, ORDER_FILLED=2,
    ORDER_CANCELLED=3, ORDER_EXPIRED=4, ORDER_REJECTED=5
} OrderState;

static const char *state_name(OrderState s) {
    const char *names[] = {"NEW","PENDING","FILLED","CANCELLED","EXPIRED","REJECTED"};
    return (s >= 0 && s <= 5) ? names[s] : "?";
}

static sqlite3 *open_db(void) {
    sqlite3 *db;
    if (sqlite3_open(ORDER_DB, &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open %s: %s\n", ORDER_DB, sqlite3_errmsg(db));
        return NULL;
    }
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
    const char *schema =
        "CREATE TABLE IF NOT EXISTS orders ("
        "  order_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  agent_id INTEGER NOT NULL,"
        "  asset TEXT NOT NULL,"
        "  direction INTEGER NOT NULL,"
        "  stake REAL NOT NULL,"
        "  price REAL NOT NULL,"
        "  fill_price REAL DEFAULT NULL,"
        "  pnl REAL DEFAULT NULL,"
        "  state INTEGER DEFAULT 0,"
        "  reject_reason TEXT DEFAULT NULL,"
        "  created_at INTEGER NOT NULL,"
        "  updated_at INTEGER NOT NULL,"
        "  resolved_at INTEGER DEFAULT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_ord_agent ON orders(agent_id);"
        "CREATE INDEX IF NOT EXISTS idx_ord_asset ON orders(asset);"
        "CREATE INDEX IF NOT EXISTS idx_ord_state ON orders(state);"
        "CREATE TABLE IF NOT EXISTS order_log ("
        "  log_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  order_id INTEGER NOT NULL,"
        "  old_state INTEGER,"
        "  new_state INTEGER NOT NULL,"
        "  detail TEXT,"
        "  ts INTEGER NOT NULL);";
    char *err = NULL;
    sqlite3_exec(db, schema, NULL, NULL, NULL);
    if (err) sqlite3_free(err);
    return db;
}

static void log_trans(sqlite3 *db, int oid, OrderState old_s, OrderState new_s, const char *detail) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO order_log (order_id,old_state,new_state,detail,ts) VALUES (?,?,?,?,?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, oid);
        sqlite3_bind_int(stmt, 2, old_s);
        sqlite3_bind_int(stmt, 3, new_s);
        sqlite3_bind_text(stmt, 4, detail?detail:"", -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 5, (sqlite3_int64)time(NULL));
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

static int cmd_init(void) {
    sqlite3 *db = open_db();
    if (!db) return 1;
    printf("Order DB initialized: %s\n", ORDER_DB);
    sqlite3_close(db);
    return 0;
}

static int cmd_new(int argc, char **argv) {
    if (argc < 7) { fprintf(stderr,"Usage: %s new <agent> <asset> <yes|no> <stake> <price>\n",argv[0]); return 1; }
    int agent_id = atoi(argv[2]);
    const char *asset = argv[3];
    int dir = (strcmp(argv[4],"yes")==0||strcmp(argv[4],"1")==0) ? 1 : 0;
    float stake = atof(argv[5]), price = atof(argv[6]);
    if (stake<=0 || price<=0) { fprintf(stderr,"Invalid stake=%.2f or price=%.4f\n",stake,price); return 1; }

    sqlite3 *db = open_db();
    if (!db) return 1;
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO orders (agent_id,asset,direction,stake,price,state,created_at,updated_at) VALUES (?,?,?,?,?,?,?,?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) { sqlite3_close(db); return 1; }
    int64_t now = (int64_t)time(NULL);
    sqlite3_bind_int(stmt,1,agent_id);
    sqlite3_bind_text(stmt,2,asset,-1,SQLITE_STATIC);
    sqlite3_bind_int(stmt,3,dir);
    sqlite3_bind_double(stmt,4,stake);
    sqlite3_bind_double(stmt,5,price);
    sqlite3_bind_int(stmt,6,ORDER_NEW);
    sqlite3_bind_int64(stmt,7,now);
    sqlite3_bind_int64(stmt,8,now);
    int rc = sqlite3_step(stmt);
    int oid = (int)sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) {
        log_trans(db, oid, ORDER_NEW, ORDER_NEW, "Order placed");
        printf("Order #%d: agent=%d asset=%s dir=%s stake=$%.2f price=%.4f [%s]\n",
               oid, agent_id, asset, dir?"YES":"NO", stake, price, state_name(ORDER_NEW));
    } else { fprintf(stderr,"Failed: %s\n",sqlite3_errmsg(db)); sqlite3_close(db); return 1; }
    sqlite3_close(db);
    return 0;
}

static int cmd_fill(int argc, char **argv) {
    if (argc<4) { fprintf(stderr,"Usage: %s fill <order_id> <fill_price>\n",argv[0]); return 1; }
    int oid=atoi(argv[2]); float fp=atof(argv[3]);
    sqlite3 *db=open_db(); if(!db) return 1;
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db,"SELECT state,stake,price,direction FROM orders WHERE order_id=?",-1,&stmt,NULL);
    sqlite3_bind_int(stmt,1,oid);
    if(sqlite3_step(stmt)!=SQLITE_ROW){ fprintf(stderr,"Order #%d not found\n",oid); sqlite3_finalize(stmt); sqlite3_close(db); return 1; }
    OrderState cur=(OrderState)sqlite3_column_int(stmt,0);
    float stake=(float)sqlite3_column_double(stmt,1);
    float ep=(float)sqlite3_column_double(stmt,2);
    int dir=sqlite3_column_int(stmt,3);
    sqlite3_finalize(stmt);
    if(cur!=ORDER_NEW && cur!=ORDER_PENDING){ fprintf(stderr,"Cannot fill #%d in state %s\n",oid,state_name(cur)); sqlite3_close(db); return 1; }
    float pnl = dir ? stake*(fp-ep)/ep : stake*(ep-fp)/ep;
    int64_t now=(int64_t)time(NULL);
    sqlite3_prepare_v2(db,"UPDATE orders SET state=2,fill_price=?,pnl=?,resolved_at=?,updated_at=? WHERE order_id=?",-1,&stmt,NULL);
    sqlite3_bind_double(stmt,1,fp); sqlite3_bind_double(stmt,2,pnl);
    sqlite3_bind_int64(stmt,3,now); sqlite3_bind_int64(stmt,4,now); sqlite3_bind_int(stmt,5,oid);
    sqlite3_step(stmt); sqlite3_finalize(stmt);
    log_trans(db,oid,cur,ORDER_FILLED,NULL);
    printf("FILLED #%d: fill=%.4f pnl=$%.2f\n",oid,fp,pnl);
    sqlite3_close(db); return 0;
}

static int cmd_cancel(int argc, char **argv) {
    if (argc<3) { fprintf(stderr,"Usage: %s cancel <order_id>\n",argv[0]); return 1; }
    int oid=atoi(argv[2]);
    sqlite3 *db=open_db(); if(!db) return 1;
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db,"SELECT state FROM orders WHERE order_id=?",-1,&stmt,NULL);
    sqlite3_bind_int(stmt,1,oid);
    if(sqlite3_step(stmt)!=SQLITE_ROW){ fprintf(stderr,"Order #%d not found\n",oid); sqlite3_finalize(stmt); sqlite3_close(db); return 1; }
    OrderState cur=(OrderState)sqlite3_column_int(stmt,0);
    sqlite3_finalize(stmt);
    if(cur!=ORDER_NEW && cur!=ORDER_PENDING){ fprintf(stderr,"Cannot cancel #%d in state %s\n",oid,state_name(cur)); sqlite3_close(db); return 1; }
    int64_t now=(int64_t)time(NULL);
    sqlite3_prepare_v2(db,"UPDATE orders SET state=3,updated_at=?,resolved_at=? WHERE order_id=?",-1,&stmt,NULL);
    sqlite3_bind_int64(stmt,1,now); sqlite3_bind_int64(stmt,2,now); sqlite3_bind_int(stmt,3,oid);
    sqlite3_step(stmt); sqlite3_finalize(stmt);
    log_trans(db,oid,cur,ORDER_CANCELLED,"Manual cancel");
    printf("CANCELLED #%d\n",oid);
    sqlite3_close(db); return 0;
}

static int cmd_expire(void) {
    sqlite3 *db=open_db(); if(!db) return 1;
    int64_t cutoff=(int64_t)time(NULL)-TTL_SECONDS;
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db,"SELECT order_id,state FROM orders WHERE state IN(0,1) AND created_at<?",-1,&stmt,NULL);
    sqlite3_bind_int64(stmt,1,cutoff);
    int expired=0;
    while(sqlite3_step(stmt)==SQLITE_ROW){
        int oid=sqlite3_column_int(stmt,0);
        OrderState old_s=(OrderState)sqlite3_column_int(stmt,1);
        int64_t now=(int64_t)time(NULL);
        sqlite3_stmt *u;
        sqlite3_prepare_v2(db,"UPDATE orders SET state=4,updated_at=? WHERE order_id=?",-1,&u,NULL);
        sqlite3_bind_int64(u,1,now); sqlite3_bind_int(u,2,oid);
        sqlite3_step(u); sqlite3_finalize(u);
        char det[128]; snprintf(det,sizeof(det),"Expired: TTL %ds",TTL_SECONDS);
        log_trans(db,oid,old_s,ORDER_EXPIRED,det);
        expired++;
    }
    sqlite3_finalize(stmt);
    printf("Expired %d stale order(s) (TTL %ds)\n",expired,TTL_SECONDS);
    sqlite3_close(db); return 0;
}

static int cmd_status(int argc, char **argv) {
    if(argc<3){ fprintf(stderr,"Usage: %s status <order_id>\n",argv[0]); return 1; }
    int oid=atoi(argv[2]);
    sqlite3 *db=open_db(); if(!db) return 1;
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db,"SELECT order_id,agent_id,asset,direction,stake,price,fill_price,pnl,state,created_at,resolved_at FROM orders WHERE order_id=?",-1,&stmt,NULL);
    sqlite3_bind_int(stmt,1,oid);
    if(sqlite3_step(stmt)==SQLITE_ROW){
        printf("=== Order #%d ===\n",sqlite3_column_int(stmt,0));
        printf("  Agent:     %d\n",sqlite3_column_int(stmt,1));
        printf("  Asset:     %s\n",sqlite3_column_text(stmt,2));
        printf("  Direction: %s\n",sqlite3_column_int(stmt,3)?"YES":"NO");
        printf("  Stake:     $%.2f\n",sqlite3_column_double(stmt,4));
        printf("  Entry:     %.4f\n",sqlite3_column_double(stmt,5));
        if(sqlite3_column_type(stmt,6)!=SQLITE_NULL) printf("  Fill:      %.4f\n",sqlite3_column_double(stmt,6));
        if(sqlite3_column_type(stmt,7)!=SQLITE_NULL) printf("  PnL:       $%.4f\n",sqlite3_column_double(stmt,7));
        printf("  State:     %s\n",state_name((OrderState)sqlite3_column_int(stmt,8)));
    } else { printf("Order #%d not found\n",oid); }
    sqlite3_finalize(stmt); sqlite3_close(db); return 0;
}

static int cmd_stats(void) {
    sqlite3 *db=open_db(); if(!db) return 1;
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db,"SELECT COUNT(*),SUM(state=0),SUM(state=1),SUM(state=2),SUM(state=3),SUM(state=4),SUM(state=5),SUM(CASE WHEN state=2 THEN pnl ELSE 0 END),AVG(CASE WHEN state=2 AND pnl IS NOT NULL THEN pnl ELSE NULL END),SUM(state=2 AND pnl>0),SUM(state=2 AND pnl<=0) FROM orders",-1,&stmt,NULL);
    if(sqlite3_step(stmt)==SQLITE_ROW){
        int total=sqlite3_column_int(stmt,0),newc=sqlite3_column_int(stmt,1),pendc=sqlite3_column_int(stmt,2);
        int fillc=sqlite3_column_int(stmt,3),cancelc=sqlite3_column_int(stmt,4),expc=sqlite3_column_int(stmt,5);
        int rejectc=sqlite3_column_int(stmt,6);
        double totpnl=sqlite3_column_double(stmt,7),avgpnl=sqlite3_column_double(stmt,8);
        int wins=sqlite3_column_int(stmt,9),losses=sqlite3_column_int(stmt,10);
        int resolved=wins+losses;
        printf("=== Order Mgmt Stats ===\n");
        printf("  Total: %d | NEW: %d | PENDING: %d | FILLED: %d\n",total,newc,pendc,fillc);
        printf("  CANCELLED: %d | EXPIRED: %d | REJECTED: %d\n",cancelc,expc,rejectc);
        printf("  Total PnL: $%.2f | Avg: $%.4f\n",totpnl,avgpnl);
        printf("  Win rate: %d/%d (%.1f%%)\n",wins,resolved,resolved>0?(double)wins/resolved*100:0);
    }
    sqlite3_finalize(stmt); sqlite3_close(db); return 0;
}

int main(int argc, char **argv) {
    if(argc<2){
        fprintf(stderr,"Usage: %s <init|new|fill|cancel|expire|status|list|stats|journal>\n",argv[0]); return 1;
    }
    if(strcmp(argv[1],"init")==0) return cmd_init();
    if(strcmp(argv[1],"new")==0) return cmd_new(argc,argv);
    if(strcmp(argv[1],"fill")==0) return cmd_fill(argc,argv);
    if(strcmp(argv[1],"cancel")==0) return cmd_cancel(argc,argv);
    if(strcmp(argv[1],"expire")==0) return cmd_expire();
    if(strcmp(argv[1],"status")==0) return cmd_status(argc,argv);
    if(strcmp(argv[1],"stats")==0) return cmd_stats();
    if(strcmp(argv[1],"list")==0){
        sqlite3 *db=open_db(); if(!db) return 1;
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db,"SELECT order_id,agent_id,asset,direction,stake,price,state,created_at FROM orders ORDER BY order_id DESC LIMIT 100",-1,&stmt,NULL);
        printf("  %-6s %-6s %-6s %-4s %-10s %-10s %-10s %s\n","OID","Agent","Asset","Dir","Stake","Price","State","Created");
        int cnt=0;
        while(sqlite3_step(stmt)==SQLITE_ROW){
            int64_t ts=sqlite3_column_int64(stmt,7); time_t tt=(time_t)ts; char tb[20]; strftime(tb,sizeof(tb),"%m/%d %H:%M",localtime(&tt));
            printf("  %-6d %-6d %-6s %-4s $%-9.2f %-10.4f %-10s %s\n",
                sqlite3_column_int(stmt,0),sqlite3_column_int(stmt,1),
                sqlite3_column_text(stmt,2),sqlite3_column_int(stmt,3)?"YES":"NO",
                sqlite3_column_double(stmt,4),sqlite3_column_double(stmt,5),
                state_name((OrderState)sqlite3_column_int(stmt,6)),tb);
            cnt++;
        }
        printf("  %d order(s)\n",cnt);
        sqlite3_finalize(stmt); sqlite3_close(db); return 0;
    }
    if(strcmp(argv[1],"journal")==0){
        printf("order_id,agent_id,asset,direction,stake,price,fill_price,pnl,state,created_at\n");
        sqlite3 *db=open_db(); if(!db) return 1;
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db,"SELECT order_id,agent_id,asset,direction,stake,price,fill_price,pnl,state,created_at FROM orders ORDER BY order_id",-1,&stmt,NULL);
        while(sqlite3_step(stmt)==SQLITE_ROW){
            printf("%d,%d,%s,%d,%.2f,%.4f,",
                sqlite3_column_int(stmt,0),sqlite3_column_int(stmt,1),
                sqlite3_column_text(stmt,2),sqlite3_column_int(stmt,3),
                sqlite3_column_double(stmt,4),sqlite3_column_double(stmt,5));
            if(sqlite3_column_type(stmt,6)!=SQLITE_NULL) printf("%.4f",sqlite3_column_double(stmt,6)); printf(",");
            if(sqlite3_column_type(stmt,7)!=SQLITE_NULL) printf("%.4f",sqlite3_column_double(stmt,7)); printf(",");
            printf("%s,%lld\n",state_name((OrderState)sqlite3_column_int(stmt,8)),(long long)sqlite3_column_int64(stmt,9));
        }
        sqlite3_finalize(stmt); sqlite3_close(db); return 0;
    }
    fprintf(stderr,"Unknown: %s\n",argv[1]); return 1;
}
