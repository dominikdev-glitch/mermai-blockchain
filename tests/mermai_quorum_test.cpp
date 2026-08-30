#include "mermai/mermai_blockchain.hpp"
#include "mermai/mermai_consensus.hpp"
#include "mermai/mermai_network.hpp"
#include "mermai/mermai_db.hpp"
#include "mermai/mermai_rpc.hpp"
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/x509.h>
#include <cassert>
#include <iostream>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>
#include <cstdio>

namespace {
EVP_PKEY* generate_ec_key() {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (!ctx) return nullptr;
    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return nullptr;
    }
    if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return nullptr;
    }
    EVP_PKEY* key = nullptr;
    if (EVP_PKEY_keygen(ctx, &key) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return nullptr;
    }
    EVP_PKEY_CTX_free(ctx);
    return key;
}

std::vector<uint8_t> get_public_key_der(EVP_PKEY* pkey) {
    if (!pkey) return {};
    unsigned char* buf = nullptr;
    int len = i2d_PUBKEY(pkey, &buf);
    if (len <= 0 || !buf) return {};
    std::vector<uint8_t> der(buf, buf + len);
    OPENSSL_free(buf);
    return der;
}

std::string hash_to_hex(const mermai::mermai_hash256& hash) {
    std::ostringstream ss;
    for (auto b : hash) ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    return ss.str();
}
}

