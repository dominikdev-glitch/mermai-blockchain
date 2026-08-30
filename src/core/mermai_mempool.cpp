/*
 * MERMAI Transaction Mempool
 * 
 * Creator: dominikdev-glitch
 * High-performance, double-spend resistant transaction pool with priority eviction
 */

#include "../include/mermai/mermai_mempool.hpp"
#include <algorithm>
#include <numeric>

namespace mermai {

mermai_mempool::mermai_mempool(size_t capacity) : max_capacity(capacity) {}

bool mermai_mempool::add_transaction(const mermai_transaction& tx) {
    std::lock_guard<std::mutex> lock(mempool_mutex);
    const std::string tx_id = tx.id.empty() ? tx.get_hash() : tx.id;
    if (transactions.find(tx_id) != transactions.end()) {
        return false; // Duplicate transaction ID
    }

    // Check for UTXO conflict across mempool
    for (const auto& input : tx.inputs) {
        auto key = std::make_pair(input.prev_tx_hash, input.prev_output_idx);
        if (pending_utxos.find(key) != pending_utxos.end()) {
            return false; // Conflict: UTXO already spent in another unconfirmed tx
        }
    }

    // If mempool is full, evict lowest fee transaction if new tx has higher fee
    if (transactions.size() >= max_capacity) {
        auto lowest = std::min_element(transactions.begin(), transactions.end(),
            [](const auto& a, const auto& b) { return a.second.fee < b.second.fee; });
        if (lowest != transactions.end() && lowest->second.fee < tx.fee) {
            // Evict lowest
            for (const auto& in : lowest->second.inputs) {
                pending_utxos.erase(std::make_pair(in.prev_tx_hash, in.prev_output_idx));
            }
            transactions.erase(lowest);
        } else {
            return false; // Mempool full and fee too low
        }
    }

    // Record pending UTXOs
    for (const auto& input : tx.inputs) {
        pending_utxos.insert(std::make_pair(input.prev_tx_hash, input.prev_output_idx));
    }

    transactions[tx_id] = tx;
    return true;
}

bool mermai_mempool::remove_transaction(const std::string& tx_id) {
    std::lock_guard<std::mutex> lock(mempool_mutex);
    auto it = transactions.find(tx_id);
    if (it == transactions.end()) return false;

    for (const auto& in : it->second.inputs) {
        pending_utxos.erase(std::make_pair(in.prev_tx_hash, in.prev_output_idx));
    }
    transactions.erase(it);
    return true;
}

bool mermai_mempool::has_transaction(const std::string& tx_id) const {
    std::lock_guard<std::mutex> lock(mempool_mutex);
    return transactions.find(tx_id) != transactions.end();
}

void mermai_mempool::clear() {
    std::lock_guard<std::mutex> lock(mempool_mutex);
    transactions.clear();
    pending_utxos.clear();
    pending_nonces.clear();
}

std::vector<mermai_transaction> mermai_mempool::get_transactions(uint32_t limit) const {
    std::lock_guard<std::mutex> lock(mempool_mutex);
    std::vector<mermai_transaction> list;
    for (const auto& pair : transactions) {
        list.push_back(pair.second);
    }
    // Sort descending by fee
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        return a.fee > b.fee;
    });
    if (list.size() > limit) {
        list.resize(limit);
    }
    return list;
}

size_t mermai_mempool::size() const {
    std::lock_guard<std::mutex> lock(mempool_mutex);
    return transactions.size();
}

bool mermai_mempool::is_utxo_spent(const std::string& tx_hash, uint32_t output_index) const {
    std::lock_guard<std::mutex> lock(mempool_mutex);
    return pending_utxos.find(std::make_pair(tx_hash, output_index)) != pending_utxos.end();
}

uint64_t mermai_mempool::get_median_fee() const {
    std::lock_guard<std::mutex> lock(mempool_mutex);
    if (transactions.empty()) return 1000;
    std::vector<uint64_t> fees;
    for (const auto& pair : transactions) fees.push_back(pair.second.fee);
    std::sort(fees.begin(), fees.end());
    return fees[fees.size() / 2];
}

} // namespace mermai
