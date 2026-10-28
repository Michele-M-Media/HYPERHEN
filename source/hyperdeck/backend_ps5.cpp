/* HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "hyperdeck_backend.hpp"
#include "pkg_portal.hpp"
#include <onion/boot_mode_5xx.h>
#include <onion/engine_selector.h>
#include <onion/elf_name.h>
#include <onion/fs.h>
#include <onion/hyperlinux_profiles.h>
#include <onion/ipc_client.hpp>
#include <onion/log.h>
#include <onion/net.h>
#include <onion/payload.h>
#include <onion/proc_query.h>
#include <onion/ready.h>
#include <onion/settings.hpp>
#include <onion/version.h>
#include <ps5/kernel.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <ctime>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <map>
#include <unistd.h>

extern "C" {
struct HyperDeckSwVersion { uint64_t pad0; char text[28]; uint32_t version; uint64_t pad1; };
int sceKernelGetProsperoSystemSwVersion(HyperDeckSwVersion*);
int sceKernelGetProcessName(int,char*);
int sceNetCtlInit(void);
int sceUserServiceInitialize(void*);
void sceUserServiceTerminate(void);
}

namespace hyperdeck {
namespace {
uint32_t firmware=0;
Onion5xxBootMode boot_mode_5xx=ONION_5XX_BOOT_MODE_DEFAULT;
bool user_service_ready=false;
int (*cpu_temperature)(int*)=nullptr;
int (*soc_temperature)(int,int*)=nullptr;
int (*fan_duty)(uint16_t*,uint64_t*)=nullptr;
constexpr const char *tool_paths[]={"/data/HyperHEN/tools/klogsrv.elf","/data/HyperHEN/tools/ftpsrv.elf","/data/HyperHEN/tools/hyperhen_debug_elfldr.elf"};
Onion5xxBootMode selected_boot_mode_5xx() {
  const Onion5xxBootMode selected=onion_5xx_boot_mode_read(ONION_5XX_BOOT_MODE_PATH);
  return onion_engine_selector_accepts(firmware,selected)?
         selected:ONION_5XX_BOOT_MODE_DEFAULT;
}
struct Payload { Entry entry; std::string path; };
bool ready_process(const char *marker) {
  pid_t pid=-1;
  return onion_ready_read_pid(marker,&pid) && pid>1 && onion_proc_is_alive(pid);
}
std::string identity(const std::string &path) {
  char key[ONION_PAYLOAD_IDENTITY_SIZE]{};
  return onion_payload_identity_from_path(path.c_str(),key,sizeof(key))?key:"";
}
pid_t payload_pid(const std::string &key) {
  char path[256]; onion_payload_pid_path(path,sizeof(path),key.c_str());
  const auto pid=onion_payload_read_pid_file(path);
  return pid>1 && onion_proc_is_alive(pid)?pid:-1;
}
std::vector<Payload> payloads() {
  std::vector<std::string> roots={"/data/HyperHEN/payloads","/data/hyperhen/payloads"};
  for(int i=0;i<8;++i) {
    roots.push_back("/mnt/usb"+std::to_string(i)+"/payloads");
    roots.push_back("/mnt/usb"+std::to_string(i)+"/HyperHEN/payloads");
  }
  std::map<std::string,Payload> found;
  for(const auto &root:roots) {
    DIR *dir=opendir(root.c_str()); if(!dir)continue;
    while(auto *file=readdir(dir)) {
      if(found.size()>=256)break;
      const size_t n=std::strlen(file->d_name);
      if(n<=4 || !onion_elf_name_has_suffix(file->d_name,n))continue;
      const std::string path=root+"/"+file->d_name;
      struct stat st{}; if(lstat(path.c_str(),&st) || !S_ISREG(st.st_mode))continue;
      const auto key=identity(path);if(key.empty())continue;
      found[key]={{key,file->d_name,payload_pid(key)>1},path};
    }
    closedir(dir);
  }
  std::vector<Payload> result;
  for(auto &[key,payload]:found)result.push_back(std::move(payload));
  return result;
}
Result set_payload(const std::string &path,const std::string &key,bool enabled) {
  const auto pid=payload_pid(key);
  if(enabled) {
    if(pid>1)return {200,"Already running"};
    if(!ready_process(ONION_READY_UTIL))return {503,"The selected profile has no ready utility service"};
    const auto result=IPC_Client::getInstance(true).LaunchPayload(path,key);
    return result==IPC_Ret::NO_ERROR?Result{200,"Service started"}:Result{503,"The loader did not confirm startup"};
  }
  if(pid<=1)return {200,"Already stopped"};
  // Never accept a PID or a path supplied by HTTP. Only an inventoried,
  // path-identity record can reach this branch, matching the classic Toolbox.
  if(!ready_process(ONION_READY_DAEMON))return {503,"The runtime service is unavailable"};
  IPC_Client::getInstance(false).ForceKillPID(static_cast<int>(pid));
  for(int waited=0;waited<500 && onion_proc_is_alive(pid);waited+=25)usleep(25000);
  if(onion_proc_is_alive(pid))return {503,"Stop not confirmed; refresh and retry"};
  char pid_path[256];onion_payload_pid_path(pid_path,sizeof(pid_path),key.c_str());
  (void)unlink(pid_path);return {200,"Service stopped"};
}
}
uint64_t monotonic_ms() { timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return static_cast<uint64_t>(t.tv_sec)*1000+t.tv_nsec/1000000; }
bool backend_initialize(bool selector_mode) {
  onion_log_configure("HyperDeck","/data/HyperHEN/HyperDeck.log");
  LOG_INFO("HyperDeck toolbox revision 10: starting pid=%d mode=%s",static_cast<int>(getpid()),selector_mode?"engine-selector":"toolbox");
  HyperDeckSwVersion sw{};
  const int sw_result = sceKernelGetProsperoSystemSwVersion(&sw);
  if(sw_result) {report_startup_failure("firmware query",sw_result);return false;}
  firmware=sw.version;
  if(onion_firmware_route(firmware)==ONION_FIRMWARE_ROUTE_UNSUPPORTED) {report_startup_failure("firmware profile",static_cast<int>(firmware));return false;}
  if(selector_mode && !onion_engine_selector_supported(firmware)) {
    report_startup_failure("engine selector firmware",static_cast<int>(firmware));
    return false;
  }
  if(onion_engine_selector_supported(firmware))
    boot_mode_5xx=selected_boot_mode_5xx();
  LOG_INFO("HyperDeck firmware 0x%08x",firmware);
  syscall(SYS_thr_set_name,-1,"hyperdeck.elf");
  LOG_INFO("HyperDeck network initialization: 0x%08x",static_cast<unsigned>(sceNetCtlInit()));
  if(selector_mode) {
    LOG_INFO("HyperDeck %s selector backend ready; privileged toolbox services intentionally deferred",
             onion_engine_selector_is_legacy(firmware)?"legacy":"5xx");
    return true;
  }
  uint32_t handle=0;
  if(!kernel_dynlib_handle(-1,"libkernel_sys.sprx",&handle)) {
    cpu_temperature=reinterpret_cast<decltype(cpu_temperature)>(kernel_dynlib_dlsym(-1,handle,"sceKernelGetCpuTemperature"));
    soc_temperature=reinterpret_cast<decltype(soc_temperature)>(kernel_dynlib_dlsym(-1,handle,"sceKernelGetSocSensorTemperature"));
    fan_duty=reinterpret_cast<decltype(fan_duty)>(kernel_dynlib_dlsym(-1,handle,"sceKernelGetCurrentFanDuty"));
  }
  IPC_Client::getInstance(false).set_recv_timeout_ms(1500);
  IPC_Client::getInstance(true).set_recv_timeout_ms(3000);
  const int user_result=sceUserServiceInitialize(nullptr);
  if(user_result) {
    LOG_WARN("PKGInstall user service initialization failed: 0x%08x",static_cast<unsigned>(user_result));
  } else if(kernel_set_ucred_authid(-1,0x4801000000000013ULL)) {
    LOG_WARN("PKGInstall service context setup failed; web installation disabled");
    sceUserServiceTerminate();
  } else {
    user_service_ready=true;
    if(!pkg::prepare_helper()) LOG_WARN("PKGInstall helper unavailable or path occupied; web installation disabled");
  }
  return true;
}
uint32_t backend_firmware() { return firmware; }
void backend_shutdown() {
  if(user_service_ready) {
    sceUserServiceTerminate();
    user_service_ready=false;
  }
}
Snapshot observe() {
  Snapshot s;s.firmware=firmware;s.version=HYPERHEN_VERSION;
  s.boot_mode_5xx=boot_mode_5xx;
  describe_stages(s);s.sampled_ms=monotonic_ms();s.fresh=true;
  const auto route=onion_firmware_route(firmware);
  for(size_t i=0;i<s.stage_count;++i) {
    const std::string name=s.stages[i].name;
    if(name=="BYEPERVISOR") {
      const char *ready=route==ONION_FIRMWARE_ROUTE_HYPERVISOR_5XX?ONION_READY_HYPERVISOR_5XX:route==ONION_FIRMWARE_ROUTE_HYPERVISOR_940?ONION_READY_HYPERVISOR_940:ONION_READY_HYPERVISOR;
      const char *failed=route==ONION_FIRMWARE_ROUTE_HYPERVISOR_5XX?ONION_READY_HYPERVISOR_5XX_FAILED:route==ONION_FIRMWARE_ROUTE_HYPERVISOR_940?ONION_READY_HYPERVISOR_940_FAILED:ONION_READY_HYPERVISOR_FAILED;
      const char *armed=route==ONION_FIRMWARE_ROUTE_HYPERVISOR_940?ONION_READY_HYPERVISOR_940_ARMED:ONION_READY_HYPERVISOR_ARMED;
      s.stages[i].state=observed_state(onion_ready_is_set(ready),onion_ready_is_set(failed),onion_ready_is_set(armed));
    }
    else if(name=="LEGACY HEN" || name=="PS5 HEN")s.stages[i].state=observed_state(onion_ready_is_set(ONION_READY_LEGACY_HEN),onion_ready_is_set(ONION_READY_LEGACY_HEN_FAILED));
    else if(name=="HYPERCORE")s.stages[i].state=observed_state(onion_ready_is_set(ONION_READY_KSTUFF),onion_ready_is_set(ONION_READY_KSTUFF_FAILED),onion_ready_is_set(ONION_READY_KSTUFF_STARTING));
    else if(name=="AUTOMOUNT")s.stages[i].state=observed_state(ready_process(ONION_READY_AUTOMOUNT),onion_ready_is_set(ONION_READY_AUTOMOUNT_FAILED));
    else {
      const char *marker=name=="DAEMON"?ONION_READY_DAEMON:
                         name=="HYPERDECK"?ONION_READY_HYPERDECK:ONION_READY_TOOLBOX;
      s.stages[i].state=ready_process(marker)?State::Ready:State::Offline;
    }
  }
  char ip[64]{}; (void)onion_net_get_ip_address(ip,sizeof(ip));if(*ip)s.ip=ip;
  int v=0; if(cpu_temperature && !cpu_temperature(&v))s.cpu=std::to_string(v)+" C";
  if(soc_temperature && !soc_temperature(0,&v))s.soc=std::to_string(v)+" C";
  uint16_t duty=0;uint64_t chassis=0;
  if(fan_duty && !fan_duty(&duty,&chassis))s.fan=std::to_string((duty*100U+512)/1024)+"%";
  for(int i=0;i<3;++i)s.tools[i]=payload_pid(identity(tool_paths[i]))>1;
  onion::Settings settings{};(void)onion::settings_load(&settings);s.overlay=settings.overlay_enabled;
  s.linux_supported=onion_hyperlinux_firmware_supported(firmware);
  return s;
}
std::vector<Entry> inventory(std::string_view kind) {
  std::vector<Entry> result;
  if(kind=="payloads")for(const auto &p:payloads())result.push_back(p.entry);
  if(kind=="plugins" && ready_process(ONION_READY_DAEMON)) {
    std::vector<PluginInventoryItem> items;
    if(IPC_Client::getInstance(false).ListPlugins(items))for(const auto &p:items)result.push_back({p.plugin_id,p.name,p.running});
  }
  return result;
}
Result apply(std::string_view action,bool enabled,std::string_view id) {
  for(int i=0;i<3;++i)if(action==std::array<std::string_view,3>{"klog","ftp","elfldr"}[i])return set_payload(tool_paths[i],identity(tool_paths[i]),enabled);
  if(action=="payload") {
    for(const auto &p:payloads())if(p.entry.id==id)return set_payload(p.path,p.entry.id,enabled);
    return {404,"Payload no longer present"};
  }
  if(action=="plugin") {
    for(const auto &p:inventory("plugins"))if(p.id==id) {
      if(p.running==enabled)return {200,"Plugin state unchanged"};
      auto &ipc=IPC_Client::getInstance(false);
      return (enabled?ipc.StartPlugin(p.id):ipc.StopPlugin(p.id))?Result{200,"Plugin request completed"}:Result{503,"Plugin operation failed"};
    }
    return {404,"Plugin unavailable"};
  }
  if(action=="linux") {
    if(!onion_hyperlinux_firmware_supported(firmware))return {409,"HyperLinux has no exact profile for this firmware"};
    constexpr const char *marker="/data/HyperHEN/boot_linux_next";
    if(!enabled) {if(unlink(marker) && errno!=ENOENT)return {500,"Cannot cancel Linux boot"};return {200,"Linux boot cancelled"};}
    return write_file_atomic(marker,"1",1)?Result{200,"Linux armed for the next clean boot. Reboot, then load HyperHEN."}:Result{500,"Cannot save Linux request"};
  }
  if(action=="overlay")return {409,"Change the telemetry overlay in Settings > HyperHEN Tools. HyperDeck does not reconfigure ShellUI hooks."};
  return {400,"Unknown action"};
}
void report_http_ready() {
  LOG_INFO("HyperDeck websrv ready on 127.0.0.1:8088; awaiting parent Media registration");
  (void)onion_ready_signal_pid(ONION_READY_HYPERDECK_LISTENER,getpid());
}
void report_startup_failure(const char *stage,int error) {
  LOG_ERROR("HyperDeck startup failed at %s: 0x%08x",stage,static_cast<unsigned>(error));
  (void)onion_ready_signal_pid(ONION_READY_HYPERDECK_FAILED,getpid());
}
void clear_http_ready() {
  for (const auto marker : {ONION_READY_HYPERDECK,ONION_READY_HYPERDECK_LISTENER})
    if(onion_ready_matches_pid(marker,getpid()))(void)onion_ready_clear(marker);
}
}
