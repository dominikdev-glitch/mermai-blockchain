/*
 * MERMAI Blockchain Core Data Structures
 * 
 * Creator: dominikdev-glitch
 * High-performance, memory-efficient blockchain data models
 */

#include "../include/mermai/mermai_blockchain.hpp"
#include "../include/mermai/mermai_consensus.hpp"
#include "../include/mermai/mermai_db.hpp"
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <openssl/err.h>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <limits>
#include <cstring>
#include <stdexcept>
#include <algorithm>

namespace mermai {

namespace {
constexpr size_t MAX_SERIALIZED_STRING = 64 * 1024;
constexpr size_t MAX_SERIALIZED_BYTES = 16 * 1024;
constexpr size_t MAX_TRANSACTION_INPUTS = 1000;
constexpr size_t MAX_TRANSACTION_OUTPUTS = 1000;
constexpr size_t MAX_BLOCK_TRANSACTIONS = 10000;

template <typename T>
void write_value(std::string& output, T value) {
    for (size_t index = 0; index < sizeof(T); ++index) {
        const size_t shift = (sizeof(T) - 1 - index) * 8;
        output.push_back(static_cast<char>((value >> shift) & 0xFF));
    }
}

template <typename T>
T read_value(const std::string& input, size_t& offset) {
    if (offset + sizeof(T) > input.size()) throw std::runtime_error("Unexpected end of payload");
    T value = 0;
    for (size_t index = 0; index < sizeof(T); ++index) {
        value = static_cast<T>((value << 8) | static_cast<uint8_t>(input[offset++]));
    }
    return value;
}

void write_bytes(std::string& output, const std::vector<uint8_t>& bytes) {
    write_value<uint32_t>(output, static_cast<uint32_t>(bytes.size()));
    output.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::vector<uint8_t> read_bytes(const std::string& input, size_t& offset) {
    const uint32_t size = read_value<uint32_t>(input, offset);
    if (size > MAX_SERIALIZED_BYTES || offset + size > input.size()) throw std::runtime_error("Invalid byte payload size");
    const auto* start = reinterpret_cast<const uint8_t*>(input.data() + offset);
    offset += size;
    return {start, start + size};
}

void write_string(std::string& output, const std::string& text) {
    write_value<uint32_t>(output, static_cast<uint32_t>(text.size()));
    output.append(text);
}

std::string read_string(const std::string& input, size_t& offset) {
    const uint32_t size = read_value<uint32_t>(input, offset);
    if (size > MAX_SERIALIZED_STRING || offset + size > input.size()) throw std::runtime_error("Invalid string payload size");
    std::string text = input.substr(offset, size);
    offset += size;
    return text;
}

std::string unsigned_transaction_payload(const mermai_transaction& tx) {
    mermai_transaction unsigned_tx = tx;
    for (auto& in : unsigned_tx.inputs) in.signature.clear();
    return unsigned_tx.serialize();
}

std::string hash_to_hex(const mermai_hash256& hash) {
    std::ostringstream ss;
    for (const auto byte : hash) ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    return ss.str();
}
}

// ============================================================================
// TRANSACTION IMPLEMENTATION
// ============================================================================

std::string mermai_transaction::serialize() const {
    std::string output;
    write_string(output, id);
    write_value<uint64_t>(output, fee);
    write_value<uint64_t>(output, timestamp);
    write_value<uint32_t>(output, static_cast<uint32_t>(inputs.size()));
    for (const auto& input : inputs) {
        write_string(output, input.prev_tx_hash);
        write_value<uint32_t>(output, input.prev_output_idx);
        write_bytes(output, input.signature);
        write_bytes(output, input.public_key);
    }
    write_value<uint32_t>(output, static_cast<uint32_t>(outputs.size()));
    for (const auto& output_item : outputs) {
        write_string(output, output_item.address);
        write_value<uint64_t>(output, output_item.amount);
    }
    return output;
}

mermai_transaction mermai_transaction::deserialize(const std::string& data) {
    size_t offset = 0;
    mermai_transaction tx;
    tx.id = read_string(data, offset);
    tx.fee = read_value<uint64_t>(data, offset);
    tx.timestamp = read_value<uint64_t>(data, offset);
    const uint32_t input_count = read_value<uint32_t>(data, offset);
    if (input_count > MAX_TRANSACTION_INPUTS) throw std::runtime_error("Too many transaction inputs");
    for (uint32_t index = 0; index < input_count; ++index) {
        mermai_tx_input input;
        input.prev_tx_hash = read_string(data, offset);
        input.prev_output_idx = read_value<uint32_t>(data, offset);
        input.signature = read_bytes(data, offset);
        input.public_key = read_bytes(data, offset);
        tx.inputs.push_back(std::move(input));
    }
    const uint32_t output_count = read_value<uint32_t>(data, offset);
    if (output_count > MAX_TRANSACTION_OUTPUTS) throw std::runtime_error("Too many transaction outputs");
    for (uint32_t index = 0; index < output_count; ++index) {
        mermai_tx_output output_item;
        output_item.address = read_string(data, offset);
        output_item.amount = read_value<uint64_t>(data, offset);
        tx.outputs.push_back(std::move(output_item));
    }
    if (offset != data.size()) throw std::runtime_error("Trailing transaction data");
    return tx;
}

std::string mermai_transaction::get_hash() const {
    std::string serialized = serialize();
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, serialized.c_str(), serialized.length());
    SHA256_Final(hash, &sha256);
    
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

bool mermai_transaction::verify() const {
    if (id.empty() || inputs.empty() || outputs.empty()) return false;
    if (inputs.size() > MAX_TRANSACTION_INPUTS || outputs.size() > MAX_TRANSACTION_OUTPUTS) return false;
    if (timestamp > static_cast<uint64_t>(std::time(nullptr)) + 120) return false;
    std::set<std::pair<std::string, uint32_t>> spent_outputs;
    for (const auto& input : inputs) {
        if (input.prev_tx_hash.empty() || !spent_outputs.emplace(input.prev_tx_hash, input.prev_output_idx).second) return false;
    }
    uint64_t total_outputs = 0;
    for (const auto& output : outputs) {
        if (output.address.empty() || output.amount == 0 || output.amount > UINT64_MAX - total_outputs) return false;
        total_outputs += output.amount;
    }
    const std::string payload = unsigned_transaction_payload(*this);
    for (const auto& input : inputs) {
        if (input.signature.empty() || input.public_key.empty()) return false;
        const unsigned char* public_key_data = input.public_key.data();
        EVP_PKEY* public_key = d2i_PUBKEY(nullptr, &public_key_data, static_cast<long>(input.public_key.size()));
        if (!public_key) return false;
        EVP_MD_CTX* verify_context = EVP_MD_CTX_new();
        const bool initialized = verify_context &&
            EVP_DigestVerifyInit(verify_context, nullptr, EVP_sha256(), nullptr, public_key) == 1 &&
            EVP_DigestVerifyUpdate(verify_context, payload.data(), payload.size()) == 1;
        const bool valid = initialized && EVP_DigestVerifyFinal(
            verify_context,
            input.signature.data(),
            input.signature.size()
        ) == 1;
        EVP_MD_CTX_free(verify_context);
        EVP_PKEY_free(public_key);
        if (!valid) return false;
    }
    return true;
}

bool mermai_transaction::sign(size_t input_idx, EVP_PKEY* private_key) {
    if (!private_key || input_idx >= inputs.size()) return false;
    if (inputs[input_idx].public_key.empty()) {
        unsigned char* pub_buf = nullptr;
        int pub_len = i2d_PUBKEY(private_key, &pub_buf);
        if (pub_len > 0 && pub_buf) {
            inputs[input_idx].public_key.assign(pub_buf, pub_buf + pub_len);
            OPENSSL_free(pub_buf);
        }
    }
    inputs[input_idx].signature.clear();
    const std::string payload = unsigned_transaction_payload(*this);
    EVP_MD_CTX* sign_context = EVP_MD_CTX_new();
    if (!sign_context) return false;
    size_t sig_len = 0;
    bool success = EVP_DigestSignInit(sign_context, nullptr, EVP_sha256(), nullptr, private_key) == 1 &&
                   EVP_DigestSignUpdate(sign_context, payload.data(), payload.size()) == 1 &&
                   EVP_DigestSignFinal(sign_context, nullptr, &sig_len) == 1;
    if (success && sig_len > 0) {
        inputs[input_idx].signature.resize(sig_len);
        success = EVP_DigestSignFinal(sign_context, inputs[input_idx].signature.data(), &sig_len) == 1;
        if (success) inputs[input_idx].signature.resize(sig_len);
    }
    EVP_MD_CTX_free(sign_context);
    return success;
}

// ============================================================================
// BLOCK VOTE IMPLEMENTATION
// ============================================================================

std::string mermai_block_vote::signing_payload() const {
    return validator_address + ":" + block_hash + ":" + std::to_string(block_height) + ":" + std::to_string(timestamp);
}

bool mermai_block_vote::sign(EVP_PKEY* private_key) {
    if (!private_key) return false;
    if (validator_public_key.empty()) {
        unsigned char* pub_buf = nullptr;
        int pub_len = i2d_PUBKEY(private_key, &pub_buf);
        if (pub_len > 0 && pub_buf) {
            validator_public_key.assign(pub_buf, pub_buf + pub_len);
            OPENSSL_free(pub_buf);
        }
    }
    signature.clear();
    const std::string payload = signing_payload();
    EVP_MD_CTX* sign_context = EVP_MD_CTX_new();
    if (!sign_context) return false;
    size_t sig_len = 0;
    bool success = EVP_DigestSignInit(sign_context, nullptr, EVP_sha256(), nullptr, private_key) == 1 &&
                   EVP_DigestSignUpdate(sign_context, payload.data(), payload.size()) == 1 &&
                   EVP_DigestSignFinal(sign_context, nullptr, &sig_len) == 1;
    if (success && sig_len > 0) {
        signature.resize(sig_len);
        success = EVP_DigestSignFinal(sign_context, signature.data(), &sig_len) == 1;
        if (success) signature.resize(sig_len);
    }
    EVP_MD_CTX_free(sign_context);
    return success;
}

bool mermai_block_vote::verify_signature() const {
    if (validator_public_key.empty() || signature.empty()) return false;
    const unsigned char* key_ptr = validator_public_key.data();
    EVP_PKEY* public_key = d2i_PUBKEY(nullptr, &key_ptr, static_cast<long>(validator_public_key.size()));
    if (!public_key) return false;
    EVP_MD_CTX* verify_context = EVP_MD_CTX_new();
    const std::string payload = signing_payload();
    const bool valid = verify_context &&
        EVP_DigestVerifyInit(verify_context, nullptr, EVP_sha256(), nullptr, public_key) == 1 &&
        EVP_DigestVerifyUpdate(verify_context, payload.data(), payload.size()) == 1 &&
        EVP_DigestVerifyFinal(verify_context, signature.data(), signature.size()) == 1;
    EVP_MD_CTX_free(verify_context);
    EVP_PKEY_free(public_key);
    return valid;
}

std::string mermai_block_vote::serialize() const {
    std::string output;
    write_string(output, validator_address);
    write_string(output, block_hash);
    write_value<uint32_t>(output, block_height);
    write_value<uint64_t>(output, timestamp);
    write_bytes(output, validator_public_key);
    write_bytes(output, signature);
    return output;
}

mermai_block_vote mermai_block_vote::deserialize(const std::string& data) {
    size_t offset = 0;
    mermai_block_vote vote;
    vote.validator_address = read_string(data, offset);
    vote.block_hash = read_string(data, offset);
    vote.block_height = read_value<uint32_t>(data, offset);
    vote.timestamp = read_value<uint64_t>(data, offset);
    vote.validator_public_key = read_bytes(data, offset);
    vote.signature = read_bytes(data, offset);
    return vote;
}

// ============================================================================
// BLOCK IMPLEMENTATION
// ============================================================================

mermai_block::mermai_block() : timestamp(std::time(nullptr)) {}

mermai_block::mermai_block(uint32_t block_height) 
    : height(block_height), timestamp(std::time(nullptr)) {}

std::string mermai_block::serialize() const {
    std::string output;
    write_value<uint32_t>(output, version);
    write_value<uint32_t>(output, height);
    write_string(output, prev_block_hash);
    write_string(output, merkle_root);
    write_value<uint64_t>(output, timestamp);
    write_value<uint32_t>(output, nonce);
    write_string(output, validator_address);
    write_value<uint64_t>(output, validator_stake);
    write_value<uint64_t>(output, validator_age);
    write_bytes(output, validator_public_key);
    write_bytes(output, validator_signature);
    write_value<uint32_t>(output, static_cast<uint32_t>(transactions.size()));
    for (const auto& tx : transactions) {
        write_string(output, tx.serialize());
    }
    return output;
}

mermai_block mermai_block::deserialize(const std::string& data) {
    size_t offset = 0;
    mermai_block block;
    block.version = read_value<uint32_t>(data, offset);
    block.height = read_value<uint32_t>(data, offset);
    block.prev_block_hash = read_string(data, offset);
    block.merkle_root = read_string(data, offset);
    block.timestamp = read_value<uint64_t>(data, offset);
    block.nonce = read_value<uint32_t>(data, offset);
    block.validator_address = read_string(data, offset);
    block.validator_stake = read_value<uint64_t>(data, offset);
    block.validator_age = read_value<uint64_t>(data, offset);
    block.validator_public_key = read_bytes(data, offset);
    block.validator_signature = read_bytes(data, offset);
    const uint32_t transaction_count = read_value<uint32_t>(data, offset);
    if (transaction_count > MAX_BLOCK_TRANSACTIONS) throw std::runtime_error("Too many block transactions");
    for (uint32_t index = 0; index < transaction_count; ++index) {
        block.transactions.push_back(mermai_transaction::deserialize(read_string(data, offset)));
    }
    if (offset != data.size()) throw std::runtime_error("Trailing block data");
    return block;
}

std::string mermai_block::signing_payload() const {
    mermai_block unsigned_block = *this;
    unsigned_block.validator_signature.clear();
    return unsigned_block.serialize();
}

bool mermai_block::sign(EVP_PKEY* private_key) {
    if (!private_key) return false;
    if (validator_public_key.empty()) {
        unsigned char* pub_buf = nullptr;
        int pub_len = i2d_PUBKEY(private_key, &pub_buf);
        if (pub_len > 0 && pub_buf) {
            validator_public_key.assign(pub_buf, pub_buf + pub_len);
            OPENSSL_free(pub_buf);
        }
    }
    validator_signature.clear();
    const std::string payload = signing_payload();
    EVP_MD_CTX* sign_context = EVP_MD_CTX_new();
    if (!sign_context) return false;
    size_t sig_len = 0;
    bool success = EVP_DigestSignInit(sign_context, nullptr, EVP_sha256(), nullptr, private_key) == 1 &&
                   EVP_DigestSignUpdate(sign_context, payload.data(), payload.size()) == 1 &&
                   EVP_DigestSignFinal(sign_context, nullptr, &sig_len) == 1;
    if (success && sig_len > 0) {
        validator_signature.resize(sig_len);
        success = EVP_DigestSignFinal(sign_context, validator_signature.data(), &sig_len) == 1;
        if (success) validator_signature.resize(sig_len);
    }
    EVP_MD_CTX_free(sign_context);
    return success;
}

bool mermai_block::verify_validator_signature() const {
    if (validator_public_key.empty() || validator_signature.empty()) return false;
    const unsigned char* public_key_data = validator_public_key.data();
    EVP_PKEY* public_key = d2i_PUBKEY(nullptr, &public_key_data, static_cast<long>(validator_public_key.size()));
    if (!public_key || public_key_data != validator_public_key.data() + validator_public_key.size()) {
        EVP_PKEY_free(public_key);
        return false;
    }
    EVP_MD_CTX* verify_context = EVP_MD_CTX_new();
    const std::string payload = signing_payload();
    const bool valid = verify_context &&
        EVP_DigestVerifyInit(verify_context, nullptr, EVP_sha256(), nullptr, public_key) == 1 &&
        EVP_DigestVerifyUpdate(verify_context, payload.data(), payload.size()) == 1 &&
        EVP_DigestVerifyFinal(verify_context, validator_signature.data(), validator_signature.size()) == 1;
    EVP_MD_CTX_free(verify_context);
    EVP_PKEY_free(public_key);
    return valid;
}

mermai_hash256 mermai_block::calculate_hash() const {
    std::string serialized = serialize();
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, serialized.c_str(), serialized.length());
    SHA256_Final(hash, &sha256);
    
