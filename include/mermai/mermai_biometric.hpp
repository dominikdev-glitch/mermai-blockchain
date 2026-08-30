#ifndef MERMAI_BIOMETRIC_HPP
#define MERMAI_BIOMETRIC_HPP

#include <string>
#include <vector>
#include <cstdint>

namespace mermai {

struct mermai_biometric_assertion {
    std::string credential_id;
    std::vector<uint8_t> authenticator_data;
    std::string client_data_json;
    std::vector<uint8_t> signature;
    std::vector<uint8_t> public_key;
    uint32_t sign_count = 0;
};

class mermai_biometric_engine {
public:
    static std::string derive_biometric_address(const std::vector<uint8_t>& public_key);

    static bool verify_assertion(
        const std::string& expected_challenge_hex,
        const mermai_biometric_assertion& assertion
    );

    static bool verify_biometric_signature(
        const std::vector<uint8_t>& data_hash,
        const std::vector<uint8_t>& signature,
        const std::vector<uint8_t>& public_key
    );

    static std::vector<uint8_t> compute_webauthn_hash(
        const std::vector<uint8_t>& authenticator_data,
        const std::string& client_data_json
    );
};

} // namespace mermai

#endif // MERMAI_BIOMETRIC_HPP
