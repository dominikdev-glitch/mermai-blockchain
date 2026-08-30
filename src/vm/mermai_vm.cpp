/*
 * MERMAI Smart Contract Virtual Machine
 * 
 * Creator: dominikdev-glitch
 * Deterministic contract execution, gas metering, and persistent storage
 */

#include "../include/mermai/mermai_vm.hpp"
#include <cstring>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <ctime>

namespace mermai::vm {

// ============================================================================
// MERMAI VM IMPLEMENTATION
// ============================================================================

mermai_vm::mermai_vm(
    const std::vector<uint8_t>& code,
    const mermai_execution_context& ctx
) : context(ctx), bytecode(code.data()), bytecode_size(code.size()) {
    context.memory.resize(65536, 0); // 64KB memory
}

bool mermai_vm::execute() {
    context.program_counter = 0;
    context.halted = false;
    
    while (!context.halted && context.program_counter < bytecode_size) {
        if (!check_gas(1)) {
            context.revert_reason = "Out of gas";
            return false;
        }
        
        mermai_opcode opcode = static_cast<mermai_opcode>(bytecode[context.program_counter++]);
        
        try {
            switch (opcode) {
                case mermai_opcode::PUSH0: {
                    std::array<uint8_t, 32> zero = {0};
                    push_word(zero);
                    context.gas_used += GAS_PUSH;
                    break;
                }
                
                case mermai_opcode::PUSH1: {
                    if (context.program_counter >= bytecode_size) return false;
                    std::array<uint8_t, 32> word = {0};
                    word[31] = bytecode[context.program_counter++];
                    push_word(word);
                    context.gas_used += GAS_PUSH;
                    break;
                }

                case mermai_opcode::PUSH32: {
                    if (context.program_counter + 32 > bytecode_size) return false;
                    std::array<uint8_t, 32> word;
                    std::copy(bytecode + context.program_counter, bytecode + context.program_counter + 32, word.begin());
                    context.program_counter += 32;
                    push_word(word);
                    context.gas_used += GAS_PUSH;
                    break;
                }
                
                case mermai_opcode::POP: {
                    if (context.stack.empty()) return false;
                    context.stack.pop_back();
                    break;
                }
                
                case mermai_opcode::ADD: {
                    if (context.stack.size() < 2) return false;
                    auto b = pop_word();
                    auto a = pop_word();
                    push_word(add(a, b));
                    context.gas_used += GAS_ADD;
                    break;
                }
                
                case mermai_opcode::MUL: {
                    if (context.stack.size() < 2) return false;
                    auto b = pop_word();
                    auto a = pop_word();
                    push_word(mul(a, b));
                    context.gas_used += GAS_MUL;
                    break;
                }
                
                case mermai_opcode::SUB: {
                    if (context.stack.size() < 2) return false;
                    auto b = pop_word();
                    auto a = pop_word();
                    push_word(sub(a, b));
                    context.gas_used += GAS_ADD;
                    break;
                }

                case mermai_opcode::DIV:
                case mermai_opcode::MOD: {
                    if (context.stack.size() < 2) return false;
                    const auto divisor = pop_word();
                    const auto dividend = pop_word();
                    bool zero = true;
                    for (const auto byte : divisor) zero = zero && byte == 0;
                    std::array<uint8_t, 32> result = {0};
                    if (!zero) {
                        uint64_t left = 0;
                        uint64_t right = 0;
                        for (size_t index = 24; index < 32; ++index) {
                            left = (left << 8) | dividend[index];
                            right = (right << 8) | divisor[index];
                        }
                        const uint64_t value = static_cast<mermai_opcode>(bytecode[context.program_counter - 1]) == mermai_opcode::DIV ? left / right : left % right;
                        for (int index = 31; index >= 24; --index) result[index] = static_cast<uint8_t>(value >> ((31 - index) * 8));
                    }
                    push_word(result);
                    context.gas_used += GAS_ADD;
                    break;
                }

                case mermai_opcode::DUP1: {
                    push_word(peek_word());
                    break;
                }

                case mermai_opcode::SWAP1: {
                    if (context.stack.size() < 2) return false;
                    std::swap(context.stack[context.stack.size() - 1], context.stack[context.stack.size() - 2]);
                    break;
                }

                case mermai_opcode::LT:
                case mermai_opcode::GT:
                case mermai_opcode::EQ: {
                    if (context.stack.size() < 2) return false;
                    const auto right = pop_word();
                    const auto left = pop_word();
                    bool equal = left == right;
                    bool less = std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end());
                    std::array<uint8_t, 32> result = {0};
                    const auto current = static_cast<mermai_opcode>(bytecode[context.program_counter - 1]);
                    result[31] = current == mermai_opcode::EQ ? equal : (current == mermai_opcode::LT ? less : (!equal && !less));
                    push_word(result);
                    break;
                }

                case mermai_opcode::MLOAD: {
                    if (context.stack.empty()) return false;
                    push_word(load_word_from_memory(static_cast<uint32_t>(pop_word()[31])));
                    break;
                }

                case mermai_opcode::MSTORE: {
                    if (context.stack.size() < 2) return false;
                    const auto offset = pop_word();
                    const auto value = pop_word();
                    store_word_to_memory(static_cast<uint32_t>(offset[31]), value);
                    break;
                }

                case mermai_opcode::SLOAD: {
                    if (context.stack.empty()) return false;
                    const auto key = pop_word();
                    const std::string storage_key(reinterpret_cast<const char*>(key.data()), key.size());
                    const auto value = load_from_storage(storage_key);
                    std::array<uint8_t, 32> word = {0};
                    const size_t copy_size = std::min(value.size(), word.size());
                    std::copy(value.end() - copy_size, value.end(), word.end() - copy_size);
                    push_word(word);
                    context.gas_used += GAS_SLOAD;
                    break;
                }

                case mermai_opcode::SSTORE: {
                    if (context.stack.size() < 2) return false;
                    const auto key = pop_word();
                    const auto value = pop_word();
                    store_to_storage(std::string(reinterpret_cast<const char*>(key.data()), key.size()), {value.begin(), value.end()});
                    break;
                }

                case mermai_opcode::JMP: {
                    if (context.stack.empty()) return false;
                    jump(static_cast<uint32_t>(pop_word()[31]));
                    break;
                }

                case mermai_opcode::JMPI: {
                    if (context.stack.size() < 2) return false;
                    const auto target = pop_word();
                    const auto condition = pop_word();
                    bool nonzero = false;
                    for (const auto byte : condition) nonzero = nonzero || byte != 0;
                    if (nonzero) jump(static_cast<uint32_t>(target[31]));
                    break;
                }

                case mermai_opcode::CALLER: {
                    std::array<uint8_t, 32> word = {0};
                    const size_t copy_size = std::min(context.caller_address.size(), word.size());
                    std::copy(context.caller_address.begin(), context.caller_address.begin() + copy_size, word.begin());
                    push_word(word);
                    break;
                }

                case mermai_opcode::CALLVALUE: {
                    std::array<uint8_t, 32> word = {0};
                    for (int i = 31; i >= 24; --i) word[i] = static_cast<uint8_t>(context.call_value >> ((31 - i) * 8));
                    push_word(word);
                    break;
                }

                case mermai_opcode::TIMESTAMP: {
                    std::array<uint8_t, 32> word = {0};
                    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
                    for (int i = 31; i >= 24; --i) word[i] = static_cast<uint8_t>(now >> ((31 - i) * 8));
                    push_word(word);
                    break;
                }

                case mermai_opcode::GASLIMIT: {
                    std::array<uint8_t, 32> word = {0};
                    uint64_t rem = context.gas_limit > context.gas_used ? context.gas_limit - context.gas_used : 0;
                    for (int i = 31; i >= 24; --i) word[i] = static_cast<uint8_t>(rem >> ((31 - i) * 8));
                    push_word(word);
                    break;
                }

                case mermai_opcode::RETURN: {
                    context.halted = true;
                    break;
                }

                case mermai_opcode::REVERT: {
                    context.halted = true;
                    context.revert_reason = "Contract reverted";
                    return false;
                }

                case mermai_opcode::STOP: {
                    context.halted = true;
                    break;
                }

                default:
                    context.revert_reason = "Unknown opcode";
                    return false;
            }
        } catch (const std::exception& e) {
            context.revert_reason = e.what();
            return false;
        }
    }
    
