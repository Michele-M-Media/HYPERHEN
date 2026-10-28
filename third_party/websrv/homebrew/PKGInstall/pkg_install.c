/* Copyright (C) 2025 John Törnblom

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include <stdio.h>
#define LANGUAGE_SIZE 8
#define PLAYGO_SCENARIO_ID_SIZE 3
#define CONTENT_ID_SIZE 0x30
#define NUM_LANGUAGES 30
#define NUM_IDS 64

typedef struct pkg_metadata {
  const char* uri;
  const char* ex_uri;
  const char* playgo_scenario_id;
  const char* content_id;
  const char* content_name;
  const char* icon_url;
} pkg_metadata_t;


typedef struct pkg_info {
  char content_id[CONTENT_ID_SIZE];
  int type;
  int platform;
} pkg_info_t;


typedef struct playgo_info {
  char languages[NUM_LANGUAGES][LANGUAGE_SIZE];
  char scenario_ids[NUM_IDS][PLAYGO_SCENARIO_ID_SIZE];
  char content_ids[NUM_IDS][CONTENT_ID_SIZE];
  unsigned char unknown[6480];
} playgo_info_t;

_Static_assert(sizeof(pkg_info_t) == CONTENT_ID_SIZE + 8,
               "SceAppInstUtil PkgInfo size mismatch");
_Static_assert(sizeof(playgo_info_t) == 9984,
               "PlayGoInfo size mismatch");
_Static_assert(sizeof(pkg_metadata_t) == 6 * sizeof(void*),
               "MetaInfo size mismatch");


int sceAppInstUtilInitialize(void);
int sceAppInstUtilInstallByPackage(const pkg_metadata_t*, pkg_info_t*, playgo_info_t*);

static void
report_phase(const char* phase) {
  FILE* log = fopen("/data/HyperHEN/HyperDeck.log", "a");
  if(log) {
    fprintf(log, "PKGInstall phase=%s\n", phase);
    fclose(log);
  }
}

static void
report_result(const char* phase, int result) {
  printf("HYPERDECK_PKG phase=%s result=0x%08x\n", phase,
         (unsigned int)result);
  fflush(stdout);
  FILE* log = fopen("/data/HyperHEN/HyperDeck.log", "a");
  if(log) {
    fprintf(log, "PKGInstall phase=%s result=0x%08x\n", phase,
            (unsigned int)result);
    fclose(log);
  }
}


int
main(int argc, char** argv) {
  playgo_info_t playgoinfo = {0};
  pkg_info_t pkginfo = {0};
  pkg_metadata_t metainfo = {
    .playgo_scenario_id = "",
    .content_name = "",
    .content_id = "",
    .icon_url = "",
    .ex_uri = "",
    .uri = ""
  };

  report_phase("helper_entered");
  if(argc < 2) {
    report_result("argv", -1);
    return -1;
  }

  report_phase("init_begin");
  const int init_result = sceAppInstUtilInitialize();
  report_result("init", init_result);
  if(init_result) {
    return init_result;
  }

  metainfo.uri = argv[1];

  report_phase("install_begin");
  const int install_result = sceAppInstUtilInstallByPackage(&metainfo, &pkginfo, &playgoinfo);
  report_result("install", install_result);
  return install_result;
}

