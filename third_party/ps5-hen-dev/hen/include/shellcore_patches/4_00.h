#pragma once
#include "common.h"

struct patch g_shellcore_patches_400_retail[] = {
    { 0x974FEE, "\x52\xEB\x08", 3 },
    { 0x974FF9, "\xE8\xD2\xFB\xFF\xFF\x58\xC3", 7 },
    { 0x974BC1, "\x31\xC0\x50\xEB\xE3", 5 },
    { 0x974BA9, "\xE8\x22\x00\x00\x00\x58\xC3", 7 },
    { 0x5307F9, "\xEB\x04", 2 },
    { 0x26F35C, "\xEB\x04", 2 },
    { 0x26F76C, "\xEB\x04", 2 },
    { 0x54E1F0, "\xEB", 1 },
    { 0x536E1D, "\x90\xE9", 2 },
    { 0x54DB8F, "\xEB", 1 },
    { 0x55137A, "\xC8\x00\x00\x00", 4 },
    { 0x1A12D1, "\xE8\xEA\x88\x47\x00\x31\xC9\xFF\xC1\xE9\xF4\x02\x00\x00", 14 },
    { 0x1A15D3, "\x83\xF8\x02\x0F\x43\xC1\xE9\x29\xFA\xFF\xFF", 11 },
    { 0x1A0FE5, "\xE9\xE7\x02\x00\x00", 5 },
    { 0x12B5EA0, "\x31\xC0\xC3", 3 }, /* VR2 Min Fw Check */
    { 0x81CA56, "\x90\x90\x90\x90\x90", 5 }, /* disable game error message */
    { /* PS4 Disc Installer Patch 1 */ 0x267DBB, "\x90\xE9", 2 },
    { /* PS5 Disc Installer Patch 1 */ 0x267E52, "\x90\xE9", 2 },
    { /* PS4 PKG Installer Patch 1 */ 0x267F6B, "\xEB", 1 },
    { /* PS5 PKG Installer Patch 1 */ 0x26803F, "\xEB", 1 },
    { /* PS4 PKG Installer Patch 2 */ 0x2684A8, "\x90\xE9", 2 },
    { /* PS5 PKG Installer Patch 2 */ 0x268679, "\xEB", 1 },
    { /* PS4 PKG Installer Patch 3 */ 0x268A45, "\x90\xE9", 2 },
    { /* PS5 PKG Installer Patch 3 */ 0x268AE2, "\x90\xE9", 2 },
    { /* PS4 PKG Installer Patch 4 */ 0x532897, "\xEB", 1 },
    { /* PS5 PKG Installer Patch 4 */ 0x5329AC, "\xEB", 1 },
    { /* PKG Installer Patch */ 0x5348C0, "\x48\x31\xC0\xC3", 4 },
};

struct patch g_shellcore_patches_400_devkit[] = {
};

struct patch g_shellcore_patches_400_testkit[] = {
};
