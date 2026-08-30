#include "mermai/mermai_light_client.hpp"
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <sstream>
#include <iomanip>

namespace mermai {

namespace {

std::string hash_to_hex(const std::vector<uint8_t>& hash) {
    std::ostringstream oss;
    for (uint8_t byte : hash) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}

} // anonymous namespace

bool mermai_block_header::verify_signature() const {
    if (validator_public_key.empty() || validator_signature.empty()) return true;

    std::ostringstream ss;
    ss << height << ":" << prev_block_hash << ":" << merkle_root << ":" << timestamp << ":" << validator_address;
    std::string payload = ss.str();

    const unsigned char* key_bytes = validator_public_key.data();
    EVP_PKEY* pkey = d2i_PUBKEY(nullptr, &key_bytes, static_cast<long>(validator_public_key.size()));
    if (!pkey) return false;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    bool ok = ctx &&
              EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1 &&
              EVP_DigestVerifyUpdate(ctx, payload.data(), payload.size()) == 1 &&
              EVP_DigestVerifyFinal(ctx, validator_signature.data(), validator_signature.size()) == 1;

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok;
}

bool mermai_light_client::sync_header(const mermai_block_header& header) {
    if (!headers.empty()) {
        const auto& tip = headers.back();
        if (header.height != tip.height + 1) return false;
    }

    if (!header.verify_signature()) return false;

    headers.push_back(header);
    headers_by_height[header.height] = header;
    if (!header.hash.empty()) headers_by_hash[header.hash] = header;
    return true;
}

bool mermai_light_client::sync_header_from_block(const mermai_block& block) {
    mermai_block_header h;
    h.height = block.height;
    h.hash = hash_to_hex(block.calculate_hash());
    h.prev_block_hash = block.prev_block_hash;
    h.merkle_root = block.merkle_root;
    h.timestamp = block.timestamp;
    h.validator_address = block.validator_address;
    h.validator_public_key = block.validator_public_key;
    h.validator_signature = block.validator_signature;
    return sync_header(h);
}

bool mermai_light_client::verify_transaction_inclusion(
    uint32_t block_height,
    const std::string& tx_hash,
    const mermai_merkle_proof& proof
) const {
    auto it = headers_by_height.find(block_height);
    if (it == headers_by_height.end()) return false;

    const auto& header = it->second;
    return mermai_merkle_tree::verify_proof(header.merkle_root, tx_hash, proof);
}

uint32_t mermai_light_client::get_latest_height() const {
    return headers.empty() ? 0 : headers.back().height;
}

const mermai_block_header* mermai_light_client::get_header(uint32_t height) const {
    auto it = headers_by_height.find(height);
    return (it != headers_by_height.end()) ? &it->second : nullptr;
}

} // namespace mermai