    mermai_hash256 result;
    result.assign(hash, hash + SHA256_DIGEST_LENGTH);
    return result;
}

bool mermai_block::verify_time_weight_pos() const {
    return !validator_address.empty() && validator_stake > 0;
}

bool mermai_block::is_valid_proposer(
    const std::string& address,
    uint64_t stake_amount,
    uint64_t stake_age_seconds
) const {
    return validator_address == address &&
           validator_stake >= stake_amount &&
           validator_age >= stake_age_seconds;
}

// ============================================================================
// BLOCKCHAIN IMPLEMENTATION
// ============================================================================

mermai_blockchain::mermai_blockchain() {
    auto genesis = std::make_shared<mermai_block>(0);
    genesis->prev_block_hash = "0000000000000000000000000000000000000000000000000000000000000000";
    genesis->timestamp = 1640000000;
    chain.push_back(genesis);
}

bool mermai_blockchain::can_add_block(const std::shared_ptr<mermai_block>& block) const {
    if (!block) return false;
    
    if (block->height != chain.size()) return false;
    if (block->prev_block_hash.empty() || block->prev_block_hash != hash_to_hex(chain.back()->calculate_hash())) return false;
    if (block->timestamp < chain.back()->timestamp || block->timestamp > std::time(nullptr) + 120) return false;
    if (!block->verify_time_weight_pos()) return false;
    if (consensus_engine && !mermai_proposal_validator::validate_proposal(block, const_cast<mermai_time_weighted_pos&>(*consensus_engine), block->timestamp)) return false;
    if (block->transactions.size() > MAX_BLOCK_TRANSACTIONS) return false;
    if ((!block->transactions.empty() && block->merkle_root != mermai_merkle_tree::calculate_tree_hash(block->transactions)) ||
        (block->transactions.empty() && !block->merkle_root.empty())) return false;
    
    std::set<std::string> transaction_ids;
    for (const auto& tx : block->transactions) {
        if (!tx.verify()) return false;
        if (!transaction_ids.emplace(tx.id).second) return false;
    }
    
    return true;
}

bool mermai_blockchain::add_block(std::shared_ptr<mermai_block> block) {
    if (!can_add_block(block)) return false;
    chain.push_back(std::move(block));
    return true;
}

bool mermai_blockchain::replace_chain_if_longer(const std::vector<std::shared_ptr<mermai_block>>& candidate_chain) {
    if (candidate_chain.empty()) return false;
    if (candidate_chain.size() <= chain.size()) return false;

    std::vector<std::shared_ptr<mermai_block>> validated_chain;
    validated_chain.reserve(candidate_chain.size());

    for (size_t index = 0; index < candidate_chain.size(); ++index) {
        const auto& block = candidate_chain[index];
        if (!block) return false;
        if (block->height != static_cast<uint32_t>(index)) return false;

        if (index == 0) {
            if (block->height != 0) return false;
            if (block->prev_block_hash != "0000000000000000000000000000000000000000000000000000000000000000") return false;
        } else {
            const auto& previous = validated_chain.back();
            if (block->prev_block_hash != hash_to_hex(previous->calculate_hash())) return false;
            if (block->timestamp < previous->timestamp) return false;
        }

        if ((index != 0 && !block->verify_time_weight_pos()) || block->transactions.size() > MAX_BLOCK_TRANSACTIONS) return false;
        if (index != 0 && consensus_engine && !mermai_proposal_validator::validate_proposal(block, const_cast<mermai_time_weighted_pos&>(*consensus_engine), block->timestamp)) return false;
        if ((!block->transactions.empty() && block->merkle_root != mermai_merkle_tree::calculate_tree_hash(block->transactions)) ||
            (block->transactions.empty() && !block->merkle_root.empty())) return false;
        std::set<std::string> transaction_ids;
        for (const auto& tx : block->transactions) {
            if (!tx.verify()) return false;
            if (!transaction_ids.emplace(tx.id).second) return false;
        }

        validated_chain.push_back(block);
    }

    chain = std::move(validated_chain);
    return true;
}

std::shared_ptr<mermai_block> mermai_blockchain::get_block(uint32_t height) const {
    if (height >= chain.size()) return nullptr;
    return chain[height];
}

std::shared_ptr<mermai_block> mermai_blockchain::get_latest_block() const {
    if (chain.empty()) return nullptr;
    return chain.back();
}

uint32_t mermai_blockchain::get_height() const {
    return chain.empty() ? 0 : static_cast<uint32_t>(chain.size() - 1);
}

bool mermai_blockchain::is_valid() const {
    return is_chain_valid();
}

bool mermai_blockchain::is_chain_valid() const {
    for (size_t i = 1; i < chain.size(); i++) {
        const auto& current = chain[i];
        const auto& previous = chain[i - 1];
        
        if (current->prev_block_hash != hash_to_hex(previous->calculate_hash())) {
            return false;
        }
        
        if (!current->verify_time_weight_pos()) {
            return false;
        }
    }
    return true;
}

uint32_t mermai_blockchain::calculate_difficulty() {
    return difficulty;
}

// ============================================================================
// CONSENSUS STATE IMPLEMENTATION
// ============================================================================

float mermai_consensus_state::calculate_validator_weight(
    const mermai_validator_stake& stake
) const {
    return stake.weight;
}

std::string mermai_consensus_state::select_proposer() const {
    if (active_validators.empty()) return "";
    return active_validators[0].address;
}

void mermai_consensus_state::slash_validator(
    const std::string& address,
    float percentage
) {
    for (auto& v : active_validators) {
        if (v.address == address) {
            v.amount = static_cast<uint64_t>(v.amount * (1.0f - percentage));
            break;
        }
    }
}

void mermai_consensus_state::add_validator(
    const std::string& address,
    uint64_t amount
) {
    mermai_validator_stake stake;
    stake.address = address;
    stake.amount = amount;
    stake.lock_time = std::time(nullptr);
    stake.last_proposal = 0;
    stake.weight = static_cast<float>(amount);
    active_validators.push_back(stake);
}

void mermai_consensus_state::remove_validator(const std::string& address) {
    active_validators.erase(
        std::remove_if(active_validators.begin(), active_validators.end(),
            [&address](const mermai_validator_stake& v) { return v.address == address; }),
        active_validators.end()
    );
}

} // namespace mermai
