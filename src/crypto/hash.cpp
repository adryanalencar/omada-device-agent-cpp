#include "openomada/crypto/hash.hpp"

#include <array>

#if defined(OPENOMADA_USE_COMMONCRYPTO)
#include <CommonCrypto/CommonDigest.h>
#elif defined(OPENOMADA_USE_OPENSSL)
#include <openssl/evp.h>
#else
#error "No OpenOmada crypto backend selected"
#endif

namespace openomada::crypto {
namespace {

std::string upper_hex(const unsigned char* bytes, std::size_t size) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(kHex[(bytes[i] >> 4) & 0x0F]);
        out.push_back(kHex[bytes[i] & 0x0F]);
    }
    return out;
}

#if defined(OPENOMADA_USE_OPENSSL)
std::string digest_evp(const EVP_MD* md, const std::uint8_t* data, std::size_t size) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_len = 0;
    if (EVP_Digest(data, size, digest.data(), &digest_len, md, nullptr) != 1) {
        return {};
    }
    return upper_hex(digest.data(), digest_len);
}
#endif

} // namespace

std::string upper_md5(std::string_view text) {
#if defined(OPENOMADA_USE_COMMONCRYPTO)
    std::array<unsigned char, CC_MD5_DIGEST_LENGTH> digest{};
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    CC_MD5(text.data(), static_cast<CC_LONG>(text.size()), digest.data());
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    return upper_hex(digest.data(), digest.size());
#else
    return digest_evp(
        EVP_md5(),
        reinterpret_cast<const std::uint8_t*>(text.data()),
        text.size()
    );
#endif
}

std::string upper_sha256(const std::uint8_t* data, std::size_t size) {
#if defined(OPENOMADA_USE_COMMONCRYPTO)
    std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
    CC_SHA256(data, static_cast<CC_LONG>(size), digest.data());
    return upper_hex(digest.data(), digest.size());
#else
    return digest_evp(EVP_sha256(), data, size);
#endif
}

std::string upper_sha256(std::string_view text) {
    return upper_sha256(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

} // namespace openomada::crypto
