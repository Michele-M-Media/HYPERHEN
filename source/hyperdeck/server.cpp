/* HyperDeck HTTP frontend. SPDX-License-Identifier: GPL-3.0-or-later
 * Asset registry / listener integration derived from websrv 0.34,
 * Copyright (C) 2024 John Törnblom. See third_party/websrv/LICENSE.
 */
#include "hyperdeck_backend.hpp"
#include "hyperdeck_engines.hpp"
#include "pkg_portal.hpp"
#include "selector_i18n.hpp"
extern "C" {
#include "asset.h"
#include "websrv.h"
}
#include <onion/boot_mode_5xx.h>
#include <onion/engine_selector.h>
#if defined(__SCE__) || defined(PS5)
#include <onion/log.h>
#endif
#include <microhttpd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <unistd.h>

namespace {
constexpr unsigned short kPort = 8088;
constexpr const char *kSelectorCookieName = "HyperHEN5xx";
constexpr const char *kSelectorEntryLocation = "/selector-5xx-r10-20261025.html";
volatile sig_atomic_t stopped = 0;
bool selector_mode = false;
std::string csrf;
struct RequestContext {};
void stop(int) { stopped = 1; }
bool selector_requested() {
#if defined(__SCE__) || defined(PS5)
  struct stat st{};
  return !lstat(ONION_5XX_SELECTOR_ACTIVE_PATH,&st) && S_ISREG(st.st_mode);
#else
  const char *enabled=std::getenv("HYPERHEN_5XX_SELECTOR_TEST");
  return enabled && !std::strcmp(enabled,"1");
#endif
}
const char *boot_mode_path() {
#if defined(__SCE__) || defined(PS5)
  return ONION_5XX_BOOT_MODE_PATH;
#else
  const char *override_path=std::getenv("HYPERHEN_5XX_MODE_PATH");
  return override_path && *override_path ? override_path : ONION_5XX_BOOT_MODE_PATH;
#endif
}
enum class ChoiceState { Pending, Selected, Invalid };
struct ChoiceSnapshot { ChoiceState state=ChoiceState::Pending; std::string selected; };
Onion5xxBootMode mode_from_token(const char *token) {
  if(!token)return ONION_5XX_BOOT_MODE_INVALID;
  if(!std::strcmp(token,ONION_5XX_BOOT_MODE_BYPERVISOR_TOKEN))
    return ONION_5XX_BOOT_MODE_BYPERVISOR;
  if(!std::strcmp(token,ONION_5XX_BOOT_MODE_HYPERCORE_TOKEN))
    return ONION_5XX_BOOT_MODE_HYPERCORE;
  if(!std::strcmp(token,ONION_5XX_BOOT_MODE_PS5HEN_TOKEN))
    return ONION_5XX_BOOT_MODE_PS5HEN;
  return ONION_5XX_BOOT_MODE_INVALID;
}
bool accepted_mode(const char *token) {
  return onion_engine_selector_accepts(hyperdeck::backend_firmware(),mode_from_token(token));
}
ChoiceSnapshot read_choice() {
  const char *path=boot_mode_path();
  struct stat entry{};
  if(lstat(path,&entry)) return {errno==ENOENT?ChoiceState::Pending:ChoiceState::Invalid,{}};
  if(!S_ISREG(entry.st_mode)) return {ChoiceState::Invalid,{}};
  const Onion5xxBootMode mode=onion_5xx_boot_mode_read(path);
  if(onion_engine_selector_accepts(hyperdeck::backend_firmware(),mode))
    return {ChoiceState::Selected,onion_5xx_boot_mode_name(mode)};
  return {ChoiceState::Invalid,{}};
}
bool write_all(int fd,const char *value,size_t length) {
  while(length) {
    const ssize_t written=write(fd,value,length);
    if(written<0) {if(errno==EINTR)continue;return false;}
    if(!written)return false;
    value+=written;length-=static_cast<size_t>(written);
  }
  return true;
}
int select_boot_mode(const char *mode) {
  if(!accepted_mode(mode))return EINVAL;
  const std::string path=boot_mode_path();
  std::string temporary;
  int fd=-1;
  for(unsigned attempt=0;attempt<16 && fd<0;++attempt) {
    uint32_t nonce=0;arc4random_buf(&nonce,sizeof(nonce));
    char suffix[64]{};
    std::snprintf(suffix,sizeof(suffix),".tmp.%d.%08x.%u",static_cast<int>(getpid()),nonce,attempt);
    temporary=path+suffix;
    fd=open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd<0 && errno!=EEXIST)return errno;
  }
  if(fd<0)return EEXIST;
  const bool saved=write_all(fd,mode,std::strlen(mode)) && !fsync(fd);
  int result=saved?0:(errno?errno:EIO);
  if(close(fd) && !result)result=errno?errno:EIO;
  if(!result && link(temporary.c_str(),path.c_str()))result=errno?errno:EIO;
  (void)unlink(temporary.c_str());
  if(!result) {
    const auto slash=path.find_last_of('/');
    const std::string parent=slash==std::string::npos?".":slash?path.substr(0,slash):"/";
    const int parent_fd=open(parent.c_str(),O_RDONLY);
    if(parent_fd>=0) {(void)fsync(parent_fd);close(parent_fd);}
  }
  return result;
}
bool selected_boot_mode_matches(const char *mode) {
  const ChoiceSnapshot choice=read_choice();
  return choice.state==ChoiceState::Selected && choice.selected==mode;
}
std::string firmware_label(uint32_t firmware) {
  char label[16]{};
  std::snprintf(label,sizeof(label),"%x.%02x",(firmware>>24)&0xffU,(firmware>>16)&0xffU);
  return label;
}
std::string boot_mode_json() {
  const auto choice=read_choice();
  const uint32_t firmware=hyperdeck::backend_firmware();
  char hexadecimal[16]{};
  std::snprintf(hexadecimal,sizeof(hexadecimal),"0x%08x",firmware);
  const char *state=choice.state==ChoiceState::Pending?"pending":choice.state==ChoiceState::Selected?"selected":"invalid";
  return "{\"token\":"+hyperdeck::json_quote(csrf)+
    ",\"firmware\":"+std::to_string(firmware)+
    ",\"firmwareHex\":"+hyperdeck::json_quote(hexadecimal)+
    ",\"firmwareLabel\":"+hyperdeck::json_quote(firmware_label(firmware))+
    ",\"family\":"+hyperdeck::json_quote(onion_engine_selector_is_legacy(firmware)?"legacy":onion_engine_selector_supported(firmware)?"5xx":"unsupported")+
    ",\"hypercoreAvailable\":"+(onion_engine_selector_hypercore_available(firmware)?"true":"false")+
    ",\"state\":"+hyperdeck::json_quote(state)+
    ",\"selected\":"+(choice.selected.empty()?"null":hyperdeck::json_quote(choice.selected))+"}";
}
bool listener_ready() {
  for (int waited=0; waited<2000; waited+=50) {
    const int fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd>=0) {
      sockaddr_in target{};target.sin_family=AF_INET;target.sin_port=htons(kPort);
#if defined(__SCE__) || defined(PS5)
      target.sin_len=sizeof(target);
#endif
      target.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
      if(connect(fd,reinterpret_cast<sockaddr*>(&target),sizeof(target))==0) {
        close(fd);return true;
      }
      close(fd);
    }
    usleep(50000);
  }
  return false;
}
const char *query(MHD_Connection *c, const char *key) {
  const char *v = MHD_lookup_connection_value(c, MHD_GET_ARGUMENT_KIND, key);
  return v ? v : "";
}
MHD_Result reply(MHD_Connection *c, unsigned status, const std::string &body) {
  auto *r=MHD_create_response_from_buffer(body.size(), const_cast<char*>(body.data()), MHD_RESPMEM_MUST_COPY);
  if (!r) return MHD_NO;
  MHD_add_response_header(r,"Content-Type","application/json; charset=utf-8");
  const auto result=websrv_queue_response(c,status,r);
  MHD_destroy_response(r); return result;
}
MHD_Result error(MHD_Connection *c, unsigned status, const std::string &message) {
  return reply(c,status,"{\"message\":"+hyperdeck::json_quote(message)+"}");
}
MHD_Result html_reply(MHD_Connection *c, unsigned status, const std::string &body) {
  auto *r=MHD_create_response_from_buffer(body.size(),const_cast<char*>(body.data()),MHD_RESPMEM_MUST_COPY);
  if(!r)return MHD_NO;
  MHD_add_response_header(r,"Content-Type","text/html; charset=utf-8");
  MHD_add_response_header(r,"Content-Language",onion_notify_locale_id());
  const auto result=websrv_queue_response(c,status,r);
  MHD_destroy_response(r);
  return result;
}
std::string selector_launch_page(const char *mode) {
  const char *label=!std::strcmp(mode,ONION_5XX_BOOT_MODE_PS5HEN_TOKEN)?"PS5 HEN":
                    !std::strcmp(mode,ONION_5XX_BOOT_MODE_BYPERVISOR_TOKEN)?"Bypervisor":"HyperCore";
  const char *title_key=onion_engine_selector_is_legacy(hyperdeck::backend_firmware())?
    "notify.selector.legacy_launch_title":"notify.selector.launch_title";
  return std::string("<!doctype html><html lang=\"")+onion_notify_locale_id()+
    "\" dir=\""+selector_i18n::direction()+"\"><head><meta charset=\"utf-8\">"+
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<meta http-equiv=\"Cache-Control\" content=\"no-store,no-cache,must-revalidate,max-age=0\">"
    "<title>"+selector_i18n::text(title_key)+"</title>"
    "<link rel=\"stylesheet\" href=\"/selector-5xx-r10-20261025.css\"></head>"
    "<body><div class=\"launch-screen\"><span class=\"launch-spinner\" aria-hidden=\"true\"></span>"
    "<p>"+selector_i18n::text("notify.selector.configuring")+"</p><strong>"+std::string(label)+"</strong>"
    "<small>"+selector_i18n::text("notify.selector.choice_saved")+"</small>"
    "</div></body></html>";
}
bool local_host(MHD_Connection *c) {
  const char *host=MHD_lookup_connection_value(c,MHD_HEADER_KIND,"Host");
  return host && (!std::strcmp(host,"127.0.0.1:8088") || !std::strcmp(host,"localhost:8088"));
}
bool authorized(MHD_Connection *c) {
  const char *token=MHD_lookup_connection_value(c,MHD_HEADER_KIND,"X-HyperDeck-Token");
  const char *origin=MHD_lookup_connection_value(c,MHD_HEADER_KIND,"Origin");
  if (origin && std::strcmp(origin,"http://127.0.0.1:8088") && std::strcmp(origin,"http://localhost:8088")) return false;
  return token && csrf==token;
}
bool local_referer(const char *referer) {
  if(!referer)return true;
  constexpr const char *loopback="http://127.0.0.1:8088/";
  constexpr const char *localhost="http://localhost:8088/";
  return !std::strncmp(referer,loopback,std::strlen(loopback)) ||
         !std::strncmp(referer,localhost,std::strlen(localhost));
}
bool native_authorized(MHD_Connection *c) {
  const char *cookie=MHD_lookup_connection_value(c,MHD_COOKIE_KIND,kSelectorCookieName);
  const char *origin=MHD_lookup_connection_value(c,MHD_HEADER_KIND,"Origin");
  const char *referer=MHD_lookup_connection_value(c,MHD_HEADER_KIND,"Referer");
  if(origin && std::strcmp(origin,"http://127.0.0.1:8088") &&
     std::strcmp(origin,"http://localhost:8088"))return false;
  return cookie && csrf==cookie && local_referer(referer);
}
const char *native_selector_mode(const char *url) {
  if(!std::strcmp(url,"/api/boot-mode/select-native/ps5hen"))
    return ONION_5XX_BOOT_MODE_PS5HEN_TOKEN;
  if(!std::strcmp(url,"/api/boot-mode/select-native/bypervisor"))
    return ONION_5XX_BOOT_MODE_BYPERVISOR_TOKEN;
  if(!std::strcmp(url,"/api/boot-mode/select-native/hypercore"))
    return ONION_5XX_BOOT_MODE_HYPERCORE_TOKEN;
  return nullptr;
}
void log_selector_choice(const char *transport,const char *mode,int result) {
  const char *family=onion_engine_selector_is_legacy(hyperdeck::backend_firmware())?
    "1.xx-4.xx":"5.xx";
#if defined(__SCE__) || defined(PS5)
  LOG_INFO("%s selector choice transport=%s mode=%s result=0x%08x",
           family,transport,mode,static_cast<unsigned>(result));
#else
  std::fprintf(stderr,"%s selector choice transport=%s mode=%s result=0x%08x\n",
               family,transport,mode,static_cast<unsigned>(result));
#endif
}
bool static_asset(const char *url) {
  if (!url || url[0] != '/' || std::strstr(url,"..") || std::strchr(url,'\\')) return false;
  const char *ext=std::strrchr(url,'.');
  if (!ext) return false;
  static constexpr const char *extensions[] = {
    ".html", ".css", ".js", ".json", ".svg", ".png"
  };
  for (const auto *allowed : extensions)
    if (!std::strcmp(ext,allowed)) return true;
  return false;
}
bool selector_asset(const char *url) {
  return url && (!std::strcmp(url,"/selector-5xx-r10-20261025.html") ||
    !std::strcmp(url,"/selector-5xx-r10-20261025.css") ||
    !std::strcmp(url,"/selector-5xx-r10-20261025.js") ||
    !std::strcmp(url,"/icon0.png"));
}
MHD_Result on_request(void*, MHD_Connection *c, const char *url, const char *method,
                     const char*, const char*, size_t *upload_size, void **ctx) {
  if (!*ctx) {
    *ctx=std::calloc(1,sizeof(RequestContext));
    return *ctx?MHD_YES:MHD_NO;
  }
  if (!local_host(c)) return error(c,403,"Local HyperDeck host required");
  if (std::strlen(url)>256) return error(c,414,"URI too long");
  if (*upload_size) { *upload_size=0; return error(c,413,selector_mode?onion_notify_tr("notify.selector.error_body"):"Request body not accepted"); }
  const bool get=!std::strcmp(method,"GET");
  if (!get && std::strcmp(method,"POST")) return error(c,405,selector_mode?onion_notify_tr("notify.selector.error_method"):"Method not allowed");
  if (!std::strcmp(url,"/api/health") && get)
    return reply(c,200,selector_mode?"{\"service\":\"hyperdeck-websrv\",\"protocol\":2,\"mode\":\"5xx-selector\"}":"{\"service\":\"hyperdeck-websrv\",\"protocol\":1}");
  if(selector_mode) {
    if(!std::strcmp(url,"/api/boot-mode") && get)return reply(c,200,boot_mode_json());
    if(const char *native_mode=native_selector_mode(url)) {
      if(!get)return error(c,405,selector_mode?onion_notify_tr("notify.selector.error_method"):"Method not allowed");
      if(!accepted_mode(native_mode))return error(c,400,onion_notify_tr(ONION_ENGINE_SELECTOR_KEY(hyperdeck::backend_firmware(), "error_mode")));
      /*
       * The PS5 WebView can replay the navigation after it has committed the
       * first local choice, sometimes without its cookie.  A committed choice
       * is immutable, so acknowledge an exact replay without authorizing a
       * second write; a different mode remains a conflict.
       */
      const ChoiceSnapshot committed=read_choice();
      if(committed.state==ChoiceState::Selected) {
        const bool same=committed.selected==native_mode;
        log_selector_choice("native-link",native_mode,same?0:EEXIST);
        if(!same)return error(c,409,onion_notify_tr(ONION_ENGINE_SELECTOR_KEY(hyperdeck::backend_firmware(), "error_conflict")));
        return html_reply(c,200,selector_launch_page(native_mode));
      }
      if(!native_authorized(c)) {
        log_selector_choice("native-link",native_mode,EACCES);
        return error(c,403,onion_notify_tr("notify.selector.error_session"));
      }
      const int selected=select_boot_mode(native_mode);
      log_selector_choice("native-link",native_mode,selected);
      if(selected==EEXIST && !selected_boot_mode_matches(native_mode))
        return error(c,409,onion_notify_tr(ONION_ENGINE_SELECTOR_KEY(hyperdeck::backend_firmware(), "error_conflict")));
      if(selected)return error(c,500,onion_notify_tr(ONION_ENGINE_SELECTOR_KEY(hyperdeck::backend_firmware(), "error_save")));
      return html_reply(c,200,selector_launch_page(native_mode));
    }
    if(!std::strcmp(url,"/api/boot-mode/select") && !get) {
      if(!authorized(c))return error(c,403,onion_notify_tr("notify.selector.error_session"));
      /* libmicrohttpd exposes decoded query values as NUL-terminated strings,
         so a value such as `bypervisor%00junk` is indistinguishable from the
         valid token.  Carry the fixed choice in a header instead: HTTP header
         values are not percent-decoded and the exact strcmp remains sound. */
      if(MHD_lookup_connection_value(c,MHD_GET_ARGUMENT_KIND,"mode"))
        return error(c,400,onion_notify_tr("notify.selector.error_query"));
      const char *mode=MHD_lookup_connection_value(c,MHD_HEADER_KIND,
                                                    "X-HyperDeck-Mode");
      if(!accepted_mode(mode))
        return error(c,400,onion_notify_tr(ONION_ENGINE_SELECTOR_KEY(hyperdeck::backend_firmware(), "error_mode")));
      const ChoiceSnapshot committed=read_choice();
      if(committed.state==ChoiceState::Selected) {
        const bool same=committed.selected==mode;
        log_selector_choice("xhr",mode,same?0:EEXIST);
        if(!same)return error(c,409,onion_notify_tr(ONION_ENGINE_SELECTOR_KEY(hyperdeck::backend_firmware(), "error_conflict")));
        return reply(c,200,"{\"state\":\"selected\",\"selected\":"+hyperdeck::json_quote(mode)+",\"message\":"+hyperdeck::json_quote(onion_notify_tr("notify.selector.accepted"))+"}");
      }
      const int selected=select_boot_mode(mode);
      log_selector_choice("xhr",mode,selected);
      if(selected==EEXIST && !selected_boot_mode_matches(mode))
        return error(c,409,onion_notify_tr(ONION_ENGINE_SELECTOR_KEY(hyperdeck::backend_firmware(), "error_conflict")));
      if(selected)return error(c,500,onion_notify_tr(ONION_ENGINE_SELECTOR_KEY(hyperdeck::backend_firmware(), "error_save")));
      return reply(c,200,"{\"state\":\"selected\",\"selected\":"+hyperdeck::json_quote(mode)+",\"message\":"+hyperdeck::json_quote(onion_notify_tr("notify.selector.accepted"))+"}");
    }
    if(get && !std::strcmp(url,"/"))url=kSelectorEntryLocation;
    if(get && selector_asset(url)) {
      unsigned long size=0;
      const unsigned char *data=hyperdeck_selector_template(url,&size);
      if(!data)return asset_request(c,url);
      const bool script=std::strstr(url,".js")!=nullptr;
      const std::string body=selector_i18n::render(
        std::string_view(reinterpret_cast<const char*>(data),size),script,hyperdeck::backend_firmware());
      if(!script)return html_reply(c,200,body);
      auto *response=MHD_create_response_from_buffer(body.size(),
        const_cast<char*>(body.data()),MHD_RESPMEM_MUST_COPY);
      if(!response)return MHD_NO;
      MHD_add_response_header(response,"Content-Type","application/javascript; charset=utf-8");
      MHD_add_response_header(response,"Content-Language",onion_notify_locale_id());
      const auto result=websrv_queue_response(c,200,response);
      MHD_destroy_response(response);
      return result;
    }
    return error(c,404,onion_notify_tr("notify.selector.error_not_found"));
  }
  if (!std::strcmp(url,"/api/installer") && get) {
    auto body=hyperdeck::pkg::snapshot_json();
    body.insert(1,"\"token\":"+hyperdeck::json_quote(csrf)+",");
    return reply(c,200,body);
  }
  if (!std::strcmp(url,"/api/engines") && get)
    return reply(c,200,hyperdeck::engines::snapshot_json(csrf));
  if (!std::strcmp(url,"/api/engines/start") && !get) {
    if (!authorized(c)) return error(c,403,"HyperDeck session required");
    const auto result=hyperdeck::engines::start(query(c,"name"));
    std::string body="{\"ready\":"+std::string(result.ready?"true":"false")+
      ",\"message\":"+hyperdeck::json_quote(result.message);
    if(!result.url.empty())body+=",\"url\":"+hyperdeck::json_quote(result.url);
    body+='}';
    return reply(c,result.status,body);
  }
  if (!std::strcmp(url,"/api/installer/scan") && !get) {
    if (!authorized(c)) return error(c,403,"HyperDeck session required");
    hyperdeck::pkg::scan(); return reply(c,200,hyperdeck::pkg::snapshot_json());
  }
  if (!std::strcmp(url,"/hbldr") && get) {
    if (!authorized(c)) return error(c,403,"HyperDeck session required");
    if (std::strcmp(query(c,"path"),hyperdeck::pkg::kHelperPath) ||
        std::strcmp(query(c,"pipe"),"1") || std::strcmp(query(c,"daemon"),"1") ||
        *query(c,"env") || *query(c,"cwd")) return error(c,400,"Only the fixed PKGInstall helper is available");
    const auto path=hyperdeck::pkg::selected_path(query(c,"args"));
    if(path.empty()) return error(c,409,"Package not inventoried, changed or disconnected. Scan again.");
    const int fd=hyperdeck::pkg::launch(path);
    if(fd<0) return error(c,503,"PKGInstall could not be started. Nothing confirmed; check the service log.");
    auto *response=MHD_create_response_from_pipe(fd);
    if(!response) {close(fd);return MHD_NO;}
    MHD_add_response_header(response,"Content-Type","text/x-log; charset=utf-8");
    const auto result=websrv_queue_response(c,200,response);
    MHD_destroy_response(response);return result;
  }
  if (get && !std::strcmp(url,"/")) return asset_request(c,"/index.html");
  if (get && static_asset(url)) return asset_request(c,url);
  return error(c,404,"Not found");
}
void on_request_completed(void*,MHD_Connection*,void **ctx,enum MHD_RequestTerminationCode) {
  if(!ctx || !*ctx)return;
  std::free(*ctx);
  *ctx=nullptr;
}
}

