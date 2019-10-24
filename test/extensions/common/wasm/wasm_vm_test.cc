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
  EXPECT_TRUE(wasm_vm->runtime() == "envoy.wasm.runtime.null");
  EXPECT_TRUE(wasm_vm->cloneable());
  auto wasm_vm_clone = wasm_vm->clone();
  EXPECT_TRUE(wasm_vm_clone != nullptr);
  EXPECT_TRUE(wasm_vm->getCustomSection("user").empty());
}

TEST(NullVmTest, NullVmMemory) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.null");
  EXPECT_EQ(wasm_vm->getMemorySize(), std::numeric_limits<uint64_t>::max());
  std::string d = "data";
  auto m = wasm_vm->getMemory(reinterpret_cast<uint64_t>(d.data()), d.size()).value();
  EXPECT_EQ(m.data(), d.data());
  EXPECT_EQ(m.size(), d.size());
  EXPECT_FALSE(wasm_vm->getMemory(0 /* nullptr */, 1 /* size */).has_value());

  uint64_t offset;
  char l;
  EXPECT_TRUE(wasm_vm->getMemoryOffset(&l, &offset));
  EXPECT_EQ(offset, reinterpret_cast<uint64_t>(&l));

  char c;
  char z = 'z';
  EXPECT_TRUE(wasm_vm->setMemory(reinterpret_cast<uint64_t>(&c), 1, &z));
  EXPECT_EQ(c, z);
  EXPECT_TRUE(wasm_vm->setMemory(0 /* nullptr */, 0 /* size */, nullptr));
  EXPECT_FALSE(wasm_vm->setMemory(0 /* nullptr */, 1 /* size */, nullptr));

  Word w(13);
  EXPECT_TRUE(
      wasm_vm->setWord(reinterpret_cast<uint64_t>(&w), std::numeric_limits<uint64_t>::max()));
  EXPECT_EQ(w.u64_, std::numeric_limits<uint64_t>::max());
  EXPECT_FALSE(wasm_vm->setWord(0 /* nullptr */, 1));

  Word w2(0);
  w.u64_ = 7;
  EXPECT_TRUE(wasm_vm->getWord(reinterpret_cast<uint64_t>(&w), &w2));
  EXPECT_EQ(w2.u64_, 7);
  EXPECT_FALSE(wasm_vm->getWord(0 /* nullptr */, &w2));
}

class MockHostFunctions {
public:
  MOCK_CONST_METHOD0(ping, void());
};

MockHostFunctions* g_host_functions;

void ping(void*) { g_host_functions->ping(); }

class WasmVmTest : public testing::Test {
public:
  void SetUp() override { g_host_functions = new MockHostFunctions(); }
  void TearDown() override { delete g_host_functions; }
};

TEST_F(WasmVmTest, V8BadCode) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.v8");
  ASSERT_TRUE(wasm_vm != nullptr);

  EXPECT_FALSE(wasm_vm->load("bad code", false));
}

TEST_F(WasmVmTest, V8Code) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.v8");
  ASSERT_TRUE(wasm_vm != nullptr);

  EXPECT_TRUE(wasm_vm->runtime() == "envoy.wasm.runtime.v8");
  EXPECT_FALSE(wasm_vm->cloneable());

  auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_rust.wasm"));
  EXPECT_TRUE(wasm_vm->load(code, false));
  EXPECT_THAT(wasm_vm->getCustomSection("producers"), HasSubstr("rustc"));
  EXPECT_TRUE(wasm_vm->getCustomSection("emscripten_metadata").empty());
}

TEST_F(WasmVmTest, V8MissingHostFunction) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.v8");
  ASSERT_TRUE(wasm_vm != nullptr);

  auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_rust.wasm"));
  EXPECT_TRUE(wasm_vm->load(code, false));

  EXPECT_THROW_WITH_MESSAGE(wasm_vm->link("test"), WasmVmException,
                            "Failed to load WASM module due to a missing import: env.ping");
}

TEST_F(WasmVmTest, V8FunctionCalls) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.v8");
  ASSERT_TRUE(wasm_vm != nullptr);

  auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_rust.wasm"));
  EXPECT_TRUE(wasm_vm->load(code, false));

  wasm_vm->registerCallback("env", "ping", ping, &ping);
  wasm_vm->link("test");

  WasmCallVoid<0> start;
  wasm_vm->getFunction("_start", &start);
  EXPECT_CALL(*g_host_functions, ping());
  start(nullptr /* no context */);

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
  wasm_vm->link("test");

  EXPECT_EQ(wasm_vm->getMemorySize(), 65536 /* stack size requested at the build-time */);

  const uint64_t test_addr = 128;

  std::string set = "test";
  EXPECT_TRUE(wasm_vm->setMemory(test_addr, set.size(), set.data()));
  auto got = wasm_vm->getMemory(test_addr, set.size()).value();
  EXPECT_EQ(sizeof("test") - 1, got.size());
  EXPECT_STREQ("test", got.data());

  EXPECT_FALSE(wasm_vm->setMemory(1024 * 1024 /* out of bound */, 1 /* size */, nullptr));
  EXPECT_FALSE(wasm_vm->getMemory(1024 * 1024 /* out of bound */, 1 /* size */).has_value());

  Word word(0);
  EXPECT_TRUE(wasm_vm->setWord(test_addr, std::numeric_limits<uint32_t>::max()));
  EXPECT_TRUE(wasm_vm->getWord(test_addr, &word));
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(), word.u64_);

  EXPECT_FALSE(wasm_vm->setWord(1024 * 1024 /* out of bound */, 1));
  EXPECT_FALSE(wasm_vm->getWord(1024 * 1024 /* out of bound */, &word));
}

} // namespace
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
