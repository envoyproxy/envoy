#include "test/extensions/common/wasm/wasm_runtime.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

std::vector<std::string> sandboxRuntimes() {
  std::vector<std::string> runtimes;
#if defined(PROXY_WASM_HAS_RUNTIME_V8)
  runtimes.push_back("v8");
#endif
#if defined(PROXY_WASM_HAS_RUNTIME_WAMR)
  runtimes.push_back("wamr");
#endif
#if defined(PROXY_WASM_HAS_RUNTIME_WASMTIME)
  runtimes.push_back("wasmtime");
#endif
  return runtimes;
}

std::vector<std::string> languages(bool cpp_only) {
  std::vector<std::string> languages;
#if defined(__x86_64__)
  // TODO(PiotrSikora): Emscripten ships binaries only for x86_64.
  languages.push_back("cpp");
#endif
  if (!cpp_only) {
    languages.push_back("rust");
  }
  return languages;
}

std::vector<std::tuple<std::string, std::string>> wasmTestMatrix(bool include_nullvm,
                                                                 bool cpp_only) {
  std::vector<std::tuple<std::string, std::string>> values;
  for (const auto& runtime : sandboxRuntimes()) {
    for (const auto& language : languages(cpp_only)) {
      values.push_back(std::make_tuple(runtime, language));
    }
  }
  if (include_nullvm) {
    values.push_back(std::make_tuple("null", "cpp"));
  }
  return values;
}

std::vector<std::tuple<std::string, std::string, bool>>
wasmDualFilterTestMatrix(bool include_nullvm, bool cpp_only) {
  std::vector<std::tuple<std::string, std::string, bool>> values;
  for (const auto& p : Envoy::Extensions::Common::Wasm::wasmTestMatrix(include_nullvm, cpp_only)) {
    values.push_back(std::make_tuple(std::get<0>(p), std::get<1>(p), true));
    values.push_back(std::make_tuple(std::get<0>(p), std::get<1>(p), false));
  }
  return values;
}

std::vector<std::tuple<std::string, std::string, bool, Http::CodecType>>
wasmDualFilterWithCodecsTestMatrix(bool include_nullvm, bool cpp_only,
                                   std::vector<Http::CodecType> codecs_type) {
  std::vector<std::tuple<std::string, std::string, bool, Http::CodecType>> values;
  for (const Http::CodecType codec_type : codecs_type) {
    for (const auto& [runtime, language] :
         Envoy::Extensions::Common::Wasm::wasmTestMatrix(include_nullvm, cpp_only)) {
      values.push_back(std::make_tuple(runtime, language, true, codec_type));
      values.push_back(std::make_tuple(runtime, language, false, codec_type));
    }
  }
  return values;
}

std::string
wasmTestParamsToString(const ::testing::TestParamInfo<std::tuple<std::string, std::string>>& p) {
  return std::get<0>(p.param) + "_" + std::get<1>(p.param);
}

std::string wasmDualFilterTestParamsToString(
    const ::testing::TestParamInfo<std::tuple<std::string, std::string, bool>>& p) {
  auto [runtime, language, direction] = p.param;
  return fmt::format("{}_{}_{}", direction ? "downstream" : "upstream", runtime, language);
}

static std::string codecToString(const Http::CodecType& e) {
  switch (e) {
  case Http::CodecType::HTTP1:
    return "http1";
  case Http::CodecType::HTTP2:
    return "http2";
  case Http::CodecType::HTTP3:
    return "http3";
  default:
    break;
  }
  return "Unknown";
}

std::string wasmDualFilterWithCodecsTestParamsToString(
    const ::testing::TestParamInfo<std::tuple<std::string, std::string, bool, Http::CodecType>>&
        p) {
  auto [runtime, language, direction, codec_type] = p.param;
  return fmt::format("{}_{}_{}_{}", direction ? "downstream" : "upstream", runtime, language,
                     codecToString(codec_type));
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
