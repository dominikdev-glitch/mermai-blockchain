#include "../include/mermai/mermai_db.hpp"
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace mermai {

// ============================================================================
// MERMAI DB IMPLEMENTATION
// ============================================================================

mermai_db::mermai_db(const std::string& path) : db_path(path) {}

namespace {
bool execute(sqlite3* db, const char* sql) {
    char* error = nullptr;
    const int result = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    sqlite3_free(error);
    return result == SQLITE_OK;
}

bool bind_text(sqlite3_stmt* statement, int index, const std::string& value) {
    return sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

bool bind_blob(sqlite3_stmt* statement, int index, const std::vector<uint8_t>& value) {
    return sqlite3_bind_blob(statement, index, value.empty() ? "" : reinterpret_cast<const char*>(value.data()), static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
}

bool bind_blob(sqlite3_stmt* statement, int index, const std::string& value) {
    return sqlite3_bind_blob(statement, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
}

std::string hash_text(const mermai_hash256& hash) {
    std::ostringstream output;
    for (const auto byte : hash) output << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(byte);
    return output.str();
}

bool has_column(sqlite3* db, const char* table, const char* column) {
    sqlite3_stmt* statement = nullptr;
    const std::string query = std::string("PRAGMA table_info(") + table + ")";
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &statement, nullptr) != SQLITE_OK) return false;
    bool found = false;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        if (name && std::string(name) == column) { found = true; break; }
    }
    sqlite3_finalize(statement);
    return found;
}

std::string address_from_public_key(const std::vector<uint8_t>& public_key) {
    if (public_key.empty()) return {};
    // Reject malformed DER keys before deriving an address from them.
    const unsigned char* key_data = public_key.data();
    EVP_PKEY* key = d2i_PUBKEY(nullptr, &key_data, static_cast<long>(public_key.size()));
    if (!key || key_data != public_key.data() + public_key.size()) {
        EVP_PKEY_free(key);
        return {};
    }
    EVP_PKEY_free(key);
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(public_key.data(), public_key.size(), digest);
    std::ostringstream output;
    output << "mrm1" << std::hex << std::setfill('0');
    for (size_t index = 0; index < 20; ++index) output << std::setw(2) << static_cast<unsigned int>(digest[index]);
    return output.str();
}
}

mermai_db::~mermai_db() {
    if (db_connection) sqlite3_close(db_connection);
}

bool mermai_db::initialize() {
    std::cout << "Initializing database at " << db_path << std::endl;
    if (sqlite3_open(db_path.c_str(), &db_connection) != SQLITE_OK) return false;
    return execute(db_connection, "PRAGMA foreign_keys = ON") &&
           execute(db_connection, "CREATE TABLE IF NOT EXISTS blocks (hash TEXT PRIMARY KEY, height INTEGER NOT NULL UNIQUE, previous_hash TEXT NOT NULL, timestamp INTEGER NOT NULL, serialized BLOB NOT NULL)") &&
           execute(db_connection, "CREATE TABLE IF NOT EXISTS transactions (id TEXT PRIMARY KEY, block_hash TEXT, serialized BLOB NOT NULL, timestamp INTEGER NOT NULL)") &&
           execute(db_connection, "CREATE TABLE IF NOT EXISTS utxos (tx_id TEXT NOT NULL, output_index INTEGER NOT NULL, address TEXT NOT NULL, amount INTEGER NOT NULL CHECK(amount > 0), PRIMARY KEY(tx_id, output_index))") &&
           execute(db_connection, "CREATE INDEX IF NOT EXISTS idx_utxos_address ON utxos(address)") &&
           execute(db_connection, "CREATE TABLE IF NOT EXISTS accounts (address TEXT PRIMARY KEY, balance INTEGER NOT NULL, nonce INTEGER NOT NULL, last_activity INTEGER NOT NULL)") &&
           execute(db_connection, "CREATE TABLE IF NOT EXISTS validators (address TEXT PRIMARY KEY, amount INTEGER NOT NULL, lock_time INTEGER NOT NULL, last_proposal INTEGER NOT NULL, weight REAL NOT NULL, public_key BLOB NOT NULL DEFAULT X'')") &&
           execute(db_connection, "CREATE TABLE IF NOT EXISTS contract_storage (contract_address TEXT NOT NULL, storage_key TEXT NOT NULL, value BLOB NOT NULL, PRIMARY KEY(contract_address, storage_key))") &&
           execute(db_connection, "CREATE TABLE IF NOT EXISTS contracts (contract_address TEXT PRIMARY KEY, code BLOB NOT NULL)") &&
           execute(db_connection, "CREATE TABLE IF NOT EXISTS mempool (id TEXT PRIMARY KEY, serialized BLOB NOT NULL, timestamp INTEGER NOT NULL)") &&
           execute(db_connection, "CREATE TABLE IF NOT EXISTS state_roots (block_height INTEGER PRIMARY KEY, state_root TEXT NOT NULL)") &&
           (has_column(db_connection, "validators", "public_key") ||
            execute(db_connection, "ALTER TABLE validators ADD COLUMN public_key BLOB NOT NULL DEFAULT X''"));
}

// ========== BLOCK OPERATIONS ==========

bool mermai_db::save_block(const std::shared_ptr<mermai_block>& block) {
    if (!db_connection || !block) return false;
    const auto serialized = block->serialize();
    const std::string hash_value = hash_text(block->calculate_hash());
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "INSERT OR REPLACE INTO blocks(hash,height,previous_hash,timestamp,serialized) VALUES(?,?,?,?,?)", -1, &statement, nullptr) != SQLITE_OK) return false;
    bind_text(statement, 1, hash_value);
    sqlite3_bind_int(statement, 2, static_cast<int>(block->height));
    bind_text(statement, 3, block->prev_block_hash);
    sqlite3_bind_int64(statement, 4, static_cast<sqlite3_int64>(block->timestamp));
    bind_blob(statement, 5, serialized);
    bool result = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    for (const auto& tx : block->transactions) {
        result = save_transaction(tx) && result;
        sqlite3_stmt* transaction_update = nullptr;
        if (sqlite3_prepare_v2(db_connection, "UPDATE transactions SET block_hash = ? WHERE id = ?", -1, &transaction_update, nullptr) == SQLITE_OK) {
            bind_text(transaction_update, 1, hash_value);
            bind_text(transaction_update, 2, tx.id.empty() ? tx.get_hash() : tx.id);
            result = sqlite3_step(transaction_update) == SQLITE_DONE && result;
        } else {
            result = false;
        }
        sqlite3_finalize(transaction_update);
    }
    return result;
}

