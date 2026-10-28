/* SPDX-License-Identifier: GPL-3.0-or-later
 * Production PRX transport with mocked console primitives, real receipts.
 */
#include <onion/sprx_loader.hpp>
#include <onion/plugin_staging.hpp>
#include <onion/plugin_worker.h>
#include <onion/fs.h>
#include <onion/pt.h>
#include <onion/ucred.h>
#include <ps5/kernel.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <unistd.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"transport check %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
__asm__(".pushsection .rodata\n.global hyperhen_plugin_worker_start\nhyperhen_plugin_worker_start:\n.byte 0x90\n.global hyperhen_plugin_worker_end\nhyperhen_plugin_worker_end:\n.popsection\n");
using namespace onion::sprx;
namespace {
GameIdentity game;
bool valid=true,stage_ok=true,writes_ok=true,metadata_change=false;
int created=0,create_error=0,read_count=0,complete_after=0,final_state=HH_PLUGIN_STARTED,metadata_reads=0;
uintptr_t auth=42;
HyperhenPluginWorker *active=nullptr;
std::string hash(64,'a');
std::map<intptr_t,size_t> regions;
std::vector<void*> allocations;
void fresh() {
  static int sequence=0;++sequence;
  game={50000+sequence,100000+sequence,0,"CUSA12345",1,1};
  valid=stage_ok=writes_ok=true;metadata_change=false;
  created=create_error=read_count=metadata_reads=0;complete_after=0;final_state=HH_PLUGIN_STARTED;
  active=nullptr;hash.assign(64,'a');CHECK(auth==42);
}
LoadOptions options() {LoadOptions o;o.timeout_ms=8;o.poll_ms=1;o.game_version="01.00";return o;}
void finish() {
  if(active && ++read_count>complete_after) {
    active->handle=7;active->start_result=final_state==HH_PLUGIN_STARTED?0:-3;
    active->state=static_cast<unsigned>(final_state);
  }
}
}
namespace onion::sprx {
bool capture_game(pid_t pid,GameIdentity &out) noexcept {if(!valid||pid!=game.pid)return false;out=game;return true;}
bool same_game(const GameIdentity &id) noexcept {return valid&&id.pid==game.pid&&id.seconds==game.seconds&&id.title==game.title;}
bool game_version(const GameIdentity &id,std::string &version,std::string &digest) noexcept {
  ++metadata_reads;version="01.00";digest.assign(64,metadata_change&&metadata_reads>1?'b':'a');return same_game(id);
}
bool stage_module(const GameIdentity &,std::string_view,uint32_t,std::string &path,std::string &digest,std::string &) noexcept {
  path="/hyperhen-modules/"+hash+".prx";digest=hash;return stage_ok;
}
}
extern "C" {
uint32_t hhp_ps5_process_sdk(const void*,size_t){return 0x03000000;}
uintptr_t set_ucred_to_ptrace(){auto old=auth;auth=99;return old;}
int set_proc_authid(pid_t,uintptr_t value){auth=value;return 0;}
int pt_attach(pid_t){return 0;}
int pt_detach(pid_t,int){return 0;}
intptr_t pt_mmap(pid_t,intptr_t,size_t bytes,int,int,int,off_t){void *p=std::calloc(1,bytes);CHECK(p);allocations.push_back(p);regions[(intptr_t)p]=bytes;return (intptr_t)p;}
int pt_munmap(pid_t,intptr_t,size_t){return 0;}
int pt_mprotect(pid_t,intptr_t,size_t,int){return 0;}
int pt_copyin(pid_t,const void *source,intptr_t destination,size_t bytes){std::memcpy((void*)destination,source,bytes);return 0;}
int pt_copyout(pid_t,intptr_t source,void *destination,size_t bytes){std::memcpy(destination,(void*)source,bytes);return 0;}
intptr_t pt_resolve(pid_t,const char *nid){return !strcmp(nid,"OxhIB8LB-PQ")?1002:1001;}
intptr_t kernel_dynlib_dlsym(pid_t,uint32_t,const char*){return 1003;}
long pt_syscall(pid_t,int number,...){
  CHECK(number==598);va_list ap;va_start(ap,number);auto scratch=va_arg(ap,intptr_t);auto second=va_arg(ap,intptr_t);va_end(ap);
  static unsigned char param[32];uint64_t address=(uintptr_t)param,size=32;
  std::memcpy((void*)scratch,&address,8);std::memcpy((void*)second,&size,8);return 0;
}
long pt_call(pid_t,intptr_t function,...){
  if(function!=1002)return 0;
  va_list ap;va_start(ap,function);auto thread=va_arg(ap,intptr_t);(void)va_arg(ap,uint64_t);(void)va_arg(ap,intptr_t);auto ctx=va_arg(ap,intptr_t);va_end(ap);
  ++created;if(create_error)return create_error;
  uint64_t handle=123;std::memcpy((void*)thread,&handle,8);active=(HyperhenPluginWorker*)ctx;return 0;
}
int kernel_proc_copyout(pid_t,intptr_t source,void *destination,size_t bytes){finish();std::memcpy(destination,(void*)source,bytes);return 0;}
int sysctl(const int*,unsigned,void *out,size_t *size,const void*,size_t){CHECK(*size==sizeof(timeval));timeval boot{123,456};std::memcpy(out,&boot,sizeof(boot));return 0;}
bool write_file_atomic(const char *path,const void *data,size_t bytes){
  if(!writes_ok)return false;std::string temp=std::string(path)+".tmp";FILE *f=std::fopen(temp.c_str(),"wb");if(!f)return false;
  bool ok=std::fwrite(data,1,bytes,f)==bytes;ok=std::fclose(f)==0&&ok;return ok&&std::rename(temp.c_str(),path)==0;
}
}
int main(){
  PtraceSprxRuntime runtime;ModuleInfo module;
  fresh();auto r=runtime.load(game.pid,"/plugin.prx","plugin.prx",options());CHECK(r.succeeded()&&created==1&&auth==42);
  CHECK(runtime.find_loaded(game.pid,"plugin.prx",&module)&&module.handle==7);
  CHECK(runtime.load(game.pid,"/plugin.prx","plugin.prx",options()).status==LoadStatus::AlreadyLoaded&&created==1);
  CHECK(runtime.load(game.pid,"/alias.prx","alias.prx",options()).status==LoadStatus::AlreadyLoaded&&created==1);
  fresh();final_state=HH_PLUGIN_FAILED;CHECK(!runtime.load(game.pid,"/bad.prx","bad.prx",options()).succeeded());
  CHECK(!runtime.find_loaded(game.pid,"bad.prx",&module));CHECK(!runtime.load(game.pid,"/bad.prx","bad.prx",options()).succeeded()&&created==1&&auth==42);
  fresh();complete_after=100000;CHECK(runtime.load(game.pid,"/slow.prx","slow.prx",options()).status==LoadStatus::Timeout&&created==1);
  CHECK(!runtime.find_loaded(game.pid,"slow.prx",&module));complete_after=0;
  CHECK(runtime.find_loaded(game.pid,"slow.prx",&module));CHECK(runtime.load(game.pid,"/slow.prx","slow.prx",options()).status==LoadStatus::AlreadyLoaded&&created==1);
  fresh();metadata_change=true;CHECK(runtime.load(game.pid,"/changed.prx","changed.prx",options()).status==LoadStatus::PolicyDenied&&created==0&&auth==42);
  fresh();writes_ok=false;CHECK(!runtime.load(game.pid,"/write.prx","write.prx",options()).succeeded()&&created==0&&auth==42);
  fresh();create_error=5;CHECK(runtime.load(game.pid,"/thread.prx","thread.prx",options()).status==LoadStatus::ThreadCreateFailed);
  create_error=0;CHECK(!runtime.load(game.pid,"/thread.prx","thread.prx",options()).succeeded()&&created==1&&auth==42);
  fresh();auto wrong=options();wrong.game_version="01.01";CHECK(runtime.load(game.pid,"/version.prx","version.prx",wrong).status==LoadStatus::PolicyDenied&&created==0);
  fresh();CHECK(runtime.load(game.pid,"/first.prx","first.prx",options()).succeeded());game.seconds++;
  CHECK(!runtime.find_loaded(game.pid,"first.prx",&module));CHECK(runtime.load(game.pid,"/first.prx","first.prx",options()).succeeded()&&created==2);
  fresh();for(unsigned i=0;i<8;++i){hash.assign(64,static_cast<char>('a'+i));auto name=std::to_string(i)+".prx";CHECK(runtime.load(game.pid,"/test.prx",name,options()).succeeded());}
  hash.assign(64,'f');CHECK(runtime.load(game.pid,"/alias.prx","another.prx",options()).status==LoadStatus::AlreadyLoaded&&created==8);
  hash.assign(64,'9');CHECK(!runtime.load(game.pid,"/ninth.prx","ninth.prx",options()).succeeded()&&created==8&&auth==42);
  for(auto *p:allocations)std::free(p);
  puts("PASS: production PRX transport, completion/late completion, duplicate aliases, failed/uncertain retries, metadata/version gates, PID generation, eight-image cap and credential restoration");
}