extern "C" MHD_Result websrv_queue_response(MHD_Connection *c, unsigned status, MHD_Response *r) {
  MHD_add_response_header(r,"Cache-Control","no-store, no-cache, must-revalidate, max-age=0");
  MHD_add_response_header(r,"Pragma","no-cache");
  MHD_add_response_header(r,"Expires","0");
  if(selector_mode) {
    const std::string cookie=std::string(kSelectorCookieName)+"="+csrf+
      "; Path=/; HttpOnly; SameSite=Strict";
    MHD_add_response_header(r,"Set-Cookie",cookie.c_str());
  }
  MHD_add_response_header(r,"X-Content-Type-Options","nosniff");
  MHD_add_response_header(r,"X-Frame-Options","DENY");
  if(selector_mode) {
    const std::string policy=
      "default-src 'self'; script-src 'self' 'nonce-"+csrf+
      "'; style-src 'self'; img-src 'self' data:; connect-src 'self'; "
      "base-uri 'none'; form-action 'none'; frame-ancestors 'none'";
    MHD_add_response_header(r,"Content-Security-Policy",policy.c_str());
  } else {
    MHD_add_response_header(r,"Content-Security-Policy",
      "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob: https:; connect-src 'self' https:; frame-ancestors 'none'");
  }
  return MHD_queue_response(c,status,r);
}