std::shared_ptr<mermai_block> mermai_db::get_block_by_hash(const std::string& hash) {
    if (!db_connection) return nullptr;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "SELECT serialized FROM blocks WHERE hash = ?", -1, &statement, nullptr) != SQLITE_OK) return nullptr;
    bind_text(statement, 1, hash);
    std::shared_ptr<mermai_block> result;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* data = static_cast<const char*>(sqlite3_column_blob(statement, 0));
        const int size = sqlite3_column_bytes(statement, 0);
        try { if (data && size > 0) result = std::make_shared<mermai_block>(mermai_block::deserialize({data, static_cast<size_t>(size)})); } catch (const std::exception&) { result = nullptr; }
    }
    sqlite3_finalize(statement);
    return result;
}

std::shared_ptr<mermai_block> mermai_db::get_block_by_height(uint32_t height) {
    if (!db_connection) return nullptr;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "SELECT serialized FROM blocks WHERE height = ?", -1, &statement, nullptr) != SQLITE_OK) return nullptr;
    sqlite3_bind_int(statement, 1, static_cast<int>(height));
    std::shared_ptr<mermai_block> result;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* data = static_cast<const char*>(sqlite3_column_blob(statement, 0));
        const int size = sqlite3_column_bytes(statement, 0);
        try { if (data && size > 0) result = std::make_shared<mermai_block>(mermai_block::deserialize({data, static_cast<size_t>(size)})); } catch (const std::exception&) { result = nullptr; }
    }
    sqlite3_finalize(statement);
    return result;
}

