#include "envoy/http/filter.h"
#include "envoy/http/filter_factory.h"
#include "envoy/server/lifecycle_notifier.h"

#include "source/common/common/base64.h"
#include "source/common/common/hex.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/http/filter_manager.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/common/wasm/wasm.h"

#include "test/extensions/common/wasm/wasm_runtime.h"
#include "test/mocks/local_reply/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"
#include "test/test_common/wasm_base.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/bytestring.h"
#include "openssl/hmac.h"
#include "openssl/sha.h"
#include "wasm_runtime.h"
#include "zlib.h"

using Envoy::Server::ServerLifecycleNotifier;
using StageCallbackWithCompletion =
    Envoy::Server::ServerLifecycleNotifier::StageCallbackWithCompletion;
using testing::Eq;
using testing::Return;

namespace Envoy {

namespace Server {
class MockServerLifecycleNotifier2 : public ServerLifecycleNotifier {
public:
  MockServerLifecycleNotifier2() = default;
  ~MockServerLifecycleNotifier2() override = default;

  using ServerLifecycleNotifier::registerCallback;

  ServerLifecycleNotifier::HandlePtr
  registerCallback(Stage stage, StageCallbackWithCompletion callback) override {
    return registerCallback2(stage, callback);
  }

  MOCK_METHOD(ServerLifecycleNotifier::HandlePtr, registerCallback, (Stage, StageCallback));
  MOCK_METHOD(ServerLifecycleNotifier::HandlePtr, registerCallback2,
              (Stage stage, StageCallbackWithCompletion callback));
};
} // namespace Server

namespace Extensions {
namespace Common {
namespace Wasm {

std::string sha256(absl::string_view data) {
  std::vector<uint8_t> digest(SHA256_DIGEST_LENGTH);
  EVP_MD_CTX* ctx(EVP_MD_CTX_new());
  auto rc = EVP_DigestInit(ctx, EVP_sha256());
  RELEASE_ASSERT(rc == 1, "Failed to init digest context");
  rc = EVP_DigestUpdate(ctx, data.data(), data.size());
  RELEASE_ASSERT(rc == 1, "Failed to update digest");
  rc = EVP_DigestFinal(ctx, digest.data(), nullptr);
  RELEASE_ASSERT(rc == 1, "Failed to finalize digest");
  EVP_MD_CTX_free(ctx);
  return {reinterpret_cast<const char*>(&digest[0]), digest.size()};
}

class TestContext : public ::Envoy::Extensions::Common::Wasm::Context {
public:
  using ::Envoy::Extensions::Common::Wasm::Context::Context;
  ~TestContext() override = default;
  using ::Envoy::Extensions::Common::Wasm::Context::log;
  proxy_wasm::WasmResult log(uint32_t level, std::string_view message) override {
    std::cerr << message << "\n";
    log_(static_cast<spdlog::level::level_enum>(level), toAbslStringView(message));
    Extensions::Common::Wasm::Context::log(static_cast<spdlog::level::level_enum>(level), message);
    return proxy_wasm::WasmResult::Ok;
  }
  MOCK_METHOD(void, log_, (spdlog::level::level_enum level, absl::string_view message));
};

class WasmCommonTest : public testing::TestWithParam<std::tuple<std::string, std::string>> {
public:
  void SetUp() override { // NOLINT(readability-identifier-naming)
    Logger::Registry::getLog(Logger::Id::wasm).set_level(spdlog::level::debug);
    clearCodeCacheForTesting();
  }
};

INSTANTIATE_TEST_SUITE_P(Runtimes, WasmCommonTest,
                         Envoy::Extensions::Common::Wasm::runtime_and_cpp_values,
                         Envoy::Extensions::Common::Wasm::wasmTestParamsToString);

TEST_P(WasmCommonTest, WasmFailState) {
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);

  auto wasm = std::make_shared<WasmHandle>(
      std::make_unique<Wasm>(plugin->wasmConfig(), "", scope, *api, cluster_manager, *dispatcher));
  auto wasm_base = std::dynamic_pointer_cast<proxy_wasm::WasmHandleBase>(wasm);
  wasm->wasm()->setFailStateForTesting(proxy_wasm::FailState::UnableToCreateVm);
  EXPECT_EQ(toWasmEvent(wasm_base), WasmEvent::UnableToCreateVm);
  wasm->wasm()->setFailStateForTesting(proxy_wasm::FailState::UnableToCloneVm);
  EXPECT_EQ(toWasmEvent(wasm_base), WasmEvent::UnableToCloneVm);
  wasm->wasm()->setFailStateForTesting(proxy_wasm::FailState::MissingFunction);
  EXPECT_EQ(toWasmEvent(wasm_base), WasmEvent::MissingFunction);
  wasm->wasm()->setFailStateForTesting(proxy_wasm::FailState::UnableToInitializeCode);
  EXPECT_EQ(toWasmEvent(wasm_base), WasmEvent::UnableToInitializeCode);
  wasm->wasm()->setFailStateForTesting(proxy_wasm::FailState::StartFailed);
  EXPECT_EQ(toWasmEvent(wasm_base), WasmEvent::StartFailed);
  wasm->wasm()->setFailStateForTesting(proxy_wasm::FailState::ConfigureFailed);
  EXPECT_EQ(toWasmEvent(wasm_base), WasmEvent::ConfigureFailed);
  wasm->wasm()->setFailStateForTesting(proxy_wasm::FailState::RuntimeError);
  EXPECT_EQ(toWasmEvent(wasm_base), WasmEvent::RuntimeError);

  uint32_t grpc_call_token1 = wasm->wasm()->nextGrpcCallId();
  EXPECT_TRUE(wasm->wasm()->isGrpcCallId(grpc_call_token1));
  uint32_t grpc_call_token2 = wasm->wasm()->nextGrpcCallId();
  EXPECT_TRUE(wasm->wasm()->isGrpcCallId(grpc_call_token2));
  EXPECT_NE(grpc_call_token1, grpc_call_token2);

  uint32_t grpc_stream_token1 = wasm->wasm()->nextGrpcStreamId();
  EXPECT_TRUE(wasm->wasm()->isGrpcStreamId(grpc_stream_token1));
  uint32_t grpc_stream_token2 = wasm->wasm()->nextGrpcStreamId();
  EXPECT_TRUE(wasm->wasm()->isGrpcStreamId(grpc_stream_token2));
  EXPECT_NE(grpc_stream_token1, grpc_stream_token2);

  uint32_t http_call_token1 = wasm->wasm()->nextHttpCallId();
  EXPECT_TRUE(wasm->wasm()->isHttpCallId(http_call_token1));
  uint32_t http_call_token2 = wasm->wasm()->nextHttpCallId();
  EXPECT_TRUE(wasm->wasm()->isHttpCallId(http_call_token2));
  EXPECT_NE(http_call_token1, http_call_token2);

  auto root_context = static_cast<Context*>(wasm->wasm()->createRootContext(plugin));
  EXPECT_EQ(root_context->getBuffer(WasmBufferType::HttpCallResponseBody), nullptr);
  EXPECT_EQ(root_context->getBuffer(WasmBufferType::PluginConfiguration), nullptr);
  delete root_context;

