#include "envoy/extensions/filters/network/wasm/v3/wasm.pb.validate.h"

#include "source/common/common/base64.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/extensions/common/wasm/wasm.h"
#include "source/extensions/filters/network/wasm/config.h"
#include "source/extensions/filters/network/wasm/wasm_filter.h"

#include "test/extensions/common/wasm/wasm_runtime.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Wasm {

class WasmNetworkFilterConfigTest
    : public testing::TestWithParam<std::tuple<std::string, std::string>> {
protected:
  WasmNetworkFilterConfigTest() : api_(Api::createApiForTest(stats_store_)) {
    ON_CALL(context_, api()).WillByDefault(ReturnRef(*api_));
    ON_CALL(context_, scope()).WillByDefault(ReturnRef(stats_store_));
    ON_CALL(context_, listenerMetadata()).WillByDefault(ReturnRef(listener_metadata_));
    ON_CALL(context_, initManager()).WillByDefault(ReturnRef(init_manager_));
    ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    ON_CALL(context_, mainThreadDispatcher()).WillByDefault(ReturnRef(dispatcher_));
  }

  void SetUp() override { Envoy::Extensions::Common::Wasm::clearCodeCacheForTesting(); }

  void initializeForRemote() {
    retry_timer_ = new Event::MockTimer();

    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      retry_timer_cb_ = timer_cb;
      return retry_timer_;
    }));
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  envoy::config::core::v3::Metadata listener_metadata_;
  Init::ManagerImpl init_manager_{"init_manager"};
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Init::ExpectableWatcherImpl init_watcher_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::MockTimer* retry_timer_;
  Event::TimerCb retry_timer_cb_;
};

INSTANTIATE_TEST_SUITE_P(Runtimes, WasmNetworkFilterConfigTest,
                         Envoy::Extensions::Common::Wasm::runtime_and_cpp_values,
                         Envoy::Extensions::Common::Wasm::wasmTestParamsToString);

TEST_P(WasmNetworkFilterConfigTest, YamlLoadFromFileWasm) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  config:
    vm_config:
      runtime: "envoy.wasm.runtime.)EOF",
                                                                    std::get<0>(GetParam()), R"EOF("
      code:
        local:
          filename: "{{ test_rundir }}/test/extensions/filters/network/wasm/test_data/test_cpp.wasm"
  )EOF"));

  envoy::extensions::filters::network::wasm::v3::Wasm proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  // Intentionally we scope the factory here, and make the context outlive it.
  // This case happens when the config is updated by ECDS, and
  // we have to make sure that contexts still hold valid WasmVMs in these cases.
  std::shared_ptr<Envoy::Extensions::Common::Wasm::Context> context = nullptr;
  {
    WasmFilterConfig factory;
    Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context_);
    EXPECT_CALL(init_watcher_, ready());
    context_.initManager().initialize(init_watcher_);
    EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);
    Network::MockConnection connection;
    EXPECT_CALL(connection, addFilter(_)).WillOnce([&context](Network::FilterSharedPtr filter) {
      context = std::static_pointer_cast<Envoy::Extensions::Common::Wasm::Context>(filter);
    });
    cb(connection);
  }
  // Check if the context still holds a valid Wasm even after the factory is destroyed.
  EXPECT_TRUE(context);
  EXPECT_TRUE(context->wasm());
  // Check if the custom stat namespace is registered during the initialization.
  EXPECT_TRUE(api_->customStatNamespaces().registered("wasmcustom"));
}

TEST_P(WasmNetworkFilterConfigTest, YamlLoadInlineWasm) {
  const std::string code =
      std::get<0>(GetParam()) != "null"
          ? TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
                "{{ test_rundir }}/test/extensions/filters/network/wasm/test_data/test_cpp.wasm"))
          : "NetworkTestCpp";
  EXPECT_FALSE(code.empty());
  const std::string yaml = absl::StrCat(R"EOF(
  config:
    vm_config:
      runtime: "envoy.wasm.runtime.)EOF",
                                        std::get<0>(GetParam()), R"EOF("
      code:
        local: { inline_bytes: ")EOF",
                                        Base64::encode(code.data(), code.size()), R"EOF(" }
  )EOF");

  envoy::extensions::filters::network::wasm::v3::Wasm proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  WasmFilterConfig factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context_);
  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

TEST_P(WasmNetworkFilterConfigTest, YamlLoadInlineBadCode) {
  const std::string yaml = absl::StrCat(R"EOF(
  config:
    name: "test"
    vm_config:
      runtime: "envoy.wasm.runtime.)EOF",
                                        std::get<0>(GetParam()), R"EOF("
      code:
        local: { inline_string: "bad code" }
  )EOF");

  envoy::extensions::filters::network::wasm::v3::Wasm proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  WasmFilterConfig factory;
  EXPECT_THROW_WITH_MESSAGE(factory.createFilterFactoryFromProto(proto_config, context_),
                            Extensions::Common::Wasm::WasmException,
                            "Unable to create Wasm network filter test");
}

