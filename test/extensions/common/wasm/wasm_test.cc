#include <stdio.h>

#include "common/event/dispatcher_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "extensions/common/wasm/wasm.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class TestContext : public Extensions::Common::Wasm::Context {
public:
  TestContext(Extensions::Common::Wasm::Wasm* wasm) : Extensions::Common::Wasm::Context(wasm) {}
  TestContext(Extensions::Common::Wasm::Wasm* wasm,
              Extensions::Common::Wasm::PluginSharedPtr plugin)
      : Extensions::Common::Wasm::Context(wasm, plugin) {}
  TestContext(Extensions::Common::Wasm::Wasm* wasm, uint32_t root_context_id,
              Extensions::Common::Wasm::PluginSharedPtr plugin)
      : Extensions::Common::Wasm::Context(wasm, root_context_id, plugin) {}
  ~TestContext() override {}
  proxy_wasm::WasmResult log(uint64_t level, absl::string_view message) override {
    std::cerr << std::string(message) << "\n";
    log_(static_cast<spdlog::level::level_enum>(level), message);
    return proxy_wasm::WasmResult::Ok;
  }
  MOCK_METHOD2(log_, void(spdlog::level::level_enum level, absl::string_view message));
};

class WasmCommonTest : public testing::TestWithParam<std::string> {};

INSTANTIATE_TEST_SUITE_P(Runtimes, WasmCommonTest, testing::Values("v8", "null"));

TEST_P(WasmCommonTest, Logging) {
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher());
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto name = "";
  auto root_id = "";
  auto vm_id = "";
  auto vm_configuration = "logging";
  auto plugin_configuration = "configure-test";
  std::string code;
  if (GetParam() == "v8") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        absl::StrCat("{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm")));
  } else {
    // The name of the Null VM plugin.
    code = "CommonWasmTestCpp";
  }
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      name, root_id, vm_id, plugin_configuration,
      envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey(vm_id, vm_configuration, code);
  auto wasm = std::make_shared<Extensions::Common::Wasm::Wasm>(
      absl::StrCat("envoy.wasm.runtime.", GetParam()), vm_id, vm_configuration, vm_key, scope,
      cluster_manager, *dispatcher);
  EXPECT_NE(wasm, nullptr);
  EXPECT_THROW_WITH_MESSAGE(wasm->error("foo"), Extensions::Common::Wasm::WasmException, "foo");
  auto wasm_weak = std::weak_ptr<Extensions::Common::Wasm::Wasm>(wasm);
  auto wasm_handle = std::make_shared<Extensions::Common::Wasm::WasmHandle>(std::move(wasm));
  auto context = std::make_unique<TestContext>(wasm_weak.lock().get());
  auto vm_context = std::make_unique<TestContext>(wasm_weak.lock().get(), plugin);

  EXPECT_CALL(*context, log_(spdlog::level::trace, Eq("test trace logging")));
  EXPECT_CALL(*context, log_(spdlog::level::debug, Eq("test debug logging")));
  EXPECT_CALL(*context, log_(spdlog::level::warn, Eq("test warn logging")));
  EXPECT_CALL(*context, log_(spdlog::level::err, Eq("test error logging")));
  EXPECT_CALL(*context, log_(spdlog::level::info, Eq("on_configuration configure-test")));
  EXPECT_CALL(*context, log_(spdlog::level::info, Eq("on_done logging")));
  EXPECT_CALL(*context, log_(spdlog::level::info, Eq("on_delete logging")));

  EXPECT_TRUE(wasm_weak.lock()->initialize(code, false));
  auto thread_local_wasm = std::make_shared<Wasm>(wasm_handle, *dispatcher);
  thread_local_wasm.reset();
  wasm_weak.lock()->setContext(context.get());
  auto root_context = context.get();
  wasm_weak.lock()->startForTesting(std::move(context), plugin);
  wasm_weak.lock()->configure(root_context, plugin);

  wasm_handle.reset();
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  // This will fault on nullptr if wasm has been deleted.
  plugin->plugin_configuration_ = "done";
  wasm_weak.lock()->configure(root_context, plugin);
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  dispatcher->clearDeferredDeleteList();
}

TEST_P(WasmCommonTest, BadSignature) {
  if (GetParam() != "v8") {
    return;
  }
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher());
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto name = "";
  auto root_id = "";
  auto vm_id = "";
  auto vm_configuration = "";
  auto plugin_configuration = "";
  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/bad_signature_cpp.wasm"));
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      name, root_id, vm_id, plugin_configuration,
      envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey(vm_id, vm_configuration, code);
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(
      absl::StrCat("envoy.wasm.runtime.", GetParam()), vm_id, vm_configuration, vm_key, scope,
      cluster_manager, *dispatcher);
  EXPECT_NE(wasm, nullptr);
  EXPECT_THROW_WITH_MESSAGE(wasm->initialize(code, false), Extensions::Common::Wasm::WasmException,
                            "Bad function signature for: proxy_on_configure");
}

