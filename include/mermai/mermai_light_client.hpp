#pragma once

#include "mermai/mermai_blockchain.hpp"
#include "mermai/mermai_merkle.hpp"
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <memory>

namespace mermai {

struct mermai_block_header {
    uint32_t height = 0;
    std::string hash;
    std::string prev_block_hash;
    std::string merkle_root;
    uint64_t timestamp = 0;
    std::string validator_address;
    mermai_public_key validator_public_key;
    mermai_signature validator_signature;

    bool verify_signature() const;
};

class mermai_light_client {
private:
    std::vector<mermai_block_header> headers;
    std::map<uint32_t, mermai_block_header> headers_by_height;
    std::map<std::string, mermai_block_header> headers_by_hash;

public:
    mermai_light_client() = default;

    bool sync_header(const mermai_block_header& header);
    bool sync_header_from_block(const mermai_block& block);

    bool verify_transaction_inclusion(
        uint32_t block_height,
        const std::string& tx_hash,
        const mermai_merkle_proof& proof
    ) const;

    uint32_t get_latest_height() const;
    const mermai_block_header* get_header(uint32_t height) const;
    size_t get_header_count() const { return headers.size(); }
};

} // namespace mermai
