/* HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "hyperdeck_engines.hpp"
#include "hyperdeck_backend.hpp"
#include "elfldr_remote.h"
#include <onion/elfldr.h>
#include <onion/log.h>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

__asm__(".pushsection .rodata\n.balign 16\n.global hyperdeck_gc_start\nhyperdeck_gc_start:\n"
        ".incbin \"" HYPERHEN_GAME_COMPRESSOR_ELF "\"\n"
        ".global hyperdeck_gc_end\nhyperdeck_gc_end:\n.balign 4\n"
        ".int hyperdeck_gc_end-hyperdeck_gc_start\n.popsection\n");
__asm__(".pushsection .rodata\n.balign 16\n.global hyperdeck_wfm_start\nhyperdeck_wfm_start:\n"
        ".incbin \"" HYPERHEN_WEB_FILE_MANAGER_ELF "\"\n"
        ".global hyperdeck_wfm_end\nhyperdeck_wfm_end:\n.balign 4\n"
        ".int hyperdeck_wfm_end-hyperdeck_wfm_start\n.popsection\n");
__asm__(".pushsection .rodata\n.balign 16\n.global hyperdeck_cr_start\nhyperdeck_cr_start:\n"
        ".incbin \"" HYPERHEN_CHEATRUNNER_ELF "\"\n"
        ".global hyperdeck_cr_end\nhyperdeck_cr_end:\n.balign 4\n"
        ".int hyperdeck_cr_end-hyperdeck_cr_start\n.popsection\n");
__asm__(".pushsection .rodata\n.balign 16\n.global hyperdeck_pu_start\nhyperdeck_pu_start:\n"
        ".incbin \"" HYPERHEN_PS5UPLOAD_ELF "\"\n"
        ".global hyperdeck_pu_end\nhyperdeck_pu_end:\n.balign 4\n"
        ".int hyperdeck_pu_end-hyperdeck_pu_start\n.popsection\n");

extern const uint8_t hyperdeck_gc_start[], hyperdeck_gc_end[];
extern const uint8_t hyperdeck_wfm_start[], hyperdeck_wfm_end[];
extern const uint8_t hyperdeck_cr_start[], hyperdeck_cr_end[];
extern const uint8_t hyperdeck_pu_start[], hyperdeck_pu_end[];

namespace hyperdeck::engines {
namespace {

struct Engine {
  const char *name;
  const char *process_name;
  unsigned short default_port;
  unsigned short port;
  unsigned short scan_count;
  const uint8_t *begin;
  const uint8_t *end;
  pid_t pid = -1;
  bool owned = false;
};

std::array<Engine,4> engine_list{{
  {"game-compressor","game-compressor.elf",5910,5910,1,hyperdeck_gc_start,hyperdeck_gc_end},
  {"file-manager","web-file-mgr.elf",8888,8888,32,hyperdeck_wfm_start,hyperdeck_wfm_end},
  {"cheatrunner","CheatRunner.elf",9999,9999,1,hyperdeck_cr_start,hyperdeck_cr_end},
  {"ps5upload","ps5upload.elf",9180,9180,1,hyperdeck_pu_start,hyperdeck_pu_end}
}};
std::mutex engine_mutex;

bool process_alive(pid_t pid) {
  if (pid <= 1) return false;
  if (kill(pid,0) == 0) return true;
  return errno == EPERM;
}

void reap_locked(Engine &engine) {
  if (!engine.owned || engine.pid <= 1) return;
  int status = 0;
  const pid_t result = waitpid(engine.pid,&status,WNOHANG);
  if (result == engine.pid || (result < 0 && errno == ECHILD)) {
    LOG_INFO("HyperDeck engine %s exited",engine.name);
    engine.pid = -1;
    engine.owned = false;
    engine.port = engine.default_port;
  }
}

bool port_ready(unsigned short port) {
  return elfldr_remote_available_on(port);
}

unsigned short configured_cheatrunner_port() {
  FILE *file = std::fopen("/data/cheatrunner/config.ini","rb");
  if (!file) return 9999;
  char line[256];
  unsigned long port = 9999;
  while (std::fgets(line,sizeof(line),file)) {
    char *key = line;
    while (*key == ' ' || *key == '\t') ++key;
    if (std::strncmp(key,"http_port",9)) continue;
    key += 9;
    while (*key == ' ' || *key == '\t') ++key;
    if (*key++ != '=') continue;
    while (*key == ' ' || *key == '\t') ++key;
    char *end = nullptr;
    const unsigned long candidate = std::strtoul(key,&end,10);
    if (end != key && candidate > 0 && candidate <= 65535) port = candidate;
    break;
  }
  std::fclose(file);
  return static_cast<unsigned short>(port);
}

unsigned short find_ready_port(Engine &engine) {
  if (!std::strcmp(engine.name,"cheatrunner")) {
    engine.default_port = configured_cheatrunner_port();
    engine.port = engine.default_port;
  }
  if (engine.port && port_ready(engine.port)) return engine.port;
  for (unsigned int offset=0; offset<engine.scan_count; ++offset) {
    const unsigned int candidate=static_cast<unsigned int>(engine.default_port)+offset;
    if (candidate<=65535 && port_ready(static_cast<unsigned short>(candidate)))
      return static_cast<unsigned short>(candidate);
  }
  return 0;
}

Engine *find_engine(std::string_view name) {
  for (auto &engine:engine_list)
    if (name == engine.name) return &engine;
  return nullptr;
}

std::string engine_url(unsigned short port) {
  return "http://127.0.0.1:"+std::to_string(port)+"/";
}

int log_fd() {
  const int fd=open("/data/HyperHEN/HyperDeck.log",O_WRONLY|O_CREAT|O_APPEND,0644);
  return fd >= 0 ? fd : STDERR_FILENO;
}

} // namespace

std::string snapshot_json(std::string_view token) {
  std::lock_guard lock(engine_mutex);
  std::string body="{\"token\":"+json_quote(token)+",\"engines\":[";
  bool first=true;
  for (auto &engine:engine_list) {
    reap_locked(engine);
    const unsigned short ready_port=find_ready_port(engine);
    if (ready_port) engine.port=ready_port;
    if (!first) body+=',';
    first=false;
    body+="{\"name\":"+json_quote(engine.name)+",\"ready\":"+
      (ready_port?"true":"false")+",\"port\":"+
      std::to_string(ready_port?ready_port:engine.port)+",\"url\":"+
      json_quote(engine_url(ready_port?ready_port:engine.port))+"}";
  }
  return body+"]}";
}

LaunchResult start(std::string_view name) {
  std::lock_guard lock(engine_mutex);
  Engine *engine=find_engine(name);
  if (!engine) return {404,false,"Unknown HyperDeck engine",{}};
  reap_locked(*engine);
  if (const unsigned short existing=find_ready_port(*engine)) {
    engine->port=existing;
    return {200,true,"Native service already ready",engine_url(existing)};
  }
  if (engine->owned && process_alive(engine->pid))
    return {409,false,"Native service is still starting",engine_url(engine->port)};

  const size_t size=static_cast<size_t>(engine->end-engine->begin);
  if (size < 4) return {503,false,"Embedded native engine is unavailable",{}};
  char *argv[]={const_cast<char*>(engine->process_name),nullptr};
  const int fd=log_fd();
  const pid_t pid=elfldr_spawn_service(fd,argv,const_cast<uint8_t*>(engine->begin),size);
  if (fd != STDERR_FILENO) close(fd);
  if (pid <= 1) {
    LOG_ERROR("HyperDeck engine %s failed to spawn",engine->name);
    return {503,false,"Native service could not be started",{}};
  }
  engine->pid=pid;
  engine->owned=true;
  LOG_INFO("HyperDeck engine %s spawned pid=%d",engine->name,static_cast<int>(pid));
  for (int waited=0; waited<15000; waited+=100) {
    if (const unsigned short ready_port=find_ready_port(*engine)) {
      engine->port=ready_port;
      LOG_INFO("HyperDeck engine %s ready on 127.0.0.1:%u",engine->name,ready_port);
      return {200,true,"Native service ready",engine_url(ready_port)};
    }
    reap_locked(*engine);
    if (!engine->owned) return {503,false,"Native service exited during startup",{}};
    usleep(100000);
  }
  return {503,false,"Native service did not open its web port",{}};
}

void reap() {
  std::lock_guard lock(engine_mutex);
  for (auto &engine:engine_list) reap_locked(engine);
}

void shutdown() {
  std::lock_guard lock(engine_mutex);
  for (auto &engine:engine_list) {
    reap_locked(engine);
    if (!engine.owned || engine.pid <= 1) continue;
    const pid_t pid=engine.pid;
    (void)kill(pid,SIGTERM);
    for (int waited=0; waited<1000 && process_alive(pid); waited+=50) usleep(50000);
    if (process_alive(pid)) (void)kill(pid,SIGKILL);
    (void)waitpid(pid,nullptr,0);
    engine.pid=-1;
    engine.owned=false;
    engine.port=engine.default_port;
  }
}

} // namespace hyperdeck::engines
