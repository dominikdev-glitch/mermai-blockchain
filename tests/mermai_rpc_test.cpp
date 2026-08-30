#include "mermai/mermai_rpc.hpp"
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <cassert>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

namespace {

std::string hex_encode(const unsigned char* data, size_t size) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (size_t index = 0; index < size; ++index) output << std::setw(2) << static_cast<unsigned int>(data[index]);
    return output.str();
}

std::string hex_encode(const std::string& value) {
    return hex_encode(reinterpret_cast<const unsigned char*>(value.data()), value.size());
}

std::string address_from_public_key(const std::vector<uint8_t>& public_key) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(public_key.data(), public_key.size(), digest);
    return "mrm1" + hex_encode(digest, 20);
}

mermai::mermai_transaction make_signed_transaction() {
    EVP_PKEY_CTX* key_context = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    assert(key_context);
    assert(EVP_PKEY_keygen_init(key_context) == 1);
    assert(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(key_context, NID_X9_62_prime256v1) == 1);
    EVP_PKEY* key = nullptr;
    assert(EVP_PKEY_keygen(key_context, &key) == 1);
    EVP_PKEY_CTX_free(key_context);

    mermai::mermai_transaction transaction;
    transaction.id = "rpc-send-tx";
    transaction.timestamp = 42;
    transaction.inputs.push_back({"previous", 0, {}, {}});
    transaction.outputs.push_back({"alice", 100});

    unsigned char* public_key_data = nullptr;
    const int public_key_size = i2d_PUBKEY(key, &public_key_data);
    assert(public_key_size > 0);
    transaction.inputs[0].public_key.assign(public_key_data, public_key_data + public_key_size);
    OPENSSL_free(public_key_data);

    EVP_MD_CTX* sign_context = EVP_MD_CTX_new();
    assert(sign_context);
    assert(EVP_DigestSignInit(sign_context, nullptr, EVP_sha256(), nullptr, key) == 1);
    const auto unsigned_payload = transaction.serialize();
    assert(EVP_DigestSignUpdate(sign_context, unsigned_payload.data(), unsigned_payload.size()) == 1);
    size_t signature_size = 0;
    assert(EVP_DigestSignFinal(sign_context, nullptr, &signature_size) == 1);
    transaction.inputs[0].signature.resize(signature_size);
    assert(EVP_DigestSignFinal(sign_context, transaction.inputs[0].signature.data(), &signature_size) == 1);
    transaction.inputs[0].signature.resize(signature_size);
    EVP_MD_CTX_free(sign_context);
    EVP_PKEY_free(key);

    assert(transaction.verify());
    return transaction;
}

} // namespace

int main() {
    const std::string path = "mermai_rpc_test.db";
    std::remove(path.c_str());
    auto blockchain = std::make_shared<mermai::mermai_blockchain>();
    auto network = std::make_shared<mermai::mermai_p2p_node>(6333, blockchain);
    auto database = std::make_shared<mermai::mermai_db>(path);
    auto consensus = std::make_shared<mermai::mermai_time_weighted_pos>();
    assert(database->initialize());
    assert(database->save_account({"alice", 250, 2, 10}));

    mermai::mermai_rpc_server rpc(6334, blockchain, network, database, consensus);
    const auto balance = rpc.handle_request("{\"jsonrpc\":\"2.0\",\"method\":\"mrm_getBalance\",\"address\":\"alice\",\"id\":1}");
    assert(balance == "{\"jsonrpc\":\"2.0\",\"result\":250,\"id\":1}");

    const auto block_count = rpc.handle_request("{\"jsonrpc\":\"2.0\",\"method\":\"eth_blockNumber\",\"id\":\"x\"}");
    assert(block_count == "{\"jsonrpc\":\"2.0\",\"result\":0,\"id\":\"x\"}");

    const auto unknown = rpc.handle_request("{\"jsonrpc\":\"2.0\",\"method\":\"missing\",\"id\":3}");
    assert(unknown.find("Method not found") != std::string::npos);

    const auto registration = rpc.handle_request("{\"jsonrpc\":\"2.0\",\"method\":\"mrm_registerValidator\",\"address\":\"validator\",\"stake\":1000000,\"id\":4}");
    assert(registration.find("\"result\":true") != std::string::npos);
    const auto validators = rpc.handle_request("{\"jsonrpc\":\"2.0\",\"method\":\"mrm_getAllValidators\",\"id\":5}");
    assert(validators.find("validator") != std::string::npos);

    const auto deployment = rpc.handle_request("{\"jsonrpc\":\"2.0\",\"method\":\"mrm_deployContract\",\"from\":\"alice\",\"bytecode\":\"0x01020103a0ff\",\"id\":6}");
    const size_t address_start = deployment.find("0x");
    assert(address_start != std::string::npos);
    const size_t address_end = deployment.find('"', address_start);
    const auto contract_address = deployment.substr(address_start, address_end - address_start);
    const auto code = rpc.handle_request("{\"jsonrpc\":\"2.0\",\"method\":\"mrm_getContractCode\",\"address\":\"" + contract_address + "\",\"id\":7}");
    assert(code.find("01020103a0ff") != std::string::npos);
    const auto call = rpc.handle_request("{\"jsonrpc\":\"2.0\",\"method\":\"mrm_callContract\",\"address\":\"" + contract_address + "\",\"id\":8}");
    assert(call.find("0000000000000000000000000000000000000000000000000000000000000005") != std::string::npos);
    const auto gas = rpc.handle_request("{\"jsonrpc\":\"2.0\",\"method\":\"mrm_estimateGas\",\"bytecode\":\"0x01020103a0ff\",\"id\":9}");
    assert(gas.find("result") != std::string::npos);

    const auto tx = make_signed_transaction();
    assert(database->save_unspent_output({"previous", 0, address_from_public_key(tx.inputs[0].public_key), 100}));
    const auto tx_payload = "0x" + hex_encode(tx.serialize());
    const auto send_tx = rpc.handle_request("{\"jsonrpc\":\"2.0\",\"method\":\"mrm_sendTransaction\",\"serialized\":\"" + tx_payload + "\",\"id\":10}");
    assert(send_tx.find("\"result\"") != std::string::npos);

    const auto get_tx = rpc.handle_request("{\"jsonrpc\":\"2.0\",\"method\":\"mrm_getTransaction\",\"txHash\":\"" + tx.id + "\",\"id\":11}");
    assert(get_tx.find(tx.id) != std::string::npos);

    std::remove(path.c_str());
    std::cout << "RPC dispatcher test passed.\n";
}
