#pragma once
#include "common.h"

struct patch g_shellcore_patches_300_retail[] = {
    { 0x9D1CAE, "\x52\xEB\x08", 3 },
    { 0x9D1CB9, "\xE8\x02\xFC\xFF\xFF\x58\xC3", 7 },
    { 0x9D18B1, "\x31\xC0\x50\xEB\xE3", 5 },
    { 0x9D1899, "\xE8\x22\x00\x00\x00\x58\xC3", 7 },
    { 0x4DCFB4, "\xEB\x04", 2 },
    { 0x2569B1, "\xEB\x04", 2 },
    { 0x25AF5A, "\xEB\x04", 2 },
    { 0x4FAFCA, "\x90\x90", 2 },
    { 0x4E335D, "\x90\xE9", 2 },
    { 0x4FBB63, "\xEB", 1 },
    { 0x4FF329, "\xD0\x00\x00\x00", 4 },
    { 0x1968C1, "\xE8\x4A\xDE\x42\x00\x31\xC9\xFF\xC1\xE9\x12\x01\x00\x00", 14 },
    { 0x1969E1, "\x83\xF8\x02\x0F\x43\xC1\xE9\xFF\xFB\xFF\xFF", 11 },
    { 0x1965C9, "\xE9\xF3\x02\x00\x00", 5 },
    { /* VR2 Min Fw Check */ 0x131FA10, "\x31\xC0\xC3", 3 },
    { /* Disable game error message */ 0x899166, "\x90\x90\x90\x90\x90", 5 },
    { /* PS4 Disc Installer Patch 1 */ 0x25284B, "\x90\xE9", 2 },
    { /* PS5 Disc Installer Patch 1 */ 0x2528C8, "\x90\xE9", 2 },
    { /* PS4 PKG Installer Patch 1 */ 0x2529CB, "\xEB", 1 },
    { /* PS5 PKG Installer Patch 1 */ 0x252A9F, "\xEB", 1 },
    { /* PS4 PKG Installer Patch 2 */ 0x252F35, "\x90\xE9", 2 },
    { /* PS5 PKG Installer Patch 2 */ 0x253120, "\xEB", 1 },
    { /* PS4 PKG Installer Patch 3 */ 0x2534E5, "\x90\xE9", 2 },
    { /* PS5 PKG Installer Patch 3 */ 0x253582, "\x90\xE9", 2 },
    { /* PS4 PKG Installer Patch 4 */ 0x4DEF37, "\xEB", 1 },
    { /* PS5 PKG Installer Patch 4 */ 0x4DF04C, "\xEB", 1 },
    { /* PKG Installer Patch */ 0x4E0F10, "\x48\x31\xC0\xC3", 4 },
};

struct patch g_shellcore_patches_300_devkit[] = {
};

struct patch g_shellcore_patches_300_testkit[] = {
    { /* disable game error message */ 0x8A3466, "\x90\x90\x90\x90\x90", 5},
    { /* PS4 Disc Installer Patch 1 */ 0x25C3EB, "\x90\xE9", 2},
    { /* PS5 Disc Installer Patch 1 */ 0x25C468, "\x90\xE9", 2},
    { /* PS4 PKG Installer Patch 1 */ 0x25C56B, "\xEB", 1},
    { /* PS5 PKG Installer Patch 1 */ 0x25C63F, "\xEB", 1},
    { /* PS4 PKG Installer Patch 2 */ 0x25CAD5, "\x90\xE9", 2},
    { /* PS5 PKG Installer Patch 2 */ 0x25CCC0, "\xeb", 1},
    { /* PS4 PKG Installer Patch 3 */ 0x25D085, "\x90\xE9", 2},
    { /* PS5 PKG Installer Patch 3 */ 0x25D122, "\x90\xE9", 2},
    { /* PS4 PKG Installer Patch 4 */ 0x4E6207, "\xEB", 1},
    { /* PS5 PKG Installer Patch 4 */ 0x4E631C, "\xEB", 1},
    { /* PKG Installer */ 0x4E7C10, "\x48\x31\xC0\xC3", 4},
};