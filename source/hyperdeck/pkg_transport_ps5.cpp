/* websrv 0.34-style daemon/pipe transport for the derived PKGInstall helper.
 * John Törnblom / HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "pkg_portal.hpp"
#include <onion/elfldr.h>
#include <onion/fs.h>
#include <onion/log.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

__asm__(".pushsection .rodata\n.balign 16\n.global pkg_helper_start\npkg_helper_start:\n"
        ".incbin \"" HYPERHEN_PKG_HELPER_ELF "\"\n"
        ".global pkg_helper_end\npkg_helper_end:\n.balign 4\n"
        ".int pkg_helper_end-pkg_helper_start\n.popsection\n");
extern const uint8_t pkg_helper_start[],pkg_helper_end[];
namespace hyperdeck::pkg {
namespace {
std::mutex launch_mutex;
pid_t last_child=-1;
bool prepared=false;
bool directory(const char *path){struct stat st{};return !lstat(path,&st)?S_ISDIR(st.st_mode):errno==ENOENT&&!mkdir(path,0755);}
bool equal_helper(){
  struct stat st{};if(lstat(kHelperPath,&st)||!S_ISREG(st.st_mode))return false;
  uint8_t *data=nullptr;size_t size=0;const auto expected=static_cast<size_t>(pkg_helper_end-pkg_helper_start);
  if(!read_file_alloc(kHelperPath,expected,&data,&size))return false;
  const bool equal=size==expected&&!std::memcmp(data,pkg_helper_start,size);free(data);return equal;
}
bool bytes(uint64_t blocks,uint64_t block_size,uint64_t &out){
  if(block_size&&blocks>std::numeric_limits<uint64_t>::max()/block_size)return false;
  out=blocks*block_size;return true;
}
}
bool prepare_helper(){
  prepared=false;
  if(!directory("/data/HyperHEN")||!directory("/data/HyperHEN/bin"))return false;
  struct stat st{};
  if(!lstat(kHelperPath,&st)) {
    if(!S_ISREG(st.st_mode))return false;
    if(equal_helper()){prepared=true;return true;}
    prepared=write_file_atomic(kHelperPath,pkg_helper_start,static_cast<size_t>(pkg_helper_end-pkg_helper_start))&&equal_helper();
    return prepared;
  }
  if(errno!=ENOENT)return false;
  prepared=write_file_atomic(kHelperPath,pkg_helper_start,static_cast<size_t>(pkg_helper_end-pkg_helper_start))&&equal_helper();
  return prepared;
}
bool helper_available(){return prepared;}
int launch(const std::string &path){
  std::lock_guard lock(launch_mutex);
  if(!prepared)return -1;
  if(last_child>1){int status=0;const auto result=waitpid(last_child,&status,WNOHANG);if(result==0||(result<0&&errno!=ECHILD))return -1;last_child=-1;}
  int fds[2];if(pipe(fds))return -1;
  char *argv[]={const_cast<char*>(kHelperName),const_cast<char*>(path.c_str()),nullptr};
  const auto pid=elfldr_spawn_service(fds[1],argv,const_cast<uint8_t*>(pkg_helper_start),static_cast<size_t>(pkg_helper_end-pkg_helper_start));
  close(fds[1]);
  if(pid<=1){close(fds[0]);return -1;}
  last_child=pid;
  LOG_INFO("PKGInstall dispatched pid=%d path=%s (system completion not asserted)",static_cast<int>(pid),path.c_str());
  return fds[0];
}
void reap(){
  std::lock_guard lock(launch_mutex);
  if(last_child>1){int status=0;const auto result=waitpid(last_child,&status,WNOHANG);if(result==last_child||(result<0&&errno==ECHILD))last_child=-1;}
}
std::vector<Volume> volumes(){
  struct statfs *mounts=nullptr;const int count=getmntinfo(&mounts,MNT_NOWAIT);
  std::vector<Volume> result;
  for(int i=0;i<count;++i){const auto &m=mounts[i];std::string path=m.f_mntonname;Volume v;v.path=path;
    if(path=="/user"){v.name="Console storage";v.kind="internal";}
    else if(path=="/mnt/ext0"||path=="/mnt/ext1"){v.name="M.2 / extended storage";v.kind="nvme";}
    else if(path.size()==9&&path.compare(0,8,"/mnt/usb")==0&&path[8]>='0'&&path[8]<='7'){
      v.name="USB "+std::to_string(path[8]-'0'+1);v.kind="usb";
    }else continue;
    uint64_t free=0;
    if(m.f_bsize<=0||m.f_bavail<0||!bytes(m.f_blocks,m.f_bsize,v.total)||!v.total||
       !bytes(m.f_bfree,m.f_bsize,free)||!bytes(m.f_bavail,m.f_bsize,v.available))continue;
    v.used=v.total-std::min(v.total,free);v.available=std::min(v.available,std::min(free,v.total));
    result.push_back(std::move(v));
  }return result;
}
std::vector<std::string> roots(){
  std::vector<std::string> result;for(int i=0;i<8;++i)result.push_back("/mnt/usb"+std::to_string(i));
  result.push_back("/mnt/ext0");result.push_back("/mnt/ext1");
  for(int i=0;i<8;++i)result.push_back("/usb"+std::to_string(i));
  return result;
}
}
