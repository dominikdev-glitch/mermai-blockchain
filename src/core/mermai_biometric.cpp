#include "mermai/mermai_biometric.hpp"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/x509.h>
#include <openssl/param_build.h>
#include <openssl/core_names.h>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace mermai {

namespace {

EVP_PKEY* parse_ec_public_key(const std::vector<uint8_t>& key_bytes) {
    if (key_bytes.empty()) return nullptr;

    // 1. Try DER X.509 SubjectPublicKeyInfo
    const unsigned char* p = key_bytes.data();
    EVP_PKEY* pkey = d2i_PUBKEY(nullptr, &p, static_cast<long>(key_bytes.size()));
    if (pkey) return pkey;

    // 2. Try raw uncompressed (65 bytes with 0x04) or compressed (33 bytes with 0x02/0x03) EC point
    if (key_bytes.size() == 65 || key_bytes.size() == 33) {
        OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
        if (bld) {
            OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME, const_cast<char*>("prime256v1"), 0);
            OSSL_PARAM_BLD_push_octet_string(bld, OSSL_PKEY_PARAM_PUB_KEY, key_bytes.data(), key_bytes.size());
            OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(bld);
            OSSL_PARAM_BLD_free(bld);

            if (params) {
                EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
                if (ctx) {
                    if (EVP_PKEY_fromdata_init(ctx) > 0) {
                        EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_PUBLIC_KEY, params);
                    }
                    EVP_PKEY_CTX_free(ctx);
                }
                OSSL_PARAM_free(params);
            }
        }
    }
    return pkey;
}

} // anonymous namespace

std::string mermai_biometric_engine::derive_biometric_address(const std::vector<uint8_t>& public_key) {
    if (public_key.empty()) return "";
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(public_key.data(), public_key.size(), hash);
    std::ostringstream ss;
    ss << "mrm_bio1";
    for (size_t i = 0; i < 20; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

std::vector<uint8_t> mermai_biometric_engine::compute_webauthn_hash(
    const std::vector<uint8_t>& authenticator_data,
    const std::string& client_data_json
) {
    unsigned char client_hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(client_data_json.data()), client_data_json.size(), client_hash);

    std::vector<uint8_t> payload = authenticator_data;
    payload.insert(payload.end(), client_hash, client_hash + SHA256_DIGEST_LENGTH);

    std::vector<uint8_t> final_hash(SHA256_DIGEST_LENGTH);
    SHA256(payload.data(), payload.size(), final_hash.data());
    return final_hash;
}

bool mermai_biometric_engine::verify_biometric_signature(
    const std::vector<uint8_t>& data_hash,
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& public_key
) {
    if (data_hash.empty() || signature.empty() || public_key.empty()) return false;

    EVP_PKEY* pkey = parse_ec_public_key(public_key);
    if (!pkey) return false;

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    bool valid = false;
    if (ctx) {
        if (EVP_PKEY_verify_init(ctx) > 0) {
            // WebAuthn ASN.1 DER ECDSA signature verification
            int ret = EVP_PKEY_verify(ctx, signature.data(), signature.size(), data_hash.data(), data_hash.size());
            valid = (ret == 1);
        }
        EVP_PKEY_CTX_free(ctx);
    }
    EVP_PKEY_free(pkey);
    return valid;
}

bool mermai_biometric_engine::verify_assertion(
    const std::string& expected_challenge_hex,
    const mermai_biometric_assertion& assertion
) {
    if (assertion.client_data_json.empty() || assertion.authenticator_data.empty() || assertion.public_key.empty() || assertion.signature.empty()) {
        return false;
    }

    if (!expected_challenge_hex.empty() &&
        assertion.client_data_json.find(expected_challenge_hex) == std::string::npos) {
        return false;
    }

    // AuthenticatorData minimum length is 37 bytes (32 rpIdHash + 1 flags + 4 signCount)
    if (assertion.authenticator_data.size() < 37) return false;

    // User Present (UP) bit (bit 0) MUST be set by hardware authenticator
    uint8_t flags = assertion.authenticator_data[32];
    if ((flags & 0x01) == 0) return false;

    const auto signed_hash = compute_webauthn_hash(assertion.authenticator_data, assertion.client_data_json);
    return verify_biometric_signature(signed_hash, assertion.signature, assertion.public_key);
}

} // namespace mermai
