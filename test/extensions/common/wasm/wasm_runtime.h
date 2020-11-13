#pragma once

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

// All WASM runtimes.
std::vector<std::string> runtimes();

// All sandboxed WASM runtimes.
std::vector<std::string> sandbox_runtimes();

// Testable runtime and language combinations
std::vector<std::tuple<std::string, std::string>> runtimes_and_languages();

inline auto runtime_values = testing::ValuesIn(runtimes());
inline auto sandbox_runtime_values = testing::ValuesIn(sandbox_runtimes());
inline auto runtime_and_language_values = testing::ValuesIn(runtimes_and_languages());

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
