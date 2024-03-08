#pragma once

#include <cstdio>

#include "envoy/extensions/wasm/v3/wasm.pb.validate.h"
#include "envoy/server/lifecycle_notifier.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/common/wasm/wasm.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

#define MOCK_CONTEXT_LOG_                                                                          \
  using Context::log;                                                                              \
  proxy_wasm::WasmResult log(uint32_t level, std::string_view message) override {                  \
    log_(static_cast<spdlog::level::level_enum>(level), toAbslStringView(message));                \
    return proxy_wasm::WasmResult::Ok;                                                             \
  }                                                                                                \
  MOCK_METHOD(void, log_, (spdlog::level::level_enum level, absl::string_view message))

class DeferredRunner {
public:
  ~DeferredRunner() {
    if (f_) {
      f_();
    }
  }
  void setFunction(std::function<void()> f) { f_ = f; }

private:
  std::function<void()> f_;
};

template <typename Base = testing::Test> class WasmTestBase : public Base {
public:
  // NOLINTNEXTLINE(readability-identifier-naming)
  void SetUp() override { clearCodeCacheForTesting(); }

  void setupBase(const std::string& runtime, const std::string& code, CreateContextFn create_root) {
    Api::ApiPtr api = Api::createApiForTest(stats_store_);
    scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("wasm."));

    envoy::extensions::wasm::v3::PluginConfig plugin_config;
    *plugin_config.mutable_root_id() = root_id_;
    *plugin_config.mutable_name() = "plugin_name";
    plugin_config.set_fail_open(fail_open_);
    plugin_config.mutable_configuration()->set_value(plugin_configuration_);
    *plugin_config.mutable_vm_config()->mutable_environment_variables() = envs_;

    auto vm_config = plugin_config.mutable_vm_config();
    vm_config->set_vm_id("vm_id");
    vm_config->set_runtime(absl::StrCat("envoy.wasm.runtime.", runtime));
    ProtobufWkt::StringValue vm_configuration_string;
    vm_configuration_string.set_value(vm_configuration_);
    vm_config->mutable_configuration()->PackFrom(vm_configuration_string);
    vm_config->mutable_code()->mutable_local()->set_inline_bytes(code);

    plugin_ = std::make_shared<Extensions::Common::Wasm::Plugin>(
        plugin_config, envoy::config::core::v3::TrafficDirection::INBOUND, local_info_,
        &listener_metadata_);
    plugin_->wasmConfig().allowedCapabilities() = allowed_capabilities_;
    // Passes ownership of root_context_.
    Extensions::Common::Wasm::createWasm(
        plugin_, scope_, cluster_manager_, init_manager_, dispatcher_, *api, lifecycle_notifier_,
        remote_data_provider_, [this](WasmHandleSharedPtr wasm) { wasm_ = wasm; }, create_root);
    plugin_handle_ = getOrCreateThreadLocalPlugin(
        wasm_, plugin_, dispatcher_,
        [this, create_root](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) {
          root_context_ = static_cast<Context*>(create_root(wasm, plugin));
          return root_context_;
        });
    wasm_ = plugin_handle_->wasmHandle();
  }

  WasmHandleSharedPtr& wasm() { return wasm_; }
  Context* rootContext() { return root_context_; }

  DeferredRunner deferred_runner_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr scope_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Init::MockManager> init_manager_;
  WasmHandleSharedPtr wasm_;
  PluginSharedPtr plugin_;
  PluginHandleSharedPtr plugin_handle_;
  NiceMock<Envoy::Ssl::MockConnectionInfo> ssl_;
  NiceMock<Envoy::Network::MockConnection> connection_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockServerLifecycleNotifier> lifecycle_notifier_;
  envoy::config::core::v3::Metadata listener_metadata_;
  Context* root_context_ = nullptr; // Unowned.
  RemoteAsyncDataProviderPtr remote_data_provider_;

  void setRootId(std::string root_id) { root_id_ = root_id; }
  void setVmConfiguration(std::string vm_configuration) { vm_configuration_ = vm_configuration; }
  void setPluginConfiguration(std::string plugin_configuration) {
    plugin_configuration_ = plugin_configuration;
  }
  void setFailOpen(bool fail_open) { fail_open_ = fail_open; }
  void setAllowedCapabilities(proxy_wasm::AllowedCapabilitiesMap allowed_capabilities) {
    allowed_capabilities_ = allowed_capabilities;
  }
  void setEnvs(envoy::extensions::wasm::v3::EnvironmentVariables envs) { envs_ = envs; }

private:
  std::string root_id_ = "";
  std::string vm_configuration_ = "";
  bool fail_open_ = false;
  std::string plugin_configuration_ = "";
  proxy_wasm::AllowedCapabilitiesMap allowed_capabilities_ = {};
  envoy::extensions::wasm::v3::EnvironmentVariables envs_ = {};
};

template <typename Base = testing::Test> class WasmHttpFilterTestBase : public WasmTestBase<Base> {
public:
  template <typename TestFilter> void setupFilterBase() {
    auto wasm = WasmTestBase<Base>::wasm_ ? WasmTestBase<Base>::wasm_->wasm().get() : nullptr;
    int root_context_id = wasm ? wasm->getRootContext(WasmTestBase<Base>::plugin_, false)->id() : 0;
    context_ =
        std::make_unique<TestFilter>(wasm, root_context_id, WasmTestBase<Base>::plugin_handle_);
    context_->setDecoderFilterCallbacks(decoder_callbacks_);
    context_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  std::unique_ptr<Context> context_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> request_stream_info_;
};

template <typename Base = testing::Test>
class WasmNetworkFilterTestBase : public WasmTestBase<Base> {
public:
  template <typename TestFilter> void setupFilterBase() {
    auto wasm = WasmTestBase<Base>::wasm_ ? WasmTestBase<Base>::wasm_->wasm().get() : nullptr;
    int root_context_id = wasm ? wasm->getRootContext(WasmTestBase<Base>::plugin_, false)->id() : 0;
    context_ =
        std::make_unique<TestFilter>(wasm, root_context_id, WasmTestBase<Base>::plugin_handle_);
    context_->initializeReadFilterCallbacks(read_filter_callbacks_);
    context_->initializeWriteFilterCallbacks(write_filter_callbacks_);
  }

  std::unique_ptr<Context> context_;
  NiceMock<Network::MockReadFilterCallbacks> read_filter_callbacks_;
  NiceMock<Network::MockWriteFilterCallbacks> write_filter_callbacks_;
};

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
