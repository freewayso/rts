#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

namespace pf {

/* Minimal SHA-256 for password hashing (public-domain style compact impl). */
void Sha256(const void *data, size_t len, uint8_t out[32]);
std::string Sha256Hex(const void *data, size_t len);

/* Generate random bytes (CryptGenRandom / getentropy / rand fallback). */
bool RandomBytes(void *dst, size_t n);
std::string RandomTokenHex(size_t nbytes);

} // namespace pf