TEST_P(WasmNetworkFilterConfigTest, YamlLoadInlineBadCodeFailOpenNackConfig) {
  const std::string yaml = absl::StrCat(R"EOF(
  config:
    name: "test"
    fail_open: true
    vm_config:
      runtime: "envoy.wasm.runtime.)EOF",
                                        std::get<0>(GetParam()), R"EOF("
      code:
        local: { inline_string: "bad code" }
  )EOF");

  envoy::extensions::filters::network::wasm::v3::Wasm proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  WasmFilterConfig factory;
  EXPECT_THROW_WITH_MESSAGE(factory.createFilterFactoryFromProto(proto_config, context_),
                            Extensions::Common::Wasm::WasmException,
                            "Unable to create Wasm network filter test");
}

TEST_P(WasmNetworkFilterConfigTest, FilterConfigFailClosed) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  config:
    vm_config:
      runtime: "envoy.wasm.runtime.)EOF",
                                                                    std::get<0>(GetParam()), R"EOF("
      code:
        local:
          filename: "{{ test_rundir }}/test/extensions/filters/network/wasm/test_data/test_cpp.wasm"
  )EOF"));

  envoy::extensions::filters::network::wasm::v3::Wasm proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  NetworkFilters::Wasm::FilterConfig filter_config(proto_config, context_);
  filter_config.wasmForTest()->fail(proxy_wasm::FailState::RuntimeError, "");
  auto context = filter_config.createFilter();
  EXPECT_EQ(context->wasm(), nullptr);
  EXPECT_TRUE(context->isFailed());
}

TEST_P(WasmNetworkFilterConfigTest, FilterConfigFailOpen) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  config:
    fail_open: true
    vm_config:
      runtime: "envoy.wasm.runtime.)EOF",
                                                                    std::get<0>(GetParam()), R"EOF("
      code:
        local:
          filename: "{{ test_rundir }}/test/extensions/filters/network/wasm/test_data/test_cpp.wasm"
  )EOF"));

  envoy::extensions::filters::network::wasm::v3::Wasm proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  NetworkFilters::Wasm::FilterConfig filter_config(proto_config, context_);
  filter_config.wasmForTest()->fail(proxy_wasm::FailState::RuntimeError, "");
  EXPECT_EQ(filter_config.createFilter(), nullptr);
}

TEST_P(WasmNetworkFilterConfigTest, FilterConfigCapabilitiesUnrestrictedByDefault) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  config:
    vm_config:
      runtime: "envoy.wasm.runtime.)EOF",
                                                                    std::get<0>(GetParam()), R"EOF("
      code:
        local:
          filename: "{{ test_rundir }}/test/extensions/filters/network/wasm/test_data/test_cpp.wasm"
    capability_restriction_config:
      allowed_capabilities:
  )EOF"));

  envoy::extensions::filters::network::wasm::v3::Wasm proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  NetworkFilters::Wasm::FilterConfig filter_config(proto_config, context_);
  auto wasm = filter_config.wasmForTest();
  EXPECT_TRUE(wasm->capabilityAllowed("proxy_log"));
  EXPECT_TRUE(wasm->capabilityAllowed("proxy_on_vm_start"));
  EXPECT_TRUE(wasm->capabilityAllowed("proxy_http_call"));
  EXPECT_TRUE(wasm->capabilityAllowed("proxy_on_log"));
  EXPECT_FALSE(filter_config.createFilter() == nullptr);
}

TEST_P(WasmNetworkFilterConfigTest, FilterConfigCapabilityRestriction) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  config:
    vm_config:
      runtime: "envoy.wasm.runtime.)EOF",
                                                                    std::get<0>(GetParam()), R"EOF("
      code:
        local:
          filename: "{{ test_rundir }}/test/extensions/filters/network/wasm/test_data/test_cpp.wasm"
    capability_restriction_config:
      allowed_capabilities:
        proxy_log:
        proxy_on_new_connection:
  )EOF"));

  envoy::extensions::filters::network::wasm::v3::Wasm proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  NetworkFilters::Wasm::FilterConfig filter_config(proto_config, context_);
  auto wasm = filter_config.wasmForTest();
  EXPECT_TRUE(wasm->capabilityAllowed("proxy_log"));
  EXPECT_TRUE(wasm->capabilityAllowed("proxy_on_new_connection"));
  EXPECT_FALSE(wasm->capabilityAllowed("proxy_http_call"));
  EXPECT_FALSE(wasm->capabilityAllowed("proxy_on_log"));
  EXPECT_FALSE(filter_config.createFilter() == nullptr);
}

TEST_P(WasmNetworkFilterConfigTest, FilterConfigAllowOnVmStart) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  config:
    vm_config:
      runtime: "envoy.wasm.runtime.)EOF",
                                                                    std::get<0>(GetParam()), R"EOF("
      code:
        local:
          filename: "{{ test_rundir }}/test/extensions/filters/network/wasm/test_data/test_cpp.wasm"
    capability_restriction_config:
      allowed_capabilities:
        proxy_on_vm_start:
        proxy_get_property:
        proxy_on_context_create:
  )EOF"));

  envoy::extensions::filters::network::wasm::v3::Wasm proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  WasmFilterConfig factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context_);
  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

} // namespace Wasm
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