  Filters::Common::Expr::CelStatePrototype wasm_state_prototype(
      true, Filters::Common::Expr::CelStateType::Bytes, "",
      StreamInfo::FilterState::LifeSpan::FilterChain);
  auto wasm_state = std::make_unique<Filters::Common::Expr::CelState>(wasm_state_prototype);
  Protobuf::Arena arena;
  EXPECT_EQ(wasm_state->exprValue(&arena, true).type(),
            Filters::Common::Expr::CelValue::Type::kNullType);
  wasm_state->setValue("foo");
  auto any = wasm_state->serializeAsProto();
  EXPECT_TRUE(static_cast<ProtobufWkt::Any*>(any.get())->Is<ProtobufWkt::BytesValue>());
}

TEST_P(WasmCommonTest, Logging) {
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto vm_configuration = "logging";
  auto plugin_configuration = "configure-test";

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);
  plugin_config.mutable_configuration()->set_value(plugin_configuration);

  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        absl::StrCat("{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm")));
  } else {
    // The name of the Null VM plugin.
    code = "CommonWasmTestCpp";
  }
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);

  auto vm_key = proxy_wasm::makeVmKey("", vm_configuration, code);
  auto wasm = std::make_shared<Extensions::Common::Wasm::Wasm>(plugin->wasmConfig(), vm_key, scope,
                                                               *api, cluster_manager, *dispatcher);
  EXPECT_NE(wasm, nullptr);
  EXPECT_NE(wasm->buildVersion(), "");
  EXPECT_NE(std::unique_ptr<ContextBase>(wasm->createContext(plugin)), nullptr);
  wasm->setCreateContextForTesting(
      [](Wasm*, const std::shared_ptr<Plugin>&) -> ContextBase* { return nullptr; },
      [](Wasm*, const std::shared_ptr<Plugin>&) -> ContextBase* { return nullptr; });
  EXPECT_EQ(std::unique_ptr<ContextBase>(wasm->createContext(plugin)), nullptr);
  auto wasm_weak = std::weak_ptr<Extensions::Common::Wasm::Wasm>(wasm);
  auto wasm_handle = std::make_shared<Extensions::Common::Wasm::WasmHandle>(std::move(wasm));
  EXPECT_TRUE(wasm_weak.lock()->load(code, false));
  EXPECT_TRUE(wasm_weak.lock()->initialize());
  auto thread_local_wasm = std::make_shared<Wasm>(wasm_handle, *dispatcher);
  thread_local_wasm.reset();

  auto wasm_lock = wasm_weak.lock();
  wasm_lock->setCreateContextForTesting(
      nullptr, [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
        auto root_context = new TestContext(wasm, plugin);
        EXPECT_CALL(*root_context,
                    log_(spdlog::level::info, Eq("on_configuration configure-test")));
        EXPECT_CALL(*root_context, log_(spdlog::level::trace, Eq("test trace logging")));
        EXPECT_CALL(*root_context, log_(spdlog::level::debug, Eq("test debug logging")));
        EXPECT_CALL(*root_context, log_(spdlog::level::warn, Eq("test warn logging")));
        EXPECT_CALL(*root_context, log_(spdlog::level::err, Eq("test error logging")));
        EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("log level is 1")));
        EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("on_done logging")));
        EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("on_delete logging")));
        return root_context;
      });

  auto root_context = static_cast<TestContext*>(wasm_weak.lock()->start(plugin));
  EXPECT_EQ(root_context->getConfiguration(), "logging");
  if (std::get<0>(GetParam()) != "null") {
    EXPECT_TRUE(root_context->validateConfiguration("", plugin));
  }
  wasm_weak.lock()->configure(root_context, plugin);
  EXPECT_EQ(root_context->getStatus().first, 0);

  wasm_handle.reset();
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  // This will fault on nullptr if wasm has been deleted.
  wasm_weak.lock()->setTimerPeriod(root_context->id(), std::chrono::milliseconds(10));
  wasm_weak.lock()->tickHandler(root_context->id());
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  dispatcher->clearDeferredDeleteList();
}

TEST_P(WasmCommonTest, BadSignature) {
  if (std::get<0>(GetParam()) != "v8") {
    return;
  }
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/bad_signature_cpp.wasm"));
  EXPECT_FALSE(code.empty());

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey("", "", code);
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(plugin->wasmConfig(), vm_key, scope,
                                                               *api, cluster_manager, *dispatcher);

  EXPECT_TRUE(wasm->load(code, false));
  EXPECT_FALSE(wasm->initialize());
  EXPECT_TRUE(wasm->isFailed());
}

TEST_P(WasmCommonTest, Segv) {
  if (std::get<0>(GetParam()) != "v8") {
    return;
  }
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto vm_configuration = "segv";
  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm"));
  EXPECT_FALSE(code.empty());

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey("", vm_configuration, code);
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(plugin->wasmConfig(), vm_key, scope,
                                                               *api, cluster_manager, *dispatcher);
  EXPECT_TRUE(wasm->load(code, false));
  EXPECT_TRUE(wasm->initialize());
  TestContext* root_context = nullptr;
  wasm->setCreateContextForTesting(
      nullptr, [&root_context](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
        root_context = new TestContext(wasm, plugin);
        EXPECT_CALL(*root_context, log_(spdlog::level::err, Eq("before badptr")));
        return root_context;
      });
  wasm->start(plugin);
  EXPECT_TRUE(wasm->isFailed());

  // Subsequent calls should be NOOP(s).

  root_context->onResolveDns(0, Envoy::Network::DnsResolver::ResolutionStatus::Completed, {});
  Envoy::Stats::MockMetricSnapshot stats_snapshot;
  root_context->onStatsUpdate(stats_snapshot);
}

TEST_P(WasmCommonTest, DivByZero) {
  if (std::get<0>(GetParam()) != "v8") {
    return;
  }
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto vm_configuration = "divbyzero";

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);

  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm"));
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey("", vm_configuration, code);
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(plugin->wasmConfig(), vm_key, scope,
                                                               *api, cluster_manager, *dispatcher);
  EXPECT_NE(wasm, nullptr);
  auto context = std::make_unique<TestContext>(wasm.get());
  EXPECT_TRUE(wasm->load(code, false));
  EXPECT_TRUE(wasm->initialize());
  wasm->setCreateContextForTesting(
      nullptr, [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
        auto root_context = new TestContext(wasm, plugin);
        EXPECT_CALL(*root_context, log_(spdlog::level::err, Eq("before div by zero")));
        return root_context;
      });
  wasm->start(plugin);
}

TEST_P(WasmCommonTest, IntrinsicGlobals) {
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto vm_configuration = "globals";

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);

  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        absl::StrCat("{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm")));
  } else {
    // The name of the Null VM plugin.
    code = "CommonWasmTestCpp";
  }
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey("", vm_configuration, code);
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(plugin->wasmConfig(), vm_key, scope,
                                                               *api, cluster_manager, *dispatcher);

  EXPECT_NE(wasm, nullptr);
  EXPECT_TRUE(wasm->load(code, false));
  EXPECT_TRUE(wasm->initialize());
  wasm->setCreateContextForTesting(
      nullptr, [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
        auto root_context = new TestContext(wasm, plugin);
        EXPECT_CALL(*root_context, log_(spdlog::level::warn, Eq("NaN nan")));
        EXPECT_CALL(*root_context, log_(spdlog::level::warn, Eq("inf inf"))).Times(3);
        return root_context;
      });
  wasm->start(plugin);
}

TEST_P(WasmCommonTest, Utilities) {
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;

  auto vm_configuration = "utilities";
  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);

  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        absl::StrCat("{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm")));
  } else {
    // The name of the Null VM plugin.
    code = "CommonWasmTestCpp";
  }
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey("", vm_configuration, code);
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(plugin->wasmConfig(), vm_key, scope,
                                                               *api, cluster_manager, *dispatcher);

  EXPECT_NE(wasm, nullptr);
  EXPECT_TRUE(wasm->load(code, false));
  EXPECT_TRUE(wasm->initialize());
  wasm->setCreateContextForTesting(
      nullptr, [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
        auto root_context = new TestContext(wasm, plugin);
        EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("on_vm_start utilities")));
        return root_context;
      });
  wasm->start(plugin);

  // Context
  auto context = std::make_unique<Context>();
  context->error("error");

  // Buffer
  Extensions::Common::Wasm::Buffer buffer;
  Extensions::Common::Wasm::Buffer const_buffer;
  Extensions::Common::Wasm::Buffer string_buffer;
  auto buffer_impl = std::make_unique<Envoy::Buffer::OwnedImpl>("contents");
  buffer.set(buffer_impl.get());
  const_buffer.set(static_cast<const ::Envoy::Buffer::Instance*>(buffer_impl.get()));
  string_buffer.set("contents");
  std::string data("contents");
  if (std::get<0>(GetParam()) != "null") {
    EXPECT_EQ(WasmResult::InvalidMemoryAccess,
              buffer.copyTo(wasm.get(), 0, 1 << 30 /* length too long */, 0, 0));
    EXPECT_EQ(WasmResult::InvalidMemoryAccess,
              buffer.copyTo(wasm.get(), 0, 1, 1 << 30 /* bad pointer location */, 0));
    EXPECT_EQ(WasmResult::InvalidMemoryAccess,
              buffer.copyTo(wasm.get(), 0, 1, 0, 1 << 30 /* bad size location */));
    EXPECT_EQ(WasmResult::BadArgument, buffer.copyFrom(1, 1, data));
    EXPECT_EQ(WasmResult::BadArgument, const_buffer.copyFrom(1, 1, data));
    EXPECT_EQ(WasmResult::BadArgument, string_buffer.copyFrom(1, 1, data));
  }
}

