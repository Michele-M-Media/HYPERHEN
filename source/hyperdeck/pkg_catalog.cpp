/* Catalogue adapted from PIZZA HEN 2.00 util/source/PkgManager.cpp.
 * Attribution: PIZZA HEN / Michele Media; HyperHEN contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Installs remain the original websrv /hbldr -> PKGInstall path.
 */
#include "pkg_portal.hpp"
#include "hyperdeck_backend.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <sstream>
#include <strings.h>
#include <sys/stat.h>
namespace hyperdeck::pkg {
namespace {
struct File {std::string name,path;dev_t dev;ino_t ino;off_t size;time_t modified,changed;long modified_ns,changed_ns;};
std::mutex mutex;
std::vector<File> catalog;
std::vector<std::string> mounted;
size_t visited=0;
bool truncated=false, scanned=false;
std::string argument(std::string value) {
  // Identical two-argument convention to PIZZA's upstreamPkgArgs().
  std::string escaped;for(char c:value){if(c==' ')escaped+='\\';escaped+=c;}return escaped;
}
bool matches(const File &file,const struct stat &st) {
  return S_ISREG(st.st_mode)&&file.dev==st.st_dev&&file.ino==st.st_ino&&file.size==st.st_size&&
      file.modified==st.st_mtime&&file.changed==st.st_ctime&&
      file.modified_ns==st.st_mtim.tv_nsec&&file.changed_ns==st.st_ctim.tv_nsec;
}
bool parents(const std::string &path) {
  for(size_t i=1;i<path.size();++i)if(path[i]=='/'){
    struct stat st{};if(lstat(path.substr(0,i).c_str(),&st)||!S_ISDIR(st.st_mode))return false;
  }return true;
}
void walk(const std::string &root,int depth,std::vector<File>&out) {
  if(depth>4||out.size()>=512||visited>=8192){truncated=true;return;}
  struct stat root_stat{};
  if(!parents(root)||lstat(root.c_str(),&root_stat)||!S_ISDIR(root_stat.st_mode))return;
  DIR *dir=opendir(root.c_str());if(!dir)return;
  while(auto *entry=readdir(dir)){
    if(!std::strcmp(entry->d_name,".")||!std::strcmp(entry->d_name,"..")||
       !strcasecmp(entry->d_name,"$RECYCLE.BIN")||!strcasecmp(entry->d_name,"System Volume Information"))continue;
    if(++visited>8192||out.size()>=512){truncated=true;break;}
    std::string path=root+"/"+entry->d_name;if(path.size()>900)continue;
    struct stat st{};if(lstat(path.c_str(),&st))continue;
    if(S_ISDIR(st.st_mode)){walk(path,depth+1,out);continue;}
    std::string name=entry->d_name;
    if(!S_ISREG(st.st_mode)||st.st_size<=0||name.size()<5||strcasecmp(name.c_str()+name.size()-4,".pkg"))continue;
    if(std::any_of(out.begin(),out.end(),[&](const File&f){return f.dev==st.st_dev&&f.ino==st.st_ino;}))continue;
    out.push_back({name,path,st.st_dev,st.st_ino,st.st_size,st.st_mtime,st.st_ctime,st.st_mtim.tv_nsec,st.st_ctim.tv_nsec});
  }closedir(dir);
}
}
void scan(){
  std::lock_guard lock(mutex);std::vector<File> next;mounted.clear();visited=0;truncated=false;
  std::vector<std::pair<dev_t,ino_t>> seen;
  for(const auto &root:roots()){
    struct stat st{};if(stat(root.c_str(),&st)||!S_ISDIR(st.st_mode))continue;
    if(std::any_of(seen.begin(),seen.end(),[&](const auto&id){return id.first==st.st_dev&&id.second==st.st_ino;}))continue;
    char resolved[1024];if(!realpath(root.c_str(),resolved))continue;
    seen.emplace_back(st.st_dev,st.st_ino);mounted.push_back(root);walk(resolved,0,next);
  }
  std::sort(next.begin(),next.end(),[](const File&a,const File&b){return a.path<b.path;});
  catalog=std::move(next);scanned=true;
}
std::string selected_path(const std::string &args){
  if(args.size()>2048)return {};
  std::lock_guard lock(mutex);
  for(const auto &file:catalog)if(args==std::string(kHelperName)+" "+argument(file.path)){
    struct stat st{};if(parents(file.path)&&!lstat(file.path.c_str(),&st)&&matches(file,st))return file.path;
  }return {};
}
std::string snapshot_json(){
  std::lock_guard lock(mutex);std::ostringstream out;
  out<<"{\"scanned\":"<<(scanned?"true":"false")<<",\"limited\":"<<(truncated?"true":"false")
     <<",\"helper\":"<<(helper_available()?"true":"false")<<",\"volumes\":[";
  bool first=true;for(const auto &v:volumes()){
    if(!first)out<<',';first=false;
    out<<"{\"name\":"<<json_quote(v.name)<<",\"path\":"<<json_quote(v.path)<<",\"kind\":"<<json_quote(v.kind)
       <<",\"total\":"<<v.total<<",\"used\":"<<v.used<<",\"available\":"<<v.available<<'}';
  }
  out<<"],\"packages\":[";first=true;
  for(const auto &f:catalog){if(!first)out<<',';first=false;
    out<<"{\"name\":"<<json_quote(f.name)<<",\"path\":"<<json_quote(f.path)<<",\"size\":"<<f.size<<'}';}
  out<<"],\"mounted_roots\":[";first=true;for(const auto &path:mounted){if(!first)out<<',';first=false;out<<json_quote(path);}
  out<<"]}";return out.str();
}
}
