/*
 * MERMAI JSON-RPC 2.0 API Server
 * 
 * Creator: dominikdev-glitch
 * Robust RPC server with Ethereum-compatible and Mermai-specific methods
 */

#include "../include/mermai/mermai_rpc.hpp"
#include "../include/mermai/mermai_vm.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <openssl/sha.h>
#include <openssl/x509.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace mermai {

namespace {

std::string json_string(const std::string& input) {
    std::string output = "\"";
    for (const char c : input) {
        if (c == '"') output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else if (c == '\b') output += "\\b";
        else if (c == '\f') output += "\\f";
        else if (c == '\n') output += "\\n";
        else if (c == '\r') output += "\\r";
        else if (c == '\t') output += "\\t";
        else output += c;
    }
    output += '"';
    return output;
}

std::string request_id(const std::string& body) {
    const std::string needle = "\"id\"";
    const size_t pos = body.find(needle);
    if (pos == std::string::npos) return "null";
    const size_t colon = body.find(':', pos + needle.size());
    if (colon == std::string::npos) return "null";
    size_t start = colon + 1;
    while (start < body.size() && (body[start] == ' ' || body[start] == '\t' || body[start] == '\r' || body[start] == '\n')) ++start;
    if (start >= body.size()) return "null";
    if (body[start] == '"') {
        const size_t end = body.find('"', start + 1);
        if (end == std::string::npos) return "null";
        return body.substr(start, end - start + 1);
    }
    size_t end = start;
    while (end < body.size() && (isdigit(body[end]) || body[end] == '-' || body[end] == '.')) ++end;
    return body.substr(start, end - start);
}

std::string request_string(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const size_t pos = body.find(needle);
    if (pos == std::string::npos) return "";
    const size_t colon = body.find(':', pos + needle.size());
    if (colon == std::string::npos) return "";
    const size_t quote_start = body.find('"', colon + 1);
    if (quote_start == std::string::npos) return "";
    const size_t quote_end = body.find('"', quote_start + 1);
    if (quote_end == std::string::npos) return "";
    return body.substr(quote_start + 1, quote_end - quote_start - 1);
}

uint64_t request_number(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const size_t pos = body.find(needle);
    if (pos == std::string::npos) return 0;
    const size_t colon = body.find(':', pos + needle.size());
    if (colon == std::string::npos) return 0;
    size_t start = colon + 1;
    while (start < body.size() && (body[start] == ' ' || body[start] == '\t' || body[start] == '\r' || body[start] == '\n')) ++start;
    if (start >= body.size()) return 0;
    if (body[start] == '"') {
        const size_t end = body.find('"', start + 1);
        if (end == std::string::npos) return 0;
        const std::string text = body.substr(start + 1, end - start - 1);
        try {
            return text.rfind("0x", 0) == 0 ? std::stoull(text, nullptr, 16) : std::stoull(text);
        } catch (...) {
            return 0;
        }
    }
    size_t end = start;
    while (end < body.size() && (isdigit(body[end]) || body[end] == 'x' || isxdigit(body[end]))) ++end;
    const std::string text = body.substr(start, end - start);
    try {
        return text.rfind("0x", 0) == 0 ? std::stoull(text, nullptr, 16) : std::stoull(text);
    } catch (...) {
        return 0;
    }
}

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    std::string s = hex;
    if (s.rfind("0x", 0) == 0) s = s.substr(2);
    for (size_t i = 0; i + 1 < s.size(); i += 2) {
        try {
            bytes.push_back(static_cast<uint8_t>(std::stoul(s.substr(i, 2), nullptr, 16)));
        } catch (...) {}
    }
    return bytes;
}

std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    std::ostringstream ss;
    for (auto b : bytes) ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    return ss.str();
}

}

mermai_rpc_server::mermai_rpc_server(
    uint16_t rpc_port,
    std::shared_ptr<mermai_blockchain> chain,
    std::shared_ptr<mermai_p2p_node> network,
    std::shared_ptr<mermai_db> database,
    std::shared_ptr<mermai_time_weighted_pos> consensus_engine,
    std::shared_ptr<mermai_quorum_collector> quorum_collector
) : port(rpc_port), blockchain(chain), p2p_node(network), db(database),
    consensus(consensus_engine), quorum(quorum_collector) {}