TEST_P(WasmCommonTest, Stats) {
  // We set logger level to critical here to gain more coverage.
  Logger::Registry::getLog(Logger::Id::wasm).set_level(spdlog::level::critical);

  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto vm_configuration = "stats";

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);

  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        absl::StrCat("{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm")));
  } else {
    // The name of the Null VM plugin.
    code = "CommonWasmTestCpp";
  }
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey("", vm_configuration, code);
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(plugin->wasmConfig(), vm_key, scope,
                                                               *api, cluster_manager, *dispatcher);

  EXPECT_NE(wasm, nullptr);
  EXPECT_TRUE(wasm->load(code, false));
  EXPECT_TRUE(wasm->initialize());
  wasm->setCreateContextForTesting(
      nullptr, [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
        auto root_context = new TestContext(wasm, plugin);
        EXPECT_CALL(*root_context, log_(spdlog::level::trace, Eq("get counter = 1")));
        EXPECT_CALL(*root_context, log_(spdlog::level::debug, Eq("get counter = 2")));
        // recordMetric on a Counter is the same as increment.
        EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("get counter = 5")));
        EXPECT_CALL(*root_context, log_(spdlog::level::warn, Eq("get gauge = 2")));
        // Get is not supported on histograms.
        EXPECT_CALL(*root_context, log_(spdlog::level::err, Eq("get histogram = Unsupported")));
        return root_context;
      });
  wasm->start(plugin);
}

TEST_P(WasmCommonTest, Foreign) {
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto vm_configuration = "foreign";

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);

  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(plugin->wasmConfig(), "", scope,
                                                               *api, cluster_manager, *dispatcher);
  EXPECT_NE(wasm, nullptr);
  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        absl::StrCat("{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm")));
  } else {
    // The name of the Null VM plugin.
    code = "CommonWasmTestCpp";
  }
  EXPECT_FALSE(code.empty());
  EXPECT_TRUE(wasm->load(code, false));
  EXPECT_TRUE(wasm->initialize());
  wasm->setCreateContextForTesting(
      nullptr, [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
        auto root_context = new TestContext(wasm, plugin);
        EXPECT_CALL(*root_context, log_(spdlog::level::trace, Eq("compress 2000 -> 23")));
        EXPECT_CALL(*root_context, log_(spdlog::level::debug, Eq("uncompress 23 -> 2000")));
        return root_context;
      });
  wasm->start(plugin);
}

TEST_P(WasmCommonTest, OnForeign) {
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto vm_configuration = "on_foreign";

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);

  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  proxy_wasm::AllowedCapabilitiesMap allowed_capabilities;
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(plugin->wasmConfig(), "", scope,
                                                               *api, cluster_manager, *dispatcher);
  EXPECT_NE(wasm, nullptr);
  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        absl::StrCat("{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm")));
  } else {
    // The name of the Null VM plugin.
    code = "CommonWasmTestCpp";
  }
  EXPECT_FALSE(code.empty());
  EXPECT_TRUE(wasm->load(code, false));
  EXPECT_TRUE(wasm->initialize());
  TestContext* test_context = nullptr;
  wasm->setCreateContextForTesting(
      nullptr, [&test_context](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
        auto context = new TestContext(wasm, plugin);
        EXPECT_CALL(*context, log_(spdlog::level::debug, Eq("on_foreign start")));
        EXPECT_CALL(*context, log_(spdlog::level::info, Eq("on_foreign_function 7 13")));
        test_context = context;
        return context;
      });
  wasm->start(plugin);
  test_context->onForeignFunction(7, 13);
}

TEST_P(WasmCommonTest, WASI) {
  if (std::get<0>(GetParam()) == "null") {
    // This test has no meaning unless it is invoked by actual Wasm code
    return;
  }
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto vm_configuration = "WASI";

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);

  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(plugin->wasmConfig(), "", scope,
                                                               *api, cluster_manager, *dispatcher);

  EXPECT_NE(wasm, nullptr);
  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        absl::StrCat("{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm")));
  } else {
    // The name of the Null VM plugin.
    code = "CommonWasmTestCpp";
  }
  EXPECT_FALSE(code.empty());
  EXPECT_TRUE(wasm->load(code, false));
  EXPECT_TRUE(wasm->initialize());
  wasm->setCreateContextForTesting(
      nullptr, [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
        auto root_context = new TestContext(wasm, plugin);
        EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("WASI write to stdout")));
        EXPECT_CALL(*root_context, log_(spdlog::level::err, Eq("WASI write to stderr")));
        return root_context;
      });
  wasm->start(plugin);
}

TEST_P(WasmCommonTest, VmCache) {
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Server::MockServerLifecycleNotifier2> lifecycle_notifier;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  RemoteAsyncDataProviderPtr remote_data_provider;
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto vm_configuration = "vm_cache";
  auto plugin_configuration = "done";

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);
  plugin_config.mutable_configuration()->set_value(plugin_configuration);

  ServerLifecycleNotifier::StageCallbackWithCompletion lifecycle_callback;
  EXPECT_CALL(lifecycle_notifier, registerCallback2(_, _))
      .WillRepeatedly(
          Invoke([&](ServerLifecycleNotifier::Stage,
                     StageCallbackWithCompletion callback) -> ServerLifecycleNotifier::HandlePtr {
            lifecycle_callback = callback;
            return nullptr;
          }));

  auto vm_config = plugin_config.mutable_vm_config();
  vm_config->set_runtime(absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam())));
  ProtobufWkt::StringValue vm_configuration_string;
  vm_configuration_string.set_value(vm_configuration);
  vm_config->mutable_configuration()->PackFrom(vm_configuration_string);
  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        absl::StrCat("{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm")));
  } else {
    // The name of the Null VM plugin.
    code = "CommonWasmTestCpp";
  }
  EXPECT_FALSE(code.empty());
  vm_config->mutable_code()->mutable_local()->set_inline_bytes(code);
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);

  WasmHandleSharedPtr wasm_handle;
  createWasm(plugin, scope, cluster_manager, init_manager, *dispatcher, *api, lifecycle_notifier,
             remote_data_provider,
             [&wasm_handle](const WasmHandleSharedPtr& w) { wasm_handle = w; });
  EXPECT_NE(wasm_handle, nullptr);
  lifecycle_callback([] {});

  WasmHandleSharedPtr wasm_handle2;
  createWasm(plugin, scope, cluster_manager, init_manager, *dispatcher, *api, lifecycle_notifier,
             remote_data_provider,
             [&wasm_handle2](const WasmHandleSharedPtr& w) { wasm_handle2 = w; });
  EXPECT_NE(wasm_handle2, nullptr);
  EXPECT_EQ(wasm_handle, wasm_handle2);

  auto plugin_handle_local = getOrCreateThreadLocalPlugin(
      wasm_handle, plugin,
      [&dispatcher](const WasmHandleBaseSharedPtr& base_wasm) -> WasmHandleBaseSharedPtr {
        auto wasm =
            std::make_shared<Wasm>(std::static_pointer_cast<WasmHandle>(base_wasm), *dispatcher);
        wasm->setCreateContextForTesting(
            nullptr, [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
              auto root_context = new TestContext(wasm, plugin);
              EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("on_vm_start vm_cache")));
              EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("on_done logging")));
              EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("on_delete logging")));
              return root_context;
            });
        return std::make_shared<WasmHandle>(wasm);
      },
      [](const WasmHandleBaseSharedPtr& wasm_handle,
         const PluginBaseSharedPtr& plugin) -> PluginHandleBaseSharedPtr {
        return std::make_shared<PluginHandle>(std::static_pointer_cast<WasmHandle>(wasm_handle),
                                              std::static_pointer_cast<Plugin>(plugin));
      });
  wasm_handle.reset();
  wasm_handle2.reset();

  auto wasm = plugin_handle_local->wasm();
  plugin_handle_local.reset();

  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  wasm->configure(wasm->getContext(1), plugin);
  plugin.reset();
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  dispatcher->clearDeferredDeleteList();

  proxy_wasm::clearWasmCachesForTesting();
}

