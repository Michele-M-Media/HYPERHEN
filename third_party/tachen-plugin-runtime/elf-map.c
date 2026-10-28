// SPDX-License-Identifier: GPL-3.0-or-later
#include "elf-image.h"
#include <onion/pt.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ps5/kernel.h>

static int page_protection(const uint8_t *data,const HhpElfImage *image,uint64_t page){
  unsigned flags=0;
  for(unsigned i=0;i<image->header.phnum;i++){
    HhpElfProgram p;memcpy(&p,data+image->header.phoff+i*sizeof p,sizeof p);
    if(p.type==1&&p.memsz&&p.vaddr<page+16384&&page<p.vaddr+p.memsz)flags|=p.flags;
  }
  return ((flags&1)?PROT_EXEC:0)|((flags&2)?PROT_WRITE:0)|((flags&4)?PROT_READ:0);
}

intptr_t hhp_elf_map(pid_t pid,const uint8_t *data,size_t size){
  HhpElfImage image;char reason[128];
  if(hhp_elf_inspect(data,size,&image,reason,sizeof reason))return 0;
  size_t bytes=(size_t)(image.highest-image.lowest);
  void *mirror=malloc(bytes);if(!mirror)return 0;
  int flags=MAP_PRIVATE|MAP_ANONYMOUS;
  intptr_t wanted=image.header.type==2?(intptr_t)image.lowest:0;
  if(image.header.type==2)flags|=MAP_FIXED;
  intptr_t base=pt_mmap(pid,wanted,bytes,PROT_READ|PROT_WRITE,flags,-1,0);
  if(base==-1){free(mirror);return 0;}
  int failed=hhp_elf_materialize(data,size,&image,(uint64_t)base,mirror,bytes);
  if(!failed)failed=pt_copyin(pid,mirror,base,bytes);
  free(mirror);
  // SDK images may place adjacent segments in one 16 KiB page. Apply the union
  // once per page, coalescing matching pages; never let a later data segment
  // remove execute access from the preceding text segment's shared page.
  for(uint64_t start=image.lowest;start<image.highest&&!failed;){
    int prot=page_protection(data,&image,start);uint64_t end=start+16384;
    while(end<image.highest&&page_protection(data,&image,end)==prot)end+=16384;
    intptr_t at=base+(intptr_t)(start-image.lowest);size_t length=(size_t)(end-start);
    failed=(prot&PROT_EXEC)?kernel_mprotect(pid,at,length,prot):pt_mprotect(pid,at,length,prot);start=end;
  }
  if(!failed)failed=pt_msync(pid,base,bytes,MS_SYNC);
  if(failed){pt_munmap(pid,base,bytes);return 0;}
  return base+(intptr_t)(image.header.entry-image.lowest);
}

