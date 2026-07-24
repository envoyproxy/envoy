#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/listener/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/network/echo/v3/echo.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/router/string_accessor_impl.h"

#include "test/integration/integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

namespace Envoy {

namespace {
// ObjectFactory used by the typed filter-state integration test: the dynamic-module listener filter
// writes through this factory via
// envoy_dynamic_module_callback_listener_filter_set_filter_state_typed on accept and reads it back
// on the same connection.
class ListenerWrittenTypedObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return "envoy.test.listener_written_typed_object"; }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return std::make_unique<Router::StringAccessorImpl>(data);
  }
};

REGISTER_FACTORY(ListenerWrittenTypedObjectFactory, StreamInfo::FilterState::ObjectFactory);

// ObjectFactory used by the filter-chain selection integration test: the listener filter writes the
// selection value through this factory, and the filter chain matcher reads it back as a string via
// envoy.matching.inputs.filter_state to pick a filter chain.
class FilterChainSelectorObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return "envoy.test.filter_chain_selector"; }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return std::make_unique<Router::StringAccessorImpl>(data);
  }
};

REGISTER_FACTORY(FilterChainSelectorObjectFactory, StreamInfo::FilterState::ObjectFactory);
} // namespace

class DynamicModulesListenerSdkIntegrationTest : public testing::TestWithParam<std::string>,
                                                 public BaseIntegrationTest {
public:
  DynamicModulesListenerSdkIntegrationTest()
      : BaseIntegrationTest(GetParam() == "cpp" ? Network::Address::IpVersion::v4
                                                : Network::Address::IpVersion::v6,
                            ConfigHelper::baseConfig() + R"EOF(
    filter_chains:
      - filters:
        - name: envoy.filters.network.echo
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.echo.v3.Echo
)EOF") {}

protected:
  void initializeFilter(const std::string& filter_name) {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/" +
                                    GetParam()),
        1);

    config_helper_.addConfigModifier([filter_name](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter = listener->add_listener_filters();
      filter->set_name("envoy.filters.listener.dynamic_modules");

      envoy::extensions::filters::listener::dynamic_modules::v3::DynamicModuleListenerFilter
          dynamic_module_config;
      dynamic_module_config.mutable_dynamic_module_config()->set_name("listener_integration_test");
      dynamic_module_config.set_filter_name(filter_name);
      std::ignore = filter->mutable_typed_config()->PackFrom(dynamic_module_config);
    });

    BaseIntegrationTest::initialize();
  }
};

#ifndef __SANITIZE_ADDRESS__
auto DynamicModulesListenerSdkIntegrationTestValues = testing::Values("rust", "go", "cpp");
#else
auto DynamicModulesListenerSdkIntegrationTestValues = testing::Values("rust", "go");
#endif

INSTANTIATE_TEST_SUITE_P(SdkLanguages, DynamicModulesListenerSdkIntegrationTest,
                         DynamicModulesListenerSdkIntegrationTestValues,
                         [](const testing::TestParamInfo<std::string>& info) {
                           return info.param;
                         });

TEST_P(DynamicModulesListenerSdkIntegrationTest, WriteToSocket) {
  initializeFilter("write_to_socket");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("ping"));
  tcp_client->waitForData("ping");
  tcp_client->close();
}

TEST_P(DynamicModulesListenerSdkIntegrationTest, ReadAttributes) {
  if (GetParam() != "rust") {
    GTEST_SKIP() << "the read_attributes filter is only in the rust test module";
  }
  initializeFilter("read_attributes");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("ping"));
  tcp_client->waitForData("ping");
  tcp_client->close();
}

TEST_P(DynamicModulesListenerSdkIntegrationTest, BufferRead) {
  initializeFilter("buffer_read");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("ping"));
  tcp_client->waitForData("ping");
  tcp_client->close();
}

// Regression test for the listener filter shared-ownership fix. A make_unique-owned listener filter
// aborted the worker when a module issued an HTTP callout, because sendHttpCallout calls
// shared_from_this which throws std::bad_weak_ptr on a non-shared-owned object. The production
// factory now shares ownership via an adapter. The autonomous upstream services the callout so the
// filter resumes the accept. Rust-only because the bug is in the language-agnostic C++ factory and
// one module exercises it.
TEST_P(DynamicModulesListenerSdkIntegrationTest, HttpCalloutOnAcceptDoesNotCrash) {
  if (GetParam() != "rust") {
    GTEST_SKIP() << "the http_callout_on_accept filter is only in the rust test module";
  }
  autonomous_upstream_ = true;
  initializeFilter("http_callout_on_accept");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("ping"));
  tcp_client->waitForData("ping");
  tcp_client->close();
}