uint32_t mermai_db::get_latest_block_height() {
    if (!db_connection) return 0;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "SELECT COALESCE(MAX(height), 0) FROM blocks", -1, &statement, nullptr) != SQLITE_OK) return 0;
    uint32_t height = 0;
    if (sqlite3_step(statement) == SQLITE_ROW) height = static_cast<uint32_t>(sqlite3_column_int(statement, 0));
    sqlite3_finalize(statement);
    return height;
}

// ========== TRANSACTION OPERATIONS ==========

bool mermai_db::save_transaction(const mermai_transaction& tx) {
    if (!db_connection) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "INSERT OR REPLACE INTO transactions(id,serialized,timestamp) VALUES(?,?,?)", -1, &statement, nullptr) != SQLITE_OK) return false;
    const auto serialized = tx.serialize();
    const std::string id = tx.id.empty() ? tx.get_hash() : tx.id;
    bind_text(statement, 1, id);
    bind_blob(statement, 2, serialized);
    sqlite3_bind_int64(statement, 3, static_cast<sqlite3_int64>(tx.timestamp));
    const bool result = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return result;
}

mermai_transaction mermai_db::get_transaction(const std::string& tx_id) {
    if (!db_connection) return mermai_transaction();

    auto load_transaction = [&](const char* query) -> mermai_transaction {
        sqlite3_stmt* statement = nullptr;
        mermai_transaction result;
        if (sqlite3_prepare_v2(db_connection, query, -1, &statement, nullptr) != SQLITE_OK) return result;
        bind_text(statement, 1, tx_id);
        if (sqlite3_step(statement) == SQLITE_ROW) {
            const auto* data = static_cast<const char*>(sqlite3_column_blob(statement, 0));
            const int size = sqlite3_column_bytes(statement, 0);
            try {
                if (data && size > 0) result = mermai_transaction::deserialize({data, static_cast<size_t>(size)});
            } catch (const std::exception&) {
            }
        }
        sqlite3_finalize(statement);
        return result;
    };

    mermai_transaction result = load_transaction("SELECT serialized FROM transactions WHERE id = ?");
    if (!result.id.empty()) return result;
    return load_transaction("SELECT serialized FROM mempool WHERE id = ?");
}

bool mermai_db::apply_block(const std::shared_ptr<mermai_block>& block) {
    if (!db_connection || !block || !execute(db_connection, "BEGIN IMMEDIATE TRANSACTION")) return false;
    bool result = true;
    for (const auto& tx : block->transactions) {
        const std::string tx_id = tx.id.empty() ? tx.get_hash() : tx.id;
        if (!tx.inputs.empty()) {
            if (!validate_transaction_inputs(tx)) { result = false; break; }
            for (const auto& input : tx.inputs) {
                if (!spend_unspent_output(input.prev_tx_hash, input.prev_output_idx)) { result = false; break; }
            }
            if (!result) break;
        }
        for (uint32_t index = 0; index < tx.outputs.size(); ++index) {
            if (!save_unspent_output({tx_id, index, tx.outputs[index].address, tx.outputs[index].amount})) { result = false; break; }
        }
        if (!result || !remove_from_mempool(tx_id)) {
            // It is valid for a block to contain a transaction not previously
            // seen in our mempool; only a database error should abort it.
            if (!result) break;
        }
    }
    if (result) result = save_block(block);
    if (!execute(db_connection, result ? "COMMIT" : "ROLLBACK")) result = false;
    return result;
}

// ========== UTXO STATE ===========

