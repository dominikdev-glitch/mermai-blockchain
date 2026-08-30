#include "mermai/mermai_merkle.hpp"
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace mermai {

namespace {

std::string sha256_hex(const unsigned char* data, size_t len) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(data, len, hash);
    std::ostringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = (uint8_t)strtol(byteString.c_str(), nullptr, 16);
        bytes.push_back(byte);
    }
    return bytes;
}

} // anonymous namespace

std::string mermai_merkle_tree::hash_leaf(const std::string& data) {
    return sha256_hex(reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

std::string mermai_merkle_tree::hash_pair(const std::string& left_hex, const std::string& right_hex) {
    auto left_bytes = hex_to_bytes(left_hex);
    auto right_bytes = hex_to_bytes(right_hex);
    std::vector<uint8_t> combined;
    combined.reserve(left_bytes.size() + right_bytes.size());
    combined.insert(combined.end(), left_bytes.begin(), left_bytes.end());
    combined.insert(combined.end(), right_bytes.begin(), right_bytes.end());
    return sha256_hex(combined.data(), combined.size());
}

mermai_merkle_tree::mermai_merkle_tree(const std::vector<std::string>& leaf_hashes) {
    build(leaf_hashes);
}

void mermai_merkle_tree::build(const std::vector<std::string>& leaf_hashes) {
    levels.clear();
    if (leaf_hashes.empty()) {
        levels.push_back({sha256_hex(reinterpret_cast<const unsigned char*>(""), 0)});
        return;
    }

    levels.push_back(leaf_hashes);

    while (levels.back().size() > 1) {
        const auto& current_level = levels.back();
        std::vector<std::string> next_level;

        for (size_t i = 0; i < current_level.size(); i += 2) {
            const std::string& left = current_level[i];
            const std::string& right = (i + 1 < current_level.size()) ? current_level[i + 1] : left;
            next_level.push_back(hash_pair(left, right));
        }

        levels.push_back(next_level);
    }
}

std::string mermai_merkle_tree::get_root() const {
    if (levels.empty() || levels.back().empty()) return "";
    return levels.back()[0];
}

size_t mermai_merkle_tree::get_leaf_count() const {
    if (levels.empty()) return 0;
    return levels[0].size();
}

std::string mermai_merkle_tree::get_leaf(size_t index) const {
    if (levels.empty() || index >= levels[0].size()) return "";
    return levels[0][index];
}

mermai_merkle_proof mermai_merkle_tree::generate_proof(size_t leaf_index) const {
    mermai_merkle_proof proof;
    if (levels.empty() || leaf_index >= levels[0].size()) return proof;

    proof.leaf_hash = levels[0][leaf_index];
    size_t index = leaf_index;

    for (size_t lvl = 0; lvl + 1 < levels.size(); ++lvl) {
        const auto& current_level = levels[lvl];
        bool is_right_child = (index % 2 != 0);
        size_t sibling_index = is_right_child ? index - 1 : index + 1;

        std::string sibling_hash;
        if (sibling_index < current_level.size()) {
            sibling_hash = current_level[sibling_index];
        } else {
            sibling_hash = current_level[index];
        }

        mermai_merkle_proof_step step;
        step.hash = sibling_hash;
        step.is_right = !is_right_child;
        proof.steps.push_back(step);

        index /= 2;
    }

    return proof;
}

bool mermai_merkle_tree::verify_proof(
    const std::string& expected_root,
    const std::string& leaf_hash,
    const mermai_merkle_proof& proof
) {
    std::string current_hash = leaf_hash;
    for (const auto& step : proof.steps) {
        if (step.is_right) {
            current_hash = hash_pair(current_hash, step.hash);
        } else {
            current_hash = hash_pair(step.hash, current_hash);
        }
    }
    return current_hash == expected_root;
}

} // namespace mermai
