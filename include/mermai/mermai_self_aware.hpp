#ifndef MERMAI_SELF_AWARE_HPP
#define MERMAI_SELF_AWARE_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace mermai {

enum class node_health_status {
    OPTIMAL,
    DEGRADED,
    PARTITIONED,
    UNDER_ATTACK
};

struct peer_telemetry {
    std::string peer_id;
    uint64_t latency_ms = 0;
    uint64_t blocks_propagated = 0;
    uint64_t invalid_messages = 0;
    double reliability_score = 1.0;
};

struct node_introspection {
    double health_score = 100.0;
    node_health_status status = node_health_status::OPTIMAL;
    uint64_t avg_block_time_ms = 2000;
    uint64_t mempool_congestion_percent = 0;
    uint32_t active_peer_count = 0;
    std::string diagnostic_summary = "All systems operating normally.";
};

class mermai_self_aware_engine {
private:
    uint64_t last_evaluation_time = 0;
    std::unordered_map<std::string, peer_telemetry> peers;
    std::vector<uint64_t> block_intervals;
    node_introspection current_state;

public:
    mermai_self_aware_engine();
    void record_block(uint64_t block_height, uint64_t timestamp_ms);
    void record_peer_event(const std::string& peer_id, uint64_t latency_ms, bool valid_message);
    node_introspection evaluate_health(uint64_t mempool_size, uint64_t max_mempool_capacity);
    double get_adaptive_gas_multiplier() const;
    bool should_isolate_peer(const std::string& peer_id) const;
    std::string get_status_json() const;
};

} // namespace mermai

#endif // MERMAI_SELF_AWARE_HPP
