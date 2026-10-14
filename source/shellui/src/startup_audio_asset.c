/* Copyright (C) 2026 HyperHEN contributors */

#ifndef HYPERHEN_SHELLUI_HYPE_VOICE_PCM
#error "HYPERHEN_SHELLUI_HYPE_VOICE_PCM must name the embedded startup PCM"
#endif

__asm__(
    ".pushsection .rodata.hyperhen_shellui_hype_voice,\"a\",@progbits\n"
    ".global hyperhen_shellui_hype_voice_pcm_start\n"
    ".type hyperhen_shellui_hype_voice_pcm_start, @object\n"
    ".balign 256\n"
    "hyperhen_shellui_hype_voice_pcm_start:\n"
    ".incbin \"" HYPERHEN_SHELLUI_HYPE_VOICE_PCM "\"\n"
    ".global hyperhen_shellui_hype_voice_pcm_end\n"
    ".type hyperhen_shellui_hype_voice_pcm_end, @object\n"
    "hyperhen_shellui_hype_voice_pcm_end:\n"
    ".size hyperhen_shellui_hype_voice_pcm_start, "
    "hyperhen_shellui_hype_voice_pcm_end-"
    "hyperhen_shellui_hype_voice_pcm_start\n"
    ".popsection\n");
