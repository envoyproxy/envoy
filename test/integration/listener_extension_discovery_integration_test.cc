#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/extension/v3/config_discovery.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/filters/test_listener_filter.h"
#include "test/integration/filters/test_listener_filter.pb.h"
#include "test/integration/integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

enum class ListenerMatcherType { NULLMATCHER, ANYMATCHER, NOTANYMATCHER };

constexpr absl::string_view EcdsClusterName = "ecds_cluster";
constexpr absl::string_view Ecds2ClusterName = "ecds2_cluster";

class ListenerExtensionDiscoveryIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                                  public BaseIntegrationTest {
public:
  ListenerExtensionDiscoveryIntegrationTest()
      : BaseIntegrationTest(ipVersion(), ConfigHelper::baseConfig()), filter_name_("foo"),
        data_("HelloWorld"), port_name_("http") {
    // TODO(ggreenway): add tag extraction rules.
    // Missing stat tag-extraction rule for stat
    // 'extension_config_discovery.tcp_listener_filter.foo.grpc.ecds_cluster.streams_closed_7' and
    // stat_prefix 'ecds_cluster'.
    skip_tag_extraction_rule_check_ = true;
  }

  void addDynamicFilter(const std::string& name, bool apply_without_warming,
                        bool set_default_config = true, bool rate_limit = false,
                        ListenerMatcherType matcher = ListenerMatcherType::NULLMATCHER,
                        bool second_connection = false) {
    config_helper_.addConfigModifier([name, apply_without_warming, set_default_config, rate_limit,
                                      matcher, second_connection,
                                      this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      listener->set_stat_prefix("listener_stat");
      auto* listener_filter = listener->add_listener_filters();

      listener_filter->set_name(name);

      auto* discovery = listener_filter->mutable_config_discovery();
      discovery->add_type_urls(
          "type.googleapis.com/test.integration.filters.TestTcpListenerFilterConfig");
      if (set_default_config) {
        auto default_configuration = test::integration::filters::TestTcpListenerFilterConfig();
        default_configuration.set_drain_bytes(default_drain_bytes_);
        discovery->mutable_default_config()->PackFrom(default_configuration);
      }

      discovery->set_apply_default_config_without_warming(apply_without_warming);
      discovery->mutable_config_source()->set_resource_api_version(
          envoy::config::core::v3::ApiVersion::V3);
      auto* api_config_source = discovery->mutable_config_source()->mutable_api_config_source();
      api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
      api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
      if (rate_limit) {
        api_config_source->mutable_rate_limit_settings()->mutable_max_tokens()->set_value(10);
      }
      addListenerFilterMatcher(listener_filter, matcher);
      auto* grpc_service = api_config_source->add_grpc_services();
      if (!second_connection) {
        setGrpcService(*grpc_service, std::string(EcdsClusterName),
                       getEcdsFakeUpstream().localAddress());
      } else {
        setGrpcService(*grpc_service, std::string(Ecds2ClusterName),
                       getEcds2FakeUpstream().localAddress());
      }
    });
  }

  void addStaticFilter(const std::string& name, uint32_t drain_bytes,
                       ListenerMatcherType matcher = ListenerMatcherType::NULLMATCHER) {
    config_helper_.addConfigModifier(
        [name, drain_bytes, matcher, this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* listener_filter =
              bootstrap.mutable_static_resources()->mutable_listeners(0)->add_listener_filters();
          listener_filter->set_name(name);
          auto configuration = test::integration::filters::TestTcpListenerFilterConfig();
          configuration.set_drain_bytes(drain_bytes);
          listener_filter->mutable_typed_config()->PackFrom(configuration);
          addListenerFilterMatcher(listener_filter, matcher);
        });
  }

  // Add listener filter matcher config.
  void addListenerFilterMatcher(envoy::config::listener::v3::ListenerFilter* listener_filter,
                                ListenerMatcherType matcher) {
    if (matcher == ListenerMatcherType::ANYMATCHER) {
      listener_filter->mutable_filter_disabled()->set_any_match(true);
    } else if (matcher == ListenerMatcherType::NOTANYMATCHER) {
      listener_filter->mutable_filter_disabled()->mutable_not_match()->set_any_match(true);
    }
  }

