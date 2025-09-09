#pragma once

#include "envoy/http/codec.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

// Testable runtime and language combinations.
std::vector<std::tuple<std::string, std::string>> wasmTestMatrix(bool include_nullvm,
                                                                 bool cpp_only);

std::vector<std::tuple<std::string, std::string, bool>>
wasmDualFilterTestMatrix(bool include_nullvm, bool cpp_only);

std::vector<std::tuple<std::string, std::string, bool, Http::CodecType>>
wasmDualFilterWithCodecsTestMatrix(bool include_nullvm, bool cpp_only,
                                   std::vector<Http::CodecType> codecs_type);

inline auto runtime_and_cpp_values = testing::ValuesIn(wasmTestMatrix(true, true));
inline auto sandbox_runtime_and_cpp_values = testing::ValuesIn(wasmTestMatrix(false, true));
inline auto dual_filter_sandbox_runtime_and_cpp_values =
    testing::ValuesIn(wasmDualFilterTestMatrix(false, true));
inline auto dual_filter_with_codecs_sandbox_runtime_and_cpp_values =
    testing::ValuesIn(wasmDualFilterWithCodecsTestMatrix(
        false, true, {Envoy::Http::CodecType::HTTP1, Envoy::Http::CodecType::HTTP2}));
inline auto runtime_and_language_values = testing::ValuesIn(wasmTestMatrix(true, false));
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
