#pragma once

#include "mermai/mermai_blockchain.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <map>

namespace mermai {

struct mermai_multisig_account {
    std::string address;
    uint32_t threshold = 1;
    std::vector<mermai_public_key> signers;
    uint64_t balance = 0;
    uint64_t nonce = 0;

    std::string serialize() const;
    static mermai_multisig_account deserialize(const std::string& data);
};

struct mermai_multisig_signature {
    mermai_public_key public_key;
    mermai_signature signature;
};

struct mermai_multisig_tx {
    mermai_transaction base_tx;
    std::string multisig_address;
    std::vector<mermai_multisig_signature> signatures;

    std::string serialize() const;
    static mermai_multisig_tx deserialize(const std::string& data);
    std::string signing_payload() const;
};

class mermai_multisig_engine {
public:
    static mermai_multisig_account create_account(
        uint32_t threshold,
        const std::vector<mermai_public_key>& public_keys
    );

    static std::string calculate_multisig_address(
        uint32_t threshold,
        const std::vector<mermai_public_key>& public_keys
    );

    static bool add_signature(
        mermai_multisig_tx& tx,
        const mermai_public_key& public_key,
        const mermai_signature& signature
    );

    static bool verify_multisig_tx(
        const mermai_multisig_account& account,
        const mermai_multisig_tx& tx
    );
};

} // namespace mermai
