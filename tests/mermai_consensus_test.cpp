#include "mermai/mermai_consensus.hpp"
#include <algorithm>
#include <cassert>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
std::string hash_to_hex(const mermai::mermai_hash256& hash) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : hash) output << std::setw(2) << static_cast<unsigned int>(byte);
    return output.str();
}
}

int main() {
    mermai::mermai_time_weighted_pos consensus;
    assert(consensus.register_validator("validator-a", 1000000));
    assert(consensus.register_validator("validator-b", 2000000));

    const uint64_t round_time = 1700000000;
    const auto first = consensus.select_block_proposer(round_time);
    const auto second = consensus.select_block_proposer(round_time);
    assert(!first.empty() && first == second);

    auto validator = std::make_shared<mermai::mermai_block>(1);
    validator->validator_address = first;
    validator->validator_stake = consensus.get_consensus_state().active_validators.front().amount;
    assert(validator->verify_time_weight_pos());

    auto secured_consensus = std::make_shared<mermai::mermai_time_weighted_pos>();
    assert(secured_consensus->register_validator("validator-one", 1000000));
    assert(secured_consensus->register_validator("validator-two", 2000000));
    mermai::mermai_blockchain secured_chain;
    secured_chain.set_consensus_engine(secured_consensus);
    const uint64_t slot = static_cast<uint64_t>(std::time(nullptr) / 10) * 10;
    const std::string selected = secured_consensus->select_block_proposer(slot);
    const auto& validators = secured_consensus->get_consensus_state().active_validators;
    const auto selected_validator = std::find_if(validators.begin(), validators.end(), [&selected](const auto& item) { return item.address == selected; });
    assert(selected_validator != validators.end());
    auto valid_block = std::make_shared<mermai::mermai_block>(1);
    valid_block->prev_block_hash = hash_to_hex(secured_chain.get_latest_block()->calculate_hash());
    valid_block->timestamp = slot;
    valid_block->validator_address = selected;
    valid_block->validator_stake = selected_validator->amount;
    valid_block->validator_age = slot >= selected_validator->lock_time ? slot - selected_validator->lock_time : 0;
    assert(secured_chain.add_block(valid_block));

    std::cout << "Consensus determinism test passed.\n";
}
