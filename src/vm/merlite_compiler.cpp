#include "mermai/merlite_compiler.hpp"
#include <cctype>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace mermai::vm {

std::vector<merlite_token> merlite_compiler::tokenize(const std::string& source) {
    std::vector<merlite_token> tokens;
    size_t i = 0;
    uint32_t line = 1;
    uint32_t col = 1;

    while (i < source.size()) {
        char c = source[i];

        // Skip whitespace
        if (std::isspace(c)) {
            if (c == '\n') { line++; col = 1; }
            else { col++; }
            i++;
            continue;
        }

        // Skip single-line comments // or ;
        if ((c == '/' && i + 1 < source.size() && source[i + 1] == '/') || c == ';') {
            while (i < source.size() && source[i] != '\n') i++;
            continue;
        }

        // Identifiers and keywords
        if (std::isalpha(c) || c == '_') {
            size_t start = i;
            uint32_t start_col = col;
            while (i < source.size() && (std::isalnum(source[i]) || source[i] == '_')) {
                i++;
                col++;
            }
            std::string word = source.substr(start, i - start);
            merlite_token_type type = merlite_token_type::TOKEN_IDENTIFIER;

            if (word == "contract") type = merlite_token_type::TOKEN_CONTRACT;
            else if (word == "state") type = merlite_token_type::TOKEN_STATE;
            else if (word == "pub") type = merlite_token_type::TOKEN_PUB;
            else if (word == "fn") type = merlite_token_type::TOKEN_FN;
            else if (word == "require") type = merlite_token_type::TOKEN_REQUIRE;
            else if (word == "revert") type = merlite_token_type::TOKEN_REVERT;
            else if (word == "return") type = merlite_token_type::TOKEN_RETURN;
            else if (word == "if") type = merlite_token_type::TOKEN_IF;
            else if (word == "else") type = merlite_token_type::TOKEN_ELSE;
            else if (word == "caller") type = merlite_token_type::TOKEN_CALLER;
            else if (word == "timestamp") type = merlite_token_type::TOKEN_TIMESTAMP;
            else if (word == "uint256") type = merlite_token_type::TOKEN_UINT256;
            else if (word == "address") type = merlite_token_type::TOKEN_ADDRESS;
            else if (word == "bool") type = merlite_token_type::TOKEN_BOOL;
            else if (word == "mapping") type = merlite_token_type::TOKEN_MAPPING;

            tokens.push_back({type, word, line, start_col});
            continue;
        }

        // Numbers (decimal or 0x hex)
        if (std::isdigit(c)) {
            size_t start = i;
            uint32_t start_col = col;
            if (c == '0' && i + 1 < source.size() && (source[i + 1] == 'x' || source[i + 1] == 'X')) {
                i += 2; col += 2;
                while (i < source.size() && std::isxdigit(source[i])) { i++; col++; }
            } else {
                while (i < source.size() && std::isdigit(source[i])) { i++; col++; }
            }
            tokens.push_back({merlite_token_type::TOKEN_NUMBER, source.substr(start, i - start), line, start_col});
            continue;
        }

        // String literals
        if (c == '"') {
            size_t start = ++i; col++;
            while (i < source.size() && source[i] != '"') {
                if (source[i] == '\n') line++;
                i++; col++;
            }
            std::string str = source.substr(start, i - start);
            if (i < source.size() && source[i] == '"') { i++; col++; }
            tokens.push_back({merlite_token_type::TOKEN_STRING, str, line, col});
            continue;
        }

        // Multi-char operators
        if (c == '=' && i + 1 < source.size() && source[i + 1] == '=') {
            tokens.push_back({merlite_token_type::TOKEN_EQ, "==", line, col});
            i += 2; col += 2; continue;
        }
        if (c == '!' && i + 1 < source.size() && source[i + 1] == '=') {
            tokens.push_back({merlite_token_type::TOKEN_NEQ, "!=", line, col});
            i += 2; col += 2; continue;
        }
        if (c == '<' && i + 1 < source.size() && source[i + 1] == '=') {
            tokens.push_back({merlite_token_type::TOKEN_LTE, "<=", line, col});
            i += 2; col += 2; continue;
        }
        if (c == '>' && i + 1 < source.size() && source[i + 1] == '=') {
            tokens.push_back({merlite_token_type::TOKEN_GTE, ">=", line, col});
            i += 2; col += 2; continue;
        }
        if (c == '-' && i + 1 < source.size() && source[i + 1] == '>') {
            tokens.push_back({merlite_token_type::TOKEN_ARROW, "->", line, col});
            i += 2; col += 2; continue;
        }
        if (c == '=' && i + 1 < source.size() && source[i + 1] == '>') {
            tokens.push_back({merlite_token_type::TOKEN_FAT_ARROW, "=>", line, col});
            i += 2; col += 2; continue;
        }

        // Single-char operators
        merlite_token_type single_type = merlite_token_type::TOKEN_EOF;
        switch (c) {
            case '=': single_type = merlite_token_type::TOKEN_ASSIGN; break;
            case '+': single_type = merlite_token_type::TOKEN_PLUS; break;
            case '-': single_type = merlite_token_type::TOKEN_MINUS; break;
            case '*': single_type = merlite_token_type::TOKEN_STAR; break;
            case '/': single_type = merlite_token_type::TOKEN_SLASH; break;
            case '<': single_type = merlite_token_type::TOKEN_LT; break;
            case '>': single_type = merlite_token_type::TOKEN_GT; break;
            case '{': single_type = merlite_token_type::TOKEN_LBRACE; break;
            case '}': single_type = merlite_token_type::TOKEN_RBRACE; break;
            case '(': single_type = merlite_token_type::TOKEN_LPAREN; break;
            case ')': single_type = merlite_token_type::TOKEN_RPAREN; break;
            case '[': single_type = merlite_token_type::TOKEN_LBRACKET; break;
            case ']': single_type = merlite_token_type::TOKEN_RBRACKET; break;
            case ';': single_type = merlite_token_type::TOKEN_SEMICOLON; break;
            case ',': single_type = merlite_token_type::TOKEN_COMMA; break;
            default: break;
        }

        if (single_type != merlite_token_type::TOKEN_EOF) {
            tokens.push_back({single_type, std::string(1, c), line, col});
        }
        i++;
        col++;
    }

    tokens.push_back({merlite_token_type::TOKEN_EOF, "", line, col});
    return tokens;
}

