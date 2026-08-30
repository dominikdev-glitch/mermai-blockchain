#include "mermai/merlite_compiler.hpp"
#include "mermai/mermai_vm.hpp"
#include <cassert>
#include <iostream>

int main() {
    std::cout << "Running merlite_compiler_test..." << std::endl;

    std::string merlite_source = R"(
        contract MerliteToken {
            state uint256 totalSupply;
            state address owner;
            state mapping(address => uint256) balances;

            pub fn constructor() {
                owner = caller;
                totalSupply = 1000000;
            }

            pub fn transfer(to: address, amount: uint256) -> bool {
                require(amount > 0, "Invalid amount");
                return true;
            }
        }
    )";

    auto result = mermai::vm::merlite_compiler::compile(merlite_source);
    assert(result.success);
    assert(!result.bytecode.empty());
    assert(result.storage_layout.count("totalSupply") > 0);
    assert(result.storage_layout.count("owner") > 0);

    std::cout << "  [PASS] Merlite source compiled successfully!" << std::endl;
    std::cout << "  Bytecode: " << result.hex_bytecode << std::endl;
    std::cout << "  Storage Slots allocated: " << result.storage_layout.size() << std::endl;
    std::cout << "  Estimated deployment gas: " << result.estimated_deployment_gas << std::endl;

    // Test execution of compiled Merlite bytecode inside MermaiVM
    mermai::vm::mermai_execution_context ctx;
    ctx.gas_limit = 1000000;
    ctx.caller_address = "mrm_alice_val1";
    ctx.contract_address = "0x1234567890abcdef1234567890abcdef12345678";

    mermai::vm::mermai_vm vm_instance(result.bytecode, ctx);
    assert(vm_instance.execute());
    std::cout << "  [PASS] Compiled Merlite bytecode executed deterministically in MermaiVM!" << std::endl;

    std::cout << "\nmerlite_compiler_test PASSED 100%!" << std::endl;
    return 0;
}
