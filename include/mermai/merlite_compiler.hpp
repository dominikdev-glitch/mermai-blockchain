#ifndef MERMAI_MERLITE_COMPILER_HPP
#define MERMAI_MERLITE_COMPILER_HPP

#include "mermai/mermai_vm.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>

namespace mermai::vm {

enum class merlite_token_type {
    TOKEN_CONTRACT,
    TOKEN_STATE,
    TOKEN_PUB,
    TOKEN_FN,
    TOKEN_REQUIRE,
    TOKEN_REVERT,
    TOKEN_RETURN,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_CALLER,
    TOKEN_TIMESTAMP,
    TOKEN_UINT256,
    TOKEN_ADDRESS,
    TOKEN_BOOL,
    TOKEN_MAPPING,
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_ASSIGN,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_EQ,
    TOKEN_NEQ,
    TOKEN_LT,
    TOKEN_GT,
    TOKEN_LTE,
    TOKEN_GTE,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_SEMICOLON,
    TOKEN_COMMA,
    TOKEN_ARROW,
    TOKEN_FAT_ARROW,
    TOKEN_EOF
};

struct merlite_token {
    merlite_token_type type;
    std::string text;
    uint32_t line;
    uint32_t column;
};

struct merlite_compilation_result {
    bool success = false;
    std::vector<uint8_t> bytecode;
    std::string hex_bytecode;
    std::vector<std::string> errors;
    std::map<std::string, uint32_t> storage_layout;
    std::vector<std::string> functions;
    uint64_t estimated_deployment_gas = 0;
};

class merlite_compiler {
public:
    static std::vector<merlite_token> tokenize(const std::string& source);
    static merlite_compilation_result compile(const std::string& source);
    static std::string disassemble(const std::vector<uint8_t>& bytecode);
};

} // namespace mermai::vm

#endif // MERMAI_MERLITE_COMPILER_HPP