TEST_P(WasmCommonTest, RemoteCode) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Server::MockServerLifecycleNotifier> lifecycle_notifier;
  Init::ExpectableWatcherImpl init_watcher;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  RemoteAsyncDataProviderPtr remote_data_provider;
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto vm_configuration = "vm_cache";
  auto plugin_configuration = "done";

  std::string code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      absl::StrCat("{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm")));

  Extensions::Common::Wasm::PluginSharedPtr plugin;
  {
    // plugin_config is only valid in this scope.
    // test that the proto_config parameter is released after the factory is created
    envoy::extensions::wasm::v3::PluginConfig plugin_config;
    *plugin_config.mutable_vm_config()->mutable_runtime() =
        absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
    plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);
    plugin_config.mutable_configuration()->set_value(plugin_configuration);

    auto vm_config = plugin_config.mutable_vm_config();
    vm_config->set_runtime(absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam())));
    ProtobufWkt::BytesValue vm_configuration_bytes;
    vm_configuration_bytes.set_value(vm_configuration);
    vm_config->mutable_configuration()->PackFrom(vm_configuration_bytes);
    std::string sha256 = Extensions::Common::Wasm::sha256(code);
    std::string sha256Hex =
        Hex::encode(reinterpret_cast<const uint8_t*>(&*sha256.begin()), sha256.size());
    vm_config->mutable_code()->mutable_remote()->set_sha256(sha256Hex);
    vm_config->mutable_code()->mutable_remote()->mutable_http_uri()->set_uri(
        "http://example.com/test.wasm");
    vm_config->mutable_code()->mutable_remote()->mutable_http_uri()->set_cluster("example_com");
    vm_config->mutable_code()->mutable_remote()->mutable_http_uri()->mutable_timeout()->set_seconds(
        5);

    plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
        plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  }

  WasmHandleSharedPtr wasm_handle;
  NiceMock<Http::MockAsyncClient> client;
  NiceMock<Http::MockAsyncClientRequest> request(&client);

  cluster_manager.initializeThreadLocalClusters({"example_com"});
  EXPECT_CALL(cluster_manager.thread_local_cluster_, httpAsyncClient())
      .WillOnce(ReturnRef(cluster_manager.thread_local_cluster_.async_client_));
  EXPECT_CALL(cluster_manager.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
            response->body().add(code);
            callbacks.onSuccess(request, std::move(response));
            return nullptr;
          }));

  Init::TargetHandlePtr init_target_handle;
  EXPECT_CALL(init_manager, add(_)).WillOnce(Invoke([&](const Init::Target& target) {
    init_target_handle = target.createHandle("test");
  }));
  createWasm(plugin, scope, cluster_manager, init_manager, *dispatcher, *api,

             lifecycle_notifier, remote_data_provider,
             [&wasm_handle](const WasmHandleSharedPtr& w) { wasm_handle = w; });

  EXPECT_CALL(init_watcher, ready());
  init_target_handle->initialize(init_watcher);

  EXPECT_NE(wasm_handle, nullptr);

  auto plugin_handle_local = getOrCreateThreadLocalPlugin(
      wasm_handle, plugin,
      [&dispatcher](const WasmHandleBaseSharedPtr& base_wasm) -> WasmHandleBaseSharedPtr {
        auto wasm =
            std::make_shared<Wasm>(std::static_pointer_cast<WasmHandle>(base_wasm), *dispatcher);
        wasm->setCreateContextForTesting(
            nullptr, [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
              auto root_context = new TestContext(wasm, plugin);
              EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("on_vm_start vm_cache")));
              EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("on_done logging")));
              EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("on_delete logging")));
              return root_context;
            });
        return std::make_shared<WasmHandle>(wasm);
      },
      [](const WasmHandleBaseSharedPtr& wasm_handle,
         const PluginBaseSharedPtr& plugin) -> PluginHandleBaseSharedPtr {
        return std::make_shared<PluginHandle>(std::static_pointer_cast<WasmHandle>(wasm_handle),
                                              std::static_pointer_cast<Plugin>(plugin));
      });
  wasm_handle.reset();

  auto wasm = plugin_handle_local->wasm();
  plugin_handle_local.reset();

  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  wasm->configure(wasm->getContext(1), plugin);
  plugin.reset();
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  dispatcher->clearDeferredDeleteList();
}

TEST_P(WasmCommonTest, RemoteCodeMultipleRetry) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Server::MockServerLifecycleNotifier> lifecycle_notifier;
  Init::ExpectableWatcherImpl init_watcher;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  RemoteAsyncDataProviderPtr remote_data_provider;
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto vm_configuration = "vm_cache";
  auto plugin_configuration = "done";

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);
  plugin_config.mutable_configuration()->set_value(plugin_configuration);

  std::string code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      absl::StrCat("{{ test_rundir }}/test/extensions/common/wasm/test_data/test_cpp.wasm")));

  auto vm_config = plugin_config.mutable_vm_config();
  vm_config->set_runtime(absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam())));
  ProtobufWkt::StringValue vm_configuration_string;
  vm_configuration_string.set_value(vm_configuration);
  vm_config->mutable_configuration()->PackFrom(vm_configuration_string);
  std::string sha256 = Extensions::Common::Wasm::sha256(code);
  std::string sha256Hex =
      Hex::encode(reinterpret_cast<const uint8_t*>(&*sha256.begin()), sha256.size());
  int num_retries = 3;
  vm_config->mutable_code()->mutable_remote()->set_sha256(sha256Hex);
  vm_config->mutable_code()->mutable_remote()->mutable_http_uri()->set_uri(
      "http://example.com/test.wasm");
  vm_config->mutable_code()->mutable_remote()->mutable_http_uri()->set_cluster("example_com");
  vm_config->mutable_code()->mutable_remote()->mutable_http_uri()->mutable_timeout()->set_seconds(
      5);
  vm_config->mutable_code()
      ->mutable_remote()
      ->mutable_retry_policy()
      ->mutable_num_retries()
      ->set_value(num_retries);
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);

  WasmHandleSharedPtr wasm_handle;
  NiceMock<Http::MockAsyncClient> client;
  NiceMock<Http::MockAsyncClientRequest> request(&client);

  cluster_manager.initializeThreadLocalClusters({"example_com"});
  EXPECT_CALL(cluster_manager.thread_local_cluster_, httpAsyncClient())
      .WillRepeatedly(ReturnRef(cluster_manager.thread_local_cluster_.async_client_));
  EXPECT_CALL(cluster_manager.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(Invoke([&, retry = num_retries](
                                 Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                                 const Http::AsyncClient::RequestOptions&) mutable
                             -> Http::AsyncClient::Request* {
        if (retry-- == 0) {
          Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(
              Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "503"}}}));
          callbacks.onSuccess(request, std::move(response));
          return nullptr;
        } else {
          Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(
              Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
          response->body().add(code);
          callbacks.onSuccess(request, std::move(response));
          return nullptr;
        }
      }));

  Init::TargetHandlePtr init_target_handle;
  EXPECT_CALL(init_manager, add(_)).WillOnce(Invoke([&](const Init::Target& target) {
    init_target_handle = target.createHandle("test");
  }));
  createWasm(plugin, scope, cluster_manager, init_manager, *dispatcher, *api, lifecycle_notifier,
             remote_data_provider,
             [&wasm_handle](const WasmHandleSharedPtr& w) { wasm_handle = w; });

  EXPECT_CALL(init_watcher, ready());
  init_target_handle->initialize(init_watcher);

  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_NE(wasm_handle, nullptr);

  auto plugin_handle_local = getOrCreateThreadLocalPlugin(
      wasm_handle, plugin,
      [&dispatcher](const WasmHandleBaseSharedPtr& base_wasm) -> WasmHandleBaseSharedPtr {
        auto wasm =
            std::make_shared<Wasm>(std::static_pointer_cast<WasmHandle>(base_wasm), *dispatcher);
        wasm->setCreateContextForTesting(
            nullptr, [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
              auto root_context = new TestContext(wasm, plugin);
              EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("on_vm_start vm_cache")));
              EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("on_done logging")));
              EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("on_delete logging")));
              return root_context;
            });
        return std::make_shared<WasmHandle>(wasm);
      },
      [](const WasmHandleBaseSharedPtr& wasm_handle,
         const PluginBaseSharedPtr& plugin) -> PluginHandleBaseSharedPtr {
        return std::make_shared<PluginHandle>(std::static_pointer_cast<WasmHandle>(wasm_handle),
                                              std::static_pointer_cast<Plugin>(plugin));
      });
  wasm_handle.reset();

  auto wasm = plugin_handle_local->wasm();
  plugin_handle_local.reset();

  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  wasm->configure(wasm->getContext(1), plugin);
  plugin.reset();
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  dispatcher->clearDeferredDeleteList();
}

// test that wasm imports/exports do not work when ABI restriction is enforced
TEST_P(WasmCommonTest, RestrictCapabilities) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto vm_configuration = "restrict_all";

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);

  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_restriction_cpp.wasm"));
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey("", vm_configuration, code);

  // restriction enforced if allowed_capabilities is non-empty
  proxy_wasm::AllowedCapabilitiesMap allowed_capabilities{
      {"foo", proxy_wasm::SanitizationConfig()}};
  plugin->wasmConfig().allowedCapabilities() = allowed_capabilities;
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(plugin->wasmConfig(), vm_key, scope,
                                                               *api, cluster_manager, *dispatcher);

  EXPECT_FALSE(wasm->capabilityAllowed("proxy_on_vm_start"));
  EXPECT_FALSE(wasm->capabilityAllowed("proxy_log"));
  EXPECT_FALSE(wasm->capabilityAllowed("fd_write"));

  EXPECT_NE(wasm, nullptr);
  auto context = std::make_unique<TestContext>(wasm.get());
  EXPECT_TRUE(wasm->load(code, false));
  EXPECT_TRUE(wasm->initialize());

  // expect no call because proxy_on_vm_start is restricted
  wasm->setCreateContextForTesting(
      nullptr, [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
        auto root_context = new TestContext(wasm, plugin);
        EXPECT_CALL(*root_context,
                    log_(spdlog::level::info, Eq("after proxy_on_vm_start: written by proxy_log")))
            .Times(0);
        EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("WASI write to stdout"))).Times(0);
        return root_context;
      });
  wasm->start(plugin);
}

