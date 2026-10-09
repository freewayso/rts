#include "pf_crypto.h"
#include <cstring>
#include <cstdio>
#include <ctime>
#include <vector>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace pf {
namespace {

uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

void sha256_transform(uint32_t state[8], const uint8_t block[64])
{
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t w[64];
    for(int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)|
               ((uint32_t)block[i*4+2]<<8)|((uint32_t)block[i*4+3]);
    }
    for(int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
        uint32_t s1 = rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=state[0],b=state[1],c=state[2],d=state[3];
    uint32_t e=state[4],f=state[5],g=state[6],h=state[7];
    for(int i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e,6)^rotr(e,11)^rotr(e,25);
        uint32_t ch = (e&f)^((~e)&g);
        uint32_t t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a,2)^rotr(a,13)^rotr(a,22);
        uint32_t maj = (a&b)^(a&c)^(b&c);
        uint32_t t2 = S0 + maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

} // namespace

void Sha256(const void *data, size_t len, uint8_t out[32])
{
    uint32_t state[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };
    const uint8_t *p = (const uint8_t*)data;
    uint8_t block[64];
    size_t off = 0;
    uint64_t bitlen = (uint64_t)len * 8;
    while(off + 64 <= len) {
        sha256_transform(state, p + off);
        off += 64;
    }
    size_t rem = len - off;
    memset(block, 0, 64);
    memcpy(block, p + off, rem);
    block[rem] = 0x80;
    if(rem >= 56) {
        sha256_transform(state, block);
        memset(block, 0, 64);
    }
    for(int i = 0; i < 8; i++)
        block[63 - i] = (uint8_t)(bitlen >> (8 * i));
    sha256_transform(state, block);
    for(int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(state[i] >> 24);
        out[i*4+1] = (uint8_t)(state[i] >> 16);
        out[i*4+2] = (uint8_t)(state[i] >> 8);
        out[i*4+3] = (uint8_t)(state[i]);
    }
}

std::string Sha256Hex(const void *data, size_t len)
{
    uint8_t dig[32];
    Sha256(data, len, dig);
    char hex[65];
    for(int i = 0; i < 32; i++)
        sprintf(hex + i*2, "%02x", dig[i]);
    hex[64] = 0;
    return hex;
}

bool RandomBytes(void *dst, size_t n)
{
#ifdef _WIN32
    HCRYPTPROV prov = 0;
    if(!CryptAcquireContext(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return false;
    BOOL ok = CryptGenRandom(prov, (DWORD)n, (BYTE*)dst);
    CryptReleaseContext(prov, 0);
    return ok != 0;
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if(fd < 0)
        return false;
    size_t got = 0;
    while(got < n) {
        ssize_t r = read(fd, (char*)dst + got, n - got);
        if(r <= 0) { close(fd); return false; }
        got += (size_t)r;
    }
    close(fd);
    return true;
#endif
}

std::string RandomTokenHex(size_t nbytes)
{
    std::vector<uint8_t> buf(nbytes);
    if(!RandomBytes(buf.data(), nbytes)) {
        /* weak fallback */
        for(size_t i = 0; i < nbytes; i++)
            buf[i] = (uint8_t)(rand() ^ (time(nullptr) + (int)i));
    }
    static const char *hexd = "0123456789abcdef";
    std::string s;
    s.resize(nbytes * 2);
    for(size_t i = 0; i < nbytes; i++) {
        s[i*2]   = hexd[buf[i] >> 4];
        s[i*2+1] = hexd[buf[i] & 0xf];
    }
    return s;
}

} // namespace pf
