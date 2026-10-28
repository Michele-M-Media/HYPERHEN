/* Test-only backend. Never linked into a PS5 ELF. */
#include "hyperdeck_backend.hpp"
#include <chrono>
#include <cstdlib>
namespace hyperdeck {
namespace { std::array<bool,3> running{}; bool plugin_running=false,payload_running=false; }
uint64_t monotonic_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
bool backend_initialize(){return true;}
void backend_shutdown(){}
Snapshot observe(){
  Snapshot s;const char *f=std::getenv("HYPERDECK_TEST_FW");s.firmware=f?static_cast<uint32_t>(std::strtoul(f,nullptr,16)):0x10010000U;
  s.version="HOST TEST / NOT A PS5";describe_stages(s);
  for(size_t i=0;i<s.stage_count;++i)s.stages[i].state=State::Ready;
  s.tools=running;s.cpu="57 C";s.soc="60 C";s.fan="35%";s.ip="TEST ONLY";s.sampled_ms=monotonic_ms();s.fresh=true;
  return s;
}
Result apply(std::string_view action,bool enabled,std::string_view id){
  for(int i=0;i<3;++i)if(action==std::array<std::string_view,3>{"klog","ftp","elfldr"}[i]){running[i]=enabled;return {200,"Test state updated"};}
  if(action=="payload"&&id=="test-payload"){payload_running=enabled;return {200,"Test payload updated"};}
  if(action=="plugin"&&id=="test-plugin"){plugin_running=enabled;return {200,"Test plugin updated"};}
  return {400,"Unknown test action"};
}
std::vector<Entry> inventory(std::string_view kind){
  return kind=="plugins"?std::vector<Entry>{{"test-plugin","Test plugin",plugin_running}}:
         std::vector<Entry>{{"test-payload","Test <script>payload</script>.elf",payload_running}};
}
void report_http_ready(){}
void report_startup_failure(const char*,int){}
void clear_http_ready(){}
}
