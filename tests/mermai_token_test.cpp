#include "mermai/mermai_token.hpp"
#include <cassert>
#include <iostream>

int main() {
    std::cout << "Starting Mermai Phase 6 - MRM-20 Token Standard tests..." << std::endl;

    mermai::mermai_token_contract token("Mermai USD", "mUSD", 18, 1000000, "alice");

    assert(token.name() == "Mermai USD");
    assert(token.symbol() == "mUSD");
    assert(token.decimals() == 18);
    assert(token.total_supply() == 1000000);
    assert(token.owner() == "alice");
    assert(token.balance_of("alice") == 1000000);
    assert(token.balance_of("bob") == 0);
    std::cout << "  [OK] Token initialized: Supply=" << token.total_supply() << " Owner=alice" << std::endl;

    assert(token.transfer("alice", "bob", 200000));
    assert(token.balance_of("alice") == 800000);
    assert(token.balance_of("bob") == 200000);
    std::cout << "  [OK] Transfer alice -> bob: 200,000 mUSD" << std::endl;

    assert(!token.transfer("bob", "carol", 300000));
    assert(token.balance_of("bob") == 200000);
    std::cout << "  [OK] Overdraft transfer rejected" << std::endl;

    assert(token.approve("bob", "carol", 50000));
    assert(token.allowance("bob", "carol") == 50000);

    assert(token.transfer_from("carol", "bob", "dave", 30000));
    assert(token.balance_of("bob") == 170000);
    assert(token.balance_of("dave") == 30000);
    assert(token.allowance("bob", "carol") == 20000);
    std::cout << "  [OK] transfer_from with allowance: 30,000 mUSD to dave" << std::endl;

    assert(!token.transfer_from("carol", "bob", "dave", 25000));
    std::cout << "  [OK] Exceeding allowance rejected" << std::endl;

    assert(token.mint("alice", "carol", 100000));
    assert(token.total_supply() == 1100000);
    assert(token.balance_of("carol") == 100000);
    assert(!token.mint("bob", "bob", 50000));
    std::cout << "  [OK] Owner minting allowed, non-owner rejected" << std::endl;

    assert(token.burn("alice", 100000));
    assert(token.total_supply() == 1000000);
    assert(token.balance_of("alice") == 700000);
    std::cout << "  [OK] Token burning reduces supply" << std::endl;

    const auto& events = token.get_events();
    assert(!events.empty());
    std::cout << "  [OK] Total token events emitted: " << events.size() << std::endl;

    std::cout << "All MRM-20 Token tests passed successfully!" << std::endl;
    return 0;
}