// test with proxy_on_vm_start allowed, but proxy_log restricted
TEST_P(WasmCommonTest, AllowOnVmStart) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto vm_configuration = "allow_on_vm_start";

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);

  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_restriction_cpp.wasm"));
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey("", vm_configuration, code);
  proxy_wasm::AllowedCapabilitiesMap allowed_capabilities{
      {"proxy_on_vm_start", proxy_wasm::SanitizationConfig()}};
  plugin->wasmConfig().allowedCapabilities() = allowed_capabilities;
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(plugin->wasmConfig(), vm_key, scope,
                                                               *api, cluster_manager, *dispatcher);

  EXPECT_TRUE(wasm->capabilityAllowed("proxy_on_vm_start"));
  EXPECT_FALSE(wasm->capabilityAllowed("proxy_log"));
  EXPECT_FALSE(wasm->capabilityAllowed("fd_write"));

  EXPECT_NE(wasm, nullptr);
  auto context = std::make_unique<TestContext>(wasm.get());
  EXPECT_TRUE(wasm->load(code, false));
  EXPECT_TRUE(wasm->initialize());

  // proxy_on_vm_start will trigger proxy_log and fd_write, but expect no call because both are
  // restricted
  wasm->setCreateContextForTesting(
      nullptr, [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
        auto root_context = new TestContext(wasm, plugin);
        EXPECT_CALL(*root_context,
                    log_(spdlog::level::info, Eq("after proxy_on_vm_start: written by proxy_log")))
            .Times(0);
        EXPECT_CALL(*root_context, log_(spdlog::level::info,
                                        Eq("after proxy_on_context_create: written by proxy_log")))
            .Times(0);
        EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("WASI write to stdout"))).Times(0);
        return root_context;
      });
  wasm->start(plugin);
}

// test with both proxy_on_vm_start and proxy_log allowed, but WASI restricted
TEST_P(WasmCommonTest, AllowLog) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto vm_configuration = "allow_log";

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);

  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_restriction_cpp.wasm"));
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey("", vm_configuration, code);
  proxy_wasm::AllowedCapabilitiesMap allowed_capabilities{
      {"proxy_on_vm_start", proxy_wasm::SanitizationConfig()},
      {"proxy_log", proxy_wasm::SanitizationConfig()}};
  plugin->wasmConfig().allowedCapabilities() = allowed_capabilities;
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(plugin->wasmConfig(), vm_key, scope,
                                                               *api, cluster_manager, *dispatcher);

  // Restrict capabilities, but allow proxy_log
  EXPECT_TRUE(wasm->capabilityAllowed("proxy_on_vm_start"));
  EXPECT_TRUE(wasm->capabilityAllowed("proxy_log"));
  EXPECT_FALSE(wasm->capabilityAllowed("fd_write"));

  EXPECT_NE(wasm, nullptr);
  auto context = std::make_unique<TestContext>(wasm.get());
  EXPECT_TRUE(wasm->load(code, false));
  EXPECT_TRUE(wasm->initialize());

  // Expect proxy_log since allowed, but no call to WASI since restricted
  wasm->setCreateContextForTesting(
      nullptr, [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
        auto root_context = new TestContext(wasm, plugin);
        EXPECT_CALL(*root_context,
                    log_(spdlog::level::info, Eq("after proxy_on_vm_start: written by proxy_log")));
        EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("WASI write to stdout"))).Times(0);
        return root_context;
      });
  wasm->start(plugin);
}

// test with both proxy_on_vm_start and fd_write allowed, but proxy_log restricted
TEST_P(WasmCommonTest, AllowWASI) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto vm_configuration = "allow_wasi";

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);

  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_restriction_cpp.wasm"));
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey("", vm_configuration, code);
  proxy_wasm::AllowedCapabilitiesMap allowed_capabilities{
      {"proxy_on_vm_start", proxy_wasm::SanitizationConfig()},
      {"fd_write", proxy_wasm::SanitizationConfig()}};
  plugin->wasmConfig().allowedCapabilities() = allowed_capabilities;
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(plugin->wasmConfig(), vm_key, scope,
                                                               *api, cluster_manager, *dispatcher);

  // Restrict capabilities, but allow fd_write
  EXPECT_TRUE(wasm->capabilityAllowed("proxy_on_vm_start"));
  EXPECT_FALSE(wasm->capabilityAllowed("proxy_log"));
  EXPECT_TRUE(wasm->capabilityAllowed("fd_write"));

  EXPECT_NE(wasm, nullptr);
  auto context = std::make_unique<TestContext>(wasm.get());
  EXPECT_TRUE(wasm->load(code, false));
  EXPECT_TRUE(wasm->initialize());

  wasm->setCreateContextForTesting(
      nullptr, [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
        auto root_context = new TestContext(wasm, plugin);
        EXPECT_CALL(*root_context,
                    log_(spdlog::level::info, Eq("after proxy_on_vm_start: written by proxy_log")))
            .Times(0);
        EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("WASI write to stdout")));
        return root_context;
      });
  wasm->start(plugin);
}

// test a different callback besides proxy_on_vm_start
TEST_P(WasmCommonTest, AllowOnContextCreate) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto vm_configuration = "allow_on_context_create";

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);

  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_restriction_cpp.wasm"));
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey("", vm_configuration, code);
  proxy_wasm::AllowedCapabilitiesMap allowed_capabilities{
      {"proxy_on_vm_start", proxy_wasm::SanitizationConfig()},
      {"proxy_on_context_create", proxy_wasm::SanitizationConfig()},
      {"proxy_log", proxy_wasm::SanitizationConfig()}};
  plugin->wasmConfig().allowedCapabilities() = allowed_capabilities;
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(plugin->wasmConfig(), vm_key, scope,
                                                               *api, cluster_manager, *dispatcher);

  // Restrict capabilities, but allow proxy_log
  EXPECT_TRUE(wasm->capabilityAllowed("proxy_on_vm_start"));
  EXPECT_TRUE(wasm->capabilityAllowed("proxy_on_context_create"));
  EXPECT_TRUE(wasm->capabilityAllowed("proxy_log"));

  EXPECT_NE(wasm, nullptr);
  auto context = std::make_unique<TestContext>(wasm.get());
  EXPECT_TRUE(wasm->load(code, false));
  EXPECT_TRUE(wasm->initialize());

  // Expect two calls to proxy_log, from proxy_on_vm_start and from proxy_on_context_create
  wasm->setCreateContextForTesting(
      nullptr, [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
        auto root_context = new TestContext(wasm, plugin);
        EXPECT_CALL(*root_context,
                    log_(spdlog::level::info, Eq("after proxy_on_vm_start: written by proxy_log")));
        EXPECT_CALL(*root_context, log_(spdlog::level::info,
                                        Eq("after proxy_on_context_create: written by proxy_log")));
        return root_context;
      });
  wasm->start(plugin);
}

// test that a copy-constructed thread-local Wasm still enforces the same policy
TEST_P(WasmCommonTest, ThreadLocalCopyRetainsEnforcement) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto vm_configuration = "thread_local_copy";

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() =
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam()));
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration);

  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_restriction_cpp.wasm"));
  EXPECT_FALSE(code.empty());
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto vm_key = proxy_wasm::makeVmKey("", vm_configuration, code);
  proxy_wasm::AllowedCapabilitiesMap allowed_capabilities{
      {"proxy_on_vm_start", proxy_wasm::SanitizationConfig()},
      {"fd_write", proxy_wasm::SanitizationConfig()}};
  plugin->wasmConfig().allowedCapabilities() = allowed_capabilities;
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(plugin->wasmConfig(), vm_key, scope,
                                                               *api, cluster_manager, *dispatcher);

  // Restrict capabilities
  EXPECT_TRUE(wasm->capabilityAllowed("proxy_on_vm_start"));
  EXPECT_FALSE(wasm->capabilityAllowed("proxy_log"));
  EXPECT_TRUE(wasm->capabilityAllowed("fd_write"));

  EXPECT_NE(wasm, nullptr);
  auto context = std::make_unique<TestContext>(wasm.get());
  EXPECT_TRUE(wasm->load(code, false));
  EXPECT_TRUE(wasm->initialize());

  auto wasm_handle = std::make_shared<Extensions::Common::Wasm::WasmHandle>(std::move(wasm));
  auto thread_local_wasm = std::make_shared<Wasm>(wasm_handle, *dispatcher);

  EXPECT_NE(thread_local_wasm, nullptr);
  context = std::make_unique<TestContext>(thread_local_wasm.get());
  EXPECT_TRUE(thread_local_wasm->initialize());

  EXPECT_TRUE(thread_local_wasm->capabilityAllowed("proxy_on_vm_start"));
  EXPECT_FALSE(thread_local_wasm->capabilityAllowed("proxy_log"));
  EXPECT_TRUE(thread_local_wasm->capabilityAllowed("fd_write"));

  // Module will call proxy_log, expect no call since all capabilities restricted
  thread_local_wasm->setCreateContextForTesting(
      nullptr, [](Wasm* thread_local_wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
        auto root_context = new TestContext(thread_local_wasm, plugin);
        EXPECT_CALL(*root_context,
                    log_(spdlog::level::info, Eq("after proxy_on_vm_start: written by proxy_log")))
            .Times(0);
        EXPECT_CALL(*root_context, log_(spdlog::level::info, Eq("WASI write to stdout")));
        return root_context;
      });
  thread_local_wasm->start(plugin);
}

