#pragma once

#include "gtest/gtest.h"
#include "include/proxy-wasm/wasm.h"

namespace proxy_wasm {

// gcc 9 is unable to resolve overloads of gtest built-in value printers for proxy_wasm::WasmResult
// most likely because it is `enum class`.
// As a workaround add custom printer to solve this problem. This change should work fine for clang
// or cl compilers as well.
inline void PrintTo(const proxy_wasm::WasmResult& result, std::ostream* os) {
  *os << static_cast<uint32_t>(result); // whatever needed to print bar to os
}

} // namespace proxy_wasm

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

// All Wasm runtimes.
std::vector<std::string> runtimes();

// All sandboxed Wasm runtimes.
std::vector<std::string> sandboxRuntimes();

// All programming languages used in Wasm tests.
std::vector<std::string> languages();

// Testable runtime and language combinations
std::vector<std::tuple<std::string, std::string>> runtimesAndLanguages();

inline auto runtime_values = testing::ValuesIn(runtimes());
inline auto sandbox_runtime_values = testing::ValuesIn(sandboxRuntimes());
inline auto language_values = testing::ValuesIn(languages());
inline auto runtime_and_language_values = testing::ValuesIn(runtimesAndLanguages());

std::string wasmTestParamsToString(const ::testing::TestParamInfo<std::string>& p);

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
