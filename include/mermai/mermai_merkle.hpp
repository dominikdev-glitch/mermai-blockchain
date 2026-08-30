#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace mermai {

struct mermai_merkle_proof_step {
    std::string hash;
    bool is_right = false;
};

struct mermai_merkle_proof {
    std::string leaf_hash;
    std::vector<mermai_merkle_proof_step> steps;
};

class mermai_merkle_tree {
private:
    std::vector<std::vector<std::string>> levels;

public:
    mermai_merkle_tree() = default;
    explicit mermai_merkle_tree(const std::vector<std::string>& leaf_hashes);

    void build(const std::vector<std::string>& leaf_hashes);
    std::string get_root() const;
    size_t get_leaf_count() const;
    std::string get_leaf(size_t index) const;

    mermai_merkle_proof generate_proof(size_t leaf_index) const;
    static bool verify_proof(const std::string& expected_root, const std::string& leaf_hash, const mermai_merkle_proof& proof);

    static std::string hash_pair(const std::string& left_hex, const std::string& right_hex);
    static std::string hash_leaf(const std::string& data);
};

} // namespace mermai