class WasmCommonContextTest : public Common::Wasm::WasmHttpFilterTestBase<
                                  testing::TestWithParam<std::tuple<std::string, std::string>>> {
public:
  WasmCommonContextTest() = default;

  void setup(const std::string& code, std::string vm_configuration, std::string root_id = "") {
    setRootId(root_id);
    setVmConfiguration(vm_configuration);
    setupBase(std::get<0>(GetParam()), code,
              [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
                return new TestContext(wasm, plugin);
              });
  }
  void setupContext() { setupFilterBase<TestContext>(); }

  TestContext& rootContext() { return *static_cast<TestContext*>(root_context_); }
  TestContext& context() { return *static_cast<TestContext*>(context_.get()); }
};

INSTANTIATE_TEST_SUITE_P(Runtimes, WasmCommonContextTest,
                         Envoy::Extensions::Common::Wasm::runtime_and_cpp_values);

TEST_P(WasmCommonContextTest, OnDnsResolve) {
  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(absl::StrCat(
        "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_context_cpp.wasm")));
  } else {
    // The name of the Null VM plugin.
    code = "CommonWasmTestContextCpp";
  }
  EXPECT_FALSE(code.empty());

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  NiceMock<Network::MockDnsResolverFactory> dns_resolver_factory;
  Registry::InjectFactory<Network::DnsResolverFactory> registered_dns_factory(dns_resolver_factory);
  EXPECT_CALL(dns_resolver_factory, createDnsResolver(_, _, _))
      .WillRepeatedly(Return(dns_resolver));
  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(
          testing::DoAll(testing::SaveArg<2>(&dns_callback), Return(&active_dns_query)));

  setup(code, "context");
  setupContext();
  EXPECT_CALL(rootContext(), log_(spdlog::level::warn, Eq("TestRootContext::onResolveDns 1")));
  EXPECT_CALL(rootContext(), log_(spdlog::level::warn, Eq("TestRootContext::onResolveDns 2")));
  EXPECT_CALL(rootContext(), log_(spdlog::level::info,
                                  Eq("TestRootContext::onResolveDns dns 1001 192.168.1.101:0")));
  EXPECT_CALL(rootContext(), log_(spdlog::level::info,
                                  Eq("TestRootContext::onResolveDns dns 1001 192.168.1.102:0")));
  EXPECT_CALL(rootContext(), log_(spdlog::level::warn, Eq("TestRootContext::onDone 1")));

  dns_callback(
      Network::DnsResolver::ResolutionStatus::Completed, "",
      TestUtility::makeDnsResponse({"192.168.1.101", "192.168.1.102"}, std::chrono::seconds(1001)));

  rootContext().onResolveDns(1 /* token */, Envoy::Network::DnsResolver::ResolutionStatus::Failure,
                             {});
  if (std::get<0>(GetParam()) == "null") {
    rootContext().onTick(0);
  }
  if (std::get<0>(GetParam()) == "v8") {
    rootContext().onQueueReady(0);
  }
  // Wait till the Wasm is destroyed and then the late callback should do nothing.
  deferred_runner_.setFunction([dns_callback] {
    dns_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
                 TestUtility::makeDnsResponse({"192.168.1.101", "192.168.1.102"},
                                              std::chrono::seconds(1001)));
  });
}

TEST_P(WasmCommonContextTest, EmptyContext) {
  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(absl::StrCat(
        "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_context_cpp.wasm")));
  } else {
    // The name of the Null VM plugin.
    code = "CommonWasmTestContextCpp";
  }
  EXPECT_FALSE(code.empty());

  setup(code, "context", "empty");
  setupContext();

  root_context_->onResolveDns(0, Envoy::Network::DnsResolver::ResolutionStatus::Completed, {});
  NiceMock<Envoy::Stats::MockMetricSnapshot> stats_snapshot;
  root_context_->onStatsUpdate(stats_snapshot);
  root_context_->validateConfiguration("", plugin_);
}

// test that we don't send the local reply twice when the wasm code panics
TEST_P(WasmCommonContextTest, LocalReplyWhenPanic) {
  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(absl::StrCat(
        "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_context_cpp.wasm")));
  } else {
    // Let's not cause crashes in Null VM
    return;
  }
  EXPECT_FALSE(code.empty());

  setup(code, "context", "panic during request processing");
  setupContext();
  // In the case of VM failure, failStream is called, so we need to make sure that we don't send the
  // local reply twice.
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Envoy::Http::Code::ServiceUnavailable, testing::Eq(""), _,
                             testing::Eq(Grpc::Status::WellKnownGrpcStatus::Unavailable),
                             testing::Eq("wasm_fail_stream")));

  // Create in-VM context.
  context().onCreate();
  EXPECT_EQ(proxy_wasm::FilterDataStatus::StopIterationNoBuffer, context().onRequestBody(0, false));
}

// test that in case -1 is send from wasm it propagate nullopt
TEST_P(WasmCommonContextTest, ProcessInvalidGRPCStatusCodeAsEmptyInLocalReply) {
  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(absl::StrCat(
        "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_context_cpp.wasm")));
  } else {
    // The name of the Null VM plugin.
    code = "CommonWasmTestContextCpp";
  }
  EXPECT_FALSE(code.empty());

  setup(code, "context", "send local reply grpc");
  setupContext();
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce([this](Http::ResponseHeaderMap&, bool) { context().onResponseHeaders(0, false); });
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Envoy::Http::Code::OK, testing::Eq("body"), _,
                                                 testing::Eq(absl::nullopt), testing::Eq("ok")));

  // Create in-VM context.
  context().onCreate();
  EXPECT_EQ(proxy_wasm::FilterDataStatus::StopIterationNoBuffer, context().onRequestBody(0, false));
}

// test that in case valid grpc status is send from wasm it propagate as it is
TEST_P(WasmCommonContextTest, ProcessValidGRPCStatusCodeAsEmptyInLocalReply) {
  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(absl::StrCat(
        "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_context_cpp.wasm")));
  } else {
    // The name of the Null VM plugin.
    code = "CommonWasmTestContextCpp";
  }
  EXPECT_FALSE(code.empty());

  setup(code, "context", "send local reply grpc");
  setupContext();
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce([this](Http::ResponseHeaderMap&, bool) { context().onResponseHeaders(0, false); });
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Envoy::Http::Code::OK, testing::Eq("body"), _,
                             testing::Eq(Grpc::Status::WellKnownGrpcStatus::PermissionDenied),
                             testing::Eq("ok")));

  // Create in-VM context.
  context().onCreate();
  EXPECT_EQ(proxy_wasm::FilterDataStatus::StopIterationNoBuffer, context().onRequestBody(1, false));
}

class WasmLocalReplyTest : public WasmCommonContextTest {
public:
  WasmLocalReplyTest() = default;