mermai_rpc_server::~mermai_rpc_server() {
    stop_server();
}

bool mermai_rpc_server::start_server() {
    if (server_running) return false;
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    const SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) { WSACleanup(); return false; }
#else
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
#endif
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, "0.0.0.0", &server_addr.sin_addr);
    if (bind(sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0 || listen(sock, 64) < 0) {
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return false;
    }
    server_socket = static_cast<std::uintptr_t>(sock);
    server_running = true;
    server_thread = std::thread([this]() {
        while (server_running) {
            sockaddr_in client_addr{};
#ifdef _WIN32
            int client_len = sizeof(client_addr);
            const SOCKET client = accept(static_cast<SOCKET>(server_socket), reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client == INVALID_SOCKET) break;
#else
            socklen_t client_len = sizeof(client_addr);
            const int client = accept(static_cast<int>(server_socket), reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client < 0) break;
#endif
            std::thread([this, client]() {
                char buffer[4096] = {0};
#ifdef _WIN32
                const int bytes = recv(client, buffer, sizeof(buffer) - 1, 0);
#else
                const int bytes = static_cast<int>(recv(client, buffer, sizeof(buffer) - 1, 0));
#endif
                if (bytes > 0) {
                    std::string request(buffer, bytes);
                    const size_t body_pos = request.find("\r\n\r\n");
                    const std::string body = (body_pos != std::string::npos) ? request.substr(body_pos + 4) : request;
                    const std::string response_body = handle_request(body);
                    std::ostringstream response;
                    response << "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                             << response_body.size() << "\r\n\r\n" << response_body;
                    const std::string resp_str = response.str();
#ifdef _WIN32
                    send(client, resp_str.c_str(), static_cast<int>(resp_str.size()), 0);
                    closesocket(client);
#else
                    send(client, resp_str.c_str(), resp_str.size(), 0);
                    close(client);
#endif
                }
            }).detach();
        }
    });
    return true;
}

void mermai_rpc_server::stop_server() {
    if (!server_running) return;
    server_running = false;
#ifdef _WIN32
    if (server_socket) closesocket(static_cast<SOCKET>(server_socket));
    server_socket = 0;
    WSACleanup();
#else
    if (server_socket) close(static_cast<int>(server_socket));
    server_socket = 0;
#endif
    if (server_thread.joinable()) server_thread.join();
}

