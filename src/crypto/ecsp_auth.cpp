#include "openomada/crypto/ecsp_auth.hpp"

#include "openomada/crypto/hash.hpp"

namespace openomada::crypto {

std::string calculate_ecsp2_auth(
    std::string_view username,
    std::string_view encrypted_password,
    std::string_view random_key
) {
    std::string original;
    original.reserve(username.size() + encrypted_password.size());
    original.append(username);
    original.append(encrypted_password);

    const std::string first_hash = upper_sha256(original);

    std::string proof_input;
    proof_input.reserve(first_hash.size() + random_key.size());
    proof_input.append(first_hash);
    proof_input.append(random_key);
    return upper_sha256(proof_input);
}

std::string calculate_md5_mode_auth(
    std::string_view username,
    std::string_view plain_password,
    std::string_view random_key
) {
    return calculate_ecsp2_auth(username, upper_md5(plain_password), random_key);
}

} // namespace openomada::crypto

