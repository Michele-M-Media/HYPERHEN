/* Execute the production post-pipeline orchestration with bounded doubles. */
#include "bootstrap_hyperdeck.h"
#include "bootstrap_notify.h"
#include <onion/firmware_capabilities.h>
#include <onion/log.h>
#include <onion/ready.h>
#include <onion/proc_query.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifndef HYPERHEN_HYPERDECK_MEDIA_LAUNCHER
#define HYPERHEN_HYPERDECK_MEDIA_LAUNCHER 0
#endif
#define EXPECTED_INSTALLS HYPERHEN_HYPERDECK_MEDIA_LAUNCHER

static int disabled,ready,alive,named,toolbox_ready,listener,spawn_ok,listener_after_spawn;
static int spawns,clears,http_polls,toolbox_polls,failed,failure_after_spawn,ready_delay;
static int notifications,installs,install_result,install_fail_once,probe_ok,probes,records;
static uint64_t now_ms;
static char last_stage[64];
volatile int onion_log_runtime_level=ONION_LOG_OFF;
void onion_log_write(onion_log_level level,const char *fmt,...){(void)level;(void)fmt;}
void bootstrap_notify(const char *fmt,...){assert(fmt && *fmt);notifications++;}
void bootstrap_hyperdeck_record(const char *stage,int result){
  (void)result;records++;snprintf(last_stage,sizeof(last_stage),"%s",stage);
}
int __wrap_access(const char *path,int mode){
  (void)mode;assert(!strcmp(path,"/data/HyperHEN/no_hyperdeck"));return disabled?0:-1;
}
int __wrap_usleep(useconds_t delay){
  now_ms+=delay/1000;
  if(delay==100000){
    http_polls++;if(ready_delay>0 && http_polls>=ready_delay)listener=1;
  }else{assert(delay==250000);toolbox_polls++;}
  return 0;
}
int __wrap_clock_gettime(clockid_t clock,struct timespec *out){
  assert(clock==CLOCK_MONOTONIC);out->tv_sec=now_ms/1000;out->tv_nsec=(now_ms%1000)*1000000;return 0;
}
bool onion_ready_read_pid(const char *marker,pid_t *pid){
  if(!strcmp(marker,ONION_READY_HYPERDECK)){*pid=42;return ready;}
  if(!strcmp(marker,ONION_READY_HYPERDECK_LISTENER)){*pid=42;return listener;}
  if(!strcmp(marker,ONION_READY_HYPERDECK_FAILED)){*pid=42;return failed;}
  assert(!strcmp(marker,ONION_READY_TOOLBOX));*pid=55;return toolbox_ready;
}
bool onion_ready_signal_pid(const char *marker,pid_t pid){
  assert(!strcmp(marker,ONION_READY_HYPERDECK) && pid==42 && probes>0);
  assert(EXPECTED_INSTALLS ? installs>0 : installs==0);
  ready=1;return true;
}
bool onion_ready_clear(const char *marker){
  if(!strcmp(marker,ONION_READY_HYPERDECK))ready=0;
  else if(!strcmp(marker,ONION_READY_HYPERDECK_FAILED))failed=0;
  else{assert(!strcmp(marker,ONION_READY_HYPERDECK_LISTENER));listener=0;}
  clears++;return true;
}
bool onion_proc_is_alive(pid_t pid){assert(pid==42 || pid==55);return pid==55?toolbox_ready:alive;}
pid_t onion_find_pid(const char *name){
  if(!strcmp(name,"hyperdeck.elf"))return named?42:-1;
  assert(!strcmp(name,"SceShellUI"));return toolbox_ready?55:-1;
}
pid_t bootstrap_hyperdeck_spawn(const uint8_t *elf,size_t size){
  assert(elf && size>4 && elf[0]==127 && elf[1]=='E');spawns++;
  if(!spawn_ok)return -1;
  named=alive=1;listener=listener_after_spawn;failed=failure_after_spawn;return 42;
}
bool bootstrap_hyperdeck_http_probe(void){assert(listener && alive);probes++;return probe_ok;}
int bootstrap_hyperdeck_install_media(void){
  assert(listener && alive && probe_ok && probes>0);installs++;
  return install_fail_once && installs==1?-1:install_result;
}
static void reset(void){
  disabled=ready=named=listener=failed=failure_after_spawn=ready_delay=0;
  notifications=spawns=clears=http_polls=toolbox_polls=installs=probes=records=0;
  install_result=install_fail_once=0;
  alive=toolbox_ready=spawn_ok=listener_after_spawn=probe_ok=1;
  now_ms=1000;last_stage[0]=0;
}
static void check_all_exact_profiles(void){
  static const uint32_t profiles[]={
    0x01000000U,0x01010000U,0x01020000U,0x01050000U,0x01100000U,0x01110000U,0x01120000U,0x01130000U,0x01140000U,
    0x02000000U,0x02200000U,0x02250000U,0x02300000U,0x02500000U,0x02700000U,0x03000000U,0x03100000U,
    0x03200000U,0x03210000U,0x04000000U,0x04020000U,0x04030000U,0x04500000U,0x04510000U,
    0x05000000U,0x05020000U,0x05100000U,0x05500000U,0x06000000U,0x06020000U,0x06500000U,0x07000000U,0x07010000U,
    0x07200000U,0x07400000U,0x07600000U,0x07610000U,0x08000000U,0x08200000U,0x08400000U,0x08600000U,0x09000000U,
    0x09050000U,0x09200000U,0x09400000U,0x09600000U,0x10000000U,0x10010000U,0x10200000U,0x10400000U,0x10600000U,
    0x11000000U,0x11200000U,0x11400000U,0x11600000U,0x12000000U,0x12020000U,0x12200000U,0x12400000U,0x12600000U,
    0x12700000U,0x13000000U,0x13200000U,0x13400000U,0x13420000U,0x13600000U
  };
  for(size_t i=0;i<sizeof(profiles)/sizeof(profiles[0]);i++){
    const uint32_t caps=onion_firmware_capabilities(onion_firmware_route(profiles[i]));
    assert(caps!=0);reset();toolbox_ready=0;bootstrap_hyperdeck_start(profiles[i]);
    if(caps&ONION_FIRMWARE_CAP_TOOLBOX)assert(!spawns && toolbox_polls==360 && !installs);
    else assert(spawns==1 && !toolbox_polls && installs==EXPECTED_INSTALLS && ready);
    reset();bootstrap_hyperdeck_start(profiles[i]);assert(spawns==1 && installs==EXPECTED_INSTALLS && ready);
  }
}
int main(void){
#if !HYPERHEN_HYPERDECK_MEDIA_LAUNCHER
  reset();bootstrap_hyperdeck_start(0x10010000U);assert(spawns==1 && !installs && ready);
  reset();install_result=-1;bootstrap_hyperdeck_start(0x10010000U);assert(!installs && ready);
  reset();disabled=1;bootstrap_hyperdeck_start(0x10010000U);assert(!spawns && !installs);
  reset();bootstrap_hyperdeck_start(0x13990000U);assert(!spawns && !installs);
  reset();listener_after_spawn=0;bootstrap_hyperdeck_start(0x10010000U);assert(!ready && !installs);
  reset();probe_ok=0;bootstrap_hyperdeck_start(0x10010000U);assert(!ready && !installs);
  reset();ready=listener=named=1;bootstrap_hyperdeck_start(0x10010000U);assert(!spawns && !installs && ready);
  check_all_exact_profiles();
  puts("PASS: Media-launcher-disabled startup makes zero title registrations across all exact profile gates; missing listener/disabled/unsupported routes stay closed. Host test, not firmware hardware certification.");
#else
  reset();bootstrap_hyperdeck_start(0x10010000U);
  assert(spawns==1 && installs==1 && ready && notifications==1 && !strcmp(last_stage,"ready"));
  reset();disabled=1;bootstrap_hyperdeck_start(0x10010000U);assert(!spawns && !records && !installs);
  reset();bootstrap_hyperdeck_start(0x13990000U);assert(!spawns && !records && !installs);
  reset();ready=listener=named=1;bootstrap_hyperdeck_start(0x10010000U);assert(!spawns && installs==1 && ready);
  reset();listener=named=1;alive=0;bootstrap_hyperdeck_start(0x10010000U);assert(spawns==1 && installs==1);
  reset();spawn_ok=0;bootstrap_hyperdeck_start(0x10010000U);assert(spawns==1 && !installs && !ready);
  reset();listener_after_spawn=0;bootstrap_hyperdeck_start(0x10010000U);assert(http_polls==200 && !installs && !ready);
  reset();listener_after_spawn=0;ready_delay=70;bootstrap_hyperdeck_start(0x10010000U);assert(http_polls==70 && installs==1 && ready);
  reset();listener_after_spawn=0;failure_after_spawn=1;bootstrap_hyperdeck_start(0x10010000U);assert(!installs && !http_polls);
  reset();probe_ok=0;bootstrap_hyperdeck_start(0x10010000U);assert(!installs && !ready && !strcmp(last_stage,"http_probe_timeout"));
  reset();install_fail_once=1;bootstrap_hyperdeck_start(0x10010000U);assert(installs==1 && ready);
  reset();install_result=-1;bootstrap_hyperdeck_start(0x10010000U);assert(installs==1 && ready);
  reset();toolbox_ready=0;bootstrap_hyperdeck_start(0x10010000U);assert(!spawns && !installs && toolbox_polls==360);
  reset();named=1;listener_after_spawn=0;ready_delay=3;bootstrap_hyperdeck_start(0x10010000U);
  assert(!spawns && http_polls==3 && installs==1 && ready);
  check_all_exact_profiles();
  puts("HyperDeck bootstrap: exact-profile gates, real-PID/HTTP-before-Media order, missing/delayed listener, duplicate/stale PID, failed spawn and deferred Media registration checks passed.");
#endif
}
