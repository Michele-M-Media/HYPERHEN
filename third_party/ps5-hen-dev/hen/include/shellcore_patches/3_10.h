#pragma once
#include "common.h"

struct patch g_shellcore_patches_310_retail[] = {
    { 0x9D1CEE, "\x52\xEB\x08", 3 },
    { 0x9D1CF9, "\xE8\x02\xFC\xFF\xFF\x58\xC3", 7 },
    { 0x9D18F1, "\x31\xC0\x50\xEB\xE3", 5 },
    { 0x9D18D9, "\xE8\x22\x00\x00\x00\x58\xC3", 7 },
    { 0x4DCFF4, "\xEB\x04", 2 },
    { 0x2569F1, "\xEB\x04", 2 },
    { 0x25AF9A, "\xEB\x04", 2 },
    { 0x4FB00A, "\x90\x90", 2 },
    { 0x4E339D, "\x90\xE9", 2 },
    { 0x4FBBA3, "\xEB", 1 },
    { 0x4FF369, "\xD0\x00\x00\x00", 4 },
    { 0x1968C1, "\xE8\x8A\xDE\x42\x00\x31\xC9\xFF\xC1\xE9\x12\x01\x00\x00", 14 },
    { 0x1969E1, "\x83\xF8\x02\x0F\x43\xC1\xE9\xFF\xFB\xFF\xFF", 11 },
    { 0x1965C9, "\xE9\xF3\x02\x00\x00", 5 },
    { /* VR2 Min Fw Check */ 0x131FA50, "\x31\xC0\xC3", 3 },
    { /* Disable game error message */ 0x8991A6, "\x90\x90\x90\x90\x90", 5 },
    { /* PS4 Disc Installer Patch 1 */ 0x25288B, "\x90\xE9", 2 },
    { /* PS5 Disc Installer Patch 1 */ 0x252908, "\x90\xE9", 2 },
    { /* PS4 PKG Installer Patch 1 */ 0x252A0B, "\xEB", 1 },
    { /* PS5 PKG Installer Patch 1 */ 0x252ADF, "\xEB", 1 },
    { /* PS4 PKG Installer Patch 2 */ 0x252F75, "\x90\xE9", 2 },
    { /* PS5 PKG Installer Patch 2 */ 0x253160, "\xEB", 1 },
    { /* PS4 PKG Installer Patch 3 */ 0x253525, "\x90\xE9", 2 },
    { /* PS5 PKG Installer Patch 3 */ 0x2535C2, "\x90\xE9", 2 },
    { /* PS4 PKG Installer Patch 4 */ 0x4DEF77, "\xEB", 1 },
    { /* PS5 PKG Installer Patch 4 */ 0x4DF08C, "\xEB", 1 },
    { /* PKG Installer Patch */ 0x4E0F50, "\x48\x31\xC0\xC3", 4 },
};

struct patch g_shellcore_patches_310_devkit[] = {
};

struct patch g_shellcore_patches_310_testkit[] = {
};