/* Vendored from etaHEN/Source Code/util/include/mc4/mc4decrypter.h
   Adjusted only to use sibling-dir includes for this build. */

#pragma once

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>
#include "base64.h"

#define CBC 1
#include "aes.h"

uint8_t* encrypt_data(uint8_t* data, size_t* size);
uint8_t* decrypt_data(uint8_t* data, size_t* size);
