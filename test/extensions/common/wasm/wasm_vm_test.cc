#include "envoy/registry/registry.h"

#include "extensions/common/wasm/null/null_vm_plugin.h"
#include "extensions/common/wasm/wasm_vm.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;

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

TEST(BadVmTest, NoRuntime) {
  EXPECT_THROW_WITH_MESSAGE(createWasmVm(""), WasmVmException,
                            "Failed to create WASM VM with unspecified runtime.");
}

TEST(BadVmTest, BadRuntime) {
  EXPECT_THROW_WITH_MESSAGE(createWasmVm("envoy.wasm.runtime.invalid"), WasmVmException,
                            "Failed to create WASM VM using envoy.wasm.runtime.invalid runtime. "
                            "Envoy was compiled without support for it.");
}

TEST(NullVmTest, NullVmStartup) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.null");
  EXPECT_TRUE(wasm_vm != nullptr);
  EXPECT_TRUE(wasm_vm->cloneable());
  auto wasm_vm_clone = wasm_vm->clone();
  EXPECT_TRUE(wasm_vm_clone != nullptr);
  EXPECT_TRUE(wasm_vm->getUserSection("user").empty());
}

TEST(NullVmTest, NullVmMemory) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.null");
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

TEST(NullVmTest, NullVmStart) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.null");
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

class MockHostFunctions {
public:
  MOCK_CONST_METHOD0(ping, void());
};

MockHostFunctions* g_host_functions;

void ping(void*) { g_host_functions->ping(); }

class WasmVmTest : public testing::Test {
public:
  virtual void SetUp() { g_host_functions = new MockHostFunctions(); }
  virtual void TearDown() { delete g_host_functions; }
};

TEST_F(WasmVmTest, V8BadCode) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.v8");
  ASSERT_TRUE(wasm_vm != nullptr);

  EXPECT_FALSE(wasm_vm->load("bad code", false));
}

TEST_F(WasmVmTest, V8Code) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.v8");
  ASSERT_TRUE(wasm_vm != nullptr);
  EXPECT_FALSE(wasm_vm->cloneable());

  auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_rust.wasm"));
  EXPECT_TRUE(wasm_vm->load(code, false));
  EXPECT_THAT(wasm_vm->getUserSection("producers"), HasSubstr("rustc"));
  EXPECT_TRUE(wasm_vm->getUserSection("emscripten_metadata").empty());
}

TEST_F(WasmVmTest, V8MissingHostFunction) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.v8");
  ASSERT_TRUE(wasm_vm != nullptr);

  auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_rust.wasm"));
  EXPECT_TRUE(wasm_vm->load(code, false));

  EXPECT_THROW_WITH_MESSAGE(wasm_vm->link("test", false), WasmVmException,
                            "Failed to load WASM module due to a missing import: env.ping");
}

TEST_F(WasmVmTest, V8FunctionCalls) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.v8");
  ASSERT_TRUE(wasm_vm != nullptr);

  auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_rust.wasm"));
  EXPECT_TRUE(wasm_vm->load(code, false));

  wasm_vm->registerCallback("env", "ping", ping, &ping);
  wasm_vm->link("test", false);

  EXPECT_CALL(*g_host_functions, ping());
  wasm_vm->start(nullptr /* no context */);

  WasmCallWord<3> sum;
  wasm_vm->getFunction("sum", &sum);
  Word word = sum(nullptr /* no context */, 13, 14, 15);
  EXPECT_EQ(42, word.u64_);
}

TEST_F(WasmVmTest, V8Memory) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.v8");
  ASSERT_TRUE(wasm_vm != nullptr);

  auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_rust.wasm"));
  EXPECT_TRUE(wasm_vm->load(code, false));

  wasm_vm->registerCallback("env", "ping", ping, &ping);
  wasm_vm->link("test", false);

  EXPECT_EQ(wasm_vm->getMemorySize(), 65536 /* stack size requested at the build-time */);

  const uint64_t test_addr = 128;

  std::string set = "test";
  EXPECT_TRUE(wasm_vm->setMemory(test_addr, set.size(), set.data()));
  auto got = wasm_vm->getMemory(test_addr, set.size()).value();
  EXPECT_EQ(sizeof("test") - 1, got.size());
  EXPECT_STREQ("test", got.data());

  Word word(0);
  EXPECT_TRUE(wasm_vm->setWord(test_addr, std::numeric_limits<uint32_t>::max()));
  EXPECT_TRUE(wasm_vm->getWord(test_addr, &word));
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(), word.u64_);
}

} // namespace
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
