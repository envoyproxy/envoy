#include "envoy/registry/registry.h"

#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/common/wasm/wasm_runtime_factory.h"
#include "source/extensions/common/wasm/wasm_vm.h"

#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "include/proxy-wasm/bytecode_util.h"
#include "include/proxy-wasm/null_vm_plugin.h"

using proxy_wasm::Cloneable;    // NOLINT
using proxy_wasm::WasmCallVoid; // NOLINT
using proxy_wasm::WasmCallWord; // NOLINT
using proxy_wasm::Word;         // NOLINT
using testing::HasSubstr;       // NOLINT
using testing::IsEmpty;         // NOLINT
using testing::Return;          // NOLINT

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace {

TEST(EnvoyWasmVmIntegrationTest, EnvoyWasmVmIntegrationTest) {
  {
    EnvoyWasmVmIntegration wasm_vm_integration;
    for (const auto l : {spdlog::level::trace, spdlog::level::debug, spdlog::level::info,
                         spdlog::level::warn, spdlog::level::err, spdlog::level::critical}) {
      Logger::Registry::getLog(Logger::Id::wasm).set_level(l);
      EXPECT_EQ(wasm_vm_integration.getLogLevel(), static_cast<proxy_wasm::LogLevel>(l));
    }
  }
}

class TestNullVmPlugin : public proxy_wasm::NullVmPlugin {
public:
  TestNullVmPlugin() = default;
  ~TestNullVmPlugin() override = default;

  MOCK_METHOD(void, start, ());
};

TestNullVmPlugin* test_null_vm_plugin_ = nullptr;

proxy_wasm::RegisterNullVmPluginFactory register_test_null_vm_plugin("test_null_vm_plugin", []() {
  auto plugin = std::make_unique<TestNullVmPlugin>();
  test_null_vm_plugin_ = plugin.get();
  return plugin;
});

class ClearWasmRuntimeFactories {
public:
  ClearWasmRuntimeFactories() {
    saved_factories_ = Registry::FactoryRegistry<WasmRuntimeFactory>::factories();
    Registry::FactoryRegistry<WasmRuntimeFactory>::factories().clear();
    Registry::InjectFactory<WasmRuntimeFactory>::resetTypeMappings();
  }

  ~ClearWasmRuntimeFactories() {
    Registry::FactoryRegistry<WasmRuntimeFactory>::factories() = saved_factories_;
    Registry::InjectFactory<WasmRuntimeFactory>::resetTypeMappings();
  }

private:
  absl::flat_hash_map<std::string, WasmRuntimeFactory*> saved_factories_;
};

TEST(WasmEngineTest, NoAvailableEngine) {
  ClearWasmRuntimeFactories clear_factories;
  EXPECT_THAT(getFirstAvailableWasmEngineName(), IsEmpty());
}

class BaseVmTest : public testing::Test {
public:
  BaseVmTest() : scope_(Stats::ScopeSharedPtr(stats_store.createScope("wasm."))) {}

protected:
  Stats::IsolatedStoreImpl stats_store;
  Stats::ScopeSharedPtr scope_;
};

TEST_F(BaseVmTest, UnspecifiedRuntime) {
  auto wasm_vm = createWasmVm("");
  absl::string_view first_wasm_engine_name = getFirstAvailableWasmEngineName();
  // Envoy is built with "--define wasm=disabled", so no Wasm engine is available
  if (first_wasm_engine_name.empty()) {
    EXPECT_TRUE(wasm_vm.get() == nullptr);
  } else {
    ASSERT_TRUE(wasm_vm.get() != nullptr);
    EXPECT_THAT(std::string(first_wasm_engine_name), HasSubstr(wasm_vm->getEngineName()));
  }
}

TEST_F(BaseVmTest, BadRuntime) { EXPECT_EQ(createWasmVm("envoy.wasm.runtime.invalid"), nullptr); }

TEST_F(BaseVmTest, NullVmStartup) {
  auto wasm_vm = createWasmVm("envoy.wasm.runtime.null");
  EXPECT_TRUE(wasm_vm != nullptr);
  EXPECT_TRUE(wasm_vm->getEngineName() == "null");
  EXPECT_TRUE(wasm_vm->cloneable() == Cloneable::InstantiatedModule);
  auto wasm_vm_clone = wasm_vm->clone();
  EXPECT_TRUE(wasm_vm_clone != nullptr);
  EXPECT_EQ(wasm_vm->getEngineName(), "null");
  std::function<void()> f;
  EXPECT_FALSE(wasm_vm->integration()->getNullVmFunction("bad_function", false, 0, nullptr, &f));
}

TEST_F(BaseVmTest, NullVmMemory) {
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
  MOCK_METHOD(void, pong, (uint32_t), (const));
  MOCK_METHOD(uint32_t, random, (), (const));
};

#if defined(PROXY_WASM_HAS_RUNTIME_V8)
MockHostFunctions* g_host_functions;

void pong(Word value) { g_host_functions->pong(convertWordToUint32(value)); }

Word random() { return {g_host_functions->random()}; }

// pong() with wrong number of arguments.
void badPong1() {}

// pong() with wrong return type.
Word badPong2(Word) { return 2; }

// pong() with wrong argument type.
double badPong3(double) { return 3; }

class WasmVmTest : public testing::TestWithParam<bool> {
public:
  WasmVmTest() : scope_(Stats::ScopeSharedPtr(stats_store.createScope("wasm."))) {}

  void SetUp() override { // NOLINT(readability-identifier-naming)
    g_host_functions = new MockHostFunctions();
  }
  void TearDown() override { delete g_host_functions; }

  bool init(std::string code = {}) {
    wasm_vm_ = createWasmVm("envoy.wasm.runtime.v8");
    if (wasm_vm_.get() == nullptr) {
      return false;
    }

    if (code.empty()) {
      code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
          "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_rust.wasm"));
    }

    // clang-format off
    std::string_view precompiled = {};
    // clang-format on

    // Load precompiled module on Linux-x86_64, since it's only support there.
#if defined(__linux__) && defined(__x86_64__)
    if (GetParam() /* allow_precompiled */) {
      // Section name is expected to be available in the tested runtimes.
      const auto section_name = wasm_vm_->getPrecompiledSectionName();
      if (section_name.empty()) {
        return false;
      }
      if (!proxy_wasm::BytecodeUtil::getCustomSection(code, section_name, precompiled)) {
        return false;
      }
      // Precompiled module is expected to be available in the test file.
      if (precompiled.empty()) {
        return false;
      }
    }
#endif

    std::string stripped;
    if (!proxy_wasm::BytecodeUtil::getStrippedSource(code, stripped)) {
      return false;
    }

    return wasm_vm_->load(stripped, precompiled, {});
  }