std::string mermai_rpc_server::handle_request(const std::string& request) {
    const std::string id = request_id(request);
    const std::string method = request_string(request, "method");
    std::string response;

    auto get_param_str = [&](const std::string& key) -> std::string {
        std::string v = request_string(request, key);
        if (v.empty()) v = request_string(request, "params");
        return v;
    };

    if (method == "mrm_blockNumber" || method == "eth_blockNumber") {
        response = get_block_count();
    } else if (method == "mrm_getBlockByNumber" || method == "eth_getBlockByNumber") {
        uint64_t n = request_number(request, "height");
        if (n == 0) n = request_number(request, "params");
        response = get_block(static_cast<uint32_t>(n));
    } else if (method == "mrm_getBlockByHash" || method == "eth_getBlockByHash") {
        response = get_block_by_hash(get_param_str("hash"));
    } else if (method == "mrm_getFinalizedBlock") {
        response = get_finalized_block();
    } else if (method == "mrm_getBalance" || method == "eth_getBalance") {
        response = get_balance(get_param_str("address"));
    } else if (method == "mrm_getTransactionCount" || method == "eth_getTransactionCount") {
        response = get_account_nonce(get_param_str("address"));
    } else if (method == "mrm_getTransaction" || method == "mrm_getTransactionByHash" || method == "eth_getTransactionByHash") {
        std::string tx_id = request_string(request, "txHash");
        if (tx_id.empty()) tx_id = request_string(request, "tx_id");
        if (tx_id.empty()) tx_id = request_string(request, "hash");
        if (tx_id.empty()) tx_id = request_string(request, "params");
        response = get_transaction(tx_id);
    } else if (method == "mrm_sendRawTransaction" || method == "eth_sendRawTransaction" || method == "mrm_sendTransaction") {
        response = send_transaction(request);
    } else if (method == "mrm_suggestFee" || method == "eth_gasPrice") {
            response = suggest_fee();
        } else if (method == "mrm_getMetrics") {
            response = get_metrics();
        } else if (method == "mrm_registerValidator") {
        response = register_validator(request);
    } else if (method == "mrm_getValidatorInfo") {
        response = get_validator_info(get_param_str("address"));
    } else if (method == "mrm_getAllValidators") {
        response = get_all_validators();
    } else if (method == "mrm_getValidatorWeight") {
        response = get_validator_weight(get_param_str("address"));
    } else if (method == "mrm_submitVote") {
        response = submit_vote(request);
    } else if (method == "mrm_getQuorumStatus") {
        response = get_quorum_status(get_param_str("hash"));
    } else if (method == "mrm_deployContract") {
        response = deploy_contract(request);
    } else if (method == "mrm_getContractCode" || method == "eth_getCode") {
        response = get_contract_code(get_param_str("address"));
    } else if (method == "mrm_callContract" || method == "eth_call") {
        response = call_contract(request);
    } else if (method == "mrm_estimateGas" || method == "eth_estimateGas") {
        response = estimate_gas(request);
    } else if (method == "mrm_peerCount" || method == "net_peerCount") {
        response = get_peer_count();
    } else if (method == "mrm_chainId" || method == "eth_chainId") {
        response = get_chain_id();
    } else {
        response = json_error(-32601, "Method not found");
    }

    if (response.rfind("{\"error\"", 0) == 0 || response.rfind("{\"result\"", 0) == 0) {
        return "{\"jsonrpc\":\"2.0\"," + response.substr(1, response.size() - 2) + ",\"id\":" + id + "}";
    }
    return response;
}

std::string mermai_rpc_server::json_error(int code, const std::string& message) {
    return "{\"error\":{\"code\":" + std::to_string(code) + ",\"message\":" + json_string(message) + "}}";
}

// ========== BLOCK METHODS ==========

std::string mermai_rpc_server::get_block_count() {
    uint32_t height = blockchain ? blockchain->get_height() : (db ? db->get_latest_block_height() : 0);
    return "{\"result\":" + std::to_string(height) + "}";
}

std::string mermai_rpc_server::get_block(uint32_t height) {
    auto block = blockchain ? blockchain->get_block(height) : nullptr;
    if (!block && db) block = db->get_block_by_height(height);
    if (!block) return json_error(-1, "Block not found");
    return "{\"result\":{\"height\":" + std::to_string(block->height) + ",\"validator\":" + json_string(block->validator_address) + "}}";
}

std::string mermai_rpc_server::get_latest_block() {
    return get_block(blockchain ? blockchain->get_height() : 0);
}

std::string mermai_rpc_server::get_block_by_hash(const std::string& hash) {
    if (!db) return json_error(-1, "Database unavailable");
    auto block = db->get_block_by_hash(hash);
    if (!block) return json_error(-1, "Block not found");
    return "{\"result\":{\"height\":" + std::to_string(block->height) + ",\"validator\":" + json_string(block->validator_address) + "}}";
}

std::string mermai_rpc_server::get_finalized_block() {
    if (!blockchain) return json_error(-1, "Blockchain unavailable");
    uint32_t tip = blockchain->get_height();
    for (int h = static_cast<int>(tip); h >= 0; --h) {
        auto block = blockchain->get_block(static_cast<uint32_t>(h));
        if (block) {
            std::ostringstream ss;
            for (auto b : block->calculate_hash()) ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
            const std::string hash = ss.str();
            if (quorum && quorum->is_block_finalized(hash)) {
                return "{\"result\":{\"height\":" + std::to_string(block->height) + ",\"hash\":" + json_string(hash) + "}}";
            }
        }
    }
    return "{\"result\":{\"height\":0,\"hash\":\"genesis\"}}";
}

// ========== ACCOUNT METHODS ==========

