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

std::string
wasmTestParamsToString(const ::testing::TestParamInfo<std::tuple<std::string, std::string>>& p) {
  return std::get<0>(p.param) + "_" + std::get<1>(p.param);
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