protected:
  Stats::IsolatedStoreImpl stats_store;
  Stats::ScopeSharedPtr scope_;
  WasmVmPtr wasm_vm_;
};

INSTANTIATE_TEST_SUITE_P(AllowPrecompiled, WasmVmTest,
#if defined(__linux__) && defined(__x86_64__)
                         testing::Values(false, true)
#else
                         testing::Values(false)
#endif
);

TEST_P(WasmVmTest, V8BadCode) { ASSERT_FALSE(init("bad code")); }

TEST_P(WasmVmTest, V8Load) {
  ASSERT_TRUE(init());
  EXPECT_TRUE(wasm_vm_->getEngineName() == "v8");
  EXPECT_TRUE(wasm_vm_->cloneable() == Cloneable::CompiledBytecode);
  EXPECT_TRUE(wasm_vm_->clone() != nullptr);
}

TEST_P(WasmVmTest, V8BadHostFunctions) {
  ASSERT_TRUE(init());

  wasm_vm_->registerCallback("env", "random", &random, CONVERT_FUNCTION_WORD_TO_UINT32(random));
  EXPECT_FALSE(wasm_vm_->link("test"));

  wasm_vm_->registerCallback("env", "pong", &badPong1, CONVERT_FUNCTION_WORD_TO_UINT32(badPong1));
  EXPECT_FALSE(wasm_vm_->link("test"));

  wasm_vm_->registerCallback("env", "pong", &badPong2, CONVERT_FUNCTION_WORD_TO_UINT32(badPong2));
  EXPECT_FALSE(wasm_vm_->link("test"));

  wasm_vm_->registerCallback("env", "pong", &badPong3, CONVERT_FUNCTION_WORD_TO_UINT32(badPong3));
  EXPECT_FALSE(wasm_vm_->link("test"));
}