std::string mermai_rpc_server::get_account(const std::string& address) {
    if (!db) return json_error(-1, "Database unavailable");
    const auto account = db->get_account(address);
    return "{\"result\":{\"address\":" + json_string(account.address) + ",\"balance\":" + std::to_string(account.balance) + ",\"nonce\":" + std::to_string(account.nonce) + "}}";
}

std::string mermai_rpc_server::get_balance(const std::string& address) {
    if (!db) return json_error(-1, "Database unavailable");
    return "{\"result\":" + std::to_string(db->get_account(address).balance) + "}";
}

std::string mermai_rpc_server::get_account_nonce(const std::string& address) {
    if (!db) return json_error(-1, "Database unavailable");
    return "{\"result\":" + std::to_string(db->get_account(address).nonce) + "}";
}

std::string mermai_rpc_server::get_account_transactions(const std::string& address) {
    if (!db) return json_error(-1, "Database unavailable");
    const auto txs = db->get_account_transactions(address);
    return "{\"result\":[]}";
}

// ========== TRANSACTION METHODS ==========

std::string mermai_rpc_server::get_transaction(const std::string& tx_id) {
    if (!db) return json_error(-1, "Database unavailable");
    const auto tx = db->get_transaction(tx_id);
    if (tx.id.empty()) return json_error(-1, "Transaction not found");
    return "{\"result\":{\"id\":" + json_string(tx.id) + ",\"fee\":" + std::to_string(tx.fee) + "}}";
}

