/* Test-only engine runtime. Never linked into a PS5 ELF. */
#include "hyperdeck_engines.hpp"
#include "hyperdeck_backend.hpp"
#include <array>

namespace hyperdeck::engines {
namespace {
struct Engine { const char *name; unsigned short port; bool ready=false; };
std::array<Engine,4> engines{{
  {"game-compressor",5910}, {"file-manager",8888}, {"cheatrunner",9999},
  {"ps5upload",9180}
}};
}
std::string snapshot_json(std::string_view token) {
  std::string body="{\"token\":"+json_quote(token)+",\"engines\":[";
  for (size_t i=0;i<engines.size();++i) {
    const auto &engine=engines[i];
    if(i)body+=',';
    body+="{\"name\":"+json_quote(engine.name)+",\"ready\":"+
      (engine.ready?"true":"false")+",\"port\":"+std::to_string(engine.port)+
      ",\"url\":"+json_quote("http://127.0.0.1:"+std::to_string(engine.port)+"/")+"}";
  }
  return body+"]}";
}
LaunchResult start(std::string_view name) {
  for(auto &engine:engines)if(name==engine.name){engine.ready=true;return {200,true,"Host native service ready","http://127.0.0.1:"+std::to_string(engine.port)+"/"};}
  return {404,false,"Unknown HyperDeck engine",{}};
}
void reap(){}
void shutdown(){}
}
