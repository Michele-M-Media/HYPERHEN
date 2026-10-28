// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
static int hhp_metadata_predates_process(int64_t modified,int64_t modified_ns,int64_t changed,int64_t changed_ns,int64_t start,int64_t start_us){
 if(modified<=0||changed<=0||start<=0||modified_ns<0||modified_ns>=1000000000||changed_ns<0||changed_ns>=1000000000||start_us<0||start_us>=1000000)return 0;
 return (modified<start||(modified==start&&modified_ns<=start_us*1000))&&
        (changed<start||(changed==start&&changed_ns<=start_us*1000));
}

#ifdef __cplusplus
}
#endif

