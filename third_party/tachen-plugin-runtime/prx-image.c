// SPDX-License-Identifier: GPL-3.0-or-later
// Container fields follow the credited etaHEN port_prx.h; complete-buffer
// validation also bounds mappings, alignment, overlap and dynamic metadata.
#include "prx-image.h"
#include "elf-image.h"
#include <stdio.h>
#include <string.h>
#define LIMIT (UINT64_C(512)*1024*1024)
static uint16_t u16(const unsigned char *p){uint16_t v;memcpy(&v,p,2);return v;}
static uint32_t u32(const unsigned char *p){uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t u64(const unsigned char *p){uint64_t v;memcpy(&v,p,8);return v;}
int hhp_prx_inspect(const void *data,size_t size,int *platform,char *reason,size_t capacity){
  const unsigned char *p=data;size_t off=0,containerSize=size;const char *error="Invalid native module container";
  if(!p||!platform||size<64||size>128*1024*1024)goto bad;*platform=0;
  uint32_t magic=u32(p);
  if(magic==0x1d3d154f||magic==0xeef51454){
    // PS5 development FSELFs share the older magic. Unknown platform (0)
    // uses the verified target game; wrapper magic alone must not reject it.
    *platform=magic==0xeef51454?5:0;unsigned count=u16(p+24);off=32+(size_t)count*32;
    error="Invalid or incomplete SELF entry table";
    if(!count||count>128||off>size-64)goto bad;
    uint64_t declared=u64(p+16);error="SELF declared size exceeds available file data";
    if(declared<off+64||(declared>size&&declared!=((uint64_t)size+15)/16*16))goto bad;
    containerSize=declared<size?(size_t)declared:size;
    error="SELF segment lies outside available container data";
    for(unsigned i=0;i<count;i++){
      uint64_t at=u64(p+32+i*32+8),length=u64(p+32+i*32+16);
      if(at>containerSize||length>containerSize-at)goto bad;
    }
  }
  error="Invalid native module ELF headers";
  HhpElfHeader h;memcpy(&h,p+off,sizeof h);size_t available=containerSize-off;
  if(memcmp(h.ident,"\177ELF\2\1\1",7)||h.machine!=62||h.version!=1||h.type!=0xfe18||h.ehsize!=64||h.phentsize!=56||
    !h.phnum||h.phnum>128||h.phoff<64||h.phoff>available||(uint64_t)h.phnum*56>available-h.phoff)goto bad;
  uint64_t starts[128],ends[128],total=0;unsigned ranges=0;int executable=0,dynamic=0;
  for(unsigned i=0;i<h.phnum;i++){
    HhpElfProgram ph;memcpy(&ph,p+off+h.phoff+(size_t)i*56,sizeof ph);
    if(ph.type==2||ph.type==0x61000000){
      if(!ph.filesz||(!off&&(ph.offset>available||ph.filesz>available-ph.offset)))goto bad;
      dynamic=1;
    }
    if(ph.type!=1)continue;
    if(ph.filesz>ph.memsz||ph.memsz>LIMIT||total>LIMIT-ph.memsz||ph.memsz>UINT64_MAX-ph.vaddr||
      (!off&&(ph.offset>available||ph.filesz>available-ph.offset))||
      (ph.align>1&&((ph.align&(ph.align-1))||ph.vaddr%ph.align!=ph.offset%ph.align)))goto bad;
    total+=ph.memsz;
    if(ph.memsz){
      for(unsigned j=0;j<ranges;j++)if(ph.vaddr<ends[j]&&starts[j]<ph.vaddr+ph.memsz)goto bad;
      starts[ranges]=ph.vaddr;ends[ranges++]=ph.vaddr+ph.memsz;
    }
    if((ph.flags&1)&&ph.filesz)executable=1;
  }
  if(!executable||!dynamic)goto bad;
  if(reason&&capacity)reason[0]=0;return 0;
bad:
  if(reason&&capacity)snprintf(reason,capacity,"%s",error);return -1;
}
uint32_t hhp_ps5_process_sdk(const void *data,size_t size){
  const unsigned char *p=data;
  if(!p||size<32||u64(p)<32||u64(p)>4096||u32(p+8)!=0x4942524f||u32(p+12)!=5)return 0;
  uint32_t sdk=u32(p+20);return sdk>=0x01000000&&sdk<0x20000000?sdk:0;
}
int hhp_prx_match_host_sdk(void *data,size_t size,uint32_t host,uint32_t firmware,uint32_t *original){
  unsigned char *p=data;if(original)*original=0;
  int platform;if(hhp_prx_inspect(data,size,&platform,NULL,0))return -1;
  uint32_t magic=u32(p);if(magic!=0x1d3d154f&&magic!=0xeef51454)return 0;
  size_t container=u64(p+16)<size?(size_t)u64(p+16):size;
  unsigned count=u16(p+24);size_t elf=32+(size_t)count*32;
  if(p[elf+8]!=2)return 0; // ELF ABI, not the shared PS4/PS5 wrapper magic.
  HhpElfHeader h;memcpy(&h,p+elf,sizeof h);
  size_t ex=(elf+(size_t)h.phoff+(size_t)h.phnum*56+15)&~(size_t)15;
  if(ex>container||container-ex<64||u64(p+ex+8)!=1)return 0; // Never rewrite a retail SELF.
  HhpElfProgram param={0};unsigned found=0;
  for(unsigned i=0;i<h.phnum;i++){HhpElfProgram ph;memcpy(&ph,p+elf+h.phoff+i*56,56);if(ph.type==0x61000002){param=ph;found++;}}
  if(!found)return 0;if(found!=1||param.filesz<32)return -1;
  size_t physical=0;found=0;
  for(unsigned i=0;i<h.phnum;i++){
    HhpElfProgram ph;memcpy(&ph,p+elf+h.phoff+i*56,56);
    if(ph.type!=1||param.offset<ph.offset||param.offset-ph.offset>ph.filesz||ph.filesz-(param.offset-ph.offset)<32)continue;
    for(unsigned j=0;j<count;j++){
      const unsigned char *e=p+32+j*32;uint64_t flags=u64(e),at=u64(e+8),len=u64(e+16),expanded=u64(e+24);
      if((flags>>20)!=i||(flags&0xfffff)!=0x2804||len!=expanded)continue;
      uint64_t delta=param.offset-ph.offset;
      if(delta>len||len-delta<32||at>container||len>container-at)return -1;
      physical=(size_t)(at+delta);found++;
    }
  }
  if(!found)return 0; // Unsupported/compressed layout goes to the standard loader unchanged.
  if(found!=1||u64(p+physical)!=32||u32(p+physical+8)!=0x3c13f4bf||u32(p+physical+12)!=3)return -1;
  uint32_t sdk=u32(p+physical+20);if(original)*original=sdk;
  if(!host||host<0x01000000||host>=0x20000000||sdk<=host||
    (sdk&0xffff0000)>(firmware&0xffff0000)||(host&0xffff0000)>(firmware&0xffff0000))return 0;
  // The kernel rejects a PS5 PRX built against a newer SDK than its host
  // executable even when this firmware provides that SDK. Mapping tests on
  // 13.60 isolated the rejection to these four bytes (4.00 PRX / 3.00 GTA).
  // Retain all normal relocation/import and module_start checks afterward.
  memcpy(p+physical+20,&host,4);return 1;
}

