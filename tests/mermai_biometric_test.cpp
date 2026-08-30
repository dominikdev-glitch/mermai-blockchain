#include "mermai/mermai_biometric.hpp"
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/x509.h>
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    std::cout << "Running mermai_biometric_test (100% Real Cryptography)..." << std::endl;

    // 1. Generate real NIST P-256 (secp256r1) keypair
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    assert(pctx != nullptr);
    assert(EVP_PKEY_keygen_init(pctx) == 1);
    assert(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, NID_X9_62_prime256v1) == 1);
    EVP_PKEY* pkey = nullptr;
    assert(EVP_PKEY_keygen(pctx, &pkey) == 1);
    EVP_PKEY_CTX_free(pctx);

    // 2. Export public key as DER X.509 SubjectPublicKeyInfo
    unsigned char* der_pub = nullptr;
    int der_len = i2d_PUBKEY(pkey, &der_pub);
    assert(der_len > 0 && der_pub != nullptr);
    std::vector<uint8_t> pubkey_bytes(der_pub, der_pub + der_len);
    OPENSSL_free(der_pub);

    // 3. Test biometric address derivation
    std::string bio_addr = mermai::mermai_biometric_engine::derive_biometric_address(pubkey_bytes);
    assert(bio_addr.rfind("mrm_bio1", 0) == 0);
    assert(bio_addr.length() == 48);
    std::cout << "  [PASS] Biometric address derived: " << bio_addr << std::endl;

    // 4. Construct authentic WebAuthn authenticatorData (37 bytes: 32 rpIdHash + 1 flags + 4 signCount)
    std::vector<uint8_t> auth_data(37, 0xAA);
    auth_data[32] = 0x01; // User Present (UP) bit set

    std::string challenge_hex = "abcdef1234567890";
    std::string client_data_json = "{\"type\":\"webauthn.get\",\"challenge\":\"" + challenge_hex + "\",\"origin\":\"http://localhost:8080\"}";

    // Compute WebAuthn signed payload hash
    auto webauthn_hash = mermai::mermai_biometric_engine::compute_webauthn_hash(auth_data, client_data_json);
    assert(webauthn_hash.size() == 32);
    std::cout << "  [PASS] WebAuthn SHA-256 payload hash computed" << std::endl;

    // 5. Sign payload using the private key (DER ECDSA signature)
    EVP_PKEY_CTX* sctx = EVP_PKEY_CTX_new(pkey, nullptr);
    assert(sctx != nullptr);
    assert(EVP_PKEY_sign_init(sctx) == 1);
    size_t sig_len = 0;
    assert(EVP_PKEY_sign(sctx, nullptr, &sig_len, webauthn_hash.data(), webauthn_hash.size()) == 1);
    std::vector<uint8_t> signature(sig_len);
    assert(EVP_PKEY_sign(sctx, signature.data(), &sig_len, webauthn_hash.data(), webauthn_hash.size()) == 1);
    signature.resize(sig_len);
    EVP_PKEY_CTX_free(sctx);
    EVP_PKEY_free(pkey);

    // 6. Verify assertion with real cryptography
    mermai::mermai_biometric_assertion assertion;
    assertion.credential_id = "cred_12345";
    assertion.authenticator_data = auth_data;
    assertion.client_data_json = client_data_json;
    assertion.signature = signature;
    assertion.public_key = pubkey_bytes;
    assertion.sign_count = 1;

    // Valid verification test
    bool valid = mermai::mermai_biometric_engine::verify_assertion(challenge_hex, assertion);
    assert(valid);
    std::cout << "  [PASS] Real OpenSSL P-256 ECDSA assertion verified successfully!" << std::endl;

    // Tampered verification test (tamper signature)
    assertion.signature[5] ^= 0xFF;
    bool invalid = mermai::mermai_biometric_engine::verify_assertion(challenge_hex, assertion);
    assert(!invalid);
    std::cout << "  [PASS] Tampered signature correctly rejected!" << std::endl;

    std::cout << "\nmermai_biometric_test PASSED 100% (REAL CRYPTOGRAPHY)!" << std::endl;
    return 0;
}
