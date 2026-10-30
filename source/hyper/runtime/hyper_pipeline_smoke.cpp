#include "hyper_stt.hpp"
#include "hyper_dialogue.hpp"
#include "hyper_tts.hpp"
#include "hyper_locale.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static bool cancelled(void *) { return true; }

int main(int argc,char **argv) {
    if(argc!=3 && argc!=4) return 2;
    const std::string models=argv[1], data=argv[2];
    const int language=argc==4 ? std::atoi(argv[3]) : 5;
    const auto &locale=hyper_locale_for_system(language);
    const char *question="What is your name?";
    if(language==5) question="Come ti chiami?";
    else if(language==2) question="Comment tu t'appelles?";
    else if(language==4) question="Wie heisst du?";
    else if(language==3) question="Como te llamas?";
    else if(language==0) question="あなたの名前は何ですか。";
    else if(language==8) question="Как тебя зовут?";
    else if(language==10 || language==11) question="你叫什么名字？";
    HyperTtsAudio fixture;
    int code=hyper_tts(question,fixture,locale.voice,nullptr,nullptr,data.c_str());
    if(code<0 || fixture.samples.empty() || fixture.sample_rate<=0) {
        std::printf("fixture_tts_failed=%d\n",code); return 3;
    }
    const size_t samples=fixture.samples.size()*16000/static_cast<size_t>(fixture.sample_rate);
    std::vector<float> pcm(samples);
    for(size_t i=0;i<samples;++i) {
        const double source=static_cast<double>(i)*fixture.sample_rate/16000;
        const size_t index=std::min(static_cast<size_t>(source),fixture.samples.size()-1);
        const size_t next=std::min(index+1,fixture.samples.size()-1);
        const double fraction=source-index;
        pcm[i]=static_cast<float>((fixture.samples[index]*(1-fraction)+fixture.samples[next]*fraction)/32768.0);
    }
    hyper_tts_clear(fixture);
    HyperStt stt;
    auto loaded=stt.load(models+"/ggml-tiny.bin");
    if(loaded!=HyperSttStatus::Ok) {
        std::printf("stt_load_failed=%s\n",hyper_stt_status_name(loaded)); return 4;
    }
    auto transcript=stt.transcribe(pcm,nullptr,nullptr,locale.whisper_code);
    std::fill(pcm.begin(),pcm.end(),0);
    if(transcript.status!=HyperSttStatus::Ok || transcript.text.empty()) {
        std::printf("stt_failed=%s\n",hyper_stt_status_name(transcript.status)); return 5;
    }
    std::printf("synthetic_fixture_transcript=%s stt_ms=%llu\n",transcript.text.c_str(),
        static_cast<unsigned long long>(transcript.elapsed_ms));
    HyperDialogue dialogue(models+"/hyper-chat.gguf");
    dialogue.set_language(language);
    auto fast=dialogue.reply(transcript.text);
    if(fast.status!=HyperDialogueStatus::ok || fast.mode!=HyperDialogueMode::fast || fast.model_loaded)
        return 6;
    HyperTtsAudio spoken;
    code=hyper_tts(fast.text,spoken,locale.voice,nullptr,nullptr,data.c_str());
    if(code<0 || spoken.samples.empty()) return 7;
    std::printf("fast_reply=%s tts_samples=%zu tts_rate=%d\n",fast.text.c_str(),spoken.samples.size(),spoken.sample_rate);
    hyper_tts_clear(spoken);
    const auto begin=std::chrono::steady_clock::now();
    auto chat=dialogue.reply(language==5 ? "Raccontami una battuta breve su un controller che parla." :
                            "Tell me one short joke about a talking controller.");
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
    if(chat.status!=HyperDialogueStatus::ok || chat.mode!=HyperDialogueMode::chat ||
       chat.generated_tokens==0 || !chat.model_loaded || chat.text.empty()) {
        std::printf("chat_failed=%s detail=%s\n",hyper_dialogue_status_name(chat.status),chat.error.c_str());return 8;
    }
    std::printf("generated_chat=%s tokens=%u elapsed_ms=%lld\n",chat.text.c_str(),chat.generated_tokens,static_cast<long long>(ms));
    auto denied=dialogue.reply("Dimmi una cosa.",cancelled,nullptr);
    if(denied.status!=HyperDialogueStatus::cancelled) return 9;
    std::printf("PASS real Whisper + fast reply + TTS + real local LLM + cancellation; locale=%s HOST ONLY\n",locale.tag);
    return 0;
}
