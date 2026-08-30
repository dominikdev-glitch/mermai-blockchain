#include "mermai/mermai_multisig.hpp"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <sstream>
#include <iomanip>
#include <set>
#include <algorithm>

namespace mermai {

namespace {

std::string hex_encode(const uint8_t* data, size_t size) {
    std::ostringstream ss;
    for (size_t i = 0; i < size; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    }
    return ss.str();
}

} // anonymous namespace

std::string mermai_multisig_engine::calculate_multisig_address(
    uint32_t threshold,
    const std::vector<mermai_public_key>& public_keys
) {
    std::ostringstream ss;
    ss << threshold << ":";
    for (const auto& pk : public_keys) {
        ss << hex_encode(pk.data(), pk.size()) << ",";
    }
    std::string payload = ss.str();

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(payload.data()), payload.size(), hash);
    return "mrm_ms1" + hex_encode(hash, 20);
}

mermai_multisig_account mermai_multisig_engine::create_account(
    uint32_t threshold,
    const std::vector<mermai_public_key>& public_keys
) {
    mermai_multisig_account acct;
    acct.threshold = threshold;
    acct.signers = public_keys;
    acct.address = calculate_multisig_address(threshold, public_keys);
    return acct;
}

std::string mermai_multisig_tx::signing_payload() const {
    return base_tx.serialize() + ":" + multisig_address;
}

bool mermai_multisig_engine::add_signature(
    mermai_multisig_tx& tx,
    const mermai_public_key& public_key,
    const mermai_signature& signature
) {
    for (const auto& sig : tx.signatures) {
        if (sig.public_key == public_key) return false;
    }
    tx.signatures.push_back({public_key, signature});
    return true;
}

bool mermai_multisig_engine::verify_multisig_tx(
    const mermai_multisig_account& account,
    const mermai_multisig_tx& tx
) {
    if (tx.signatures.size() < account.threshold) return false;

    std::string payload = tx.signing_payload();
    std::set<std::string> seen_signers;
    uint32_t valid_signatures = 0;

    for (const auto& sig_entry : tx.signatures) {
        bool is_member = false;
        for (const auto& signer_pk : account.signers) {
            if (signer_pk == sig_entry.public_key) {
                is_member = true;
                break;
            }
        }
        if (!is_member) continue;

        std::string pk_hex = hex_encode(sig_entry.public_key.data(), sig_entry.public_key.size());
        if (seen_signers.count(pk_hex) > 0) continue;

        const unsigned char* key_bytes = sig_entry.public_key.data();
        EVP_PKEY* pkey = d2i_PUBKEY(nullptr, &key_bytes, static_cast<long>(sig_entry.public_key.size()));
        if (!pkey) continue;

        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        bool ok = ctx &&
                  EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1 &&
                  EVP_DigestVerifyUpdate(ctx, payload.data(), payload.size()) == 1 &&
                  EVP_DigestVerifyFinal(ctx, sig_entry.signature.data(), sig_entry.signature.size()) == 1;

        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);

        if (ok) {
            seen_signers.insert(pk_hex);
            valid_signatures++;
        }
    }

    return valid_signatures >= account.threshold;
}

std::string mermai_multisig_account::serialize() const {
    std::ostringstream ss;
    ss << address << "|" << threshold << "|" << balance << "|" << nonce << "|";
    for (const auto& pk : signers) {
        ss << hex_encode(pk.data(), pk.size()) << ",";
    }
    return ss.str();
}

mermai_multisig_account mermai_multisig_account::deserialize(const std::string& data) {
    mermai_multisig_account acct;
    std::stringstream ss(data);
    std::string seg;
    if (std::getline(ss, seg, '|')) acct.address = seg;
    if (std::getline(ss, seg, '|')) acct.threshold = std::stoul(seg);
    if (std::getline(ss, seg, '|')) acct.balance = std::stoull(seg);
    if (std::getline(ss, seg, '|')) acct.nonce = std::stoull(seg);
    return acct;
}

std::string mermai_multisig_tx::serialize() const {
    return base_tx.serialize() + "#" + multisig_address;
}

mermai_multisig_tx mermai_multisig_tx::deserialize(const std::string& data) {
    mermai_multisig_tx tx;
    auto pos = data.find('#');
    if (pos != std::string::npos) {
        tx.base_tx = mermai_transaction::deserialize(data.substr(0, pos));
        tx.multisig_address = data.substr(pos + 1);
    }
    return tx;
}

} // namespace mermai
