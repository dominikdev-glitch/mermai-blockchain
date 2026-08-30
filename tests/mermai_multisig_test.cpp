#include "mermai/mermai_multisig.hpp"
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <cassert>
#include <iostream>

struct KeyPair {
    EVP_PKEY* pkey = nullptr;
    mermai::mermai_public_key public_key;

    static KeyPair generate() {
        KeyPair kp;
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
        EVP_PKEY_keygen_init(ctx);
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1);
        EVP_PKEY_keygen(ctx, &kp.pkey);
        EVP_PKEY_CTX_free(ctx);

        unsigned char* pub = nullptr;
        int len = i2d_PUBKEY(kp.pkey, &pub);
        kp.public_key.assign(pub, pub + len);
        OPENSSL_free(pub);
        return kp;
    }

    mermai::mermai_signature sign(const std::string& payload) const {
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey);
        EVP_DigestSignUpdate(ctx, payload.data(), payload.size());
        size_t sig_len = 0;
        EVP_DigestSignFinal(ctx, nullptr, &sig_len);
        mermai::mermai_signature sig(sig_len);
        if (EVP_DigestSignFinal(ctx, sig.data(), &sig_len) == 1) {
            sig.resize(sig_len);
        }
        EVP_MD_CTX_free(ctx);
        return sig;
    }

    ~KeyPair() {
        if (pkey) EVP_PKEY_free(pkey);
    }
};

int main() {
    std::cout << "Starting Mermai Phase 6 - Multi-Signature (M-of-N) tests..." << std::endl;

    auto k1 = KeyPair::generate();
    auto k2 = KeyPair::generate();
    auto k3 = KeyPair::generate();
    auto k_unauthorized = KeyPair::generate();

    std::vector<mermai::mermai_public_key> signers = {k1.public_key, k2.public_key, k3.public_key};

    auto account = mermai::mermai_multisig_engine::create_account(2, signers);
    assert(account.threshold == 2);
    assert(account.signers.size() == 3);
    assert(account.address.rfind("mrm_ms1", 0) == 0);
    std::cout << "  [OK] Created 2-of-3 multisig account: " << account.address << std::endl;

    mermai::mermai_multisig_tx mtx;
    mtx.multisig_address = account.address;
    mtx.base_tx.id = "tx_ms_001";
    mtx.base_tx.timestamp = 1700000000;
    mtx.base_tx.outputs.push_back({"recipient_address", 50000});

    std::string payload = mtx.signing_payload();

    assert(!mermai::mermai_multisig_engine::verify_multisig_tx(account, mtx));

    auto sig1 = k1.sign(payload);
    assert(mermai::mermai_multisig_engine::add_signature(mtx, k1.public_key, sig1));
    assert(!mermai::mermai_multisig_engine::verify_multisig_tx(account, mtx));
    std::cout << "  [OK] 1/2 signatures insufficient as expected" << std::endl;

    auto sig2 = k2.sign(payload);
    assert(mermai::mermai_multisig_engine::add_signature(mtx, k2.public_key, sig2));
    assert(mermai::mermai_multisig_engine::verify_multisig_tx(account, mtx));
    std::cout << "  [OK] 2/2 valid threshold signatures accepted" << std::endl;

    mermai::mermai_multisig_tx mtx_bad;
    mtx_bad.multisig_address = account.address;
    mtx_bad.base_tx = mtx.base_tx;
    std::string bad_payload = mtx_bad.signing_payload();

    auto sig_bad = k_unauthorized.sign(bad_payload);
    mermai::mermai_multisig_engine::add_signature(mtx_bad, k_unauthorized.public_key, sig_bad);
    auto sig1_b = k1.sign(bad_payload);
    mermai::mermai_multisig_engine::add_signature(mtx_bad, k1.public_key, sig1_b);

    assert(!mermai::mermai_multisig_engine::verify_multisig_tx(account, mtx_bad));
    std::cout << "  [OK] Unauthorized signer signature rejected" << std::endl;

    mermai::mermai_multisig_tx mtx_dup;
    mtx_dup.multisig_address = account.address;
    mtx_dup.base_tx = mtx.base_tx;
    assert(mermai::mermai_multisig_engine::add_signature(mtx_dup, k1.public_key, sig1));
    assert(!mermai::mermai_multisig_engine::add_signature(mtx_dup, k1.public_key, sig1));
    assert(!mermai::mermai_multisig_engine::verify_multisig_tx(account, mtx_dup));
    std::cout << "  [OK] Replay/duplicate signer detection verified" << std::endl;

    std::cout << "All Multi-Signature tests passed successfully!" << std::endl;
    return 0;
}