bool mermai_db::save_unspent_output(const UnspentOutput& output) {
    if (!db_connection || output.tx_id.empty() || output.address.empty() || output.amount == 0) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "INSERT INTO utxos(tx_id,output_index,address,amount) VALUES(?,?,?,?)", -1, &statement, nullptr) != SQLITE_OK) return false;
    bind_text(statement, 1, output.tx_id);
    sqlite3_bind_int(statement, 2, static_cast<int>(output.output_index));
    bind_text(statement, 3, output.address);
    sqlite3_bind_int64(statement, 4, static_cast<sqlite3_int64>(output.amount));
    const bool result = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return result;
}

bool mermai_db::get_unspent_output(const std::string& tx_id, uint32_t output_index, UnspentOutput& output) {
    output = {};
    if (!db_connection) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "SELECT address,amount FROM utxos WHERE tx_id = ? AND output_index = ?", -1, &statement, nullptr) != SQLITE_OK) return false;
    bind_text(statement, 1, tx_id);
    sqlite3_bind_int(statement, 2, static_cast<int>(output_index));
    const bool found = sqlite3_step(statement) == SQLITE_ROW;
    if (found) {
        const auto* address = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        output = {tx_id, output_index, address ? address : "", static_cast<uint64_t>(sqlite3_column_int64(statement, 1))};
    }
    sqlite3_finalize(statement);
    return found;
}

bool mermai_db::spend_unspent_output(const std::string& tx_id, uint32_t output_index) {
    if (!db_connection) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "DELETE FROM utxos WHERE tx_id = ? AND output_index = ?", -1, &statement, nullptr) != SQLITE_OK) return false;
    bind_text(statement, 1, tx_id);
    sqlite3_bind_int(statement, 2, static_cast<int>(output_index));
    const bool result = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(db_connection) == 1;
    sqlite3_finalize(statement);
    return result;
}

bool mermai_db::validate_transaction_inputs(const mermai_transaction& tx) const {
    if (!db_connection || !tx.verify()) return false;
    uint64_t inputs_total = 0;
    uint64_t outputs_total = tx.fee;
    for (const auto& output : tx.outputs) {
        if (output.amount > UINT64_MAX - outputs_total) return false;
        outputs_total += output.amount;
    }
    for (const auto& input : tx.inputs) {
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db_connection, "SELECT address,amount FROM utxos WHERE tx_id = ? AND output_index = ?", -1, &statement, nullptr) != SQLITE_OK) return false;
        bind_text(statement, 1, input.prev_tx_hash);
        sqlite3_bind_int(statement, 2, static_cast<int>(input.prev_output_idx));
        const bool found = sqlite3_step(statement) == SQLITE_ROW;
        const auto* address_data = found ? reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)) : nullptr;
        const std::string address = address_data ? address_data : "";
        const uint64_t amount = found ? static_cast<uint64_t>(sqlite3_column_int64(statement, 1)) : 0;
        sqlite3_finalize(statement);
        if (!found || address.empty() || address_from_public_key(input.public_key) != address || amount > UINT64_MAX - inputs_total) return false;
        inputs_total += amount;
    }
    return inputs_total == outputs_total;
}

std::vector<mermai_transaction> mermai_db::get_transactions_in_block(const std::string& block_hash) {
    std::vector<mermai_transaction> transactions;
    if (!db_connection) return transactions;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "SELECT serialized FROM transactions WHERE block_hash = ? ORDER BY timestamp, id", -1, &statement, nullptr) != SQLITE_OK) return transactions;
    bind_text(statement, 1, block_hash);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* data = static_cast<const char*>(sqlite3_column_blob(statement, 0));
        const int size = sqlite3_column_bytes(statement, 0);
        try {
            if (data && size > 0) transactions.push_back(mermai_transaction::deserialize({data, static_cast<size_t>(size)}));
        } catch (const std::exception&) {
        }
    }
    sqlite3_finalize(statement);
    return transactions;
}

// ========== ACCOUNT OPERATIONS ==========

