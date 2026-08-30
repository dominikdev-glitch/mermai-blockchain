cmake_minimum_required(VERSION 3.20)
project(mermai-blockchain VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Security and Optimization Flags
if (MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /UNDEBUG)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -UNDEBUG)
endif()


function(mermai_harden_target target_name)
  if (MSVC)
    target_compile_options(${target_name} PRIVATE /guard:cf /GS)
    target_link_options(${target_name} PRIVATE /GUARD:CF /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)
  elseif(WIN32)
    target_compile_options(${target_name} PRIVATE -fstack-protector-strong)
  else()
    target_compile_options(${target_name} PRIVATE -fstack-protector-strong)
    target_link_options(${target_name} PRIVATE -Wl,-z,relro,-z,now)
  endif()
endfunction()

# Dependencies
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

# Source files
file(GLOB_RECURSE CORE_SOURCES "src/core/*.cpp")
file(GLOB_RECURSE CONSENSUS_SOURCES "src/consensus/*.cpp")
file(GLOB_RECURSE VM_SOURCES "src/vm/*.cpp")
file(GLOB_RECURSE NETWORK_SOURCES "src/network/*.cpp")
file(GLOB_RECURSE DB_SOURCES "src/db/*.cpp")
file(GLOB_RECURSE RPC_SOURCES "src/rpc/*.cpp")
file(GLOB_RECURSE CRYPTO_SOURCES "src/crypto/*.cpp")

add_library(mermai_core 
  ${CORE_SOURCES}
  ${CONSENSUS_SOURCES}
  ${VM_SOURCES}
  ${NETWORK_SOURCES}
  ${DB_SOURCES}
  ${RPC_SOURCES}
  ${CRYPTO_SOURCES}
)

target_include_directories(mermai_core PUBLIC 
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mermai_core PUBLIC 
  OpenSSL::Crypto
  OpenSSL::SSL
  SQLite3::SQLite3
  Threads::Threads
)
mermai_harden_target(mermai_core)
if(WIN32)
  target_link_libraries(mermai_core PUBLIC ws2_32)
endif()

# Node executable
add_executable(mermai-node src/mermai_main.cpp)
target_link_libraries(mermai-node PRIVATE mermai_core)
mermai_harden_target(mermai-node)

add_executable(mermai-cli src/cli/mermai_wallet.cpp)
target_link_libraries(mermai-cli PRIVATE mermai_core)
mermai_harden_target(mermai-cli)

# Tests
enable_testing()
file(GLOB_RECURSE TEST_SOURCES "tests/*.cpp")
foreach(TEST_SOURCE ${TEST_SOURCES})
  get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
  add_executable(${TEST_NAME} ${TEST_SOURCE})
  target_link_libraries(${TEST_NAME} PRIVATE mermai_core)
  mermai_harden_target(${TEST_NAME})
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

# Installation
install(TARGETS mermai-node mermai-cli RUNTIME DESTINATION bin)
