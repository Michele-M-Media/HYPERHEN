/* Copyright (C) 2025 OnionHEN / LightningMods

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

#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include "external_symbols.hpp"
#include "hooked_funcs.hpp"


#define USER_ID_ENTITY_NUMBER       0x7800100
#define USER_ID_ENTITY_NUMBER_2     0x7940100

#define ACCOUNT_ID_ENTITY_NUMBER    0x7800500
#define ACCOUNT_ID_ENTITY_NUMBER_2  0x7940500

#define ACCOUNT_TYPE_ENTITY_NUMBER   0x780b007
#define ACCOUNT_TYPE_ENTITY_NUMBER_2 0x794b007

#define ACCOUNT_ENTITY_FLAGS_NUMBER    0x7800800
#define ACCOUNT_ENTITY_FLAGS_NUMBER_2  0x7940800

#define ACCOUNT_TYPE_MAX 17

extern "C" int sceUserServiceInitialize(uint32_t*);
extern "C" int sceUserServiceGetForegroundUser(int*);
extern "C" int sceUserServiceGetUserName(int, char*, size_t);
extern "C" int sceUserServiceTerminate(void);

extern "C" int sceRegMgrGetStr(int, char*, size_t);
extern "C" int sceRegMgrGetBin(int, void*, size_t);

extern "C" int sceRegMgrSetInt(int, int);
extern "C" int sceRegMgrSetBin(int, const void*, size_t);
extern "C" int sceRegMgrSetStr(int, const char*, size_t);

struct User
{
    std::string Username;
    int32_t account_number;
    uint64_t accountID;
    char AccountType[ACCOUNT_TYPE_MAX];
};


class Activator
{
public:
    Activator(bool skip_userservice_init = false);
    bool Activate();

    inline bool Valid() const {
        return currentUser.account_number >= 1 && currentUser.account_number <= 16;
    }
    bool IsNotActivated();

    User currentUser;
private:
    int GetEntityNumber(int a, int d, int e);
    int32_t GetRegistryFromUserId(int32_t user_id);
    bool GetAccountID(uint32_t account_number, uint64_t& account_id);
    uint32_t GetAccountType(uint32_t account_number, char* account_type);
    uint32_t GetAccountFlags(uint32_t account_number);
    uint64_t GenerateAccountID(const char* username);

    bool SetAccountID(uint32_t account_number, uint64_t AccountID);
    bool SetAccountType(uint32_t account_number, char* AccountType);
    bool SetAccountFlags(uint32_t account_number, uint32_t Flags);




};
