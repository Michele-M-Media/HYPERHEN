#pragma once
#include "common.h"

struct patch g_shellcore_patches_321_retail[] = {
    { 0x9D1F9E, "\x52\xEB\x08", 3 },
    { 0x9D1FA9, "\xE8\x02\xFC\xFF\xFF\x58\xC3", 7 },
    { 0x9D1BA1, "\x31\xC0\x50\xEB\xE3", 5 },
    { 0x9D1B89, "\xE8\x22\x00\x00\x00\x58\xC3", 7 },
    { 0x4DD284, "\xEB\x04", 2 },
    { 0x256AA1, "\xEB\x04", 2 },
    { 0x25B04A, "\xEB\x04", 2 },
    { 0x4FB29A, "\x90\x90", 2 },
    { 0x4E362D, "\x90\xE9", 2 },
    { 0x4FBE33, "\xEB", 1 },
    { 0x4FF5F9, "\xD0\x00\x00\x00", 4 },
    { 0x1968C1, "\xE8\x3A\xE1\x42\x00\x31\xC9\xFF\xC1\xE9\x12\x01\x00\x00", 14 },
    { 0x1969E1, "\x83\xF8\x02\x0F\x43\xC1\xE9\xFF\xFB\xFF\xFF", 11 },
    { 0x1965C9, "\xE9\xF3\x02\x00\x00", 5 },
    { /* VR2 Min Fw Check */ 0x131FEA0, "\x31\xC0\xC3", 3 },
    { /* Disable game error message */ 0x899456, "\x90\x90\x90\x90\x90", 5 },
    { /* PS4 Disc Installer Patch 1 */ 0x25293B, "\x90\xE9", 2 },
    { /* PS5 Disc Installer Patch 1 */ 0x2529B8, "\x90\xE9", 2 },
    { /* PS4 PKG Installer Patch 1 */ 0x252ABB, "\xEB", 1 },
    { /* PS5 PKG Installer Patch 1 */ 0x252B8F, "\xEB", 1 },
    { /* PS4 PKG Installer Patch 2 */ 0x253025, "\x90\xE9", 2 },
    { /* PS5 PKG Installer Patch 2 */ 0x253210, "\xEB", 1 },
    { /* PS4 PKG Installer Patch 3 */ 0x2535D5, "\x90\xE9", 2 },
    { /* PS5 PKG Installer Patch 3 */ 0x253672, "\x90\xE9", 2 },
    { /* PS4 PKG Installer Patch 4 */ 0x4DF207, "\xEB", 1 },
    { /* PS5 PKG Installer Patch 4 */ 0x4DF31C, "\xEB", 1 },
    { /* PKG Installer Patch */ 0x4E11E0, "\x48\x31\xC0\xC3", 4 },
};

struct patch g_shellcore_patches_321_devkit[] = {
};

struct patch g_shellcore_patches_321_testkit[] = {
};