#include "envoy/registry/registry.h"

#include "extensions/common/wasm/null/null_vm_plugin.h"
#include "extensions/common/wasm/wasm_vm.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;
using testing::Return;

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
REGISTER_FACTORY(PluginFactory, Null::NullVmPluginFactory);

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
  MOCK_CONST_METHOD1(pong, void(uint32_t));
  MOCK_CONST_METHOD0(random, uint32_t());
};

MockHostFunctions* g_host_functions;

void pong(void*, Word value) { g_host_functions->pong(convertWordToUint32(value)); }

Word random(void*) { return Word(g_host_functions->random()); }

// pong() with wrong number of arguments.
void bad_pong1(void*) { return; }

// pong() with wrong return type.
Word bad_pong2(void*, Word) { return 2; }

// pong() with wrong argument type.
double bad_pong3(void*, double) { return 3; }

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
  EXPECT_TRUE(wasm_vm->clone() == nullptr);

  auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_rust.wasm"));
  EXPECT_TRUE(wasm_vm->load(code, false));

  EXPECT_THAT(wasm_vm->getCustomSection("producers"), HasSubstr("rustc"));
  EXPECT_TRUE(wasm_vm->getCustomSection("emscripten_metadata").empty());
}

TEST_F(WasmVmTest, V8BadHostFunctions) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.v8");
  ASSERT_TRUE(wasm_vm != nullptr);

  auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_rust.wasm"));
  EXPECT_TRUE(wasm_vm->load(code, false));

  wasm_vm->registerCallback("env", "random", &random, CONVERT_FUNCTION_WORD_TO_UINT32(random));
  EXPECT_THROW_WITH_MESSAGE(wasm_vm->link("test"), WasmVmException,
                            "Failed to load WASM module due to a missing import: env.pong");

  wasm_vm->registerCallback("env", "pong", &bad_pong1, CONVERT_FUNCTION_WORD_TO_UINT32(bad_pong1));
  EXPECT_THROW_WITH_MESSAGE(wasm_vm->link("test"), WasmVmException,
                            "Failed to load WASM module due to an import type mismatch: env.pong, "
                            "want: i32 -> void, but host exports: void -> void");

  wasm_vm->registerCallback("env", "pong", &bad_pong2, CONVERT_FUNCTION_WORD_TO_UINT32(bad_pong2));
  EXPECT_THROW_WITH_MESSAGE(wasm_vm->link("test"), WasmVmException,
                            "Failed to load WASM module due to an import type mismatch: env.pong, "
                            "want: i32 -> void, but host exports: i32 -> i32");

  wasm_vm->registerCallback("env", "pong", &bad_pong3, CONVERT_FUNCTION_WORD_TO_UINT32(bad_pong3));
  EXPECT_THROW_WITH_MESSAGE(wasm_vm->link("test"), WasmVmException,
                            "Failed to load WASM module due to an import type mismatch: env.pong, "
                            "want: i32 -> void, but host exports: f64 -> f64");
}

TEST_F(WasmVmTest, V8BadModuleFunctions) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.v8");
  ASSERT_TRUE(wasm_vm != nullptr);

  auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_rust.wasm"));
  EXPECT_TRUE(wasm_vm->load(code, false));

  wasm_vm->registerCallback("env", "pong", &pong, CONVERT_FUNCTION_WORD_TO_UINT32(pong));
  wasm_vm->registerCallback("env", "random", &random, CONVERT_FUNCTION_WORD_TO_UINT32(random));
  wasm_vm->link("test");

  WasmCallVoid<1> ping;
  WasmCallWord<3> sum;

  wasm_vm->getFunction("nonexistent", &ping);
  EXPECT_TRUE(ping == nullptr);

  wasm_vm->getFunction("nonexistent", &sum);
  EXPECT_TRUE(sum == nullptr);

  EXPECT_THROW_WITH_MESSAGE(wasm_vm->getFunction("ping", &sum), WasmVmException,
                            "Bad function signature for: ping");

  EXPECT_THROW_WITH_MESSAGE(wasm_vm->getFunction("sum", &ping), WasmVmException,
                            "Bad function signature for: sum");
}

TEST_F(WasmVmTest, V8FunctionCalls) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.v8");
  ASSERT_TRUE(wasm_vm != nullptr);

  auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_rust.wasm"));
  EXPECT_TRUE(wasm_vm->load(code, false));

  wasm_vm->registerCallback("env", "pong", &pong, CONVERT_FUNCTION_WORD_TO_UINT32(pong));
  wasm_vm->registerCallback("env", "random", &random, CONVERT_FUNCTION_WORD_TO_UINT32(random));
  wasm_vm->link("test");

  WasmCallVoid<1> ping;
  wasm_vm->getFunction("ping", &ping);
  EXPECT_CALL(*g_host_functions, pong(42));
  ping(nullptr /* no context */, 42);

  WasmCallWord<1> lucky;
  wasm_vm->getFunction("lucky", &lucky);
  EXPECT_CALL(*g_host_functions, random()).WillRepeatedly(Return(42));
  EXPECT_EQ(0, lucky(nullptr /* no context */, 1).u64_);
  EXPECT_EQ(1, lucky(nullptr /* no context */, 42).u64_);

  WasmCallWord<3> sum;
  wasm_vm->getFunction("sum", &sum);
  EXPECT_EQ(42, sum(nullptr /* no context */, 13, 14, 15).u64_);

  WasmCallWord<2> div;
  wasm_vm->getFunction("div", &div);
  EXPECT_THROW_WITH_MESSAGE(div(nullptr /* no context */, 42, 0), WasmException,
                            "Function: div failed: Uncaught RuntimeError: unreachable");

  WasmCallVoid<0> abort;
  wasm_vm->getFunction("abort", &abort);
  EXPECT_THROW_WITH_MESSAGE(abort(nullptr /* no context */), WasmException,
                            "Function: abort failed: Uncaught RuntimeError: unreachable");
}

TEST_F(WasmVmTest, V8Memory) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.v8");
  ASSERT_TRUE(wasm_vm != nullptr);

  auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_rust.wasm"));
  EXPECT_TRUE(wasm_vm->load(code, false));

  wasm_vm->registerCallback("env", "pong", &pong, CONVERT_FUNCTION_WORD_TO_UINT32(pong));
  wasm_vm->registerCallback("env", "random", &random, CONVERT_FUNCTION_WORD_TO_UINT32(random));
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