int main() {
  using namespace hyperdeck;
  selector_mode=selector_requested();
  selector_i18n::initialize();
  if (!backend_initialize(selector_mode)) return 1;
  struct BackendLifetime { ~BackendLifetime() { hyperdeck::engines::shutdown(); hyperdeck::backend_shutdown(); } } backend_lifetime;
  unsigned char random[24]; arc4random_buf(random,sizeof(random));
  for (const auto b:random) {char hex[3];std::snprintf(hex,sizeof(hex),"%02x",b);csrf+=hex;}
  signal(SIGPIPE,SIG_IGN); signal(SIGINT,stop); signal(SIGTERM,stop);
  sockaddr_in address{}; address.sin_family=AF_INET; address.sin_port=htons(kPort);
#if defined(__SCE__) || defined(PS5)
  address.sin_len=sizeof(address);
#endif
  address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  auto *daemon=MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_THREAD_PER_CONNECTION | MHD_USE_ITC,
    kPort,nullptr,nullptr,on_request,nullptr,
    MHD_OPTION_SOCK_ADDR,&address,MHD_OPTION_CONNECTION_LIMIT,8U,
    MHD_OPTION_CONNECTION_MEMORY_LIMIT,static_cast<size_t>(32768),
    MHD_OPTION_NOTIFY_COMPLETED,on_request_completed,nullptr,MHD_OPTION_END);
  if (!daemon) { report_startup_failure("HTTP listener",errno); std::perror("HyperDeck HTTP bind"); return 2; }
  if(!listener_ready()) {
    report_startup_failure("HTTP loopback probe",errno);
    std::fputs("HyperDeck HTTP listener did not become ready\n",stderr);
    MHD_stop_daemon(daemon);
    return 2;
  }
  std::puts(selector_mode?
    onion_engine_selector_is_legacy(backend_firmware())?
      "HyperHEN 1.xx-4.xx selector listening: http://127.0.0.1:8088/":
      "HyperHEN 5.xx selector listening: http://127.0.0.1:8088/":
    "HyperDeck HTTP listening: http://127.0.0.1:8088/"); std::fflush(stdout);
  report_http_ready();
  if(!selector_mode) {
    std::puts("HyperDeck toolbox ready; Media shortcut registration is handled by HyperHEN"); std::fflush(stdout);
  }
  while (!stopped) {
    if(selector_mode)usleep(100000);
    else {sleep(1);pkg::reap();engines::reap();}
  }
  MHD_stop_daemon(daemon); clear_http_ready(); return 0;
}
