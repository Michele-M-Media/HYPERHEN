#include "hyper_reply_tts.hpp"
#include "hyper_runtime_session.hpp"
#include <algorithm>
#include <cerrno>

int hyper_reply_tts(std::string &reply, HyperTtsAudio &audio, const char *voice,
                    int style, bool (*cancel)(void *), void *user,
                    const char *data_path) {
  constexpr size_t smaller[] = {360,270,200};
  int status = -ENOMSG;
  for (unsigned attempt=0;attempt<4;++attempt) {
    status = hyper_tts(reply,audio,voice,cancel,user,data_path,style);
    if (status != -EOVERFLOW && status != -E2BIG) return status;
    if (attempt == 3 || reply.size()<2) break;
    const auto maximum = std::min(reply.size()-1,smaller[attempt]);
    std::string shortened = hyper_worker_limit_reply(reply,maximum);
    if (shortened.empty() || shortened.size()>=reply.size()) break;
    hyper_worker_erase(reply);
    reply.swap(shortened);
    hyper_worker_erase(shortened);
  }
  return status;
}
