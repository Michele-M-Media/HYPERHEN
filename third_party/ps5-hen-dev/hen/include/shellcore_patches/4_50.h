#pragma once
#include "common.h"

struct patch g_shellcore_patches_450_retail[] = {
    { 0x97595E, "\x52\xEB\x08", 3 },
    { 0x975969, "\xE8\xD2\xFB\xFF\xFF\x58\xC3", 7 },
    { 0x975531, "\x31\xC0\x50\xEB\xE3", 5 },
    { 0x975519, "\xE8\x22\x00\x00\x00\x58\xC3", 7 },
    { 0x530F42, "\xEB\x04", 2 },
    { 0x26FA8C, "\xEB\x04", 2 },
    { 0x26FE9C, "\xEB\x04", 2 },
    { 0x54EB60, "\xEB", 1 },
    { 0x5376BD, "\x90\xE9", 2 },
    { 0x54E4FF, "\xEB", 1 },
    { 0x551CEA, "\xC8\x00\x00\x00", 4 },
    { 0x1A12D1, "\xE8\x5A\x92\x47\x00\x31\xC9\xFF\xC1\xE9\xF4\x02\x00\x00", 14 },
    { 0x1A15D3, "\x83\xF8\x02\x0F\x43\xC1\xE9\x29\xFA\xFF\xFF", 11 },
    { 0x1A0FE5, "\xE9\xE7\x02\x00\x00", 5 },
    { /* Disable game error message */ 0x81D3C6, "\x90\x90\x90\x90\x90", 5 },
    { /* PS4 Disc Installer Patch 1 */ 0x2684EB, "\x90\xE9", 2 },
    { /* PS5 Disc Installer Patch 1 */ 0x268582, "\x90\xE9", 2 },
    { /* PS4 PKG Installer Patch 1 */ 0x26869B, "\xEB", 1 },
    { /* PS5 PKG Installer Patch 1 */ 0x26876F, "\xEB", 1 },
    { /* PS4 PKG Installer Patch 2 */ 0x268BD8, "\x90\xE9", 2 },
    { /* PS5 PKG Installer Patch 2 */ 0x268DA9, "\xEB", 1 },
    { /* PS4 PKG Installer Patch 3 */ 0x269175, "\x90\xE9", 2 },
    { /* PS5 PKG Installer Patch 3 */ 0x269212, "\x90\xE9", 2 },
    { /* PS4 PKG Installer Patch 4 */ 0x533137, "\xEB", 1 },
    { /* PS5 PKG Installer Patch 4 */ 0x53324C, "\xEB", 1 },
    { /* PKG Installer Patch */ 0x535160, "\x48\x31\xC0\xC3", 4 },
};

struct patch g_shellcore_patches_450_devkit[] = {
};

struct patch g_shellcore_patches_450_testkit[] = {
};