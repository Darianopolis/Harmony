#pragma once

#include <openssl/evp.h>

#include <array>
#include <format>
#include <string>
#include <string_view>

namespace harmony {

inline auto sha256_hex(std::string_view input) -> std::string {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, input.data(), input.size());
    std::array<unsigned char, 32> digest{};
    unsigned int len = 32;
    EVP_DigestFinal_ex(ctx, digest.data(), &len);
    EVP_MD_CTX_free(ctx);

    std::string result;
    result.reserve(64);
    for (unsigned char b : digest) {
        std::format_to(std::back_inserter(result), "{:02x}", b);
    }
    return result;
}

} // namespace harmony
