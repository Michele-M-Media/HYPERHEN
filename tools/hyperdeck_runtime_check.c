/* Exercise spawn context cleanup; --probe also runs the real HTTP verifier. */
#include "bootstrap_hyperdeck.h"
#include <onion/elfldr.h>
#include <onion/log.h>
#include <ps5/kernel.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint64_t auth=0x1234;
static int spawn_result=42,spawns,auth_writes,fail_open,fail_auth,fail_sigaction;
static char log_path[256];
volatile int onion_log_runtime_level=ONION_LOG_OFF;
void onion_log_write(onion_log_level level,const char *format,...){(void)level;(void)format;}
int __real_open(const char *,int,...);
int __real_sigaction(int,const struct sigaction *,struct sigaction *);
int __wrap_open(const char *path,int flags,...){
  assert(!strcmp(path,"/data/HyperHEN/HyperDeck.log"));
  if(fail_open){errno=EACCES;return -1;}
  return __real_open(log_path,flags,0600);
}
int __wrap_sigaction(int sig,const struct sigaction *act,struct sigaction *old){
  assert(sig==SIGCHLD);
  if(fail_sigaction && act && act->sa_handler==SIG_DFL){errno=EINVAL;return -1;}
  return __real_sigaction(sig,act,old);
}
uint64_t kernel_get_ucred_authid(pid_t pid){assert(pid==-1);return auth;}
int32_t kernel_set_ucred_authid(pid_t pid,uint64_t value){
  assert(pid==-1);auth_writes++;
  if(fail_auth && value==0x4800000000010003ULL)return -1;
  auth=value;return 0;
}
pid_t elfldr_spawn(int fd,char *const argv[],uint8_t *elf,size_t size){
  assert(auth==0x4800000000010003ULL && fd>=0 && fcntl(fd,F_GETFD)>=0);
  assert(argv && !strcmp(argv[0],"hyperdeck.elf") && !argv[1]);
  assert(size==4 && elf[0]==127);
  struct sigaction current;
  assert(__real_sigaction(SIGCHLD,NULL,&current)==0 && current.sa_handler==SIG_DFL);
  assert(write(fd,"child startup captured\n",23)==23);
  spawns++;return spawn_result;
}
static void reset(void){
  auth=0x1234;spawns=auth_writes=fail_open=fail_auth=fail_sigaction=0;spawn_result=42;
}
static void restored(void){
  struct sigaction current;
  assert(auth==0x1234 && __real_sigaction(SIGCHLD,NULL,&current)==0 && current.sa_handler==SIG_IGN);
}
int main(int argc,char **argv){
  if(argc==2 && !strcmp(argv[1],"--probe"))return bootstrap_hyperdeck_http_probe()?0:1;
  char root[]="/tmp/hyperdeck-runtime.XXXXXX";
  assert(mkdtemp(root));snprintf(log_path,sizeof(log_path),"%s/HyperDeck.log",root);
  signal(SIGCHLD,SIG_IGN);
  const uint8_t elf[]={127,'E','L','F'};
  bootstrap_hyperdeck_record("before_spawn",0);
  reset();assert(bootstrap_hyperdeck_spawn(elf,sizeof(elf))==42 && spawns==1 && auth_writes==2);restored();
  reset();spawn_result=-1;assert(bootstrap_hyperdeck_spawn(elf,sizeof(elf))==-1 && spawns==1 && auth_writes==2);restored();
  reset();fail_open=1;assert(bootstrap_hyperdeck_spawn(elf,sizeof(elf))==-1 && !spawns && !auth_writes);restored();
  reset();fail_auth=1;assert(bootstrap_hyperdeck_spawn(elf,sizeof(elf))==-1 && !spawns && auth_writes==1);restored();
  reset();fail_sigaction=1;assert(bootstrap_hyperdeck_spawn(elf,sizeof(elf))==-1 && !spawns && !auth_writes);restored();
  FILE *log=fopen(log_path,"r");assert(log);
  char data[512];size_t size=fread(data,1,sizeof(data)-1,log);fclose(log);data[size]=0;
  assert(strstr(data,"stage=before_spawn") && strstr(data,"child startup captured"));
  puts("HyperDeck runtime: parent creates log, child stdio capture and spawn credential/SIGCHLD restoration passed.");
}