  void addEcdsCluster(const std::string& cluster_name) {
    // Add an xDS cluster for extension config discovery.
    config_helper_.addConfigModifier(
        [cluster_name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* ecds_cluster = bootstrap.mutable_static_resources()->add_clusters();
          ecds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
          ecds_cluster->set_name(cluster_name);
          ConfigHelper::setHttp2(*ecds_cluster);
        });
  }

  void initialize() override {
    defer_listener_finalization_ = true;
    setUpstreamCount(1);

    // Add a tcp_proxy network filter.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->add_filter_chains();
      auto* filter = filter_chain->add_filters();
      filter->set_name("envoy.filters.network.tcp_proxy");
      envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config;
      config.set_stat_prefix("tcp_stats");
      config.set_cluster("cluster_0");
      filter->mutable_typed_config()->PackFrom(config);
    });

    addEcdsCluster(std::string(EcdsClusterName));
    // Add 2nd cluster in case of two connections.
    if (two_connections_) {
      addEcdsCluster(std::string(Ecds2ClusterName));
    }
    BaseIntegrationTest::initialize();
    registerTestServerPorts({port_name_});
  }

  void resetEcdsConnection(FakeHttpConnectionPtr& connection) {
    if (connection != nullptr) {
      AssertionResult result = connection->close();
      RELEASE_ASSERT(result, result.message());
      result = connection->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      connection.reset();
    }
  }

  ~ListenerExtensionDiscoveryIntegrationTest() override {
    resetEcdsConnection(ecds_connection_);
    resetEcdsConnection(ecds2_connection_);
  }

  void createUpstreams() override {
    BaseIntegrationTest::createUpstreams();
    // Create two extension config discovery upstreams (fake_upstreams_[1] and fake_upstreams_[2]).
    addFakeUpstream(Http::CodecType::HTTP2);
    if (two_connections_) {
      addFakeUpstream(Http::CodecType::HTTP2);
    }
  }

  void waitForEcdsStream(FakeUpstream& upstream, FakeHttpConnectionPtr& connection,
                         FakeStreamPtr& stream) {
    AssertionResult result = upstream.waitForHttpConnection(*dispatcher_, connection);
    ASSERT_TRUE(result);
    result = connection->waitForNewStream(*dispatcher_, stream);
    ASSERT_TRUE(result);
    stream->startGrpcStream();
  }

  void waitXdsStream() {
    waitForEcdsStream(getEcdsFakeUpstream(), ecds_connection_, ecds_stream_);
    if (two_connections_) {
      // Wait for 2nd ECDS stream.
      waitForEcdsStream(getEcds2FakeUpstream(), ecds2_connection_, ecds2_stream_);
    }
  }

  void sendXdsResponse(const std::string& name, const std::string& version,
                       const uint32_t drain_bytes, bool ttl = false,
                       bool second_connection = false) {
    // The to-be-drained bytes has to be smaller than data size.
    ASSERT(drain_bytes <= data_.size());

    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url("type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig");
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name(name);
    envoy::service::discovery::v3::Resource resource;
    resource.set_name(name);

    auto configuration = test::integration::filters::TestTcpListenerFilterConfig();
    configuration.set_drain_bytes(drain_bytes);
    typed_config.mutable_typed_config()->PackFrom(configuration);
    resource.mutable_resource()->PackFrom(typed_config);
    if (ttl) {
      resource.mutable_ttl()->set_seconds(1);
    }
    response.add_resources()->PackFrom(resource);
    if (!second_connection) {
      ecds_stream_->sendGrpcMessage(response);
    } else {
      ecds2_stream_->sendGrpcMessage(response);
    }
  }

  // Client sends data_, which is drained by Envoy listener filter based on config, then received by
  // upstream.
  void sendDataVerifyResults(uint32_t drain_bytes) {
    test_server_->waitUntilListenersReady();
    test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
    EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort(port_name_));
    ASSERT_TRUE(tcp_client->write(data_));
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
    std::string received_data;
    ASSERT_TRUE(fake_upstream_connection->waitForData(data_.size() - drain_bytes, &received_data));
    const std::string expected_data = data_.substr(drain_bytes, std::string::npos);
    EXPECT_EQ(expected_data, received_data);
    tcp_client->close();
  }

  const uint32_t default_drain_bytes_{2};
  const std::string filter_name_;
  const std::string data_;
  const std::string port_name_;
  bool two_connections_{false};

  FakeUpstream& getEcdsFakeUpstream() const { return *fake_upstreams_[1]; }
  FakeUpstream& getEcds2FakeUpstream() const { return *fake_upstreams_[2]; }

  // gRPC two ECDS connections set-up.
  FakeHttpConnectionPtr ecds_connection_{nullptr};
  FakeStreamPtr ecds_stream_{nullptr};
  FakeHttpConnectionPtr ecds2_connection_{nullptr};
  FakeStreamPtr ecds2_stream_{nullptr};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, ListenerExtensionDiscoveryIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update to have listener filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(5);

  // Send 2nd config update to have listener filter drain 3 bytes of data.
  sendXdsResponse(filter_name_, "2", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 2);
  sendDataVerifyResults(3);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicSuccessWithAnyMatcher) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  // Add dynamic filter with any matcher, which effectively disables the filter.
  addDynamicFilter(filter_name_, false, true, false, ListenerMatcherType::ANYMATCHER);
  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update to have listener filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  // Send data, verify the listener filter doesn't drain any data.
  sendDataVerifyResults(0);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicSuccessWithNotAnyMatcher) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  // Not any matcher negates the any matcher, which effectively enable the filter.
  addDynamicFilter(filter_name_, false, true, false, ListenerMatcherType::NOTANYMATCHER);
  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update to have listener filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  // Send data, verify the listener filter drains five bytes data.
  sendDataVerifyResults(5);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicSuccessWithTtl) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false, false);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update with TTL 1s, and have listener filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5, true);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(5);

  // Wait for configuration expired. Then start a TCP connection.
  // The missing config listener filter will be installed to handle the connection.
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 2);
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort(port_name_));
  auto result = tcp_client->write(data_);
  if (result) {
    tcp_client->waitForDisconnect();
  }
  // The extension_config_missing stats counter increases by 1.
  test_server_->waitForCounterGe("listener.listener_stat.extension_config_missing", 1);

  // Send the data again. The extension_config_missing stats counter increases to 2.
  tcp_client = makeTcpConnection(lookupPort(port_name_));
  result = tcp_client->write(data_);
  if (result) {
    tcp_client->waitForDisconnect();
  }
  test_server_->waitForCounterGe("listener.listener_stat.extension_config_missing", 2);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicSuccessWithTtlWithDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false, true);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update with TTL 1s, and have listener filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5, true);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(5);

  // Wait for configuration expired. The default filter will be installed.
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 2);
  // Start a TCP connection. The default filter drain 2 bytes.
  sendDataVerifyResults(default_drain_bytes_);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicFailWithDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false, true);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update with invalid config (drain_bytes has to >=2).
  sendXdsResponse(filter_name_, "1", 1);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_fail", 1);
  // The default filter will be installed. Start a TCP connection. The default filter drain 2 bytes.
  sendDataVerifyResults(default_drain_bytes_);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicFailWithoutDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false, false);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update with invalid config (drain_bytes has to >=2).
  sendXdsResponse(filter_name_, "1", 1);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_fail", 1);
  // The missing config filter will be installed and close the connection when a correction is
  // created.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort(port_name_));
  auto result = tcp_client->write(data_);
  if (result) {
    tcp_client->waitForDisconnect();
  }
  // The extension_config_missing stats counter increases by 1.
  test_server_->waitForCounterGe("listener.listener_stat.extension_config_missing", 1);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicWithoutWarming) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  initialize();

  // Send data without send config update.
  sendDataVerifyResults(default_drain_bytes_);
  // Send update should cause a different response.
  sendXdsResponse(filter_name_, "1", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(3);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicWithoutWarmingConfigFail) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  initialize();

  // Send data without send config update.
  sendDataVerifyResults(default_drain_bytes_);
  // Send config update with invalid config (drain_bytes has to >=2).
  sendXdsResponse(filter_name_, "1", 1);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_fail", 1);
  sendDataVerifyResults(default_drain_bytes_);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, TwoSubscriptionsSameName) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addDynamicFilter(filter_name_, false);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  sendXdsResponse(filter_name_, "1", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  // Each filter drain 3 bytes.
  sendDataVerifyResults(6);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, TwoSubscriptionsDifferentName) {
  two_connections_ = true;
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", true);
  addDynamicFilter("bar", false, true, false, ListenerMatcherType::NULLMATCHER, true);
  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update.
  sendXdsResponse("foo", "1", 3);
  sendXdsResponse("bar", "1", 4, false, true);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter.foo.config_reload",
                                 1);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter.bar.config_reload",
                                 1);
  // The two filters drain 3 + 4  bytes.
  sendDataVerifyResults(7);

  // Send 2nd config update.
  sendXdsResponse("foo", "2", 4);
  sendXdsResponse("bar", "2", 5, false, true);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter.foo.config_reload",
                                 2);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter.bar.config_reload",
                                 2);
  // The two filters drain 4 + 5  bytes.
  sendDataVerifyResults(9);
}