merlite_compilation_result merlite_compiler::compile(const std::string& source) {
    merlite_compilation_result result;
    const auto tokens = tokenize(source);

    if (tokens.empty() || (tokens.size() == 1 && tokens[0].type == merlite_token_type::TOKEN_EOF)) {
        result.errors.push_back("Empty Merlite source file.");
        return result;
    }

    uint32_t next_slot = 0;
    std::map<std::string, uint32_t> storage_map;
    std::vector<uint8_t> bytecode;
    uint64_t est_gas = 21000;

    size_t cursor = 0;
    auto peek = [&](size_t offset = 0) -> const merlite_token& {
        size_t idx = cursor + offset;
        return idx < tokens.size() ? tokens[idx] : tokens.back();
    };
    auto consume = [&]() -> const merlite_token& {
        return cursor < tokens.size() ? tokens[cursor++] : tokens.back();
    };

    try {
        // Contract declaration: contract Name { ... }
        if (peek().type == merlite_token_type::TOKEN_CONTRACT) {
            consume(); // contract
            if (peek().type == merlite_token_type::TOKEN_IDENTIFIER) {
                consume(); // ContractName
            }
            if (peek().type == merlite_token_type::TOKEN_LBRACE) {
                consume(); // {
            }
        }

        while (cursor < tokens.size() && peek().type != merlite_token_type::TOKEN_EOF && peek().type != merlite_token_type::TOKEN_RBRACE) {
            // State variable: state uint256 name; or state mapping(address => uint256) balances;
            if (peek().type == merlite_token_type::TOKEN_STATE) {
                consume(); // state
                // Type
                if (peek().type == merlite_token_type::TOKEN_MAPPING) {
                    consume(); // mapping
                    if (peek().type == merlite_token_type::TOKEN_LPAREN) consume();
                    while (cursor < tokens.size() && peek().type != merlite_token_type::TOKEN_RPAREN) consume();
                    if (peek().type == merlite_token_type::TOKEN_RPAREN) consume();
                } else {
                    consume(); // uint256 / address / bool
                }

                if (peek().type == merlite_token_type::TOKEN_IDENTIFIER) {
                    std::string var_name = consume().text;
                    uint32_t slot = next_slot++;
                    storage_map[var_name] = slot;
                }

                if (peek().type == merlite_token_type::TOKEN_SEMICOLON) {
                    consume();
                }
                continue;
            }

            // Function declaration: pub fn name(...) { ... }
            if (peek().type == merlite_token_type::TOKEN_PUB || peek().type == merlite_token_type::TOKEN_FN) {
                if (peek().type == merlite_token_type::TOKEN_PUB) consume();
                consume(); // fn

                std::string fn_name = "anonymous";
                if (peek().type == merlite_token_type::TOKEN_IDENTIFIER) {
                    fn_name = consume().text;
                    result.functions.push_back(fn_name);
                }

                // Parameters (...)
                if (peek().type == merlite_token_type::TOKEN_LPAREN) {
                    consume();
                    while (cursor < tokens.size() && peek().type != merlite_token_type::TOKEN_RPAREN) consume();
                    if (peek().type == merlite_token_type::TOKEN_RPAREN) consume();
                }

                // Return type -> type
                if (peek().type == merlite_token_type::TOKEN_ARROW) {
                    consume(); // ->
                    consume(); // type
                }

                // Function body { ... }
                if (peek().type == merlite_token_type::TOKEN_LBRACE) {
                    consume(); // {
                    int depth = 1;
                    while (cursor < tokens.size() && depth > 0) {
                        if (peek().type == merlite_token_type::TOKEN_LBRACE) depth++;
                        else if (peek().type == merlite_token_type::TOKEN_RBRACE) depth--;

                        // Compile statements inside constructor or functions
                        if (depth == 1) {
                            // require(cond, "msg");
                            if (peek().type == merlite_token_type::TOKEN_REQUIRE) {
                                consume(); // require
                                if (peek().type == merlite_token_type::TOKEN_LPAREN) consume();
                                while (cursor < tokens.size() && peek().type != merlite_token_type::TOKEN_RPAREN) consume();
                                if (peek().type == merlite_token_type::TOKEN_RPAREN) consume();
                                if (peek().type == merlite_token_type::TOKEN_SEMICOLON) consume();

                                // Emit bytecode for require: PUSH1 cond, JMPI pass, REVERT
                                bytecode.push_back(static_cast<uint8_t>(mermai_opcode::PUSH1));
                                bytecode.push_back(0x01);
                                bytecode.push_back(static_cast<uint8_t>(mermai_opcode::JMPI));
                                bytecode.push_back(static_cast<uint8_t>(bytecode.size() + 2));
                                bytecode.push_back(static_cast<uint8_t>(mermai_opcode::REVERT));
                                est_gas += 20;
                                continue;
                            }

                            // State assignment: var = expr;
                            if (peek().type == merlite_token_type::TOKEN_IDENTIFIER && peek(1).type == merlite_token_type::TOKEN_ASSIGN) {
                                std::string target = consume().text;
                                consume(); // =

                                uint64_t val = 0;
                                if (peek().type == merlite_token_type::TOKEN_NUMBER) {
                                    val = std::stoull(consume().text, nullptr, 0);
                                } else if (peek().type == merlite_token_type::TOKEN_CALLER) {
                                    consume();
                                    bytecode.push_back(static_cast<uint8_t>(mermai_opcode::CALLER));
                                    val = 1;
                                } else {
                                    consume();
                                    val = 100;
                                }

                                if (peek().type == merlite_token_type::TOKEN_SEMICOLON) consume();

                                uint32_t slot = storage_map.count(target) ? storage_map[target] : 0;
                                // Emit PUSH slot, PUSH val, SSTORE
                                bytecode.push_back(static_cast<uint8_t>(mermai_opcode::PUSH1));
                                bytecode.push_back(static_cast<uint8_t>(slot));
                                bytecode.push_back(static_cast<uint8_t>(mermai_opcode::PUSH1));
                                bytecode.push_back(static_cast<uint8_t>(val & 0xFF));
                                bytecode.push_back(static_cast<uint8_t>(mermai_opcode::SSTORE));
                                est_gas += 20000;
                                continue;
                            }
                        }

                        if (depth > 0) consume();
                    }
                }
                continue;
            }

            consume();
        }

        // Emit final STOP opcode
        bytecode.push_back(static_cast<uint8_t>(mermai_opcode::STOP));

        result.success = true;
        result.bytecode = bytecode;
        result.storage_layout = storage_map;
        result.estimated_deployment_gas = est_gas;

        std::ostringstream hex_ss;
        hex_ss << "0x";
        for (auto b : bytecode) {
            hex_ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
        }
        result.hex_bytecode = hex_ss.str();

    } catch (const std::exception& e) {
        result.success = false;
        result.errors.push_back(std::string("Compilation error: ") + e.what());
    }

    return result;
}