bool mermai_db::save_account(const Account& account) {
    if (!db_connection) return false;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO accounts(address,balance,nonce,last_activity) VALUES(?,?,?,?) ON CONFLICT(address) DO UPDATE SET balance=excluded.balance,nonce=excluded.nonce,last_activity=excluded.last_activity";
    if (sqlite3_prepare_v2(db_connection, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    const bool result = bind_text(statement, 1, account.address) && sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(account.balance)) == SQLITE_OK &&
        sqlite3_bind_int(statement, 3, static_cast<int>(account.nonce)) == SQLITE_OK && sqlite3_bind_int64(statement, 4, static_cast<sqlite3_int64>(account.last_activity)) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return result;
}

mermai_db::Account mermai_db::get_account(const std::string& address) {
    Account account{address, 0, 0, 0};
    if (!db_connection) return account;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "SELECT balance,nonce,last_activity FROM accounts WHERE address = ?", -1, &statement, nullptr) == SQLITE_OK) {
        bind_text(statement, 1, address);
        if (sqlite3_step(statement) == SQLITE_ROW) {
            account.balance = static_cast<uint64_t>(sqlite3_column_int64(statement, 0));
            account.nonce = static_cast<uint32_t>(sqlite3_column_int(statement, 1));
            account.last_activity = static_cast<uint64_t>(sqlite3_column_int64(statement, 2));
        }
    }
    sqlite3_finalize(statement);
    return account;
}

bool mermai_db::update_balance(const std::string& address, uint64_t new_balance) {
    if (!db_connection) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "UPDATE accounts SET balance = ? WHERE address = ?", -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(new_balance));
    bind_text(statement, 2, address);
    const bool result = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return result;
}

bool mermai_db::increment_nonce(const std::string& address) {
    if (!db_connection) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "UPDATE accounts SET nonce = nonce + 1 WHERE address = ?", -1, &statement, nullptr) != SQLITE_OK) return false;
    bind_text(statement, 1, address);
    const bool result = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return result;
}

// ========== VALIDATOR OPERATIONS ==========

bool mermai_db::save_validator(const mermai_validator_stake& stake) {
    if (!db_connection) return false;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO validators(address,amount,lock_time,last_proposal,weight,public_key) VALUES(?,?,?,?,?,?) ON CONFLICT(address) DO UPDATE SET amount=excluded.amount,lock_time=excluded.lock_time,last_proposal=excluded.last_proposal,weight=excluded.weight,public_key=excluded.public_key";
    if (sqlite3_prepare_v2(db_connection, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    const bool result = bind_text(statement, 1, stake.address) && sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(stake.amount)) == SQLITE_OK &&
        sqlite3_bind_int64(statement, 3, static_cast<sqlite3_int64>(stake.lock_time)) == SQLITE_OK && sqlite3_bind_int64(statement, 4, static_cast<sqlite3_int64>(stake.last_proposal)) == SQLITE_OK &&
        sqlite3_bind_double(statement, 5, stake.weight) == SQLITE_OK && bind_blob(statement, 6, stake.public_key) && sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return result;
}

mermai_validator_stake mermai_db::get_validator(const std::string& address) {
    mermai_validator_stake stake{address, 0, 0, 0, 0.0f, {}};
    if (!db_connection) return stake;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "SELECT amount,lock_time,last_proposal,weight,public_key FROM validators WHERE address = ?", -1, &statement, nullptr) == SQLITE_OK) {
        bind_text(statement, 1, address);
        if (sqlite3_step(statement) == SQLITE_ROW) {
            stake.amount = static_cast<uint64_t>(sqlite3_column_int64(statement, 0));
            stake.lock_time = static_cast<uint64_t>(sqlite3_column_int64(statement, 1));
            stake.last_proposal = static_cast<uint64_t>(sqlite3_column_int64(statement, 2));
            stake.weight = static_cast<float>(sqlite3_column_double(statement, 3));
            const auto* key = static_cast<const uint8_t*>(sqlite3_column_blob(statement, 4));
            const int key_size = sqlite3_column_bytes(statement, 4);
            if (key && key_size > 0) stake.public_key.assign(key, key + key_size);
        }
    }
    sqlite3_finalize(statement);
    return stake;
}

