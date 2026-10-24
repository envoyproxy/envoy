#include "test/extensions/common/wasm/wasm_runtime.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

TEST(WasmRuntimeTest, HostTestMatrixUsesFirstAvailableSandbox) {
  std::vector<std::tuple<std::string, std::string>> expected;
  const auto runtimes = sandboxRuntimes();
  if (!runtimes.empty()) {
#if defined(__x86_64__)
    expected.emplace_back(runtimes.front(), "cpp");
#endif
    expected.emplace_back(runtimes.front(), "rust");
  }
  expected.emplace_back("null", "cpp");

  EXPECT_EQ(wasmTestMatrix(true, false, true), expected);
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