TEST_P(WasmCommonTest, Segv) {
  if (GetParam() != "v8") {
    return;
  }
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher());
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto name = "";
  auto root_id = "";
  auto vm_id = "";
  auto vm_configuration = "segv";
  auto plugin_configuration = "";
  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm"));
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      name, root_id, vm_id, plugin_configuration,
      envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey(vm_id, vm_configuration, code);
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(
      absl::StrCat("envoy.wasm.runtime.", GetParam()), vm_id, vm_configuration, vm_key, scope,
      cluster_manager, *dispatcher);
  auto context = std::make_unique<TestContext>(wasm.get());
  EXPECT_CALL(*context, log_(spdlog::level::err, Eq("before badptr")));
  EXPECT_TRUE(wasm->initialize(code, false));

  EXPECT_THROW_WITH_MESSAGE(
      wasm->startForTesting(std::move(context), plugin), Extensions::Common::Wasm::WasmException,
      "Function: proxy_on_vm_start failed: Uncaught RuntimeError: unreachable");
}

TEST_P(WasmCommonTest, DivByZero) {
  if (GetParam() != "v8") {
    return;
  }
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher());
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto name = "";
  auto root_id = "";
  auto vm_id = "";
  auto vm_configuration = "divbyzero";
  auto plugin_configuration = "";
  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm"));
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      name, root_id, vm_id, plugin_configuration,
      envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey(vm_id, vm_configuration, code);
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(
      absl::StrCat("envoy.wasm.runtime.", GetParam()), vm_id, vm_configuration, vm_key, scope,
      cluster_manager, *dispatcher);
  EXPECT_NE(wasm, nullptr);
  auto context = std::make_unique<TestContext>(wasm.get());
  EXPECT_CALL(*context, log_(spdlog::level::err, Eq("before div by zero")));
  EXPECT_TRUE(wasm->initialize(code, false));

  EXPECT_THROW_WITH_MESSAGE(
      wasm->startForTesting(std::move(context), plugin), Extensions::Common::Wasm::WasmException,
      "Function: proxy_on_vm_start failed: Uncaught RuntimeError: divide by zero");
}

TEST_P(WasmCommonTest, EmscriptenVersion) {
  if (GetParam() != "v8") {
    return;
  }
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher());
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto name = "";
  auto root_id = "";
  auto vm_id = "";
  auto vm_configuration = "";
  auto plugin_configuration = "";
  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm"));
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      name, root_id, vm_id, plugin_configuration,
      envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey(vm_id, vm_configuration, code);
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(
      absl::StrCat("envoy.wasm.runtime.", GetParam()), vm_id, vm_configuration, vm_key, scope,
      cluster_manager, *dispatcher);
  EXPECT_NE(wasm, nullptr);
  auto context = std::make_unique<TestContext>(wasm.get());
  EXPECT_TRUE(wasm->initialize(code, false));

  uint32_t major = 9, minor = 9, abi_major = 9, abi_minor = 9;
  EXPECT_TRUE(wasm->getEmscriptenVersion(&major, &minor, &abi_major, &abi_minor));
  EXPECT_EQ(major, 0);
  EXPECT_LE(minor, 3);
  // Up to (at least) emsdk 1.39.6.
  EXPECT_EQ(abi_major, 0);
  EXPECT_LE(abi_minor, 20);
}

TEST_P(WasmCommonTest, IntrinsicGlobals) {
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher());
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto name = "";
  auto root_id = "";
  auto vm_id = "";
  auto vm_configuration = "globals";
  auto plugin_configuration = "";
  std::string code;
  if (GetParam() == "v8") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        absl::StrCat("{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm")));
  } else {
    // The name of the Null VM plugin.
    code = "CommonWasmTestCpp";
  }
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      name, root_id, vm_id, plugin_configuration,
      envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey(vm_id, vm_configuration, code);
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(
      absl::StrCat("envoy.wasm.runtime.", GetParam()), vm_id, vm_configuration, vm_key, scope,
      cluster_manager, *dispatcher);
  EXPECT_NE(wasm, nullptr);
  auto context = std::make_unique<TestContext>(wasm.get());
  EXPECT_CALL(*context, log_(spdlog::level::warn, Eq("NaN nan")));
  EXPECT_CALL(*context, log_(spdlog::level::warn, Eq("inf inf"))).Times(3);
  EXPECT_TRUE(wasm->initialize(code, false));
  wasm->startForTesting(std::move(context), plugin);
}

TEST_P(WasmCommonTest, Stats) {
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher());
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto name = "";
  auto root_id = "";
  auto vm_id = "";
  auto vm_configuration = "stats";
  auto plugin_configuration = "";
  std::string code;
  if (GetParam() == "v8") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        absl::StrCat("{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm")));
  } else {
    // The name of the Null VM plugin.
    code = "CommonWasmTestCpp";
  }
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      name, root_id, vm_id, plugin_configuration,
      envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey(vm_id, vm_configuration, code);
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(
      absl::StrCat("envoy.wasm.runtime.", GetParam()), vm_id, vm_configuration, vm_key, scope,
      cluster_manager, *dispatcher);
  EXPECT_NE(wasm, nullptr);
  auto context = std::make_unique<TestContext>(wasm.get());

  EXPECT_CALL(*context, log_(spdlog::level::trace, Eq("get counter = 1")));
  EXPECT_CALL(*context, log_(spdlog::level::debug, Eq("get counter = 2")));
  // recordMetric on a Counter is the same as increment.
  EXPECT_CALL(*context, log_(spdlog::level::info, Eq("get counter = 5")));
  EXPECT_CALL(*context, log_(spdlog::level::warn, Eq("get gauge = 2")));
  // Get is not supported on histograms.
  EXPECT_CALL(*context, log_(spdlog::level::err, Eq("get histogram = Unsupported")));

  EXPECT_TRUE(wasm->initialize(code, false));
  wasm->startForTesting(std::move(context), plugin);
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
