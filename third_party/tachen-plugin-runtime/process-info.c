// SPDX-License-Identifier: GPL-3.0-or-later
#include "process-info.h"
#include <sys/sysctl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int hhp_process_info(pid_t pid,struct kinfo_proc* out){
 if(!out||pid<=0){errno=EINVAL;return -1;}
 memset(out,0,sizeof *out);
 // PS5 13.60 returns 1096 bytes while SDK 0.43 describes a 1088-byte
 // prefix. Ask the kernel for its complete record before copying that prefix.
 const size_t start_end=offsetof(struct kinfo_proc,ki_start)+sizeof out->ki_start;
 const size_t name_end=offsetof(struct kinfo_proc,ki_comm)+sizeof out->ki_comm;
 const size_t minimum=start_end>name_end?start_end:name_end;
 int mib[]={CTL_KERN,KERN_PROC,KERN_PROC_PID,pid};size_t needed=0;
 if(sysctl(mib,4,NULL,&needed,NULL,0))return -1;
 if(needed<minimum||needed>65536){errno=EINVAL;return -1;}
 void* record=malloc(needed);if(!record){errno=ENOMEM;return -1;}
 size_t length=needed;int result=sysctl(mib,4,record,&length,NULL,0),error=errno;
 if(!result){
  int declared=0;
  if(length>=sizeof declared&&length<=needed)memcpy(&declared,record,sizeof declared);
  if(length<minimum||length>needed||declared<0||(size_t)declared!=length){result=-1;error=EINVAL;}
  else{
   struct kinfo_proc info={0};memcpy(&info,record,length<sizeof info?length:sizeof info);
   if(info.ki_pid!=pid){result=-1;error=ESRCH;}
   else{info.ki_comm[COMMLEN]=0;*out=info;}
  }
 }
 free(record);if(result)errno=error;return result?-1:0;
}

