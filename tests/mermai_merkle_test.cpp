#include "mermai/mermai_merkle.hpp"
#include <cassert>
#include <iostream>
#include <vector>
#include <string>

int main() {
    std::cout << "Starting Mermai Phase 6 - Cryptographic Merkle Tree tests..." << std::endl;

    mermai::mermai_merkle_tree empty_tree(std::vector<std::string>{});
    assert(!empty_tree.get_root().empty());
    std::cout << "  [OK] Empty tree root: " << empty_tree.get_root() << std::endl;

    std::vector<std::string> leaves1 = {"leaf1_hash_abc123"};
    mermai::mermai_merkle_tree tree1(leaves1);
    assert(tree1.get_root() == leaves1[0]);
    std::cout << "  [OK] Single leaf root matches leaf hash" << std::endl;

    std::vector<std::string> leaves4 = {
        "1111111111111111111111111111111111111111111111111111111111111111",
        "2222222222222222222222222222222222222222222222222222222222222222",
        "3333333333333333333333333333333333333333333333333333333333333333",
        "4444444444444444444444444444444444444444444444444444444444444444"
    };
    mermai::mermai_merkle_tree tree4(leaves4);
    std::string root4 = tree4.get_root();
    assert(!root4.empty());
    std::cout << "  [OK] 4-leaf tree root: " << root4 << std::endl;

    for (size_t i = 0; i < leaves4.size(); ++i) {
        auto proof = tree4.generate_proof(i);
        assert(proof.leaf_hash == leaves4[i]);
        assert(mermai::mermai_merkle_tree::verify_proof(root4, leaves4[i], proof));
    }
    std::cout << "  [OK] Merkle inclusion proofs verified for all 4 leaves" << std::endl;

    auto forged_proof = tree4.generate_proof(0);
    forged_proof.steps[0].hash = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    assert(!mermai::mermai_merkle_tree::verify_proof(root4, leaves4[0], forged_proof));
    std::cout << "  [OK] Tampered proof rejected" << std::endl;

    std::vector<std::string> leaves3 = {leaves4[0], leaves4[1], leaves4[2]};
    mermai::mermai_merkle_tree tree3(leaves3);
    std::string root3 = tree3.get_root();
    for (size_t i = 0; i < leaves3.size(); ++i) {
        auto proof = tree3.generate_proof(i);
        assert(mermai::mermai_merkle_tree::verify_proof(root3, leaves3[i], proof));
    }
    std::cout << "  [OK] Odd-numbered leaf tree (3 leaves) proof verified" << std::endl;

    std::cout << "All Merkle Tree tests passed successfully!" << std::endl;
    return 0;
}
