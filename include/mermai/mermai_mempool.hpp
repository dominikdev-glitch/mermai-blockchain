#pragma once

#include "mermai_blockchain.hpp"
#include <set>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <string>

namespace mermai {

class mermai_mempool {
private:
    mutable std::mutex mempool_mutex;
    size_t max_capacity;
    
    // id -> tx
    std::map<std::string, mermai_transaction> transactions;
    // (prev_tx_hash, output_index) -> spending_tx_id
    std::set<std::pair<std::string, uint32_t>> pending_utxos;
    // address -> highest_pending_nonce
    std::map<std::string, uint64_t> pending_nonces;

public:
    explicit mermai_mempool(size_t capacity = 5000);

    bool add_transaction(const mermai_transaction& tx);
    bool remove_transaction(const std::string& tx_id);
    bool has_transaction(const std::string& tx_id) const;
    void clear();

    std::vector<mermai_transaction> get_transactions(uint32_t limit = 1000) const;
    size_t size() const;
    
    // Check if an unspent output is already claimed in mempool
    bool is_utxo_spent(const std::string& tx_hash, uint32_t output_index) const;
    
    // Fee market: calculate median fee
    uint64_t get_median_fee() const;
};

} // namespace mermai
