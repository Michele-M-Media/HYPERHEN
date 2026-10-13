#include <onion/log.h>
#include "cheats/i_cheat_parser.hpp"

#include <cstdlib>
#include <cstring>

#include "cheats/cheat_engine_internal.h"

extern "C" {
#include "mc4/aes.h"
#include "mc4/base64.h"
}


namespace onion::cheats {

/* Defined in xml_cheat_parser.cpp */
int parseXmlBuffer(char *xml, onion_cheat_file_t &out);

namespace {

const uint8_t kMc4AesKey[] = "304c6528f659c766110239a51cl5dd9c";
const uint8_t kMc4AesIv[] = "u@}kzW2u[u(8DWar";

/**
 * Decrypt MC4 payload (Base64 → AES-256-CBC) into heap XML string.
 * Caller must secure_zero + free.
 */
char *mc4Decrypt(const char *encoded, size_t encoded_size) {
  size_t bin_size = 0;
  unsigned char *bin = base64_decode(
      reinterpret_cast<const unsigned char *>(encoded), encoded_size, &bin_size);
  if (bin == nullptr) {
    return nullptr;
  }
  if (bin_size == 0 || (bin_size % 16) != 0) {
    onion_cheat_secure_zero(bin, bin_size);
    free(bin);
    return nullptr;
  }

  auto *buf = static_cast<uint8_t *>(std::calloc(bin_size + 0x100 + 1, 1));
  if (buf == nullptr) {
    onion_cheat_secure_zero(bin, bin_size);
    free(bin);
    return nullptr;
  }
  std::memcpy(buf, bin, bin_size);
  onion_cheat_secure_zero(bin, bin_size);
  free(bin);

  struct AES_ctx ctx {};
  AES_init_ctx_iv(&ctx, kMc4AesKey, kMc4AesIv);
  AES_CBC_decrypt_buffer(&ctx, buf, bin_size);
  onion_cheat_secure_zero(&ctx, sizeof(ctx));
  buf[bin_size] = '\0';
  return reinterpret_cast<char *>(buf);
}

} // namespace

class Mc4CheatParser final : public ICheatParser {
public:
  const char *name() const override { return "mc4"; }

  int parse(const uint8_t *data, size_t size, onion_cheat_file_t &out) override {
    if (data == nullptr || size == 0) {
      return -1;
    }
    char *xml = mc4Decrypt(reinterpret_cast<const char *>(data), size);
    if (xml == nullptr) {
      LOG_ERROR("[engine] mc4 decrypt failed");
      return -1;
    }
    const int rc = parseXmlBuffer(xml, out);
    onion_cheat_secure_zero(xml, std::strlen(xml));
    free(xml);
    LOG_DEBUG("[engine] mc4 cheats=%zu rc=%d", out.cheat_count, rc);
    return rc;
  }
};

std::unique_ptr<ICheatParser> makeMc4CheatParser() {
  return std::make_unique<Mc4CheatParser>();
}

} // namespace onion::cheats