    return true;
}

void mermai_vm::push_word(const std::array<uint8_t, 32>& word) {
    context.stack.push_back(word);
}

std::array<uint8_t, 32> mermai_vm::pop_word() {
    if (context.stack.empty()) {
        throw std::runtime_error("Stack underflow");
    }
    auto word = context.stack.back();
    context.stack.pop_back();
    return word;
}

std::array<uint8_t, 32> mermai_vm::peek_word(size_t depth) const {
    if (depth >= context.stack.size()) {
        throw std::runtime_error("Stack underflow");
    }
    return context.stack[context.stack.size() - 1 - depth];
}

std::array<uint8_t, 32> mermai_vm::add(
    const std::array<uint8_t, 32>& a,
    const std::array<uint8_t, 32>& b
) {
    std::array<uint8_t, 32> result = {0};
    
    uint64_t carry = 0;
    for (int i = 31; i >= 0; i--) {
        uint64_t sum = (uint64_t)a[i] + b[i] + carry;
        result[i] = sum & 0xFF;
        carry = sum >> 8;
    }
    
    return result;
}

std::array<uint8_t, 32> mermai_vm::sub(
    const std::array<uint8_t, 32>& a,
    const std::array<uint8_t, 32>& b
) {
    std::array<uint8_t, 32> result;
    
    int borrow = 0;
    for (int i = 31; i >= 0; i--) {
        int diff = (int)a[i] - b[i] - borrow;
        if (diff < 0) {
            result[i] = diff + 256;
            borrow = 1;
        } else {
            result[i] = diff;
            borrow = 0;
        }
    }
    
    return result;
}