std::vector<mermai_validator_stake> mermai_db::get_all_validators() {
    std::vector<mermai_validator_stake> validators;
    if (!db_connection) return validators;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "SELECT address,amount,lock_time,last_proposal,weight,public_key FROM validators ORDER BY address", -1, &statement, nullptr) == SQLITE_OK) {
        while (sqlite3_step(statement) == SQLITE_ROW) {
            mermai_validator_stake validator{reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)), static_cast<uint64_t>(sqlite3_column_int64(statement, 1)), static_cast<uint64_t>(sqlite3_column_int64(statement, 2)), static_cast<uint64_t>(sqlite3_column_int64(statement, 3)), static_cast<float>(sqlite3_column_double(statement, 4)), {}};
            const auto* key = static_cast<const uint8_t*>(sqlite3_column_blob(statement, 5));
            const int key_size = sqlite3_column_bytes(statement, 5);
            if (key && key_size > 0) validator.public_key.assign(key, key + key_size);
            validators.push_back(std::move(validator));
        }
    }
    sqlite3_finalize(statement);
    return validators;
}

bool mermai_db::update_validator_stake(const std::string& address, uint64_t new_stake) {
    if (!db_connection) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "UPDATE validators SET amount = ? WHERE address = ?", -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(new_stake));
    bind_text(statement, 2, address);
    const bool result = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return result;
}

bool mermai_db::remove_validator(const std::string& address) {
    if (!db_connection) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "DELETE FROM validators WHERE address = ?", -1, &statement, nullptr) != SQLITE_OK) return false;
    bind_text(statement, 1, address);
    const bool result = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return result;
}

// ========== CONTRACT STORAGE ==========

bool mermai_db::save_contract_storage(
    const std::string& contract_address,
    const std::string& key,
    const std::vector<uint8_t>& value
) {
    if (!db_connection) return false;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO contract_storage(contract_address,storage_key,value) VALUES(?,?,?) ON CONFLICT(contract_address,storage_key) DO UPDATE SET value=excluded.value";
    if (sqlite3_prepare_v2(db_connection, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    bind_text(statement, 1, contract_address);
    bind_text(statement, 2, key);
    bind_blob(statement, 3, value);
    const bool result = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return result;
}

std::vector<uint8_t> mermai_db::get_contract_storage(
    const std::string& contract_address,
    const std::string& key
) {
    if (!db_connection) return {};
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "SELECT value FROM contract_storage WHERE contract_address = ? AND storage_key = ?", -1, &statement, nullptr) != SQLITE_OK) return {};
    bind_text(statement, 1, contract_address);
    bind_text(statement, 2, key);
    std::vector<uint8_t> result;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* data = static_cast<const uint8_t*>(sqlite3_column_blob(statement, 0));
        const int size = sqlite3_column_bytes(statement, 0);
        if (data && size > 0) result.assign(data, data + size);
    }
    sqlite3_finalize(statement);
    return result;
}

std::map<std::string, std::vector<uint8_t>> mermai_db::get_contract_all_storage(
    const std::string& contract_address
) {
    std::map<std::string, std::vector<uint8_t>> result;
    if (!db_connection) return result;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "SELECT storage_key,value FROM contract_storage WHERE contract_address = ?", -1, &statement, nullptr) != SQLITE_OK) return result;
    bind_text(statement, 1, contract_address);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const char* key = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        const auto* data = static_cast<const uint8_t*>(sqlite3_column_blob(statement, 1));
        const int size = sqlite3_column_bytes(statement, 1);
        if (key) result[key] = data && size > 0 ? std::vector<uint8_t>(data, data + size) : std::vector<uint8_t>{};
    }
    sqlite3_finalize(statement);
    return result;
}

bool mermai_db::save_contract_code(const std::string& contract_address, const std::vector<uint8_t>& code) {
    if (!db_connection || contract_address.empty() || code.empty()) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "INSERT INTO contracts(contract_address,code) VALUES(?,?) ON CONFLICT(contract_address) DO UPDATE SET code=excluded.code", -1, &statement, nullptr) != SQLITE_OK) return false;
    bind_text(statement, 1, contract_address);
    bind_blob(statement, 2, code);
    const bool result = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return result;
}

