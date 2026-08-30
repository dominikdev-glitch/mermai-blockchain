#include "mermai/mermai_self_aware.hpp"
#include <cassert>
#include <iostream>

int main() {
    std::cout << "Running mermai_self_aware_test..." << std::endl;

    mermai::mermai_self_aware_engine engine;
    auto diag = engine.evaluate_health(0, 5000);
    assert(diag.health_score > 0.0);
    std::cout << "  [PASS] Health scoring: " << diag.health_score << "/100" << std::endl;

    engine.record_peer_event("peer_alice", 25, true);
    assert(!engine.should_isolate_peer("peer_alice"));
    std::cout << "  [PASS] Peer telemetry" << std::endl;

    auto congested = engine.evaluate_health(4500, 5000);
    double mult = engine.get_adaptive_gas_multiplier();
    assert(mult >= 1.5);
    std::cout << "  [PASS] Adaptive gas multiplier: " << mult << "x" << std::endl;

    std::cout << "mermai_self_aware_test PASSED!" << std::endl;
    return 0;
}
