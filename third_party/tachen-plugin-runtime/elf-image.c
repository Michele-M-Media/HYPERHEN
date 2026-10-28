// SPDX-License-Identifier: GPL-3.0-or-later
#include "elf-image.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

_Static_assert(sizeof(HhpElfHeader)==64, "ELF64 header layout");
_Static_assert(sizeof(HhpElfProgram)==56, "ELF64 program layout");
_Static_assert(sizeof(HhpElfSection)==64, "ELF64 section layout");
_Static_assert(sizeof(HhpElfRela)==24, "ELF64 relocation layout");
#define PAGE UINT64_C(16384)
#define LIMIT (UINT64_C(512)*1024*1024)
static int span(uint64_t at,uint64_t count,uint64_t total){return at<=total&&count<=total-at;}
static int reject(char *error,size_t size,const char *reason){if(size)snprintf(error,size,"%s",reason);return -1;}
static HhpElfProgram program(const uint8_t *data,const HhpElfHeader *h,unsigned i){
  HhpElfProgram p;memcpy(&p,data+h->phoff+i*sizeof p,sizeof p);return p;
}
static int mapped(const uint8_t *data,const HhpElfHeader *h,uint64_t address,uint64_t length,int file,uint64_t *offset){
  for(unsigned i=0;i<h->phnum;i++){
    HhpElfProgram p=program(data,h,i);
    if(p.type==1&&address>=p.vaddr&&span(address-p.vaddr,length,file?p.filesz:p.memsz)){
      if(offset)*offset=p.offset+(address-p.vaddr);return 1;
    }
  }
  return 0;
}
int hhp_elf_inspect(const void *input,size_t size,HhpElfImage *out,char *error,size_t error_size){
  const uint8_t *data=input;HhpElfImage image={0};HhpElfHeader *h=&image.header;
  if(!input||!out||size<64||size>128u*1024*1024)return reject(error,error_size,"ELF size is outside the payload limit");
  memcpy(h,data,sizeof *h);
  if(memcmp(h->ident,"\177ELF\2\1\1",7)||h->machine!=62||h->version!=1||h->ehsize!=64||
     (h->type!=2&&h->type!=3))return reject(error,error_size,"Expected an x86-64 executable ELF");
  if(!h->phnum||h->phnum>128||h->phentsize!=56||h->phoff<64||!span(h->phoff,(uint64_t)h->phnum*56,size))
    return reject(error,error_size,"Invalid program-header bounds");
  if(h->shnum&&(h->shnum>8192||h->shentsize!=64||h->shoff<64||!span(h->shoff,(uint64_t)h->shnum*64,size)))
    return reject(error,error_size,"Invalid section-header bounds");
  if(!h->shnum&&h->shoff)return reject(error,error_size,"Extended section numbering is unsupported");
  image.lowest=UINT64_MAX;int entry=0;unsigned dynamic_count=0;HhpElfProgram dynamic={0};
  for(unsigned i=0;i<h->phnum;i++){
    HhpElfProgram p=program(data,h,i);
    if(!span(p.offset,p.filesz,size))return reject(error,error_size,"Segment extends outside the file");
    if(p.type==3)return reject(error,error_size,"An ELF interpreter cannot be loaded as a payload");
    if(p.type==2){dynamic=p;dynamic_count++;}
    if(p.type!=1)continue;
    if(p.filesz>p.memsz)return reject(error,error_size,"File bytes exceed the loadable memory size");
    if(!p.memsz)continue;
    if(p.filesz>p.memsz||p.vaddr>UINT64_C(0x7fffffffffff)||p.memsz>UINT64_C(0x800000000000)-p.vaddr)
      return reject(error,error_size,"Invalid loadable memory bounds");
    if(p.align>1&&((p.align&(p.align-1))||p.vaddr%p.align!=p.offset%p.align))
      return reject(error,error_size,"Invalid loadable segment alignment");
    for(unsigned j=0;j<i;j++){
      HhpElfProgram q=program(data,h,j);if(q.type!=1||!q.memsz)continue;
      if(p.vaddr<q.vaddr+q.memsz&&q.vaddr<p.vaddr+p.memsz)return reject(error,error_size,"Loadable segments overlap");
    }
    if(p.vaddr<image.lowest)image.lowest=p.vaddr;
    if(p.vaddr+p.memsz>image.highest)image.highest=p.vaddr+p.memsz;
    if((p.flags&1)&&h->entry>=p.vaddr&&h->entry-p.vaddr<p.filesz)entry=1;
  }
  if(!entry||image.lowest==UINT64_MAX)return reject(error,error_size,"Entry point is outside executable file data");
  image.lowest&=~(PAGE-1);image.highest=(image.highest+PAGE-1)&~(PAGE-1);
  if(image.highest-image.lowest>LIMIT)return reject(error,error_size,"Payload memory span exceeds 512 MiB");
  if(dynamic_count>1)return reject(error,error_size,"Multiple dynamic tables are unsupported");
  uint64_t rela=0,relasz=0,relaent=24,plt=0,pltsz=0,pltkind=7,strtab=0,strsz=0,symtab=0,syment=24;
  uint64_t needed[128];unsigned needed_count=0,seen=0;
  if(dynamic_count){
    if(dynamic.filesz%16||dynamic.filesz>1024*1024)return reject(error,error_size,"Invalid dynamic table");
    int ended=0;
    for(uint64_t at=0;at<dynamic.filesz;at+=16){
      HhpElfDynamic d;memcpy(&d,data+dynamic.offset+at,16);if(!d.tag){ended=1;break;}
      if(d.tag==1){if(needed_count==128)return reject(error,error_size,"Too many needed libraries");needed[needed_count++]=d.value;}
      if((d.tag==17||d.tag==18||d.tag==35||d.tag==36)&&d.value)
        return reject(error,error_size,"REL and packed relocations are unsupported");
      unsigned bit=d.tag==7?1:d.tag==8?2:d.tag==9?4:0;
      if(bit){if(seen&bit)return reject(error,error_size,"Duplicate relocation metadata");seen|=bit;}
      if(d.tag==7)rela=d.value;if(d.tag==8)relasz=d.value;if(d.tag==9)relaent=d.value;
      if(d.tag==23)plt=d.value;if(d.tag==2)pltsz=d.value;if(d.tag==20)pltkind=d.value;
      if(d.tag==5)strtab=d.value;if(d.tag==10)strsz=d.value;if(d.tag==6)symtab=d.value;if(d.tag==11)syment=d.value;
    }
    if(!ended)return reject(error,error_size,"Unterminated dynamic table");
    if(relasz){uint64_t offset;
      if(!(seen&1)||relaent!=24||relasz%24||!mapped(data,h,rela,relasz,1,&offset))
        return reject(error,error_size,"Dynamic relocations are outside file-backed memory");
      image.relocations[image.relocation_tables++]=(HhpRelocationTable){offset,relasz/24};
    }
    if(pltsz){uint64_t offset;
      if(pltkind!=7||pltsz%24||!mapped(data,h,plt,pltsz,1,&offset))
        return reject(error,error_size,"PLT relocations are outside file-backed memory");
      image.relocations[image.relocation_tables++]=(HhpRelocationTable){offset,pltsz/24};
    }
    // PS5 payload SDK startup resolves DT_NEEDED and symbol relocations itself.
    // Validate all referenced strings/entries but leave their resolution to CRT.
    for(unsigned i=0;i<needed_count;i++){
      uint64_t offset;if(needed[i]>=strsz||strtab>UINT64_MAX-strsz||!mapped(data,h,strtab,strsz,1,&offset)||
        !memchr(data+offset+needed[i],0,(size_t)(strsz-needed[i])))return reject(error,error_size,"Invalid library-name string");
    }
  }
  for(unsigned i=0;i<h->shnum;i++){
    HhpElfSection s;memcpy(&s,data+h->shoff+i*64,64);
    if(s.type!=8&&!span(s.offset,s.size,size))return reject(error,error_size,"Section extends outside file");
    if(!(s.flags&2)||!s.size)continue;
    if(s.type==9)return reject(error,error_size,"Allocated REL sections are unsupported");
    if(s.type!=4)continue;
    if(s.entsize!=24||s.size%24)return reject(error,error_size,"Invalid relocation section entries");
    if(relasz||pltsz){
      // Never silently ignore a second allocated relocation region.
      int covered=0;for(unsigned j=0;j<image.relocation_tables;j++){
        HhpRelocationTable t=image.relocations[j];
        if(s.offset>=t.offset&&span(s.offset-t.offset,s.size,t.count*24))covered=1;
      }
      if(!covered)
        return reject(error,error_size,"Section and dynamic relocation tables disagree");
      continue;
    }
    if(image.relocation_tables==128)return reject(error,error_size,"Too many relocation tables");
    image.relocations[image.relocation_tables++]=(HhpRelocationTable){s.offset,s.size/24};
  }
  uint64_t count=0;
  for(unsigned i=0;i<image.relocation_tables;i++){
    HhpRelocationTable t=image.relocations[i];count+=t.count;
    if(count>1048576)return reject(error,error_size,"Too many payload relocations");
    for(uint64_t j=0;j<t.count;j++){
      HhpElfRela r;memcpy(&r,data+t.offset+j*24,24);
      if(!r.info)continue;
      uint32_t type=(uint32_t)r.info;uint64_t symbol=r.info>>32;
      if(type==8&&symbol)return reject(error,error_size,"Relative relocation cannot reference a symbol");
      if(type!=8&&type!=1&&type!=6&&type!=7)return reject(error,error_size,"Unsupported payload relocation type");
      if(!mapped(data,h,r.offset,8,0,NULL))return reject(error,error_size,"Relocation destination lies outside loadable memory");
      if(type!=8){
        uint64_t offset,names;uint32_t name;
        if(!dynamic_count||!symtab||syment!=24||symtab>UINT64_MAX-symbol*24||!mapped(data,h,symtab+symbol*24,24,1,&offset))
          return reject(error,error_size,"Symbol relocation references invalid symbol data");
        memcpy(&name,data+offset,4);
        if(name>=strsz||strtab>UINT64_MAX-strsz||!mapped(data,h,strtab,strsz,1,&names)||!memchr(data+names+name,0,(size_t)(strsz-name)))
          return reject(error,error_size,"Symbol relocation references an invalid name");
      }
    }
  }
  *out=image;if(error_size)error[0]=0;return 0;
}
int hhp_elf_materialize(const void *input,size_t size,const HhpElfImage *image,uint64_t base,void *output,size_t output_size){
  HhpElfImage checked;
  if(!image||hhp_elf_inspect(input,size,&checked,NULL,0))return -1;
  if(image->lowest!=checked.lowest||image->highest!=checked.highest)return -1;
  image=&checked;
  if(output_size!=image->highest-image->lowest||base<image->lowest||base>UINT64_C(0x800000000000)-output_size)return -1;
  const uint8_t *data=input;uint8_t *mirror=output;uint64_t bias=base-image->lowest;
  memset(mirror,0,output_size);
  for(unsigned i=0;i<image->header.phnum;i++){
    HhpElfProgram p=program(data,&image->header,i);
    if(p.type==1&&p.filesz)memcpy(mirror+p.vaddr-image->lowest,data+p.offset,(size_t)p.filesz);
  }
  for(unsigned i=0;i<image->relocation_tables;i++){
    HhpRelocationTable t=image->relocations[i];
    for(uint64_t j=0;j<t.count;j++){
      HhpElfRela r;memcpy(&r,data+t.offset+j*24,24);if(r.info!=8)continue;
      if((r.addend>=0&&(uint64_t)r.addend>UINT64_MAX-bias)||
         (r.addend<0&&(uint64_t)(-(r.addend+1))+1>bias))return -1;
      uint64_t value=bias+(uint64_t)r.addend;memcpy(mirror+r.offset-image->lowest,&value,8);
    }
  }
  return 0;
}