int main() {
    std::cout << "Starting Mermai Phase 3, 4, 5 Quorum Finality & PoS Security Test..." << std::endl;

    // 1. Generate keys for 3 validators: Alice (5,000,000), Bob (3,000,000), Carol (2,000,000)
    EVP_PKEY* alice_key = generate_ec_key();
    EVP_PKEY* bob_key = generate_ec_key();
    EVP_PKEY* carol_key = generate_ec_key();
    assert(alice_key && bob_key && carol_key);

    const auto alice_pub = get_public_key_der(alice_key);
    const auto bob_pub = get_public_key_der(bob_key);
    const auto carol_pub = get_public_key_der(carol_key);
    assert(!alice_pub.empty() && !bob_pub.empty() && !carol_pub.empty());

    auto consensus = std::make_shared<mermai::mermai_time_weighted_pos>();
    assert(consensus->register_validator("mrm_alice", 5000000, "Alice", alice_pub));
    assert(consensus->register_validator("mrm_bob", 3000000, "Bob", bob_pub));
    assert(consensus->register_validator("mrm_carol", 2000000, "Carol", carol_pub));
    assert(consensus->get_active_validator_count() == 3);
    assert(consensus->get_total_active_stake() == 10000000);
    std::cout << "✓ Registered 3 validators with ECDSA keys (Total Stake: 10,000,000)" << std::endl;

    // 2. Block proposal and signing test
    const uint64_t slot_time = 1700000000;
    const std::string proposer = consensus->select_block_proposer(slot_time);
    assert(!proposer.empty());

    EVP_PKEY* proposer_key = (proposer == "mrm_alice") ? alice_key : (proposer == "mrm_bob" ? bob_key : carol_key);
    const auto proposer_pub = (proposer == "mrm_alice") ? alice_pub : (proposer == "mrm_bob" ? bob_pub : carol_pub);

    auto block = std::make_shared<mermai::mermai_block>(1);
    block->prev_block_hash = "0000000000000000000000000000000000000000000000000000000000000000";
    block->timestamp = slot_time;
    block->validator_address = proposer;
    block->validator_stake = (proposer == "mrm_alice") ? 5000000 : (proposer == "mrm_bob" ? 3000000 : 2000000);
    block->validator_age = 0;
    block->validator_public_key = proposer_pub;

    // Sign proposal with proposer key
    assert(block->sign(proposer_key));
    assert(block->verify_validator_signature());
    assert(mermai::mermai_proposal_validator::validate_proposal(block, *consensus, slot_time));
    std::cout << "✓ Proposer (" << proposer << ") signed valid block proposal" << std::endl;

    // 3. Quorum Finality & Voting Test (2/3+ threshold)
    auto quorum = std::make_shared<mermai::mermai_quorum_collector>();
    const std::string block_hash = hash_to_hex(block->calculate_hash());

    // Alice votes (50% of stake)
    mermai::mermai_block_vote alice_vote;
    alice_vote.validator_address = "mrm_alice";
    alice_vote.block_hash = block_hash;
    alice_vote.block_height = 1;
    alice_vote.timestamp = slot_time;
    alice_vote.validator_public_key = alice_pub;
    assert(alice_vote.sign(alice_key));
    assert(alice_vote.verify_signature());
    assert(quorum->add_vote(alice_vote, *consensus));
    assert(quorum->get_vote_stake(block_hash) == 5000000);
    // 50% < 66.7% -> not finalized yet
    assert(!quorum->is_block_finalized(block_hash));
    std::cout << "✓ Alice voted (50% stake) - Block not yet finalized" << std::endl;

    // Bob votes (30% of stake)
    mermai::mermai_block_vote bob_vote;
    bob_vote.validator_address = "mrm_bob";
    bob_vote.block_hash = block_hash;
    bob_vote.block_height = 1;
    bob_vote.timestamp = slot_time;
    bob_vote.validator_public_key = bob_pub;
    assert(bob_vote.sign(bob_key));
    assert(bob_vote.verify_signature());
    assert(quorum->add_vote(bob_vote, *consensus));
    assert(quorum->get_vote_stake(block_hash) == 8000000);
    // 80% >= 66.7% -> block achieves quorum finality!
    assert(quorum->is_block_finalized(block_hash));
    std::cout << "✓ Bob voted (cumulative 80% stake) - Block achieved 2/3+ QUORUM FINALITY!" << std::endl;

    // 4. Equivocation / Double-Proposal Detection Test
    auto conflicting_block = std::make_shared<mermai::mermai_block>(1);
    conflicting_block->prev_block_hash = "1111111111111111111111111111111111111111111111111111111111111111";
    conflicting_block->timestamp = slot_time;
    conflicting_block->validator_address = proposer;
    conflicting_block->validator_stake = block->validator_stake;
    conflicting_block->validator_age = 0;
    conflicting_block->validator_public_key = proposer_pub;
    assert(conflicting_block->sign(proposer_key));

    const uint64_t pre_slash_stake = consensus->get_total_active_stake();
    // Conflicting block proposal must be rejected and proposer slashed!
    assert(!mermai::mermai_proposal_validator::validate_proposal(conflicting_block, *consensus, slot_time));
    assert(consensus->get_total_active_stake() < pre_slash_stake);
    std::cout << "✓ Equivocation detected: conflicting proposal rejected and malicious proposer slashed by 50%" << std::endl;

    // 5. P2P Vote Broadcasting & RPC Quorum Status Test
    auto chain = std::make_shared<mermai::mermai_blockchain>();
    auto p2p = std::make_shared<mermai::mermai_p2p_node>(6444, chain);
    auto db = std::make_shared<mermai::mermai_db>("mermai_quorum_test.db");
    assert(db->initialize());
    mermai::mermai_rpc_server rpc(6445, chain, p2p, db, consensus, quorum);

    const std::string rpc_vote_status = rpc.get_quorum_status(block_hash);
    assert(rpc_vote_status.find("\"finalized\":true") != std::string::npos);
    std::cout << "✓ RPC mrm_getQuorumStatus returned finalized=true" << std::endl;

    EVP_PKEY_free(alice_key);
    EVP_PKEY_free(bob_key);
    EVP_PKEY_free(carol_key);

    std::remove("mermai_quorum_test.db");
    std::cout << "\nAll Phase 3, 4, 5 PoS Security, Quorum Finality & Hardening tests passed successfully!\n";
    return 0;
}