std::vector<uint8_t> mermai_db::get_contract_code(const std::string& contract_address) {
    if (!db_connection) return {};
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "SELECT code FROM contracts WHERE contract_address = ?", -1, &statement, nullptr) != SQLITE_OK) return {};
    bind_text(statement, 1, contract_address);
    std::vector<uint8_t> result;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* data = static_cast<const uint8_t*>(sqlite3_column_blob(statement, 0));
        const int size = sqlite3_column_bytes(statement, 0);
        if (data && size > 0) result.assign(data, data + size);
    }
    sqlite3_finalize(statement);
    return result;
}

// ========== MEMPOOL OPERATIONS ==========

bool mermai_db::add_to_mempool(const mermai_transaction& tx) {
    if (!db_connection) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "INSERT OR REPLACE INTO mempool(id,serialized,timestamp) VALUES(?,?,?)", -1, &statement, nullptr) != SQLITE_OK) return false;
    const auto serialized = tx.serialize();
    const std::string id = tx.id.empty() ? tx.get_hash() : tx.id;
    bind_text(statement, 1, id);
    bind_blob(statement, 2, serialized);
    sqlite3_bind_int64(statement, 3, static_cast<sqlite3_int64>(tx.timestamp));
    const bool result = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return result;
}

bool mermai_db::remove_from_mempool(const std::string& tx_id) {
    if (!db_connection) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "DELETE FROM mempool WHERE id = ?", -1, &statement, nullptr) != SQLITE_OK) return false;
    bind_text(statement, 1, tx_id);
    const bool result = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return result;
}

std::vector<mermai_transaction> mermai_db::get_mempool(uint32_t max_count) {
    std::vector<mermai_transaction> transactions;
    if (!db_connection) return transactions;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "SELECT serialized FROM mempool ORDER BY timestamp, id LIMIT ?", -1, &statement, nullptr) != SQLITE_OK) return transactions;
    sqlite3_bind_int(statement, 1, static_cast<int>(max_count));
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* data = static_cast<const char*>(sqlite3_column_blob(statement, 0));
        const int size = sqlite3_column_bytes(statement, 0);
        try {
            if (data && size > 0) transactions.push_back(mermai_transaction::deserialize({data, static_cast<size_t>(size)}));
        } catch (const std::exception&) {
        }
    }
    sqlite3_finalize(statement);
    return transactions;
}

void mermai_db::clear_mempool() {
    if (db_connection) execute(db_connection, "DELETE FROM mempool");
}

// ========== TRANSACTION HISTORY ==========

std::vector<mermai_transaction> mermai_db::get_account_transactions(
    const std::string& address,
    uint32_t limit
) {
    std::vector<mermai_transaction> transactions;
    if (!db_connection) return transactions;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "SELECT serialized FROM transactions ORDER BY timestamp DESC, id DESC", -1, &statement, nullptr) != SQLITE_OK) return transactions;
    while (sqlite3_step(statement) == SQLITE_ROW && transactions.size() < limit) {
        const auto* data = static_cast<const char*>(sqlite3_column_blob(statement, 0));
        const int size = sqlite3_column_bytes(statement, 0);
        try {
            if (!data || size <= 0) continue;
            mermai_transaction transaction = mermai_transaction::deserialize({data, static_cast<size_t>(size)});
            bool belongs_to_account = false;
            for (const auto& output : transaction.outputs) {
                if (output.address == address) {
                    belongs_to_account = true;
                    break;
                }
            }
            if (!belongs_to_account) {
                for (const auto& input : transaction.inputs) {
                    if (!input.public_key.empty() && address_from_public_key(input.public_key) == address) {
                        belongs_to_account = true;
                        break;
                    }
                }
            }
            if (belongs_to_account) transactions.push_back(std::move(transaction));
        } catch (const std::exception&) {
        }
    }
    sqlite3_finalize(statement);
    return transactions;
}

