#include "hyper_locale.hpp"
#include <whisper.h>
#include <cstdio>

int main() {
    for(int id=0;id<=30;++id) {
        const auto &locale=hyper_locale_for_system(id);
        const int stt=whisper_lang_id(locale.whisper_code);
        if(locale.system_id!=id || stt<0) return 1;
        for(unsigned message=0;message<10;++message)
            if(!*hyper_message(static_cast<HyperMessage>(message),id)) return 2;
        if(!hyper_fast_reply("Hyper, are you there?",id)) return 3;
        std::printf("%d\t%s\t%s\t%s\t%d\n",id,locale.tag,locale.whisper_code,locale.voice,stt);
    }
    return 0;
}
