#include "mermai/mermai_self_aware.hpp"
#include <numeric>
#include <sstream>

namespace mermai {

mermai_self_aware_engine::mermai_self_aware_engine() {
    current_state.health_score = 100.0;
    current_state.status = node_health_status::OPTIMAL;
}

void mermai_self_aware_engine::record_block(uint64_t, uint64_t timestamp_ms) {
    if (last_evaluation_time > 0 && timestamp_ms > last_evaluation_time) {
        block_intervals.push_back(timestamp_ms - last_evaluation_time);
        if (block_intervals.size() > 50) block_intervals.erase(block_intervals.begin());
    }
    last_evaluation_time = timestamp_ms;
}

void mermai_self_aware_engine::record_peer_event(const std::string& peer_id, uint64_t latency_ms, bool valid_message) {
    auto& tel = peers[peer_id];
    tel.peer_id = peer_id;
    tel.latency_ms = (tel.latency_ms == 0) ? latency_ms : (tel.latency_ms * 7 + latency_ms * 3) / 10;
    if (valid_message) {
        tel.blocks_propagated++;
        tel.reliability_score = std::min(1.0, tel.reliability_score + 0.05);
    } else {
        tel.invalid_messages++;
        tel.reliability_score = std::max(0.0, tel.reliability_score - 0.25);
    }
}

node_introspection mermai_self_aware_engine::evaluate_health(uint64_t mempool_size, uint64_t max_mempool_capacity) {
    double score = 100.0;

    if (max_mempool_capacity > 0) {
        current_state.mempool_congestion_percent = (mempool_size * 100) / max_mempool_capacity;
        if (current_state.mempool_congestion_percent > 80) score -= 20.0;
        else if (current_state.mempool_congestion_percent > 50) score -= 10.0;
    }

    current_state.active_peer_count = static_cast<uint32_t>(peers.size());
    if (!block_intervals.empty()) {
        uint64_t sum = std::accumulate(block_intervals.begin(), block_intervals.end(), 0ULL);
        current_state.avg_block_time_ms = sum / block_intervals.size();
    }

    current_state.health_score = std::max(0.0, score);
    current_state.status = (score >= 80.0) ? node_health_status::OPTIMAL : node_health_status::DEGRADED;
    return current_state;
}

double mermai_self_aware_engine::get_adaptive_gas_multiplier() const {
    if (current_state.mempool_congestion_percent > 80) return 2.0;
    if (current_state.mempool_congestion_percent > 50) return 1.5;
    return 1.0;
}

bool mermai_self_aware_engine::should_isolate_peer(const std::string& peer_id) const {
    auto it = peers.find(peer_id);
    if (it == peers.end()) return false;
    return it->second.reliability_score < 0.2 || it->second.invalid_messages > 10;
}

std::string mermai_self_aware_engine::get_status_json() const {
    std::ostringstream ss;
    ss << "{\"health_score\":" << current_state.health_score << "}";
    return ss.str();
}

} // namespace mermai
