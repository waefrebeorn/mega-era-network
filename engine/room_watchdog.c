/**
 * room_watchdog.c — Room health watchdog for ALL 16 rooms
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

#define MAX_AGE_SECS  300
#define HB_DIR        "/home/wubu2/.hermes/infra/heartbeats"
#define ROOMS_ROOT    "/home/wubu2/.hermes/pm_logs/rooms"

static const char *ALL_ROOMS[16] = {
    "btc_main","consensus","crypto_prices","economic",
    "elections","kalshi","macro","manifold",
    "momentum","options","polymarket","predictit",
    "science_tech","sports","stocks","weather"
};

static void log_msg(const char *msg){time_t n=time(NULL);struct tm*tm=localtime(&n);printf("[wd] %02d:%02d:%02d %s\n",tm->tm_hour,tm->tm_min,tm->tm_sec,msg);fflush(stdout);}
static void write_heartbeat(const char *name){mkdir(HB_DIR,0755);char p[256];snprintf(p,sizeof(p),"%s/%s.heartbeat",HB_DIR,name);FILE*f=fopen(p,"w");if(f){fprintf(f,"%ld",(long)time(NULL));fclose(f);}}
static int snapshot_fresh(const char*r){char p[512];snprintf(p,sizeof(p),"%s/%s/room_snapshot.json",ROOMS_ROOT,r);struct stat st;if(stat(p,&st)!=0)return 0;return(difftime(time(NULL),st.st_mtime)<MAX_AGE_SECS);}
static int engine_exists(const char*p){struct stat st;return(stat(p,&st)==0&&(st.st_mode&S_IXUSR));}
static void cycle_engine(const char*n,const char*w,const char*e){char cmd[1024];snprintf(cmd,sizeof(cmd),"cd %s && ROOM_DIR=%s timeout 120 %s >/dev/null 2>&1",w,w,e);char m[128];if(system(cmd)==0){snprintf(m,sizeof(m),"%s cycled OK",n);write_heartbeat(n);}else{snprintf(m,sizeof(m),"%s cycle FAILED",n);}log_msg(m);}
static void cycle_all(void){for(int i=0;i<16;i++){char w[512],e[512],v[512];snprintf(w,sizeof(w),"%s/%s",ROOMS_ROOT,ALL_ROOMS[i]);snprintf(e,sizeof(e),"%s/room_engine",w);snprintf(v,sizeof(v),"%s/room_engine_v3",w);if(engine_exists(v))cycle_engine(ALL_ROOMS[i],w,"./room_engine_v3");else if(engine_exists(e))cycle_engine(ALL_ROOMS[i],w,"./room_engine");else{char m[128];snprintf(m,sizeof(m),"%s: no engine",ALL_ROOMS[i]);log_msg(m);}}}
int main(){int a=1;for(int i=0;i<16;i++)if(!snapshot_fresh(ALL_ROOMS[i])){a=0;break;}if(a){log_msg("All 16 rooms healthy");for(int i=0;i<16;i++)write_heartbeat(ALL_ROOMS[i]);write_heartbeat("rooms");return 0;}log_msg("Some stale — cycling all");cycle_all();a=1;for(int i=0;i<16;i++)if(!snapshot_fresh(ALL_ROOMS[i])){a=0;break;}if(a){log_msg("All fresh after cycle");write_heartbeat("rooms");}else{log_msg("Still stale");}return 0;}