TEST_P(DynamicModulesListenerSdkIntegrationTest, DynamicMetadataBatch) {
  if (GetParam() != "rust") {
    GTEST_SKIP() << "the dynamic_metadata filter is only in the rust test module";
  }
  initializeFilter("dynamic_metadata");

  // The filter sets batch metadata and reads it back on accept, asserting in the module. A failure
  // there stops the chain and the echo upstream never returns the payload.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("ping"));
  tcp_client->waitForData("ping");
  tcp_client->close();
}

TEST_P(DynamicModulesListenerSdkIntegrationTest, FilterStateTyped) {
  if (GetParam() != "rust") {
    GTEST_SKIP() << "the filter_state_typed filter is only in the rust test module";
  }
  initializeFilter("filter_state_typed");

  // The filter writes a typed filter state through the harness-registered ObjectFactory and reads
  // it back on accept, asserting in the module. A failure there stops the chain and the echo
  // upstream never returns the payload.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("ping"));
  tcp_client->waitForData("ping");
  tcp_client->close();
}

// Proves a filter chain matcher selects a filter chain from filter state that a dynamic-module
// listener filter set. The listener filter writes a per-connection selection value into typed
// filter state on accept; the matcher then reads it back via envoy.matching.inputs.filter_state and
// routes the connection to one of two otherwise-identical direct_response chains. The distinct
// direct_response payloads prove which chain the matcher chose, i.e. that a succeeding chain
// consumed the listener-filter-set filter state.
class DynamicModulesListenerFilterChainSelectionIntegrationTest : public BaseIntegrationTest,
                                                                  public testing::Test {
public:
  DynamicModulesListenerFilterChainSelectionIntegrationTest()
      : BaseIntegrationTest(Network::Address::IpVersion::v4, ConfigHelper::baseConfig() + R"EOF(
    filter_chain_matcher:
      matcher_tree:
        input:
          name: filter_state
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.FilterStateInput
            key: envoy.test.filter_chain_selector
        exact_match_map:
          map:
            "chain_a":
              action:
                name: filter-chain-name
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: chain_a
            "chain_b":
              action:
                name: filter-chain-name
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: chain_b
    filter_chains:
    - name: chain_a
      filters:
      - name: envoy.filters.network.direct_response
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
          response:
            inline_string: "served_by_chain_a"
    - name: chain_b
      filters:
      - name: envoy.filters.network.direct_response
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
          response:
            inline_string: "served_by_chain_b"
)EOF") {}

protected:
  // Initializes the listener filter with a fixed selection value, mimicking a deterministic
  // per-endpoint routing decision made inside the filter.
  void initializeWithSelection(const std::string& selection) {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);

    config_helper_.addConfigModifier([selection](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter = listener->add_listener_filters();
      filter->set_name("envoy.filters.listener.dynamic_modules");

      envoy::extensions::filters::listener::dynamic_modules::v3::DynamicModuleListenerFilter
          dynamic_module_config;
      dynamic_module_config.mutable_dynamic_module_config()->set_name("listener_integration_test");
      dynamic_module_config.set_filter_name("filter_chain_selector");
      Protobuf::StringValue selection_config;
      selection_config.set_value(selection);
      std::ignore = dynamic_module_config.mutable_filter_config()->PackFrom(selection_config);
      std::ignore = filter->mutable_typed_config()->PackFrom(dynamic_module_config);
    });

    BaseIntegrationTest::initialize();
  }
};

TEST_F(DynamicModulesListenerFilterChainSelectionIntegrationTest, SelectsChainA) {
  initializeWithSelection("chain_a");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->waitForData("served_by_chain_a");
  tcp_client->waitForDisconnect();
}

TEST_F(DynamicModulesListenerFilterChainSelectionIntegrationTest, SelectsChainB) {
  initializeWithSelection("chain_b");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->waitForData("served_by_chain_b");
  tcp_client->waitForDisconnect();
}

} // namespace Envoy
