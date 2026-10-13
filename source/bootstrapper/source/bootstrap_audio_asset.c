/* Copyright (C) 2026 HyperHEN contributors */

#ifndef HYPERHEN_HYPE_VOICE_PCM
#error "HYPERHEN_HYPE_VOICE_PCM must name the embedded startup PCM asset"
#endif

__asm__(
    ".pushsection .rodata.hyperhen_hype_voice,\"a\",@progbits\n"
    ".global hyperhen_hype_voice_pcm_start\n"
    ".type hyperhen_hype_voice_pcm_start, @object\n"
    ".balign 256\n"
    "hyperhen_hype_voice_pcm_start:\n"
    ".incbin \"" HYPERHEN_HYPE_VOICE_PCM "\"\n"
    ".global hyperhen_hype_voice_pcm_end\n"
    ".type hyperhen_hype_voice_pcm_end, @object\n"
    "hyperhen_hype_voice_pcm_end:\n"
    ".size hyperhen_hype_voice_pcm_start, "
    "hyperhen_hype_voice_pcm_end-hyperhen_hype_voice_pcm_start\n"
    ".popsection\n");
