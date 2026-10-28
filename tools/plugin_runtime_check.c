/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <onion/plugin_worker.h>
#include "elf-image.h"
#include "prx-image.h"
#include "game-info.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"check failed line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void put32(unsigned char *p,uint32_t value){memcpy(p,&value,4);}
static void put64(unsigned char *p,uint64_t value){memcpy(p,&value,8);}
static void elf_fixture(unsigned char *p) {
  memset(p,0,512);
  HhpElfHeader h={0};memcpy(h.ident,"\177ELF\2\1\1",7);
  h.type=3;h.machine=62;h.version=1;h.entry=128;h.phoff=64;h.ehsize=64;h.phentsize=56;h.phnum=1;
  memcpy(p,&h,64);
  HhpElfProgram load={.type=1,.flags=5,.filesz=512,.memsz=512,.align=1};memcpy(p+64,&load,56);
}
static void prx_fixture(unsigned char *p) {
  memset(p,0,1024);put32(p,0x1d3d154f);put64(p+16,1024);p[24]=1;
  put64(p+32,0x2804);put64(p+40,512);put64(p+48,256);put64(p+56,256);
  HhpElfHeader h={0};memcpy(h.ident,"\177ELF\2\1\1",7);h.ident[8]=2;
  h.type=0xfe18;h.machine=62;h.version=1;h.phoff=64;h.ehsize=64;h.phentsize=56;h.phnum=3;memcpy(p+64,&h,64);
  HhpElfProgram load={.type=1,.flags=5,.offset=0x1000,.vaddr=0x1000,.filesz=256,.memsz=256,.align=0x1000};
  HhpElfProgram dyn={.type=2,.offset=0x1080,.filesz=32};
  HhpElfProgram param={.type=0x61000002,.offset=0x1040,.filesz=32,.memsz=32};
  memcpy(p+128,&load,56);memcpy(p+184,&dyn,56);memcpy(p+240,&param,56);
  put64(p+312,1);put64(p+576,32);put32(p+584,0x3c13f4bf);put32(p+588,3);put32(p+596,0x04000031);
}
static int handle_value, initializer_value, activate_value, paired, load_calls, activation_calls;
static int mock_load(const char *path,uint64_t a,const void *b,unsigned c,const void *d,int *out){
  CHECK(!strcmp(path,"/hyperhen-modules/test.prx"));CHECK(!a&&!b&&!c&&!d&&out);
  ++load_calls;*out=initializer_value;return handle_value;
}
static int mock_activate(int argc,const char **argv){CHECK(!argc&&!argv);++activation_calls;return activate_value;}
static int mock_unload(void){return 0;}
static int mock_dlsym(int handle,const char *name,void **out){
  CHECK(handle==handle_value);*out=NULL;
  if(paired && !strcmp(name,"plugin_load")){*out=(void *)(uintptr_t)mock_activate;return 0;}
  if(paired==2 && !strcmp(name,"plugin_unload")){*out=(void *)(uintptr_t)mock_unload;return 0;}
  return -1;
}
static void worker_checks(void *(*entry)(void *)) {
  const int cases[][6]={{7,0,0,0,HH_PLUGIN_STARTED,0},{0,0,0,0,HH_PLUGIN_STARTED,0},
    {-1,0,0,2,HH_PLUGIN_FAILED,0},{7,-1,0,2,HH_PLUGIN_FAILED,0},
    {7,1,0,2,HH_PLUGIN_STARTED,1},{7,0,-1,2,HH_PLUGIN_FAILED,1},
    {7,0,0,1,HH_PLUGIN_STARTED,0},{7,0,0,2,HH_PLUGIN_STARTED,1}};
  for(unsigned i=0;i<sizeof cases/sizeof cases[0];++i){
    HyperhenPluginWorker ctx={0};ctx.load_start_module=(uintptr_t)mock_load;ctx.dlsym=(uintptr_t)mock_dlsym;
    strcpy(ctx.path,"/hyperhen-modules/test.prx");
    handle_value=cases[i][0];initializer_value=cases[i][1];activate_value=cases[i][2];paired=cases[i][3];
    load_calls=activation_calls=0;CHECK(entry(&ctx)==NULL);
    CHECK(ctx.state==(unsigned)cases[i][4]);CHECK(load_calls==1);CHECK(activation_calls==cases[i][5]);
    CHECK(ctx.handle==handle_value&&ctx.start_result==initializer_value);
  }
}
int main(int argc,char **argv) {
  CHECK(argc==2);
  unsigned char elf[512],mirror[16384];HhpElfImage image;char reason[192];elf_fixture(elf);
  CHECK(!hhp_elf_inspect(elf,sizeof elf,&image,reason,sizeof reason));
  CHECK(!hhp_elf_materialize(elf,sizeof elf,&image,0x100000,mirror,sizeof mirror));
  CHECK(!memcmp(elf,mirror,sizeof elf));
  HhpElfHeader h;memcpy(&h,elf,64);h.phnum=2;memcpy(elf,&h,64);
  HhpElfProgram dyn={.type=2,.offset=256,.filesz=64};memcpy(elf+120,&dyn,56);
  HhpElfDynamic tags[]={{7,320},{8,24},{9,24},{0,0}};memcpy(elf+256,tags,sizeof tags);
  HhpElfRela rela={400,8,128};memcpy(elf+320,&rela,sizeof rela);
  CHECK(!hhp_elf_inspect(elf,sizeof elf,&image,reason,sizeof reason));
  CHECK(!hhp_elf_materialize(elf,sizeof elf,&image,0x100000,mirror,sizeof mirror));
  uint64_t relocated;memcpy(&relocated,mirror+400,8);CHECK(relocated==0x100080);
  rela.offset=UINT64_MAX;memcpy(elf+320,&rela,sizeof rela);CHECK(hhp_elf_inspect(elf,sizeof elf,&image,reason,sizeof reason));
  elf_fixture(elf);memcpy(&h,elf,64);h.phoff=UINT64_MAX;memcpy(elf,&h,64);CHECK(hhp_elf_inspect(elf,sizeof elf,&image,reason,sizeof reason));
  unsigned char module[1024],original[1024];uint32_t old;prx_fixture(module);memcpy(original,module,sizeof module);
  CHECK(hhp_prx_match_host_sdk(module,sizeof module,0x03000027,0x13600007,&old)==1&&old==0x04000031);
  put32(original+596,0x03000027);CHECK(!memcmp(module,original,sizeof module));
  CHECK(!hhp_prx_match_host_sdk(module,sizeof module,0x03000027,0x13600007,&old));
  prx_fixture(module);memcpy(original,module,sizeof module);
  CHECK(!hhp_prx_match_host_sdk(module,sizeof module,0,0x13600007,&old)&&!memcmp(module,original,sizeof module));
  CHECK(!hhp_prx_match_host_sdk(module,sizeof module,0x03000027,0x03000000,&old)&&!memcmp(module,original,sizeof module));
  module[72]=1;memcpy(original,module,sizeof module);CHECK(!hhp_prx_match_host_sdk(module,sizeof module,0x03000027,0x13600007,&old)&&!memcmp(module,original,sizeof module));
  prx_fixture(module);put64(module+312,2);memcpy(original,module,sizeof module);CHECK(!hhp_prx_match_host_sdk(module,sizeof module,0x03000027,0x13600007,&old)&&!memcmp(module,original,sizeof module));
  unsigned char app[96]={0};HhpAppInfo info;put32(app,123);memcpy(app+16,"PPSA00001",10);CHECK(!hhp_app_info_decode(app,sizeof app,&info)&&info.titleOffset==16);
  memset(app+16,0,16);memcpy(app+20,"CUSA12345",10);CHECK(!hhp_app_info_decode(app,sizeof app,&info)&&info.titleOffset==20);
  put32(app,0);CHECK(hhp_app_info_decode(app,sizeof app,&info));
  worker_checks(hyperhen_plugin_worker);
  FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long size=ftell(f);CHECK(size>0&&size<16384);rewind(f);
  void *code=mmap(NULL,16384,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(code!=MAP_FAILED);
  CHECK(fread(code,1,(size_t)size,f)==(size_t)size);fclose(f);CHECK(!mprotect(code,16384,PROT_READ|PROT_EXEC));
  worker_checks((void *(*)(void *))code);CHECK(!munmap(code,16384));
  puts("PASS: native and PS5-built worker (16 cases), ELF bounds/relocations, PRX SDK copies and app-info layouts");
  return 0;
}
