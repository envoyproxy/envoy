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
#if defined(ENVOY_WASM_WASMTIME)
  runtimes.push_back("wasmtime");
#endif
  return runtimes;
}

std::vector<std::tuple<std::string, std::string>> runtimesAndLanguages() {
  std::vector<std::tuple<std::string, std::string>> values;
  for (const auto& runtime : sandboxRuntimes()) {
    values.push_back(std::make_tuple(runtime, "cpp"));
    values.push_back(std::make_tuple(runtime, "rust"));
  }
  values.push_back(std::make_tuple("null", "cpp"));
  return values;
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