  void setup(const std::string& code, std::string vm_configuration, std::string root_id = "") {
    WasmCommonContextTest::setup(code, vm_configuration, root_id);
    filter_manager_ = std::make_unique<Http::DownstreamFilterManager>(
        filter_manager_callbacks_, dispatcher_, connection_, 0, nullptr, true, 10000,
        filter_factory_, local_reply_, protocol_, time_source_, filter_state_, overload_manager_);
    request_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}};
    request_data_ = Envoy::Buffer::OwnedImpl("body");
  }

  Http::StreamFilterSharedPtr filter() { return context_; }

  Http::FilterFactoryCb createWasmFilter() {
    return [this](Http::FilterChainFactoryCallbacks& callbacks) {
      callbacks.addStreamFilter(filter());
    };
  }

  void setupContext() {
    WasmCommonContextTest::setupContext();
    ON_CALL(filter_factory_, createFilterChain(_))
        .WillByDefault(Invoke([this](Http::FilterChainManager& manager) -> bool {
          auto factory = createWasmFilter();
          manager.applyFilterFactoryCb({}, factory);
          return true;
        }));
    ON_CALL(filter_manager_callbacks_, requestHeaders())
        .WillByDefault(Return(makeOptRef(*request_headers_)));
    filter_manager_->createFilterChain();
    filter_manager_->requestHeadersInitialized();
  }

  std::unique_ptr<Http::FilterManager> filter_manager_;
  NiceMock<Http::MockFilterManagerCallbacks> filter_manager_callbacks_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Envoy::Http::MockFilterChainFactory> filter_factory_;
  NiceMock<LocalReply::MockLocalReply> local_reply_;
  Http::Protocol protocol_{Http::Protocol::Http2};
  NiceMock<MockTimeSystem> time_source_;
  StreamInfo::FilterStateSharedPtr filter_state_ =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  NiceMock<Server::MockOverloadManager> overload_manager_;
  Http::RequestHeaderMapPtr request_headers_;
  Envoy::Buffer::OwnedImpl request_data_;
};

INSTANTIATE_TEST_SUITE_P(Runtimes, WasmLocalReplyTest,
                         Envoy::Extensions::Common::Wasm::runtime_and_cpp_values);

TEST_P(WasmLocalReplyTest, DuplicateLocalReply) {
  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(absl::StrCat(
        "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_context_cpp.wasm")));
  } else {
    // The name of the Null VM plugin.
    code = "CommonWasmTestContextCpp";
  }
  EXPECT_FALSE(code.empty());

  setup(code, "context", "send local reply twice");
  setupContext();

  // Even if sendLocalReply is called multiple times it should only generate a single
  // response to the client, so encodeHeaders should only be called once
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, _));
  EXPECT_CALL(filter_manager_callbacks_, endStream());
  filter_manager_->decodeHeaders(*request_headers_, false);
  filter_manager_->decodeData(request_data_, false);
  filter_manager_->destroyFilters();
}

TEST_P(WasmLocalReplyTest, LocalReplyInRequestAndResponse) {
  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(absl::StrCat(
        "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_context_cpp.wasm")));
  } else {
    code = "CommonWasmTestContextCpp";
  }
  EXPECT_FALSE(code.empty());

  setup(code, "context", "local reply in request and response");
  setupContext();

  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, _));
  EXPECT_CALL(filter_manager_callbacks_, endStream());
  filter_manager_->decodeHeaders(*request_headers_, false);
  filter_manager_->decodeData(request_data_, false);
  filter_manager_->destroyFilters();
}

TEST_P(WasmLocalReplyTest, PanicDuringResponse) {
  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(absl::StrCat(
        "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_context_cpp.wasm")));
  } else {
    // Let's not cause crashes in Null VM
    return;
  }
  EXPECT_FALSE(code.empty());

  setup(code, "context", "panic during response processing");
  setupContext();

  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, _));
  EXPECT_CALL(filter_manager_callbacks_, endStream());

  filter_manager_->decodeHeaders(*request_headers_, false);
  filter_manager_->decodeData(request_data_, false);
  filter_manager_->destroyFilters();
}

class PluginConfigTest : public testing::TestWithParam<std::tuple<std::string, std::string>> {
public:
  PluginConfigTest() = default;

  // NOLINTNEXTLINE(readability-identifier-naming)
  void SetUp() override { clearCodeCacheForTesting(); }

  void setUp(const std::string& plugin_config_yaml, bool singleton) {
    envoy::extensions::wasm::v3::PluginConfig plugin_config;
    TestUtility::loadFromYaml(plugin_config_yaml, plugin_config);
    plugin_config_ = std::make_shared<PluginConfig>(
        plugin_config, server_, server_.scope(), server_.initManager(),
        envoy::config::core::v3::TrafficDirection::UNSPECIFIED, nullptr /* metadata */, singleton);
  }

  void createContext() {
    context_ = plugin_config_->createContext();
    if (context_ != nullptr) {
      context_->setDecoderFilterCallbacks(decoder_callbacks_);
      context_->setEncoderFilterCallbacks(encoder_callbacks_);
      context_->onCreate();
    }
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_;
  PluginConfigSharedPtr plugin_config_;

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;

  std::shared_ptr<Context> context_;
};

INSTANTIATE_TEST_SUITE_P(PluginConfigRuntimes, PluginConfigTest,
                         Envoy::Extensions::Common::Wasm::runtime_and_cpp_values,
                         Envoy::Extensions::Common::Wasm::wasmTestParamsToString);

TEST_P(PluginConfigTest, FailReloadPolicy) {
  auto [runtime, language] = GetParam();
  if (runtime == "null") {
    return;
  }

  const std::string runtime_name = runtime;

  const std::string code =
      TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(absl::StrCat(
          "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_context_cpp.wasm")));

  auto test_func = [this, runtime_name, code](bool singleton) {
    const std::string plugin_config_yaml = fmt::format(
        R"EOF(
name: "{}_test_wasm_reload"
root_id: "panic during request processing"
failure_policy: FAIL_RELOAD
vm_config:
  runtime: "envoy.wasm.runtime.{}"
  configuration:
      "@type": "type.googleapis.com/google.protobuf.StringValue"
      value: "some configuration"
  code:
    local:
      inline_bytes: "{}"
)EOF",
        singleton, runtime_name, Base64::encode(code.data(), code.size()));

    proxy_wasm::clearWasmCachesForTesting(); // clear the cache to make sure we get a new one wasm.

    setUp(plugin_config_yaml, singleton);
    Envoy::Buffer::OwnedImpl request_body;

    Wasm* initial_wasm = plugin_config_->wasm();
    EXPECT_NE(nullptr, initial_wasm);
    EXPECT_EQ(initial_wasm->fail_state(), proxy_wasm::FailState::Ok);

    // Create first context.
    createContext();
    EXPECT_NE(nullptr, context_.get());

    EXPECT_CALL(decoder_callbacks_,
                sendLocalReply(Envoy::Http::Code::ServiceUnavailable, testing::Eq(""), _,
                               testing::Eq(Grpc::Status::WellKnownGrpcStatus::Unavailable),
                               testing::Eq("wasm_fail_stream")));
    EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
              context_->decodeData(request_body, true));

    // The wasm should be in runtime error state now.
    EXPECT_EQ(initial_wasm->fail_state(), proxy_wasm::FailState::RuntimeError);

    // Wait 2 seconds to avoid possible backoff.
    server_.dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::seconds(2));

    // Create second context and reload the wasm vm will be reload automatically.
    createContext();
    EXPECT_NE(nullptr, context_.get());
    Wasm* context_wasm = context_->wasm();

    EXPECT_NE(nullptr, context_wasm);
    EXPECT_NE(initial_wasm, context_wasm);
    EXPECT_EQ(context_wasm->fail_state(), proxy_wasm::FailState::Ok);

    EXPECT_EQ(plugin_config_->wasmStats().vm_reload_success_.value(), 1);
    EXPECT_EQ(plugin_config_->wasmStats().vm_reload_backoff_.value(), 0);

    EXPECT_CALL(decoder_callbacks_,
                sendLocalReply(Envoy::Http::Code::ServiceUnavailable, testing::Eq(""), _,
                               testing::Eq(Grpc::Status::WellKnownGrpcStatus::Unavailable),
                               testing::Eq("wasm_fail_stream")));
    EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
              context_->decodeData(request_body, true));

    // The wasm should be in runtime error state again.
    EXPECT_EQ(context_wasm->fail_state(), proxy_wasm::FailState::RuntimeError);

    // The wasm failed again and the PluginConfig::wasm() will try to reload again but will backoff.
    // The previous wasm will be returned.
    EXPECT_EQ(plugin_config_->wasm(), context_wasm);

    EXPECT_EQ(plugin_config_->wasmStats().vm_reload_success_.value(), 1);
    EXPECT_EQ(plugin_config_->wasmStats().vm_reload_backoff_.value(), 1);

    // Wait 2 seconds.
    server_.dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::seconds(2));

    // Now the wasm should be reloaded again after the backoff.
    Wasm* latest_wasm = plugin_config_->wasm();
    EXPECT_NE(nullptr, latest_wasm);
    EXPECT_NE(context_wasm, latest_wasm);
    EXPECT_EQ(latest_wasm->fail_state(), proxy_wasm::FailState::Ok);

    // Mock another failure.
    latest_wasm->fail(proxy_wasm::FailState::MissingFunction, "mocked failure");

    // Wait 2 seconds.
    server_.dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::seconds(2));

    // No reload should happen because the fail state is not reloadable.
    EXPECT_EQ(plugin_config_->wasm(), latest_wasm);

    // Create a new context.
    createContext();
    EXPECT_NE(nullptr, context_.get());

    // Fallback to the FAIL_CLOSED policy.
    EXPECT_CALL(decoder_callbacks_,
                sendLocalReply(Envoy::Http::Code::ServiceUnavailable, testing::Eq(""), _,
                               testing::Eq(Grpc::Status::WellKnownGrpcStatus::Unavailable),
                               testing::Eq("wasm_fail_stream")));
    EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
              context_->decodeData(request_body, true));
  };

  test_func(false);
  test_func(true);
}

