#include <algorithm>

#include "test/extensions/common/wasm/wasm_runtime.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

TEST(WasmRuntimeTest, V8IsFirstSandboxRuntimeWhenAvailable) {
  const auto runtimes = sandboxRuntimes();
  if (std::find(runtimes.begin(), runtimes.end(), "v8") == runtimes.end()) {
    GTEST_SKIP() << "V8 not compiled in";
  }
  EXPECT_EQ(runtimes.front(), "v8");
}

TEST(WasmRuntimeTest, HostTestMatrixUsesFirstAvailableSandbox) {
  std::vector<std::tuple<std::string, std::string>> expected;
  const auto runtimes = sandboxRuntimes();
  if (!runtimes.empty()) {
    for (const auto& language : languages(false)) {
      expected.emplace_back(runtimes.front(), language);
    }
  }
  expected.emplace_back("null", "cpp");

  EXPECT_EQ(wasmTestMatrix(true, false, true), expected);
}

TEST(WasmRuntimeTest, FirstSandboxCppMatrixExcludesNullVM) {
  const auto matrix = wasmTestMatrix(false, true, true);
  EXPECT_TRUE(std::none_of(matrix.begin(), matrix.end(),
                           [](const auto& value) { return std::get<0>(value) == "null"; }));
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
