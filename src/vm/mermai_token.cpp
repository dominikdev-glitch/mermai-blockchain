#include "mermai/mermai_token.hpp"

namespace mermai {

mermai_token_contract::mermai_token_contract(
    const std::string& name,
    const std::string& symbol,
    uint8_t decimals,
    uint64_t initial_supply,
    const std::string& owner
) : name_(name), symbol_(symbol), decimals_(decimals), total_supply_(initial_supply), owner_(owner) {
    if (initial_supply > 0 && !owner.empty()) {
        balances[owner] = initial_supply;
        event_logs.push_back({"Transfer", "", owner, initial_supply});
    }
}

uint64_t mermai_token_contract::balance_of(const std::string& address) const {
    auto it = balances.find(address);
    return (it != balances.end()) ? it->second : 0;
}

uint64_t mermai_token_contract::allowance(const std::string& owner, const std::string& spender) const {
    auto it = allowances.find(owner);
    if (it == allowances.end()) return 0;
    auto sp_it = it->second.find(spender);
    return (sp_it != it->second.end()) ? sp_it->second : 0;
}

bool mermai_token_contract::transfer(const std::string& caller, const std::string& to, uint64_t amount) {
    if (caller.empty() || to.empty()) return false;
    if (balances[caller] < amount) return false;

    balances[caller] -= amount;
    balances[to] += amount;
    event_logs.push_back({"Transfer", caller, to, amount});
    return true;
}

bool mermai_token_contract::approve(const std::string& caller, const std::string& spender, uint64_t amount) {
    if (caller.empty() || spender.empty()) return false;
    allowances[caller][spender] = amount;
    event_logs.push_back({"Approval", caller, spender, amount});
    return true;
}

bool mermai_token_contract::transfer_from(
    const std::string& spender,
    const std::string& from,
    const std::string& to,
    uint64_t amount
) {
    if (spender.empty() || from.empty() || to.empty()) return false;
    if (balances[from] < amount) return false;
    if (allowances[from][spender] < amount) return false;

    allowances[from][spender] -= amount;
    balances[from] -= amount;
    balances[to] += amount;
    event_logs.push_back({"Transfer", from, to, amount});
    return true;
}

bool mermai_token_contract::mint(const std::string& caller, const std::string& to, uint64_t amount) {
    if (caller != owner_ || to.empty()) return false;
    total_supply_ += amount;
    balances[to] += amount;
    event_logs.push_back({"Mint", "", to, amount});
    return true;
}

bool mermai_token_contract::burn(const std::string& caller, uint64_t amount) {
    if (balances[caller] < amount) return false;
    balances[caller] -= amount;
    total_supply_ -= amount;
    event_logs.push_back({"Burn", caller, "", amount});
    return true;
}

std::vector<uint8_t> mermai_token_contract::compile_to_bytecode() const {
    std::vector<uint8_t> bytecode;
    bytecode.push_back(static_cast<uint8_t>(vm::mermai_opcode::PUSH0));
    bytecode.push_back(static_cast<uint8_t>(vm::mermai_opcode::STOP));
    return bytecode;
}

} // namespace mermai
