// SPDX-License-Identifier: GPL-3.0-or-later
#include "game-source.h"
#include "game-info.h"
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include "process-info.h"
#include <sys/time.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ps5/kernel.h>
static int (*getInfo)(int,void*);static int resolved;
int hhp_game_info_available(void){
 if(!resolved){getInfo=dlsym(RTLD_DEFAULT,"sceKernelGetAppInfo");resolved=1;}
 return getInfo!=NULL;
}
int hhp_game_app_info(int pid,HhpAppInfo* out){
 if(!hhp_game_info_available())return -1;
 _Alignas(8) unsigned char bytes[96]={0};if(getInfo(pid,bytes))return -1;
 return hhp_app_info_decode(bytes,sizeof bytes,out);
}
int hhp_game_identity(int pid,const char* generation,const char* title){
 struct timeval boot;size_t size=sizeof boot;int bm[]={CTL_KERN,KERN_BOOTTIME};
 if(sysctl(bm,2,&boot,&size,NULL,0)||size!=sizeof boot)return 0;
 struct kinfo_proc info={0};
 if(hhp_process_info(pid,&info)||info.ki_start.tv_sec<=0||info.ki_start.tv_usec<0||info.ki_start.tv_usec>=1000000)return 0;
 char actual[128];snprintf(actual,sizeof actual,"%lld:%lld/%d/%lld:%lld",(long long)boot.tv_sec,(long long)boot.tv_usec,pid,(long long)info.ki_start.tv_sec,(long long)info.ki_start.tv_usec);
 HhpAppInfo app;return !strcmp(actual,generation)&&!hhp_game_app_info(pid,&app)&&!strcmp(app.title,title);
}
static int sandbox_name(const char* entry,const char* title){
 if(strncmp(entry,title,9)||entry[9]!='_')return 0;size_t n=strlen(entry+10);if(!n||n>6)return 0;
 for(size_t i=10;entry[i];i++)if(entry[i]<'0'||entry[i]>'9')return 0;return 1;
}
int hhp_game_sandbox_open_path(int pid,const char* generation,const char* title,char* path,size_t pathCapacity,char* message,size_t capacity){
 int root=-1;DIR* directory=NULL;const char* reason="Running application sandbox could not be opened";
 if(path&&pathCapacity)path[0]=0;
 if(!generation||!title||pid<=1||strlen(generation)>=128||!hhp_game_title(title)||!hhp_game_identity(pid,generation,title)){
  reason="Application identity changed before metadata was read";goto done;
 }
 // SDK returns the directory vnode for an open directory. Match the process
 // root, not a title prefix that may also match abandoned game sandboxes.
 intptr_t gameRoot=kernel_get_proc_rootdir(pid);if(!gameRoot)goto done;
 intptr_t expected=gameRoot,globalRoot=kernel_get_root_vnode();
 if(expected==globalRoot){intptr_t jail=kernel_get_proc_jaildir(pid);if(jail&&jail!=globalRoot)expected=jail;}
 int global=expected==globalRoot;
 directory=opendir("/mnt/sandbox");if(!directory)goto done;struct dirent* entry;char selected[32]={0};unsigned count=0;
 while((entry=readdir(directory))){
  if(++count>4096){if(root>=0)close(root);root=-1;reason="Sandbox listing exceeded its bounded inspection limit";goto done;}
  if(!sandbox_name(entry->d_name,title))continue;
  int candidate=openat(dirfd(directory),entry->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(candidate<0)continue;
  if(!global&&kernel_get_proc_file(getpid(),candidate)!=expected){close(candidate);continue;}
  // Some legitimate game loaders deliberately jailbreak both root/jaildir.
  // In that case retain the unique active app0 fallback, ignoring empty stale
  // directories, rather than requiring the game to remain sandboxed.
  if(global){int app=openat(candidate,"app0",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(app<0){close(candidate);continue;}close(app);
   if(root>=0){close(candidate);close(root);root=-1;reason="More than one active sandbox matches the game";goto done;}}
  root=candidate;snprintf(selected,sizeof selected,"%s",entry->d_name);if(!global)break;
 }
 if(root<0){reason="The running application's sandbox is not ready";goto done;}
 if(!hhp_game_identity(pid,generation,title)||kernel_get_proc_rootdir(pid)!=gameRoot){close(root);root=-1;reason="Game instance changed while opening its sandbox";}
 if(root>=0&&path&&pathCapacity){int n=snprintf(path,pathCapacity,"/mnt/sandbox/%s",selected);if(n<0||(size_t)n>=pathCapacity){close(root);root=-1;path[0]=0;}}
done:
 if(directory)closedir(directory);if(message&&capacity)snprintf(message,capacity,"%s",root<0?reason:"");return root;
}
int hhp_game_sandbox_open(int pid,const char* generation,const char* title,char* message,size_t capacity){
 return hhp_game_sandbox_open_path(pid,generation,title,NULL,0,message,capacity);
}
#include "game-metadata-time.h"
static int registered_open(const char* title){
 const char* parts[]={"system_data","priv","appmeta",title};
 int fd=open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(fd<0)return -1;
 for(unsigned i=0;i<4;i++){int next=openat(fd,parts[i],O_RDONLY|O_DIRECTORY|O_NOFOLLOW);int error=errno;close(fd);fd=next;errno=error;if(fd<0)break;}
 return fd;
}
int hhp_game_metadata_read_ex(int pid,const char* generation,const char* title,unsigned char** output,size_t* length,char* message,size_t capacity,int* registered){
 int result=-1,root=-1,app=-1,sys=-1,file=-1;unsigned char* bytes=NULL;
 int fallback=0;struct kinfo_proc process={0};if(registered)*registered=0;
 const char* reason="Running application metadata could not be read";char sandbox_reason[192],stat_reason[192];*output=NULL;*length=0;
 root=hhp_game_sandbox_open(pid,generation,title,sandbox_reason,sizeof sandbox_reason);
 if(root<0){reason=sandbox_reason;goto done;}
 app=openat(root,"app0",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(app<0)goto done;
 sys=openat(app,"sce_sys",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(sys<0)goto done;
 int ps5=!memcmp(title,"PPS",3)||!memcmp(title,"MOUU",4);const char* name=ps5?"param.json":"param.sfo";
 file=openat(sys,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
 if(file<0&&!ps5&&errno==ENOENT){
  // Do not substitute cached metadata for an inaccessible/corrupt mounted
  // file, or for a sandbox that could not itself be verified and opened.
  close(sys);sys=registered_open(title);if(sys<0)goto done;
  file=openat(sys,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);fallback=1;
 }
 if(file<0)goto done;
 struct stat before,after,named;
 if(fstat(file,&before)){snprintf(stat_reason,sizeof stat_reason,"Game metadata stat failed (errno %d)",errno);reason=stat_reason;goto done;}
 if(!S_ISREG(before.st_mode)||before.st_size<20||before.st_size>1024*1024){
  snprintf(stat_reason,sizeof stat_reason,"Game metadata has an invalid type or size (mode %o, bytes %lld)",(unsigned)before.st_mode,(long long)before.st_size);reason=stat_reason;goto done;
 }
 if(fallback&&(hhp_process_info(pid,&process)||
   !hhp_metadata_predates_process(before.st_mtim.tv_sec,before.st_mtim.tv_nsec,before.st_ctim.tv_sec,before.st_ctim.tv_nsec,
     process.ki_start.tv_sec,process.ki_start.tv_usec))){
  reason="Registered game metadata changed after this game started; restart the game before using game features";goto done;
 }
 size_t size=(size_t)before.st_size;bytes=malloc(size);if(!bytes){reason="Not enough memory for game metadata";goto done;}
 for(size_t at=0;at<size;){ssize_t n=read(file,bytes+at,size-at);if(n<0&&errno==EINTR)continue;if(n<=0){reason="Game metadata read ended early";goto done;}at+=(size_t)n;}
 if(fstat(file,&after)||fstatat(sys,name,&named,AT_SYMLINK_NOFOLLOW)||!S_ISREG(named.st_mode)||
   named.st_dev!=before.st_dev||named.st_ino!=before.st_ino||after.st_size!=before.st_size||named.st_size!=before.st_size||
   after.st_mtim.tv_sec!=before.st_mtim.tv_sec||after.st_mtim.tv_nsec!=before.st_mtim.tv_nsec||
   after.st_ctim.tv_sec!=before.st_ctim.tv_sec||after.st_ctim.tv_nsec!=before.st_ctim.tv_nsec||
   named.st_mtim.tv_sec!=before.st_mtim.tv_sec||named.st_mtim.tv_nsec!=before.st_mtim.tv_nsec||
   named.st_ctim.tv_sec!=before.st_ctim.tv_sec||named.st_ctim.tv_nsec!=before.st_ctim.tv_nsec||!hhp_game_identity(pid,generation,title)){
  reason="Game instance or mounted metadata changed during inspection";goto done;
 }
 *output=bytes;*length=size;bytes=NULL;result=0;if(registered)*registered=fallback;
done:
 free(bytes);if(file>=0)close(file);if(sys>=0)close(sys);if(app>=0)close(app);if(root>=0)close(root);
 if(message&&capacity)snprintf(message,capacity,"%s",result?reason:"");return result;
}
int hhp_game_metadata_read(int pid,const char* generation,const char* title,unsigned char** output,size_t* length,char* message,size_t capacity){
 return hhp_game_metadata_read_ex(pid,generation,title,output,length,message,capacity,NULL);
}

