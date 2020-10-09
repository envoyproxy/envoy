#include "envoy/extensions/filters/network/wasm/v3/wasm.pb.validate.h"

#include "common/common/base64.h"
#include "common/common/hex.h"
#include "common/crypto/utility.h"

#include "extensions/common/wasm/wasm.h"
#include "extensions/filters/network/wasm/config.h"
#include "extensions/filters/network/wasm/wasm_filter.h"

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

class WasmNetworkFilterConfigTest : public testing::TestWithParam<std::string> {
protected:
  WasmNetworkFilterConfigTest() : api_(Api::createApiForTest(stats_store_)) {
    ON_CALL(context_, api()).WillByDefault(ReturnRef(*api_));
    ON_CALL(context_, scope()).WillByDefault(ReturnRef(stats_store_));
    ON_CALL(context_, listenerMetadata()).WillByDefault(ReturnRef(listener_metadata_));
    ON_CALL(context_, initManager()).WillByDefault(ReturnRef(init_manager_));
    ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    ON_CALL(context_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
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

// NB: this is required by VC++ which can not handle the use of macros in the macro definitions
// used by INSTANTIATE_TEST_SUITE_P.
auto testing_values = testing::Values(
#if defined(ENVOY_WASM_V8)
    "v8",
#endif
#if defined(ENVOY_WASM_WAVM)
    "wavm",
#endif
    "null");
INSTANTIATE_TEST_SUITE_P(Runtimes, WasmNetworkFilterConfigTest, testing_values);

TEST_P(WasmNetworkFilterConfigTest, YamlLoadFromFileWasm) {
  if (GetParam() == "null") {
    return;
  }
  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  config:
    vm_config:
      runtime: "envoy.wasm.runtime.)EOF",
                                                                    GetParam(), R"EOF("
      code:
        local:
          filename: "{{ test_rundir }}/test/extensions/filters/network/wasm/test_data/test_cpp.wasm"
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

TEST_P(WasmNetworkFilterConfigTest, YamlLoadInlineWasm) {
  const std::string code =
      GetParam() != "null"
          ? TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
                "{{ test_rundir }}/test/extensions/filters/network/wasm/test_data/test_cpp.wasm"))
          : "NetworkTestCpp";
  EXPECT_FALSE(code.empty());
  const std::string yaml = absl::StrCat(R"EOF(
  config:
    vm_config:
      runtime: "envoy.wasm.runtime.)EOF",
                                        GetParam(), R"EOF("
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
                                        GetParam(), R"EOF("
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
                                        GetParam(), R"EOF("
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

TEST_P(WasmNetworkFilterConfigTest, FilterConfigFailOpen) {
  if (GetParam() == "null") {
    return;
  }
  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  config:
    fail_open: true
    vm_config:
      runtime: "envoy.wasm.runtime.)EOF",
                                                                    GetParam(), R"EOF("
      code:
        local:
          filename: "{{ test_rundir }}/test/extensions/filters/network/wasm/test_data/test_cpp.wasm"
  )EOF"));

  envoy::extensions::filters::network::wasm::v3::Wasm proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  NetworkFilters::Wasm::FilterConfig filter_config(proto_config, context_);
  filter_config.wasm()->fail(proxy_wasm::FailState::RuntimeError, "");
  EXPECT_EQ(filter_config.createFilter(), nullptr);
}

} // namespace Wasm
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
