#include "envoy/registry/registry.h"

#include "source/extensions/common/wasm/wasm_runtime_factory.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class FakeRuntimeFactory : public WasmRuntimeFactory {
public:
  WasmVmPtr createWasmVm() override { return nullptr; }
  std::string name() const override { return "envoy.wasm.runtime.mock"; }
};

TEST(WasmRuntimeFactoryTest, DestructFactory) {
  EXPECT_NO_THROW({
    auto factory = new FakeRuntimeFactory();
    delete factory;
  });
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
