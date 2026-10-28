// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stddef.h>
typedef struct {uint32_t appId;unsigned titleOffset;char title[10];} HhpAppInfo;
int hhp_game_title(const char* title);
int hhp_app_info_decode(const unsigned char* bytes,size_t length,HhpAppInfo* out);
int hhp_game_app_info(int pid,HhpAppInfo* out);
int hhp_game_info_available(void);

#ifdef __cplusplus
}
#endif

