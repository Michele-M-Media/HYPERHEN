/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

Shared LNC (app launch) ABI for daemon / util / injectees.
Do not re-declare LncAppParam / Flag elsewhere — include this header.
*/

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING 0x8094000c
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_KILL_NEEDED 0x80940010
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_SUSPEND_NEEDED 0x80940011
#define SCE_LNC_ERROR_APP_NOT_FOUND 0x80940031

typedef enum OnionLncFlag {
  Flag_None = 0,
  SkipLaunchCheck = 1,
  SkipResumeCheck = 1,
  SkipSystemUpdateCheck = 2,
  RebootPatchInstall = 4,
  VRMode = 8,
  NonVRMode = 16,
  Pft = 32UL,
  RaIsConfirmed = 64UL,
  ShellUICheck = 128UL
} Flag;

typedef struct LncAppParam {
  uint32_t sz;
  int user_id;
  uint32_t app_opt;
  uint64_t crash_report;
  Flag check_flag;
} LncAppParam;

int sceUserServiceGetForegroundUser(int *userId);
int sceLncUtilLaunchApp(const char *tid, const char *argv[], LncAppParam *param);
uint32_t sceLncUtilKillApp(uint32_t appId);

#ifdef __cplusplus
}
#endif
