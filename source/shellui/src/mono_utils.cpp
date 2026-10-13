/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Extracted from mono_utils.cpp for module locality.
 */

#include "hooked_funcs.hpp"
#include "external_symbols.hpp"
#include "ipc.hpp"
#include <cctype>
#include <cstring>
#include <string>
#include <vector>
#include <pthread.h>

extern "C" int sceKernelLoadStartModule(const char *name, size_t argc, const void *argv, uint32_t flags, uint32_t pOpt, int *pResid);
extern "C" int sceKernelDlsym(int lib, const char *name, void **func);

std::vector<unsigned char> encrypt_decrypt(const unsigned char *data, size_t size, const std::string &key) {
  std::vector<unsigned char> result(size);
  size_t key_len = key.size();

  for (size_t i = 0; i < size; ++i) {
    result[i] = data[i] ^ key[i % key_len];
  }
  return result;
}

static inline bool is_base64(unsigned char c) {
  return (isalnum(c) || (c == '+') || (c == '/'));
}

std::string base64_decode(const std::string &encoded_string) {
  int in_len = encoded_string.size();
  int i = 0, j = 0, in_ = 0;
  unsigned char char_array_4[4], char_array_3[3];
  std::string ret;

  while (in_len-- && (encoded_string[in_] != '=') &&
         is_base64(encoded_string[in_])) {
    char_array_4[i++] = encoded_string[in_];
    in_++;
    if (i == 4) {
      for (i = 0; i < 4; i++)
        char_array_4[i] = base64_chars.find(char_array_4[i]);

      char_array_3[0] =
          (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] =
          ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

      for (i = 0; (i < 3); i++)
        ret += char_array_3[i];
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 4; j++)
      char_array_4[j] = 0;

    for (j = 0; j < 4; j++)
      char_array_4[j] = base64_chars.find(char_array_4[j]);

    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] =
        ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

    for (j = 0; (j < i - 1); j++)
      ret += char_array_3[j];
  }

  return ret;
}

int find_and_replace(unsigned char * buffer, int buffer_size,
  const char * target,
    const char * replacement) {

  size_t target_len = strlen(target);
  size_t replacement_len = strlen(replacement);

  // Search for the target string in the buffer
  unsigned char * found = NULL;
  for (size_t i = 0; i <= buffer_size - target_len; i++) {
    if (memcmp(buffer + i, target, target_len) == 0) {
      found = buffer + i;
      break;
    }
  }

  if (!found) {
    return 0; // Target string not found
  }

  // If replacement and target are same length, simple replacement
  if (replacement_len == target_len) {
    memcpy(found, replacement, replacement_len);
    return 1;
  }

  // If replacement is shorter than target, need to shift data left
  if (replacement_len < target_len) {
    // Copy replacement
    memcpy(found, replacement, replacement_len);

    // Move remaining data left
    size_t bytes_after = buffer_size - (found - buffer) - target_len;
    memmove(found + replacement_len, found + target_len, bytes_after);

    return 1;
  }

  // If replacement is longer than target, need to shift data right
  // This assumes buffer has enough space allocated!
  size_t bytes_after = buffer_size - (found - buffer) - target_len;
  memmove(found + replacement_len, found + target_len, bytes_after);
  memcpy(found, replacement, replacement_len);

  return 1;
}

// Function to replace all occurrences
int replace_all(unsigned char * buffer, int * buffer_size, int buffer_capacity,
  const char * target,
    const char * replacement) {

  size_t target_len = strlen(target);
  size_t replacement_len = strlen(replacement);
  int count = 0;

  for (size_t i = 0; i <= * buffer_size - target_len; i++) {
    if (memcmp(buffer + i, target, target_len) == 0) {
      // Check if we have enough space for replacement
      size_t new_size = * buffer_size + (replacement_len - target_len);
      if (new_size > buffer_capacity) {
        fprintf(stderr, "Buffer too small for replacement\n");
        return count;
      }

      // Shift data if needed
      if (replacement_len != target_len) {
        memmove(buffer + i + replacement_len,
          buffer + i + target_len,
          * buffer_size - i - target_len);

        // Update buffer size
        * buffer_size = new_size;
      }

      // Copy replacement
      memcpy(buffer + i, replacement, replacement_len);

      // Update position
      i += replacement_len - 1;
      count++;
    }
  }

  return count;
}

int ItemzLaunchByUri(const char *uri)
{

  if (!uri)
    return -1;

  SceShellUIUtilLaunchByUriParam Param;
  Param.size = sizeof(SceShellUIUtilLaunchByUriParam);
  sceShellUIUtilInitialize();
  sceUserServiceGetForegroundUser((int *)&Param.userId);

  return sceShellUIUtilLaunchByUri(uri, &Param);
}

void GoToHome()
{
  ItemzLaunchByUri("pshomeui:navigateToHome?bootCondition=psButton");
}
struct URIThreadData {
  std::string uri;  // Copy the string to avoid dangling pointers
};

void* GoToURIThread(void *arg) {
  URIThreadData* data = static_cast<URIThreadData*>(arg);
  
  ItemzLaunchByUri(data->uri.c_str());
  
  delete data;  // Clean up the allocated data
  pthread_exit(nullptr);
  return nullptr;
}

void GoToURI(const char* uri) {
  if (!uri) {
      LOG_DEBUG("GoToURI: URI is null");
      return;
  }
  
  // Create a copy of the URI data
  URIThreadData* data = new URIThreadData{std::string(uri)};
  
  pthread_t t;
  if (pthread_create(&t, nullptr, GoToURIThread, data) != 0) {
      LOG_ERROR("Failed to create thread for GoToURI");
      delete data;  // Clean up on failure
      return;
  }
  
  // Detach the thread so it cleans up automatically
  pthread_detach(t);
}

