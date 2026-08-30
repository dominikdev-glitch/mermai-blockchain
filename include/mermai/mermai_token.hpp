#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <memory>
#include "mermai/mermai_vm.hpp"

namespace mermai {

struct mermai_token_event {
    std::string event_type;
    std::string from;
    std::string to;
    uint64_t value = 0;
};

class mermai_token_contract {
private:
    std::string name_;
    std::string symbol_;
    uint8_t decimals_ = 18;
    uint64_t total_supply_ = 0;
    std::string owner_;

    std::map<std::string, uint64_t> balances;
    std::map<std::string, std::map<std::string, uint64_t>> allowances;
    std::vector<mermai_token_event> event_logs;

public:
    mermai_token_contract(
        const std::string& name,
        const std::string& symbol,
        uint8_t decimals,
        uint64_t initial_supply,
        const std::string& owner
    );

    const std::string& name() const { return name_; }
    const std::string& symbol() const { return symbol_; }
    uint8_t decimals() const { return decimals_; }
    uint64_t total_supply() const { return total_supply_; }
    const std::string& owner() const { return owner_; }

    uint64_t balance_of(const std::string& address) const;
    uint64_t allowance(const std::string& owner, const std::string& spender) const;

    bool transfer(const std::string& caller, const std::string& to, uint64_t amount);
    bool approve(const std::string& caller, const std::string& spender, uint64_t amount);
    bool transfer_from(const std::string& spender, const std::string& from, const std::string& to, uint64_t amount);
    bool mint(const std::string& caller, const std::string& to, uint64_t amount);
    bool burn(const std::string& caller, uint64_t amount);

    const std::vector<mermai_token_event>& get_events() const { return event_logs; }
    void clear_events() { event_logs.clear(); }

    std::vector<uint8_t> compile_to_bytecode() const;
};

} // namespace mermai
