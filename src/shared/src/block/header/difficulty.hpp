#pragma once
#include "block/header/hash_exponential_request.hpp"
#include "crypto/hash.hpp"
#include "difficulty_declaration.hpp"
#include "general/byte_order.hpp"
#include "general/hex.hpp"
#include "general/reader.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// TargetV1 encoding (4 bytes):
//
// byte 0:   number of required zeros,
// byte 1-3: 24 base 2 digits starting at position [byte 0] from left
// Note: maximum is 256-32=224 because more difficult targets won't be
//       necessary is most likely not in this case the bits with index
//       224-255
//
// The following constants are defined in terms of host uint32_t numbers for convenience
// and need to be converted to big endian (network byte order) to match the byte
// ordering required above
//

inline TargetV1 TargetV1::from_raw(const uint8_t* pos)
{
    return TargetV1 { readuint32(pos) };
};

inline TargetV1::TargetV1(double difficulty)
{
    if (difficulty < 1.0)
        difficulty = 1.0;
    int exp;
    double coef = std::frexp(difficulty, &exp);
    double inv = 1 / coef; // will be in the interval (1,2]
    if (exp - 1 >= 256 - 24) {
        data = HARDESTTARGET_HOST;
        return;
    };
    uint32_t zeros = exp - 1;
    if (inv == 2.0) {
        set(zeros, 0x00FFFFFFu);
    } else [[likely]] { // need to shift by 23 to the left
        uint32_t digits(std::ldexp(inv, 23));
        if (digits < 0x00800000u)
            set(zeros, 0x00800000u);
        else if (digits > 0x00ffffffu)
            set(zeros, 0x00FFFFFFu);
        else
            set(zeros, digits);
    }
}

inline uint32_t TargetV1::zeros8() const
{
    return data >> 24;
};

inline uint32_t TargetV1::bits24() const
{ // returns values in [2^23,2^24)
    return 0x00FFFFFFul & data;
}

[[nodiscard]] inline bool TargetV1::compatible(const Hash& hash) const
{
    auto zeros = zeros8();
    if (zeros > (256u - 4 * 8u))
        return false;
    uint32_t bits = bits24();
    if ((bits & 0x00800000u) == 0)
        return false; // first digit must be 1
    const size_t zerobytes = zeros / 8; // number of complete zero bytes
    const size_t shift = zeros & 0x07u;

    for (size_t i = 0; i < zerobytes; ++i)
        if (hash[31 - i] != 0u)
            return false; // here we need zeros

    uint32_t threshold = bits << (8u - shift);
    uint32_t candidate;
    uint8_t* dst = reinterpret_cast<uint8_t*>(&candidate);
    const uint8_t* src = &hash[28 - zerobytes];
    dst[0] = src[3];
    dst[1] = src[2];
    dst[2] = src[1];
    dst[3] = src[0];
    candidate = ntoh32(candidate);
    if (candidate > threshold) {
        return false;
    }
    if (candidate < threshold) [[likely]] {
        return true;
    }
    for (size_t i = 0; i < 28 - zerobytes; ++i)
        if (hash[i] != 0)
            return false;
    return true;
}
inline double TargetV1::difficulty() const
{
    const int zeros = zeros8();
    double dbits = bits24();
    return std::ldexp(1 / dbits, zeros + 24);
}
inline TargetV1 TargetV1::genesis()
{
    return GENESISTARGET_HOST;
}

// TargetV2 encoding (4 bytes):
//
// byte 0:   number of required zeros,
// byte 1-3: 24 base 2 digits starting at position [byte 0] from left
// Note: maximum is 256-32=224 because more difficult targets won't be
//       necessary is most likely not in this case the bits with index
//       224-255
//
// The following constants are defined in terms of host uint32_t numbers for convenience
// and need to be converted to big endian (network byte order) to match the byte
// ordering required above
//

constexpr TargetV2::TargetV2(uint32_t data)
    : data(data) {};

inline TargetV2 TargetV2::from_raw(const uint8_t* pos)
{
    return TargetV2 { readuint32(pos) };
};

inline TargetV2::TargetV2(double difficulty)
    : TargetV2(0u)
{
    if (difficulty < 1.0)
        difficulty = 1.0;
    int exp;
    double coef = std::frexp(difficulty, &exp);
    double inv = 1 / coef; // will be in the interval (1,2]
    uint32_t zeros = exp - 1;
    if (zeros >= 3 * 256) {
        data = MaxTargetHost;
        return;
    };
    if (inv == 2.0) {
        set(zeros, 0x003fffffu);
    } else [[likely]] { // need to shift by 21 to the left
        uint32_t digits(std::ldexp(inv, 21));
        if (digits < 0x00200000u)
            set(zeros, 0x00200000u);
        else if (digits > 0x003fffffu)
            set(zeros, 0x003fffffu);
        else
            set(zeros, digits);
    }
}

inline uint32_t TargetV2::bits22()
    const
{ // returns values in [2^21,2^22)
    return 0x003FFFFFul & data;
}

inline uint32_t TargetV2::zeros10() const
{
    return data >> 22;
};


inline double TargetV2::difficulty() const
{
    const int zeros = zeros10();
    double dbits = bits22();
    return std::ldexp(
        1 / dbits,
        zeros + 22); // first digit  of ((uint8_t*)(&encodedDifficulty))[1] is 1,
                     // compensate for other 23 digts of the 3 byte mantissa
}

inline TargetV2 TargetV2::genesis_testnet()
{
    return (uint32_t(29) << 22) | 0x003FFFFFu;
}

inline TargetV2 TargetV2::initial()
{
    return (uint32_t(43) << 22) | 0x003FFFFFu;
}

inline TargetV2 TargetV2::initialv2()
{
    return (uint32_t(40) << 22) | 0x003FFFFFu;
}

inline bool TargetV2::compatible(const HashExponentialDigest& digest) const
{
    auto zerosTarget { zeros10() };
    assert(digest.negExp > 0);
    auto zerosDigest { digest.negExp - 1 };
    if (zerosTarget > zerosDigest)
        return false;
    // double bound(uint64_t(bits22() << 10) << (zerosDigest - zerosTarget));
    // double val(digest.data);
    // spdlog::info("fraction: {}", val / bound);
    if (zerosTarget < zerosDigest)
        return true;
    auto bits32 { bits22() << 10 };
    return digest.data < bits32;
}

inline std::string Target::hex_string() const{
    return serialize_hex(binary());
}