std::string mermai_rpc_server::send_transaction(const std::string& tx_json) {
    std::string raw = request_string(tx_json, "raw");
    if (raw.empty()) raw = request_string(tx_json, "serialized");
    if (raw.rfind("0x", 0) == 0) {
        auto bytes = hex_to_bytes(raw);
        raw = std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    try {
        mermai_transaction tx = raw.empty() ? mermai_transaction() : mermai_transaction::deserialize(raw);
        if (tx.id.empty()) {
            tx.id = request_string(tx_json, "id");
            tx.timestamp = request_number(tx_json, "timestamp");
            tx.fee = request_number(tx_json, "fee");
        }
        if (db && !db->validate_transaction_inputs(tx)) return json_error(-32602, "Invalid transaction inputs");
        if (db && !db->add_to_mempool(tx)) return json_error(-32000, "Mempool insertion failed");
        if (p2p_node) p2p_node->broadcast_transaction(std::make_shared<mermai_transaction>(tx));
        return "{\"result\":" + json_string(tx.id.empty() ? tx.get_hash() : tx.id) + "}";
    } catch (const std::exception& e) {
        return json_error(-32602, e.what());
    }
}

std::string mermai_rpc_server::get_transaction_status(const std::string& tx_id) {
    if (!db) return json_error(-1, "Database unavailable");
    const auto tx = db->get_transaction(tx_id);
    return tx.id.empty() ? "{\"result\":\"NOT_FOUND\"}" : "{\"result\":\"CONFIRMED\"}";
}

std::string mermai_rpc_server::estimate_gas(const std::string& call_json) {
    std::string code_hex = request_string(call_json, "bytecode");
    if (code_hex.empty()) code_hex = request_string(call_json, "code");
    const auto code = hex_to_bytes(code_hex);
    if (code.empty()) return "{\"result\":\"0x5208\"}";
    vm::mermai_execution_context ctx;
    ctx.gas_limit = 1000000;
    vm::mermai_vm vm_instance(code, ctx);
    vm_instance.execute();
    std::ostringstream hex_gas;
    hex_gas << "0x" << std::hex << vm_instance.get_context().gas_used;
    return "{\"result\":" + json_string(hex_gas.str()) + "}";
}

// ========== CONTRACT METHODS ==========

std::string mermai_rpc_server::deploy_contract(const std::string& deployment_json) {
    std::string code_hex = request_string(deployment_json, "bytecode");
    if (code_hex.empty()) code_hex = request_string(deployment_json, "code");
    const auto code = hex_to_bytes(code_hex);
    if (code.empty()) return json_error(-32602, "Invalid contract bytecode");
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_CTX context;
    SHA256_Init(&context);
    SHA256_Update(&context, code.data(), code.size());
    SHA256_Final(digest, &context);
    std::ostringstream address;
    address << "0x";
    for (size_t i = 0; i < 20; ++i) address << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    const std::string contract_address = address.str();
    if (db && !db->save_contract_code(contract_address, code)) return json_error(-32000, "Failed to save contract");
    return "{\"result\":" + json_string(contract_address) + "}";
}

std::string mermai_rpc_server::call_contract(const std::string& call_json) {
    const std::string address = request_string(call_json, "address");
    std::vector<uint8_t> code;
    if (!address.empty() && db) {
        code = db->get_contract_code(address);
    }
    if (code.empty()) {
        std::string code_hex = request_string(call_json, "bytecode");
        if (code_hex.empty()) code_hex = request_string(call_json, "code");
        if (!code_hex.empty()) code = hex_to_bytes(code_hex);
    }
    if (code.empty()) return json_error(-1, "Contract code not found");
    
    vm::mermai_execution_context ctx;
    ctx.gas_limit = 1000000;
    ctx.caller_address = request_string(call_json, "from");
    ctx.contract_address = address;
    vm::mermai_vm vm_instance(code, ctx);
    if (!vm_instance.execute()) return json_error(-32000, "Execution failed");
    
    std::string return_hex;
    const auto& stack = vm_instance.get_context().stack;
    if (!stack.empty()) {
        for (const auto b : stack.back()) {
            std::ostringstream ss;
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
            return_hex += ss.str();
        }
    } else {
        return_hex = std::string(64, '0');
    }
    return "{\"result\":" + json_string("0x" + return_hex) + "}";
}

std::string mermai_rpc_server::get_contract_storage(const std::string& contract_address, const std::string& key) {
    if (!db) return json_error(-1, "Database unavailable");
    const auto val = db->get_contract_storage(contract_address, key);
    return "{\"result\":" + json_string(bytes_to_hex(val)) + "}";
}

std::string mermai_rpc_server::get_contract_code(const std::string& contract_address) {
    if (!db) return json_error(-1, "Database unavailable");
    const auto code = db->get_contract_code(contract_address);
    if (code.empty()) return json_error(-1, "Contract not found");
    return "{\"result\":" + json_string("0x" + bytes_to_hex(code)) + "}";
}

// ========== NETWORK METHODS ==========

std::string mermai_rpc_server::get_peer_count() {
    uint32_t count = p2p_node ? p2p_node->get_peer_count() : 0;
    return "{\"result\":" + std::to_string(count) + "}";
}

std::string mermai_rpc_server::get_network_info() {
    return "{\"result\":{\"chainId\":1337,\"protocolVersion\":\"1.0.0\"}}";
}

std::string mermai_rpc_server::get_syncing_status() {
    return "{\"result\":false}";
}

// ========== VALIDATOR & CONSENSUS METHODS ==========

std::string mermai_rpc_server::register_validator(const std::string& validator_json) {
    if (!consensus) return json_error(-32000, "Consensus unavailable");
    const std::string address = request_string(validator_json, "address");
    const uint64_t stake = request_number(validator_json, "stake");
    const std::string pubkey_hex = request_string(validator_json, "public_key");
    const auto public_key = hex_to_bytes(pubkey_hex);
    
    if (address.empty() || stake < 1000000 || !consensus->register_validator(address, stake, "", public_key)) {
        return json_error(-32602, "Invalid validator registration");
    }
    for (const auto& validator : consensus->get_consensus_state().active_validators) {
        if (validator.address == address && db) db->save_validator(validator);
    }
    return "{\"result\":true}";
}

std::string mermai_rpc_server::get_validator_info(const std::string& address) {
    if (!db) return json_error(-1, "Database unavailable");
    const auto validator = db->get_validator(address);
    if (validator.amount == 0) return json_error(-1, "Validator not found");
    std::ostringstream result;
    result << "{\"result\":{\"address\":" << json_string(validator.address)
           << ",\"amount\":" << validator.amount << ",\"lockTime\":" << validator.lock_time
           << ",\"lastProposal\":" << validator.last_proposal << ",\"weight\":" << validator.weight << "}}";
    return result.str();
}

std::string mermai_rpc_server::get_all_validators() {
    if (!db) return json_error(-1, "Database unavailable");
    const auto validators = db->get_all_validators();
    std::string result = "{\"result\":[";
    for (size_t index = 0; index < validators.size(); ++index) {
        if (index) result += ',';
        result += "{\"address\":" + json_string(validators[index].address) + ",\"amount\":" + std::to_string(validators[index].amount) + "}";
    }
    return result + "]}";
}

std::string mermai_rpc_server::get_validator_weight(const std::string& address) {
    if (!db) return json_error(-1, "Database unavailable");
    const auto validator = db->get_validator(address);
    if (validator.amount == 0) return json_error(-1, "Validator not found");
    return "{\"result\":" + std::to_string(validator.weight) + "}";
}

std::string mermai_rpc_server::submit_vote(const std::string& vote_json) {
    if (!quorum || !consensus) return json_error(-32000, "Quorum service unavailable");
    mermai_block_vote vote;
    vote.validator_address = request_string(vote_json, "validator_address");
    vote.block_hash = request_string(vote_json, "block_hash");
    vote.block_height = static_cast<uint32_t>(request_number(vote_json, "block_height"));
    vote.timestamp = request_number(vote_json, "timestamp");
    vote.validator_public_key = hex_to_bytes(request_string(vote_json, "validator_public_key"));
    vote.signature = hex_to_bytes(request_string(vote_json, "signature"));
    
    if (!quorum->add_vote(vote, *consensus)) {
        return json_error(-32602, "Invalid vote signature or unregistered validator");
    }
    if (p2p_node) p2p_node->broadcast_vote(vote);
    return "{\"result\":true}";
}

std::string mermai_rpc_server::get_quorum_status(const std::string& block_hash) {
    if (!quorum || !consensus) return json_error(-32000, "Quorum service unavailable");
    const bool finalized = quorum->is_block_finalized(block_hash);
    const uint64_t vote_stake = quorum->get_vote_stake(block_hash);
    const uint64_t total_stake = consensus->get_total_active_stake();
    return "{\"result\":{\"finalized\":" + std::string(finalized ? "true" : "false") +
           ",\"voteStake\":" + std::to_string(vote_stake) +
           ",\"totalStake\":" + std::to_string(total_stake) + "}}";
}

// ========== UTILITY METHODS ==========

std::string mermai_rpc_server::get_chain_id() {
    return "{\"result\":1337}";
}

std::string mermai_rpc_server::get_protocol_version() {
    return "{\"result\":\"1.0.0\"}";
}

std::string mermai_rpc_server::get_mining_difficulty() {
    return "{\"result\":4}";
}


// ============================================================================
// PHASE 5: PROMETHEUS-COMPATIBLE METRICS ENDPOINT
// ============================================================================
std::string mermai_rpc_server::get_metrics() {
    uint32_t block_height = blockchain ? blockchain->get_height() : 0;
    uint32_t active_validators = consensus ? consensus->get_active_validator_count() : 0;
    uint32_t peers_connected = p2p_node ? p2p_node->get_peer_count() : 0;

    std::ostringstream out;
    out << "# HELP mermai_block_height Current chain tip height\n";
    out << "# TYPE mermai_block_height gauge\n";
    out << "mermai_block_height " << block_height << "\n\n";

    out << "# HELP mermai_validators_active Number of active validators\n";
    out << "# TYPE mermai_validators_active gauge\n";
    out << "mermai_validators_active " << active_validators << "\n\n";

    out << "# HELP mermai_peers_connected Number of connected P2P peers\n";
    out << "# TYPE mermai_peers_connected gauge\n";
    out << "mermai_peers_connected " << peers_connected << "\n\n";

    out << "# HELP mermai_mempool_size Number of unconfirmed transactions\n";
    out << "# TYPE mermai_mempool_size gauge\n";
    out << "mermai_mempool_size 0\n";

    // Return as plain text (Prometheus format), not JSON
    return out.str();
}

// ============================================================================
// PHASE 2: FEE SUGGESTION
// ============================================================================
std::string mermai_rpc_server::suggest_fee() {
    std::ostringstream oss;
    oss << "{\"result\":{\"min_fee\":1000,\"median_fee\":5000,\"fast_fee\":10000}}";
    return oss.str();
}

} // namespace mermai
