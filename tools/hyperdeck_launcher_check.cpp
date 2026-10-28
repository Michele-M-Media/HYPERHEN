/* Real Media installer, with credential/API doubles and path-confined host IO. */
#include "hyperdeck_backend.hpp"
#include <onion/fs.h>
#include <onion/log.h>
#include <ps5/kernel.h>
#include <cassert>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs=std::filesystem;
extern "C" int sceAppInstUtilInitialize();
extern "C" int sceAppInstUtilTerminate();
namespace {
std::string root; int installs=0,auth_changes=0,initialization=0,terminations=0,polls=0;
bool symbols=true,native_symbols=true,fail_icon=false,create_metadata=true,deferred_metadata=false;
bool module_available=true,lifecycle_symbols=true;
uint64_t auth=0x123456789ULL;
std::string confined(const char *path){
  const std::string p=path;
  assert(p=="/user/app/HYPR00001" || p.rfind("/user/app/HYPR00001/",0)==0 ||
         p.rfind("/user/appmeta/HYPR00001/",0)==0);
  return root+p.substr(5);
}
void put(const std::string &path,const std::string &value){std::ofstream(path,std::ios::binary)<<value;}
void age_pending(){
  const std::string path=root+"/app/HYPR00001/.hyperdeck-media-pending";
  timespec times[2]{};
  times[0].tv_sec=times[1].tv_sec=std::time(nullptr)-120;
  assert(!utimensat(AT_FDCWD,path.c_str(),times,0));
}
void reset(){char tmp[]="/tmp/hyperdeck-launcher.XXXXXX";const char *dir=mkdtemp(tmp);assert(dir);root=dir;fs::create_directories(root+"/app");fs::create_directories(root+"/appmeta/HYPR00001");installs=auth_changes=initialization=terminations=polls=0;symbols=native_symbols=create_metadata=module_available=lifecycle_symbols=true;fail_icon=deferred_metadata=false;auth=0x123456789ULL;}
int install(const char *title,const char *path,void *reserved){
  assert(!std::strcmp(title,"HYPR00001") && !std::strcmp(path,"/user/app/") && !reserved);
  assert(auth==0x4801000000000013ULL);installs++;
  if(create_metadata && !deferred_metadata)put(root+"/appmeta/HYPR00001/param.json","registered");return 0;
}
}
extern "C" {
extern const unsigned char hyperdeck_param[]="{\"titleId\":\"HYPR00001\",\"titleName\":\"Hyperdeck\"}";
extern const unsigned long hyperdeck_param_size=sizeof(hyperdeck_param)-1;
extern const unsigned char hyperdeck_icon[]={127,80,78,71};
extern const unsigned long hyperdeck_icon_size=sizeof(hyperdeck_icon);
volatile int onion_log_runtime_level=ONION_LOG_OFF;
void onion_log_write(onion_log_level,const char*,...){}
int __real_lstat(const char*,struct stat*);
int __real_mkdir(const char*,mode_t);
int __real_access(const char*,int);
int __real_unlink(const char*);
int __wrap_lstat(const char *p,struct stat *s){return __real_lstat(confined(p).c_str(),s);}
int __wrap_mkdir(const char *p,mode_t m){return __real_mkdir(confined(p).c_str(),m);}
int __wrap_access(const char *p,int m){return __real_access(confined(p).c_str(),m);}
int __wrap_unlink(const char *p){return __real_unlink(confined(p).c_str());}
int __wrap_usleep(useconds_t delay){
  assert(delay==100000);polls++;
  if(deferred_metadata && polls==2)put(root+"/appmeta/HYPR00001/param.json","registered");
  return 0;
}
void *__wrap_dlopen(const char *path,int){
  assert(!std::strcmp(path,"/system/common/lib/libSceAppInstUtil.sprx"));
  assert(auth==0x4801000000000013ULL);return module_available?reinterpret_cast<void*>(1):nullptr;
}
void *__wrap_dlsym(void*,const char *name){
  if(!std::strcmp(name,"sceAppInstUtilInitialize"))return lifecycle_symbols?reinterpret_cast<void*>(sceAppInstUtilInitialize):nullptr;
  if(!std::strcmp(name,"sceAppInstUtilTerminate"))return lifecycle_symbols?reinterpret_cast<void*>(sceAppInstUtilTerminate):nullptr;
  assert(!std::strcmp(name,"sceAppInstUtilAppInstallTitleDir"));
  return native_symbols?reinterpret_cast<void*>(install):nullptr;
}
int __wrap_dlclose(void *module){assert(module==reinterpret_cast<void*>(1));return 0;}
bool read_file_alloc(const char *p,size_t max,uint8_t **out,size_t *size){
  std::ifstream f(confined(p),std::ios::binary);if(!f)return false;
  const std::string bytes((std::istreambuf_iterator<char>(f)),{});
  if(bytes.empty() || bytes.size()>max)return false;
  *out=static_cast<uint8_t*>(malloc(bytes.size()+1));assert(*out);std::memcpy(*out,bytes.data(),bytes.size());*size=bytes.size();return true;
}
bool write_file_atomic(const char *p,const void *data,size_t size){
  assert(auth==0x4801000000000013ULL); // staging requires the installer context
  const auto target=confined(p);if(fail_icon&&std::string(p).find("icon0.png")!=std::string::npos)return false;
  std::ofstream f(target,std::ios::binary);if(!f)return false;f.write(static_cast<const char*>(data),size);return f.good();
}
uint64_t kernel_get_ucred_authid(pid_t){return auth;}
int32_t kernel_set_ucred_authid(pid_t,uint64_t value){auth=value;auth_changes++;return 0;}
int kernel_dynlib_handle(pid_t,const char*,uint32_t *handle){*handle=1;return 0;}
intptr_t kernel_dynlib_resolve(pid_t,uint32_t,const char*){return symbols?reinterpret_cast<intptr_t>(install):0;}
intptr_t kernel_dynlib_dlsym(pid_t,uint32_t,const char *name){
  if(!std::strcmp(name,"sceAppInstUtilInitialize"))return lifecycle_symbols?reinterpret_cast<intptr_t>(sceAppInstUtilInitialize):0;
  if(!std::strcmp(name,"sceAppInstUtilTerminate"))return lifecycle_symbols?reinterpret_cast<intptr_t>(sceAppInstUtilTerminate):0;
  return symbols?reinterpret_cast<intptr_t>(install):0;
}
int sceAppInstUtilInitialize(){assert(auth==0x4801000000000013ULL);return initialization;}
int sceAppInstUtilTerminate(){terminations++;return 0;}
}
int main(){
  reset();assert(hyperdeck::install_media_launcher());assert(installs==1 && auth_changes==2 && auth==0x123456789ULL && terminations==1);
  assert(hyperdeck::install_media_launcher());assert(installs==1 && auth_changes==2 && terminations==1);
  fs::remove(root+"/appmeta/HYPR00001/param.json");
  assert(hyperdeck::install_media_launcher());assert(installs==2 && auth_changes==4 && terminations==2);
  reset();fs::create_directories(root+"/app/HYPR00001/sce_sys");put(root+"/app/HYPR00001/sce_sys/param.json","other app");
  assert(!hyperdeck::install_media_launcher());assert(installs==0 && auth_changes==0 && !fs::exists(root+"/app/HYPR00001/sce_sys/icon0.png"));
  reset();symbols=native_symbols=false;assert(!hyperdeck::install_media_launcher());assert(installs==0 && auth_changes==2 && auth==0x123456789ULL && terminations==1);
  assert(!fs::exists(root+"/app/HYPR00001/sce_sys/param.json"));
  reset();native_symbols=false;assert(hyperdeck::install_media_launcher());assert(installs==1 && auth==0x123456789ULL);
  reset();initialization=-1;assert(!hyperdeck::install_media_launcher());assert(installs==0 && auth_changes==2 && auth==0x123456789ULL && terminations==0);
  reset();fail_icon=true;assert(!hyperdeck::install_media_launcher());assert(installs==0 && auth_changes==2 && terminations==1 && auth==0x123456789ULL);
  fail_icon=false;assert(hyperdeck::install_media_launcher());assert(installs==1 && auth==0x123456789ULL);
  reset();fs::create_directories(root+"/foreign");fs::create_directory_symlink(root+"/foreign",root+"/app/HYPR00001");
  assert(!hyperdeck::install_media_launcher());assert(installs==0 && auth_changes==0 && fs::is_empty(root+"/foreign"));
  reset();create_metadata=false;assert(hyperdeck::install_media_launcher());
  assert(installs==1 && polls==0 && !fs::exists(root+"/app/HYPR00001/.hyperdeck-media-registered") && auth==0x123456789ULL);
  assert(hyperdeck::install_media_launcher());assert(installs==1);
  age_pending();create_metadata=true;assert(hyperdeck::install_media_launcher());assert(installs==2);
  reset();deferred_metadata=true;assert(hyperdeck::install_media_launcher());assert(installs==1 && polls==0);
  reset();module_available=false;assert(hyperdeck::install_media_launcher());assert(installs==1 && terminations==1 && auth==0x123456789ULL);
  reset();lifecycle_symbols=false;assert(!hyperdeck::install_media_launcher());assert(!installs && !terminations && auth==0x123456789ULL);
  std::cout<<"HyperDeck Media installer: deferred metadata, single-attempt registration, stale-marker recovery, staging credentials, export fallback, idempotence and collisions passed.\n";
}