TEST_P(PluginConfigTest, FailClosedPolicy) {
  auto [runtime, language] = GetParam();
  if (runtime == "null") {
    return;
  }

  const std::string runtime_name = runtime;

  const std::string code =
      TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(absl::StrCat(
          "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_context_cpp.wasm")));

  auto test_func = [this, runtime_name, code](bool singleton) {
    const std::string plugin_config_yaml = fmt::format(
        R"EOF(
name: "{}_test_wasm_reload"
root_id: "panic during request processing"
failure_policy: FAIL_CLOSED
vm_config:
  runtime: "envoy.wasm.runtime.{}"
  configuration:
      "@type": "type.googleapis.com/google.protobuf.StringValue"
      value: "some configuration"
  code:
    local:
      inline_bytes: "{}"
)EOF",
        singleton, runtime_name, Base64::encode(code.data(), code.size()));

    proxy_wasm::clearWasmCachesForTesting(); // clear the cache to make sure we get a new one wasm.

    setUp(plugin_config_yaml, singleton);
    Envoy::Buffer::OwnedImpl request_body;

    Wasm* initial_wasm = plugin_config_->wasm();
    EXPECT_NE(nullptr, initial_wasm);
    EXPECT_EQ(initial_wasm->fail_state(), proxy_wasm::FailState::Ok);

    // Create first context.
    createContext();
    EXPECT_NE(nullptr, context_.get());

    // In the case of VM failure, failStream is called, so we need to make sure that we don't send
    // the local reply twice.
    EXPECT_CALL(decoder_callbacks_,
                sendLocalReply(Envoy::Http::Code::ServiceUnavailable, testing::Eq(""), _,
                               testing::Eq(Grpc::Status::WellKnownGrpcStatus::Unavailable),
                               testing::Eq("wasm_fail_stream")));
    EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
              context_->decodeData(request_body, true));

    // The wasm should be in runtime error state.
    EXPECT_EQ(initial_wasm->fail_state(), proxy_wasm::FailState::RuntimeError);

    // Wait 2 seconds.
    server_.dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::seconds(2));

    // Create second context but the wasm is not reloaded.
    createContext();
    EXPECT_NE(nullptr, context_.get());

    Wasm* latest_wasm = plugin_config_->wasm();
    EXPECT_NE(nullptr, latest_wasm);
    EXPECT_EQ(latest_wasm, initial_wasm);
    EXPECT_EQ(latest_wasm->fail_state(), proxy_wasm::FailState::RuntimeError);

    // Request will still fail because the wasm is not reloaded.
    EXPECT_CALL(decoder_callbacks_,
                sendLocalReply(Envoy::Http::Code::ServiceUnavailable, testing::Eq(""), _,
                               testing::Eq(Grpc::Status::WellKnownGrpcStatus::Unavailable),
                               testing::Eq("wasm_fail_stream")));
    EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
              context_->decodeData(request_body, true));
  };

  test_func(false);
  test_func(true);
}

TEST_P(PluginConfigTest, FailUnspecifiedPolicy) {
  auto [runtime, language] = GetParam();
  if (runtime == "null") {
    return;
  }

  const std::string runtime_name = runtime;

  const std::string code =
      TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(absl::StrCat(
          "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_context_cpp.wasm")));

  auto test_func = [this, runtime_name, code](bool singleton) {
    const std::string plugin_config_yaml = fmt::format(
        R"EOF(
name: "{}_test_wasm_reload"
root_id: "panic during request processing"
vm_config:
  runtime: "envoy.wasm.runtime.{}"
  configuration:
      "@type": "type.googleapis.com/google.protobuf.StringValue"
      value: "some configuration"
  code:
    local:
      inline_bytes: "{}"
)EOF",
        singleton, runtime_name, Base64::encode(code.data(), code.size()));

    proxy_wasm::clearWasmCachesForTesting(); // clear the cache to make sure we get a new one wasm.

    setUp(plugin_config_yaml, singleton);
    Envoy::Buffer::OwnedImpl request_body;

    Wasm* initial_wasm = plugin_config_->wasm();
    EXPECT_NE(nullptr, initial_wasm);
    EXPECT_EQ(initial_wasm->fail_state(), proxy_wasm::FailState::Ok);

    // Create first context.
    createContext();
    EXPECT_NE(nullptr, context_.get());

    // In the case of VM failure, failStream is called, so we need to make sure that we don't send
    // the local reply twice.
    EXPECT_CALL(decoder_callbacks_,
                sendLocalReply(Envoy::Http::Code::ServiceUnavailable, testing::Eq(""), _,
                               testing::Eq(Grpc::Status::WellKnownGrpcStatus::Unavailable),
                               testing::Eq("wasm_fail_stream")));
    EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
              context_->decodeData(request_body, true));

    // The wasm should be in runtime error state.
    EXPECT_EQ(initial_wasm->fail_state(), proxy_wasm::FailState::RuntimeError);

    // Wait 2 seconds.
    server_.dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::seconds(2));

    // Create second context but the wasm is not reloaded.
    createContext();
    EXPECT_NE(nullptr, context_.get());

    Wasm* latest_wasm = plugin_config_->wasm();
    EXPECT_NE(nullptr, latest_wasm);
    EXPECT_EQ(latest_wasm, initial_wasm);
    EXPECT_EQ(latest_wasm->fail_state(), proxy_wasm::FailState::RuntimeError);

    // Request will still fail because the wasm is not reloaded.
    EXPECT_CALL(decoder_callbacks_,
                sendLocalReply(Envoy::Http::Code::ServiceUnavailable, testing::Eq(""), _,
                               testing::Eq(Grpc::Status::WellKnownGrpcStatus::Unavailable),
                               testing::Eq("wasm_fail_stream")));
    EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
              context_->decodeData(request_body, true));
  };

  test_func(false);
  test_func(true);
}

TEST_P(PluginConfigTest, FailOpenPolicy) {
  auto [runtime, language] = GetParam();
  if (runtime == "null") {
    return;
  }

  const std::string runtime_name = runtime;

  const std::string code =
      TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(absl::StrCat(
          "{{ test_rundir }}/test/extensions/common/wasm/test_data/test_context_cpp.wasm")));

  auto test_func = [this, runtime_name, code](bool singleton) {
    const std::string plugin_config_yaml = fmt::format(
        R"EOF(
name: "{}_test_wasm_reload"
root_id: "panic during request processing"
failure_policy: FAIL_OPEN
vm_config:
  runtime: "envoy.wasm.runtime.{}"
  configuration:
      "@type": "type.googleapis.com/google.protobuf.StringValue"
      value: "some configuration"
  code:
    local:
      inline_bytes: "{}"
)EOF",
        singleton, runtime_name, Base64::encode(code.data(), code.size()));

    proxy_wasm::clearWasmCachesForTesting(); // clear the cache to make sure we get a new one wasm.

    setUp(plugin_config_yaml, singleton);
    Envoy::Buffer::OwnedImpl request_body;

    Wasm* initial_wasm = plugin_config_->wasm();
    EXPECT_NE(nullptr, initial_wasm);
    EXPECT_EQ(initial_wasm->fail_state(), proxy_wasm::FailState::Ok);

    // Create first context.
    createContext();
    EXPECT_NE(nullptr, context_.get());

    EXPECT_CALL(decoder_callbacks_,
                sendLocalReply(Envoy::Http::Code::ServiceUnavailable, testing::Eq(""), _,
                               testing::Eq(Grpc::Status::WellKnownGrpcStatus::Unavailable),
                               testing::Eq("wasm_fail_stream")));
    EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
              context_->decodeData(request_body, true));

    // The wasm should be in runtime error state.
    EXPECT_EQ(initial_wasm->fail_state(), proxy_wasm::FailState::RuntimeError);

    // Wait 2 seconds.
    server_.dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::seconds(2));

    // Create second context but the wasm is not reloaded and empty context is returned.
    createContext();
    EXPECT_EQ(nullptr, context_.get());

    Wasm* latest_wasm = plugin_config_->wasm();
    EXPECT_NE(nullptr, latest_wasm);
    EXPECT_EQ(latest_wasm, initial_wasm);
    EXPECT_EQ(latest_wasm->fail_state(), proxy_wasm::FailState::RuntimeError);
  };

  test_func(false);
  test_func(true);
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