// ========== STATE SNAPSHOT ==========

bool mermai_db::save_state_root(uint32_t block_height, const std::string& state_root) {
    if (!db_connection) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "INSERT OR REPLACE INTO state_roots(block_height,state_root) VALUES(?,?)", -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(statement, 1, static_cast<int>(block_height));
    bind_text(statement, 2, state_root);
    const bool result = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return result;
}

std::string mermai_db::get_state_root(uint32_t block_height) {
    if (!db_connection) return "";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_connection, "SELECT state_root FROM state_roots WHERE block_height = ?", -1, &statement, nullptr) != SQLITE_OK) return "";
    sqlite3_bind_int(statement, 1, static_cast<int>(block_height));
    std::string result;
    if (sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_text(statement, 0)) result = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
    sqlite3_finalize(statement);
    return result;
}

// ========== UTILITY ==========

bool mermai_db::sync() {
    return db_connection && execute(db_connection, "PRAGMA wal_checkpoint(FULL)");
}

void mermai_db::backup(const std::string& backup_path) {
    if (!db_connection) return;
    sqlite3* backup_db = nullptr;
    if (sqlite3_open(backup_path.c_str(), &backup_db) != SQLITE_OK) return;
    sqlite3_backup* backup_handle = sqlite3_backup_init(backup_db, "main", db_connection, "main");
    if (backup_handle) {
        sqlite3_backup_step(backup_handle, -1);
        sqlite3_backup_finish(backup_handle);
    }
    sqlite3_close(backup_db);
}

uint64_t mermai_db::get_db_size() {
    std::error_code error;
    const auto size = std::filesystem::file_size(db_path, error);
    return error ? 0 : static_cast<uint64_t>(size);
}

// ============================================================================
// KEY-VALUE STORE IMPLEMENTATION
// ============================================================================

bool mermai_key_value_store::set(const std::string& key, const std::vector<uint8_t>& value) {
    store[key] = value;
    return true;
}

std::vector<uint8_t> mermai_key_value_store::get(const std::string& key) {
    auto it = store.find(key);
    if (it != store.end()) {
        return it->second;
    }
    return {};
}

bool mermai_key_value_store::exists(const std::string& key) const {
    return store.find(key) != store.end();
}

bool mermai_key_value_store::delete_key(const std::string& key) {
    return store.erase(key) > 0;
}

void mermai_key_value_store::clear() {
    store.clear();
}

// ============================================================================
// MERKLE TREE IMPLEMENTATION
// ============================================================================

std::string mermai_merkle_tree::calculate_tree_hash(const std::vector<mermai_transaction>& transactions) {
    if (transactions.empty()) {
        return "0000000000000000000000000000000000000000000000000000000000000000";
    }
    
    std::vector<std::string> level;
    for (const auto& transaction : transactions) level.push_back(transaction.get_hash());
    while (level.size() > 1) {
        std::vector<std::string> next_level;
        for (size_t index = 0; index < level.size(); index += 2) {
            const auto& right = index + 1 < level.size() ? level[index + 1] : level[index];
            next_level.push_back(hash_combine(level[index], right));
        }
        level = std::move(next_level);
    }
    return level.front();
}

std::string mermai_merkle_tree::verify_tree(
    const std::vector<mermai_transaction>& transactions,
    const std::string& expected_root
) {
    std::string calculated_root = calculate_tree_hash(transactions);
    return (calculated_root == expected_root) ? calculated_root : "";
}

std::string mermai_merkle_tree::hash_combine(const std::string& left, const std::string& right) {
    const std::string input = left + right;
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_CTX context;
    SHA256_Init(&context);
    SHA256_Update(&context, input.data(), input.size());
    SHA256_Final(digest, &context);
    std::ostringstream output;
    for (const auto byte : digest) output << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(byte);
    return output.str();
}

} // namespace mermai