std::array<uint8_t, 32> mermai_vm::mul(
    const std::array<uint8_t, 32>& a,
    const std::array<uint8_t, 32>& b
) {
    std::array<uint8_t, 32> result = {0};
    uint64_t a_val = 0, b_val = 0;
    for (int i = 0; i < 8; i++) {
        a_val = (a_val << 8) | a[24 + i];
        b_val = (b_val << 8) | b[24 + i];
    }
    uint64_t product = a_val * b_val;
    for (int i = 0; i < 8; i++) {
        result[31 - i] = (product >> (i * 8)) & 0xFF;
    }
    return result;
}

std::array<uint8_t, 32> mermai_vm::load_word_from_memory(uint32_t offset) {
    std::array<uint8_t, 32> word = {0};
    if (offset + 32 > context.memory.size()) {
        throw std::runtime_error("Memory access out of bounds");
    }
    std::copy(
        context.memory.begin() + offset,
        context.memory.begin() + offset + 32,
        word.begin()
    );
    return word;
}

void mermai_vm::store_word_to_memory(uint32_t offset, const std::array<uint8_t, 32>& word) {
    if (offset + 32 > context.memory.size()) {
        throw std::runtime_error("Memory access out of bounds");
    }
    std::copy(word.begin(), word.end(), context.memory.begin() + offset);
}

std::vector<uint8_t> mermai_vm::load_from_storage(const std::string& key) {
    auto it = context.storage.find(key);
    if (it != context.storage.end()) {
        return it->second;
    }
    return {};
}

void mermai_vm::store_to_storage(const std::string& key, const std::vector<uint8_t>& value) {
    context.storage[key] = value;
    context.gas_used += GAS_SSTORE;
}

void mermai_vm::jump(uint32_t target) {
    if (target >= bytecode_size) {
        throw std::runtime_error("Invalid jump target");
    }
    context.program_counter = target;
}

bool mermai_vm::check_gas(uint64_t cost) {
    if (context.gas_used + cost > context.gas_limit) {
        return false;
    }
    return true;
}

// ============================================================================
// CONTRACT COMPILER
// ============================================================================

std::vector<uint8_t> mermai_contract_compiler::compile_from_asm(const std::string& asm_code) {
    std::vector<uint8_t> bytecode;
    std::istringstream input(asm_code);
    std::string token;
    while (input >> token) {
        if (token == "PUSH0") bytecode.push_back(static_cast<uint8_t>(mermai_opcode::PUSH0));
        else if (token == "STOP") bytecode.push_back(static_cast<uint8_t>(mermai_opcode::STOP));
        else if (token == "POP") bytecode.push_back(static_cast<uint8_t>(mermai_opcode::POP));
        else if (token == "ADD") bytecode.push_back(static_cast<uint8_t>(mermai_opcode::ADD));
        else if (token == "SUB") bytecode.push_back(static_cast<uint8_t>(mermai_opcode::SUB));
        else if (token == "MUL") bytecode.push_back(static_cast<uint8_t>(mermai_opcode::MUL));
        else if (token == "DIV") bytecode.push_back(static_cast<uint8_t>(mermai_opcode::DIV));
        else if (token == "MOD") bytecode.push_back(static_cast<uint8_t>(mermai_opcode::MOD));
        else if (token == "SLOAD") bytecode.push_back(static_cast<uint8_t>(mermai_opcode::SLOAD));
        else if (token == "SSTORE") bytecode.push_back(static_cast<uint8_t>(mermai_opcode::SSTORE));
        else if (token.rfind("PUSH1:", 0) == 0) {
            bytecode.push_back(static_cast<uint8_t>(mermai_opcode::PUSH1));
            bytecode.push_back(static_cast<uint8_t>(std::stoul(token.substr(6), nullptr, 0)));
        } else {
            throw std::invalid_argument("Unknown assembly token: " + token);
        }
    }
    return bytecode;
}

bool mermai_contract_compiler::validate_bytecode(const std::vector<uint8_t>& bytecode) {
    return !bytecode.empty();
}

} // namespace mermai::vm
