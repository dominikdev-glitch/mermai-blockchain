#include "mermai/mermai_network.hpp"
#include "mermai/mermai_blockchain.hpp"
#include <cassert>
#include <iostream>
#include <memory>

int main() {
    std::cout << "Starting Mermai Phase 4 - Network Security tests..." << std::endl;

    auto blockchain = std::make_shared<mermai::mermai_blockchain>();
    auto p2p = std::make_shared<mermai::mermai_p2p_node>(6401, blockchain);

    // Test 1: Peer reputation scoring
    p2p->update_peer_reputation("192.168.1.100", 10);  // valid block
    p2p->update_peer_reputation("192.168.1.100", 10);  // valid block
    assert(p2p->get_peer_reputation("192.168.1.100") == 20);
    std::cout << "  [OK] Peer reputation: 2 valid blocks = score 20" << std::endl;

    // Test 2: Reputation penalty
    p2p->update_peer_reputation("192.168.1.200", -50); // invalid block
    assert(p2p->get_peer_reputation("192.168.1.200") == -50);
    std::cout << "  [OK] Peer reputation penalty: invalid block = -50" << std::endl;

    // Test 3: Auto-ban at -100 threshold
    p2p->update_peer_reputation("192.168.1.201", -50);
    p2p->update_peer_reputation("192.168.1.201", -50); // Now at -100
    assert(p2p->is_peer_banned("192.168.1.201"));
    std::cout << "  [OK] Auto-ban: peer at -100 reputation is banned" << std::endl;

    // Test 4: Rate limiter - allows within limit
    for (int i = 0; i < 10; ++i) {
        assert(p2p->allow_block_from_peer("192.168.1.150"));
    }
    std::cout << "  [OK] Rate limiter: 10 blocks/s allowed" << std::endl;

    // Test 5: Rate limiter - reject excess
    bool rejected = false;
    for (int i = 0; i < 20; ++i) {
        if (!p2p->allow_block_from_peer("192.168.1.160")) {
            rejected = true;
            break;
        }
    }
    assert(rejected);
    std::cout << "  [OK] Rate limiter: excess blocks rejected" << std::endl;

    // Test 6: Subnet diversity check (eclipse resistance)
    // Should allow first 3 connections from same /24
    assert(p2p->allow_subnet_connection("10.0.0.1"));
    assert(p2p->allow_subnet_connection("10.0.0.2"));
    assert(p2p->allow_subnet_connection("10.0.0.3"));
    // 4th from same subnet should be rejected
    assert(!p2p->allow_subnet_connection("10.0.0.4"));
    std::cout << "  [OK] Eclipse resistance: max 3 peers from same /24 subnet" << std::endl;

    std::cout << "\nAll Phase 4 Network Security tests passed successfully!" << std::endl;
    return 0;
}