TEST_P(WasmVmTest, V8BadModuleFunctions) {
  ASSERT_TRUE(init());

  wasm_vm_->registerCallback("env", "pong", &pong, CONVERT_FUNCTION_WORD_TO_UINT32(pong));
  wasm_vm_->registerCallback("env", "random", &random, CONVERT_FUNCTION_WORD_TO_UINT32(random));
  wasm_vm_->link("test");

  WasmCallVoid<1> ping;
  WasmCallWord<3> sum;

  wasm_vm_->getFunction("nonexistent", &ping);
  EXPECT_TRUE(ping == nullptr);

  wasm_vm_->getFunction("nonexistent", &sum);
  EXPECT_TRUE(sum == nullptr);

  wasm_vm_->getFunction("ping", &sum);
  EXPECT_TRUE(wasm_vm_->isFailed());

  wasm_vm_->getFunction("sum", &ping);
  EXPECT_TRUE(wasm_vm_->isFailed());
}

TEST_P(WasmVmTest, V8FunctionCalls) {
  ASSERT_TRUE(init());

  wasm_vm_->registerCallback("env", "pong", &pong, CONVERT_FUNCTION_WORD_TO_UINT32(pong));
  wasm_vm_->registerCallback("env", "random", &random, CONVERT_FUNCTION_WORD_TO_UINT32(random));
  wasm_vm_->link("test");

  WasmCallVoid<1> ping;
  wasm_vm_->getFunction("ping", &ping);
  EXPECT_CALL(*g_host_functions, pong(42));
  ping(nullptr /* no context */, 42);

  WasmCallWord<1> lucky;
  wasm_vm_->getFunction("lucky", &lucky);
  EXPECT_CALL(*g_host_functions, random()).WillRepeatedly(Return(42));
  EXPECT_EQ(0, lucky(nullptr /* no context */, 1).u64_);
  EXPECT_EQ(1, lucky(nullptr /* no context */, 42).u64_);

  WasmCallWord<3> sum;
  wasm_vm_->getFunction("sum", &sum);
  EXPECT_EQ(42, sum(nullptr /* no context */, 13, 14, 15).u64_);

  WasmCallWord<2> div;
  wasm_vm_->getFunction("div", &div);
  div(nullptr /* no context */, 42, 0);
  EXPECT_TRUE(wasm_vm_->isFailed());

  WasmCallVoid<0> abort;
  wasm_vm_->getFunction("abort", &abort);
  abort(nullptr /* no context */);
  EXPECT_TRUE(wasm_vm_->isFailed());
}

TEST_P(WasmVmTest, V8Memory) {
  ASSERT_TRUE(init());

  wasm_vm_->registerCallback("env", "pong", &pong, CONVERT_FUNCTION_WORD_TO_UINT32(pong));
  wasm_vm_->registerCallback("env", "random", &random, CONVERT_FUNCTION_WORD_TO_UINT32(random));
  wasm_vm_->link("test");

  EXPECT_EQ(wasm_vm_->getMemorySize(), 65536 /* stack size requested at the build-time */);

  const uint64_t test_addr = 128;

  std::string set = "test";
  EXPECT_TRUE(wasm_vm_->setMemory(test_addr, set.size(), set.data()));
  auto got = wasm_vm_->getMemory(test_addr, set.size()).value();
  EXPECT_EQ(sizeof("test") - 1, got.size());
  EXPECT_STREQ("test", got.data());

  EXPECT_FALSE(wasm_vm_->setMemory(1024 * 1024 /* out of bound */, 1 /* size */, nullptr));
  EXPECT_FALSE(wasm_vm_->getMemory(1024 * 1024 /* out of bound */, 1 /* size */).has_value());

  Word word(0);
  EXPECT_TRUE(wasm_vm_->setWord(test_addr, std::numeric_limits<uint32_t>::max()));
  EXPECT_TRUE(wasm_vm_->getWord(test_addr, &word));
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(), word.u64_);

  EXPECT_FALSE(wasm_vm_->setWord(1024 * 1024 /* out of bound */, 1));
  EXPECT_FALSE(wasm_vm_->getWord(1024 * 1024 /* out of bound */, &word));
}
#endif

} // namespace
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