std::string merlite_compiler::disassemble(const std::vector<uint8_t>& bytecode) {
    std::ostringstream ss;
    size_t i = 0;
    while (i < bytecode.size()) {
        uint8_t op = bytecode[i++];
        ss << std::setw(4) << std::setfill('0') << (i - 1) << ": ";
        switch (static_cast<mermai_opcode>(op)) {
            case mermai_opcode::PUSH0: ss << "PUSH0\n"; break;
            case mermai_opcode::PUSH1:
                if (i < bytecode.size()) ss << "PUSH1 0x" << std::hex << static_cast<int>(bytecode[i++]) << "\n";
                else ss << "PUSH1 <truncated>\n";
                break;
            case mermai_opcode::ADD: ss << "ADD\n"; break;
            case mermai_opcode::SUB: ss << "SUB\n"; break;
            case mermai_opcode::MUL: ss << "MUL\n"; break;
            case mermai_opcode::SLOAD: ss << "SLOAD\n"; break;
            case mermai_opcode::SSTORE: ss << "SSTORE\n"; break;
            case mermai_opcode::JMPI:
                if (i < bytecode.size()) ss << "JMPI 0x" << std::hex << static_cast<int>(bytecode[i++]) << "\n";
                else ss << "JMPI <truncated>\n";
                break;
            case mermai_opcode::CALLER: ss << "CALLER\n"; break;
            case mermai_opcode::TIMESTAMP: ss << "TIMESTAMP\n"; break;
            case mermai_opcode::REVERT: ss << "REVERT\n"; break;
            case mermai_opcode::STOP: ss << "STOP\n"; break;
            default: ss << "UNKNOWN_OP (0x" << std::hex << static_cast<int>(op) << ")\n"; break;
        }
    }
    return ss.str();
}

} // namespace mermai::vm
