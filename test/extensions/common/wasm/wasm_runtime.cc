#include "test/extensions/common/wasm/wasm_runtime.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

std::vector<std::string> runtimes() {
  std::vector<std::string> runtimes = sandboxRuntimes();
  runtimes.push_back("null");
  return runtimes;
}

std::vector<std::string> sandboxRuntimes() {
  std::vector<std::string> runtimes;
#if defined(ENVOY_WASM_V8)
  runtimes.push_back("v8");
#endif
#if defined(ENVOY_WASM_WAVM)
  runtimes.push_back("wavm");
#endif
#if defined(ENVOY_WASM_WAMR)
  runtimes.push_back("wamr");
#endif
#if defined(ENVOY_WASM_WASMTIME)
  runtimes.push_back("wasmtime");
#endif
  return runtimes;
}

std::vector<std::string> languages() {
  std::vector<std::string> languages;
#if !defined(__aarch64__)
  // TODO(PiotrSikora): There are no Emscripten releases for arm64.
  languages.push_back("cpp");
#endif
  languages.push_back("rust");
  return languages;
}

std::vector<std::tuple<std::string, std::string>> runtimesAndLanguages() {
  std::vector<std::tuple<std::string, std::string>> values;
  for (const auto& runtime : sandboxRuntimes()) {
    for (const auto& language : languages()) {
      values.push_back(std::make_tuple(runtime, language));
    }
  }
  values.push_back(std::make_tuple("null", "cpp"));
  return values;
}

std::string wasmTestParamsToString(const ::testing::TestParamInfo<std::string>& p) {
  return p.param;
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
