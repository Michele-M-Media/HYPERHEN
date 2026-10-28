// SPDX-License-Identifier: GPL-3.0-or-later
#include "game-info.h"
#include <string.h>
int hhp_game_title(const char* title){
 static const char* prefixes[]={"PPSA","PPSB","MOUU","CUSA","PCAS","PCJS","PCKS","CUHJ","SCUS"};
 if(!title||strlen(title)!=9)return 0;unsigned prefix=0;
 for(unsigned i=0;i<sizeof prefixes/sizeof prefixes[0];i++)if(!memcmp(title,prefixes[i],4))prefix=1;
 if(!prefix)return 0;for(unsigned i=4;i<9;i++)if(title[i]<'0'||title[i]>'9')return 0;return 1;
}
int hhp_app_info_decode(const unsigned char* bytes,size_t length,HhpAppInfo* out){
 if(!bytes||!out||length!=96)return -1;uint32_t appId;memcpy(&appId,bytes,4);
 if(!appId||appId>0x7fffffff)return -1;
 // Both public structures occupy 96 bytes: ShadowMount's title at 0x10,
 // and etaHEN's app_type followed by title at 0x14. Accept exactly one
 // structurally valid title, never infer a new offset from arbitrary bytes.
 const unsigned offsets[]={16,20};HhpAppInfo found={0};unsigned count=0;
 for(unsigned i=0;i<2;i++){
  char title[10];memcpy(title,bytes+offsets[i],10);if(title[9]||!hhp_game_title(title))continue;
  found.appId=appId;found.titleOffset=offsets[i];memcpy(found.title,title,10);count++;
 }
 if(count!=1)return -1;*out=found;return 0;
}

