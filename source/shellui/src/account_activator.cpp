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

#include "../include/account_activator.h"
#include <onion/reg_entity.h>


Activator::Activator(bool skip_userservice_init) : currentUser{}
{
    currentUser.account_number = -1;

    if (!skip_userservice_init)
    {
        int ret = sceUserServiceInitialize(NULL);

        if (ret)
        {
            LOG_ERROR("Error sceUserServiceInitialize");
            return;
        }
    }

    //
    // Get current logged user
    //
    int user_id = -1;
    char username[100] = {0};
    if (sceUserServiceGetForegroundUser(&user_id) != 0)
    {
        LOG_ERROR("Error sceUserServiceGetForegroundUser");
        return;
    }
    currentUser.account_number = GetRegistryFromUserId(user_id);

    if (currentUser.account_number == -1)
    {
        LOG_ERROR("Invalid foreground user id %d, aborting...", user_id);
        return;    
    }

    if (!GetAccountID(currentUser.account_number, currentUser.accountID))
    {
        LOG_ERROR("Error sceRegMgrGetBin(account id)");
        currentUser.account_number = -1;
        return;
    }

    // The username is needed only when an offline account has no account ID
    // and HyperHEN must generate one. Do not make an already activated account
    // unreadable merely because its display name lookup failed.
    if (sceUserServiceGetUserName(user_id, username, sizeof(username)) == 0)
        currentUser.Username = std::string(username);
    else if (currentUser.accountID == 0)
    {
        LOG_ERROR("Error sceUserServiceGetUserName");
        currentUser.account_number = -1;
        return;
    }

    GetAccountType(currentUser.account_number, currentUser.AccountType);

    LOG_DEBUG("Current user => %s", currentUser.Username.c_str());
    LOG_DEBUG("Account register number => %d", currentUser.account_number);
    LOG_DEBUG("User Account ID => %lx", currentUser.accountID);
    LOG_DEBUG("AccountType => %s", currentUser.AccountType);
    LOG_DEBUG("Account Flags => %d", GetAccountFlags(currentUser.account_number));
    
    if (!skip_userservice_init)
    {
        sceUserServiceTerminate();
    }
}


int32_t Activator::GetRegistryFromUserId(int32_t user_id)
{
    // Registry account slots are 1..16. Match the foreground user id exactly,
    // as kylin-core does, instead of relying on a potentially ambiguous
    // username prefix match.
    for (int32_t i = 1; i <= 16; ++i)
    {
        int32_t registry_user_id = -1;
        int reg_number = GetEntityNumber(i, USER_ID_ENTITY_NUMBER,
                                         USER_ID_ENTITY_NUMBER_2);
        if (sceRegMgrGetInt_hook(reg_number, &registry_user_id) == 0 &&
            registry_user_id == user_id)
        {
            return i;
        }
    }

    return -1;
}


bool Activator::GetAccountID(uint32_t account_number, uint64_t& account_id)
{
    int n = GetEntityNumber(account_number, ACCOUNT_ID_ENTITY_NUMBER, ACCOUNT_ID_ENTITY_NUMBER_2);
    account_id = 0;

    return sceRegMgrGetBin(n, &account_id, sizeof(account_id)) == 0;
}


bool Activator::SetAccountID(uint32_t account_number, uint64_t AccountID)
{
    int n = GetEntityNumber(account_number, ACCOUNT_ID_ENTITY_NUMBER, ACCOUNT_ID_ENTITY_NUMBER_2);

    return sceRegMgrSetBin(n, &AccountID, sizeof(uint64_t)) == 0;
}


bool Activator::SetAccountType(uint32_t account_number, char* AccountType)
{
    int n = GetEntityNumber(account_number, ACCOUNT_TYPE_ENTITY_NUMBER, ACCOUNT_TYPE_ENTITY_NUMBER_2);
    
    return sceRegMgrSetStr(n, AccountType, ACCOUNT_TYPE_MAX) == 0;
}


uint32_t Activator::GetAccountType(uint32_t account_number, char* account_type)
{
    int n = GetEntityNumber(account_number, ACCOUNT_TYPE_ENTITY_NUMBER, ACCOUNT_TYPE_ENTITY_NUMBER_2);

    return sceRegMgrGetStr(n, account_type, ACCOUNT_TYPE_MAX);
}


uint32_t Activator::GetAccountFlags(uint32_t account_number)
{
    int n = GetEntityNumber(account_number, ACCOUNT_ENTITY_FLAGS_NUMBER, ACCOUNT_ENTITY_FLAGS_NUMBER_2);
    int val = 0;

    sceRegMgrGetInt_hook(n, &val);

    return val;
}

bool Activator::SetAccountFlags(uint32_t account_number, uint32_t Flags)
{
    int n = GetEntityNumber(account_number, ACCOUNT_ENTITY_FLAGS_NUMBER, ACCOUNT_ENTITY_FLAGS_NUMBER_2);
    return sceRegMgrSetInt(n, Flags) == 0;
}




bool Activator::IsNotActivated()
{
    return currentUser.accountID == 0;
}

bool Activator::Activate()
{
    if (!Valid())
        return false;

    if (IsNotActivated())
    {
        uint64_t accountID = GenerateAccountID(currentUser.Username.c_str());
        char account_type[ACCOUNT_TYPE_MAX] = "np";
        uint32_t flags = 4098;

        if (accountID == 0 ||
            !SetAccountID(currentUser.account_number, accountID) ||
            !SetAccountType(currentUser.account_number, account_type) ||
            !SetAccountFlags(currentUser.account_number, flags))
            return false;
        
        //
        // Update it
        //
        currentUser.accountID = accountID;
        memcpy(currentUser.AccountType, account_type, ACCOUNT_TYPE_MAX);

        return true;
    }

    return false;
}


uint64_t Activator::GenerateAccountID(const char* username)
{
    uint64_t base = 0x5EAF00D / 0xCA7F00D;
    if (*username) 
    {
        do 
        {
            base = 0x100000001B3 * (base ^ *username++);
        } while (*username);
    }

    return base;
}

int Activator::GetEntityNumber(int a, int d, int e)
{
    return onion_reg_entity_number(a, d, e);
}
