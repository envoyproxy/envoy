#include "envoy/registry/registry.h"

#include "extensions/common/wasm/null/null_vm_plugin.h"
#include "extensions/common/wasm/wasm_vm.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace {

class TestNullVmPlugin : public Null::NullVmPlugin {
public:
  TestNullVmPlugin() = default;
  ~TestNullVmPlugin() override = default;

  MOCK_METHOD0(start, void());
};

class PluginFactory : public Null::NullVmPluginFactory {
public:
  PluginFactory() = default;

  const std::string name() const override { return "test_null_vm_plugin"; }
  std::unique_ptr<Null::NullVmPlugin> create() const override;
};

TestNullVmPlugin* test_null_vm_plugin_ = nullptr;
Envoy::Registry::RegisterFactory<PluginFactory, Null::NullVmPluginFactory> register_;

std::unique_ptr<Null::NullVmPlugin> PluginFactory::create() const {
  auto result = std::make_unique<TestNullVmPlugin>();
  test_null_vm_plugin_ = result.get();
  return result;
}

TEST(WasmVmTest, BadVmType) { EXPECT_THROW(createWasmVm("bad.vm"), WasmException); }

TEST(WasmVmTest, NullVmStartup) {
  auto wasm_vm = createWasmVm("envoy.wasm.vm.null");
  EXPECT_TRUE(wasm_vm != nullptr);
  EXPECT_TRUE(wasm_vm->cloneable());
  auto wasm_vm_clone = wasm_vm->clone();
  EXPECT_TRUE(wasm_vm_clone != nullptr);
  EXPECT_TRUE(wasm_vm->getUserSection("user").empty());
}

TEST(WasmVmTest, NullVmMemory) {
  auto wasm_vm = createWasmVm("envoy.wasm.vm.null");
  EXPECT_EQ(wasm_vm->getMemorySize(), std::numeric_limits<uint64_t>::max());
  std::string d = "data";
  auto m = wasm_vm->getMemory(reinterpret_cast<uint64_t>(d.data()), d.size()).value();
  EXPECT_EQ(m.data(), d.data());
  EXPECT_EQ(m.size(), d.size());
  uint64_t offset;
  char l;
  EXPECT_TRUE(wasm_vm->getMemoryOffset(&l, &offset));
  EXPECT_EQ(offset, reinterpret_cast<uint64_t>(&l));
  char c;
  char z = 'z';
  EXPECT_TRUE(wasm_vm->setMemory(reinterpret_cast<uint64_t>(&c), 1, &z));
  EXPECT_EQ(c, z);

  Word w(13);
  EXPECT_TRUE(
      wasm_vm->setWord(reinterpret_cast<uint64_t>(&w), std::numeric_limits<uint64_t>::max()));
  EXPECT_EQ(w.u64_, std::numeric_limits<uint64_t>::max());

  Word w2(0);
  w.u64_ = 7;
  EXPECT_TRUE(wasm_vm->getWord(reinterpret_cast<uint64_t>(&w), &w2));
  EXPECT_EQ(w2.u64_, 7);
}

TEST(WasmVmTest, NullVmStart) {
  auto wasm_vm = createWasmVm("envoy.wasm.vm.null");
  EXPECT_TRUE(wasm_vm->load("test_null_vm_plugin", true));
  wasm_vm->link("test", false);
  // Test that context argument to start is pushed and that the effective_context_id_ is reset.
  // Test that the original values are restored.
  Context* context1 = reinterpret_cast<Context*>(1);
  Context* context2 = reinterpret_cast<Context*>(2);
  current_context_ = context1;
  effective_context_id_ = 1;
  EXPECT_CALL(*test_null_vm_plugin_, start()).WillOnce(Invoke([context2]() {
    EXPECT_EQ(current_context_, context2);
    EXPECT_EQ(effective_context_id_, 0);
  }));
  wasm_vm->start(context2);
  EXPECT_EQ(current_context_, context1);
  EXPECT_EQ(effective_context_id_, 1);
}

} // namespace
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
