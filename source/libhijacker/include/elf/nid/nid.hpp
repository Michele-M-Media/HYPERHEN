#pragma once

#include "b64.hpp"
#include "../../nid.hpp"
#include <sha1.hpp>
#include "util.hpp"
#include <stddef.h>

static inline constexpr uint8_t NID_KEY[]{
	(uint8_t) 0x51, (uint8_t) 0x8D, (uint8_t) 0x64, (uint8_t) 0xA6,
	(uint8_t) 0x35, (uint8_t) 0xDE, (uint8_t) 0xD8, (uint8_t) 0xC1,
	(uint8_t) 0xE6, (uint8_t) 0xB0, (uint8_t) 0x39, (uint8_t) 0xB1,
	(uint8_t) 0xC3, (uint8_t) 0xE5, (uint8_t) 0x52, (uint8_t) 0x30
};

struct Sha1 {
	uint8_t hash[20];
};


static inline void genSha1(uint8_t *res, const StringView &str) {
	SHA1_CTX ctx;

	SHA1Init(&ctx);
	SHA1Update(&ctx, (const unsigned char*)str.c_str(), str.length());
	SHA1Update(&ctx, NID_KEY, sizeof(NID_KEY));
	SHA1Final(res, &ctx);
}

static inline constexpr void fillNid(Nid &buf, const StringView &sym) {
	constexpr size_t NID_DIGEST_LENGTH = 8;
	constexpr size_t NID_SHA_LENGTH = 20;
    uint8_t encodedDigest[NID_DIGEST_LENGTH + 1]{};
	uint8_t sha1[NID_SHA_LENGTH]{};
	genSha1(sha1, sym);
	for (size_t i = 0; i < NID_DIGEST_LENGTH; i++) {
		encodedDigest[i] = sha1[NID_DIGEST_LENGTH - 1 - i]; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
	}
	b64encode(buf.str, encodedDigest);
}
