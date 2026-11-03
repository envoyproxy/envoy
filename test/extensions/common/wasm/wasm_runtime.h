#pragma once

#include "envoy/http/codec.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

// Sandbox runtimes compiled into this build (e.g. v8, wamr, wasmtime).
// first_sandbox_* uses the first sandbox runtime without NullVM; host_test_* adds NullVM.
std::vector<std::string> sandboxRuntimes();

// Guest languages testable on this platform; "cpp" requires x86_64 (Emscripten).
std::vector<std::string> languages(bool cpp_only);

// Testable runtime and language combinations.
std::vector<std::tuple<std::string, std::string>> wasmTestMatrix(bool include_nullvm, bool cpp_only,
                                                                 bool single_runtime = false);

std::vector<std::tuple<std::string, std::string, bool>>
wasmDualFilterTestMatrix(bool include_nullvm, bool cpp_only, bool single_runtime = false);

std::vector<std::tuple<std::string, std::string, bool, Http::CodecType>>
wasmDualFilterWithCodecsTestMatrix(bool include_nullvm, bool cpp_only,
                                   std::vector<Http::CodecType> codecs_type);

inline auto runtime_and_cpp_values = testing::ValuesIn(wasmTestMatrix(true, true));
inline auto host_test_runtime_and_cpp_values = testing::ValuesIn(wasmTestMatrix(true, true, true));
inline auto first_sandbox_runtime_and_cpp_values =
    testing::ValuesIn(wasmTestMatrix(false, true, true));
inline auto sandbox_runtime_and_cpp_values = testing::ValuesIn(wasmTestMatrix(false, true));
inline auto dual_filter_sandbox_runtime_and_cpp_values =
    testing::ValuesIn(wasmDualFilterTestMatrix(false, true));
inline auto dual_filter_first_sandbox_runtime_and_cpp_values =
    testing::ValuesIn(wasmDualFilterTestMatrix(false, true, true));
inline auto dual_filter_with_codecs_sandbox_runtime_and_cpp_values =
    testing::ValuesIn(wasmDualFilterWithCodecsTestMatrix(
        false, true, {Envoy::Http::CodecType::HTTP1, Envoy::Http::CodecType::HTTP2}));
inline auto runtime_and_language_values = testing::ValuesIn(wasmTestMatrix(true, false));
inline auto host_test_runtime_and_language_values =
    testing::ValuesIn(wasmTestMatrix(true, false, true));
inline auto first_sandbox_runtime_and_language_values =
    testing::ValuesIn(wasmTestMatrix(false, false, true));
inline auto sandbox_runtime_and_language_values = testing::ValuesIn(wasmTestMatrix(false, false));

std::string
wasmTestParamsToString(const ::testing::TestParamInfo<std::tuple<std::string, std::string>>& p);
std::string wasmDualFilterTestParamsToString(
    const ::testing::TestParamInfo<std::tuple<std::string, std::string, bool>>& p);
std::string wasmDualFilterWithCodecsTestParamsToString(
    const ::testing::TestParamInfo<std::tuple<std::string, std::string, bool, Http::CodecType>>& p);

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