// Testing it works with two static listener filter configuration.
TEST_P(ListenerExtensionDiscoveryIntegrationTest, TwoStaticFilters) {
  addStaticFilter("foo", 3);
  addStaticFilter("bar", 5);
  initialize();

  // filter drain 3+5 bytes.
  sendDataVerifyResults(8);
}

// Static listener filter configuration with matcher config.
TEST_P(ListenerExtensionDiscoveryIntegrationTest, TwoStaticFiltersWithMatcher) {
  addStaticFilter("foo", 3, ListenerMatcherType::ANYMATCHER);
  addStaticFilter("bar", 5, ListenerMatcherType::NOTANYMATCHER);
  initialize();

  // The filter with any matcher configured is disabled.
  sendDataVerifyResults(5);
}

// Testing it works with mixed static/dynamic listener filter configuration.
TEST_P(ListenerExtensionDiscoveryIntegrationTest, TwoDynamicTwoStaticFilterMixed) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false);
  addStaticFilter("bar", 2);
  addDynamicFilter(filter_name_, true);
  addStaticFilter("foobar", 2);
  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse(filter_name_, "1", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  // filter drain 3 + 2 + 3  + 2 bytes.
  sendDataVerifyResults(10);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, DynamicStaticFilterMixedDifferentOrder) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addStaticFilter("bar", 2);
  addStaticFilter("baz", 2);
  addDynamicFilter(filter_name_, true);
  addDynamicFilter(filter_name_, false);
  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse(filter_name_, "1", 2);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  // filter drain 2 + 2 + 2 + 2 bytes.
  sendDataVerifyResults(8);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, DynamicStaticFilterMixedWithMatcher) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addStaticFilter("bar", 2, ListenerMatcherType::NOTANYMATCHER);
  addStaticFilter("baz", 2, ListenerMatcherType::ANYMATCHER);
  addDynamicFilter(filter_name_, true, true, false, ListenerMatcherType::NOTANYMATCHER);
  addDynamicFilter(filter_name_, false, true, false, ListenerMatcherType::ANYMATCHER);
  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse(filter_name_, "1", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  // The filters with any matcher configured are disabled. They totally drain 2 + 3 bytes.
  sendDataVerifyResults(5);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, DestroyDuringInit) {
  // If rate limiting is enabled on the config source, gRPC mux drainage updates the requests
  // queue size on destruction. The update calls out to stats scope nested under the extension
  // config subscription stats scope. This test verifies that the stats scope outlasts the gRPC
  // subscription.
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false, true);
  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  test_server_.reset();
  auto result = ecds_connection_->waitForDisconnect();
  ASSERT_TRUE(result);
  ecds_connection_.reset();
}

} // namespace
} // namespace Envoy
