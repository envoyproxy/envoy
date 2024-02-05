#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/extension/v3/config_discovery.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/filters/test_network_filter.pb.h"
#include "test/integration/integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

constexpr absl::string_view EcdsClusterName = "ecds_cluster";
constexpr absl::string_view Ecds2ClusterName = "ecds2_cluster";
constexpr absl::string_view expected_types[] = {
    "type.googleapis.com/envoy.admin.v3.BootstrapConfigDump",
    "type.googleapis.com/envoy.admin.v3.ClustersConfigDump",
    "type.googleapis.com/envoy.admin.v3.EcdsConfigDump",
    "type.googleapis.com/envoy.admin.v3.ListenersConfigDump",
    "type.googleapis.com/envoy.admin.v3.SecretsConfigDump"};

class NetworkExtensionDiscoveryIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                                 public BaseIntegrationTest {
public:
  NetworkExtensionDiscoveryIntegrationTest()
      : BaseIntegrationTest(ipVersion(), ConfigHelper::baseConfig()) {
    skip_tag_extraction_rule_check_ = true;
  }

  void addFilterChain() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      listener->set_stat_prefix("listener_stat");
      listener->add_filter_chains();
    });
  }

  void addDynamicFilter(const std::string& name, bool apply_without_warming,
                        bool set_default_config = true, bool rate_limit = false,
                        bool second_connection = false) {
    config_helper_.addConfigModifier([name, apply_without_warming, set_default_config, rate_limit,
                                      second_connection,
                                      this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      listener->set_stat_prefix("listener_stat");

      auto* filter_chain = listener->mutable_filter_chains(0);
      auto* filter = filter_chain->add_filters();
      filter->set_name(name);

      auto* discovery = filter->mutable_config_discovery();
      discovery->add_type_urls(
          "type.googleapis.com/test.integration.filters.TestDrainerNetworkFilterConfig");
      if (set_default_config) {
        auto default_configuration = test::integration::filters::TestDrainerNetworkFilterConfig();
        default_configuration.set_bytes_to_drain(default_bytes_to_drain_);
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

  void addStaticFilter(const std::string& name, uint32_t bytes_to_drain) {
    config_helper_.addConfigModifier(
        [name, bytes_to_drain](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          auto* filter_chain = listener->mutable_filter_chains(0);
          auto* filter = filter_chain->add_filters();
          filter->set_name(name);
          auto configuration = test::integration::filters::TestDrainerNetworkFilterConfig();
          configuration.set_bytes_to_drain(bytes_to_drain);
          filter->mutable_typed_config()->PackFrom(configuration);
        });
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

    addEcdsCluster(std::string(EcdsClusterName));
    // Add a tcp_proxy network filter.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->mutable_filter_chains(0);
      auto* filter = filter_chain->add_filters();
      filter->set_name("envoy.filters.network.tcp_proxy");
      envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config;
      config.set_stat_prefix("tcp_stats");
      config.set_cluster("cluster_0");
      filter->mutable_typed_config()->PackFrom(config);
    });

    // Use gRPC LDS instead of default file LDS.
    use_lds_ = false;
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* lds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      lds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      lds_cluster->set_name("lds_cluster");
      ConfigHelper::setHttp2(*lds_cluster);
    });

    // Add 2nd cluster in case of two connections.
    if (two_connections_) {
      addEcdsCluster(std::string(Ecds2ClusterName));
    }

    // Must be the last since it nukes static listeners.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      listener_config_.Swap(bootstrap.mutable_static_resources()->mutable_listeners(0));
      listener_config_.set_name(listener_name_);
      ENVOY_LOG_MISC(debug, "listener config: {}", listener_config_.DebugString());
      bootstrap.mutable_static_resources()->mutable_listeners()->Clear();
      auto* lds_config_source = bootstrap.mutable_dynamic_resources()->mutable_lds_config();
      lds_config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      auto* lds_api_config_source = lds_config_source->mutable_api_config_source();
      lds_api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
      lds_api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
      envoy::config::core::v3::GrpcService* grpc_service =
          lds_api_config_source->add_grpc_services();
      setGrpcService(*grpc_service, "lds_cluster", getLdsFakeUpstream().localAddress());
    });

    BaseIntegrationTest::initialize();
    registerTestServerPorts({port_name_});
  }

  void resetConnection(FakeHttpConnectionPtr& connection) {
    if (connection != nullptr) {
      AssertionResult result = connection->close();
      RELEASE_ASSERT(result, result.message());
      result = connection->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      connection.reset();
    }
  }

  ~NetworkExtensionDiscoveryIntegrationTest() override {
    resetConnection(ecds_connection_);
    resetConnection(lds_connection_);
    resetConnection(ecds2_connection_);
  }

  void createUpstreams() override {
    BaseIntegrationTest::createUpstreams();
    // Create the extension config discovery upstream (fake_upstreams_[1]).
    addFakeUpstream(Http::CodecType::HTTP2);
    // Create the listener config discovery upstream (fake_upstreams_[2]).
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
    // Wait for LDS stream.
    auto& lds_upstream = getLdsFakeUpstream();
    AssertionResult result = lds_upstream.waitForHttpConnection(*dispatcher_, lds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = lds_connection_->waitForNewStream(*dispatcher_, lds_stream_);
    RELEASE_ASSERT(result, result.message());
    lds_stream_->startGrpcStream();

    // Response with initial LDS.
    sendLdsResponse("initial");

    waitForEcdsStream(getEcdsFakeUpstream(), ecds_connection_, ecds_stream_);
    if (two_connections_) {
      // Wait for 2nd ECDS stream.
      waitForEcdsStream(getEcds2FakeUpstream(), ecds2_connection_, ecds2_stream_);
    }
  }

  void sendLdsResponse(const std::string& version) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url(Config::TypeUrl::get().Listener);
    response.add_resources()->PackFrom(listener_config_);
    lds_stream_->sendGrpcMessage(response);
  }

  void sendXdsResponse(const std::string& name, const std::string& version, uint32_t bytes_to_drain,
                       bool ttl = false, bool second_connection = false, bool is_terminal = false) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url("type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig");
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name(name);
    envoy::service::discovery::v3::Resource resource;
    resource.set_name(name);

    auto configuration = test::integration::filters::TestDrainerNetworkFilterConfig();
    configuration.set_bytes_to_drain(bytes_to_drain);
    configuration.set_is_terminal_filter(is_terminal);
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

  void sendDataVerifyResults(uint32_t bytes_drained) {
    test_server_->waitUntilListenersReady();
    test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
    EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort(port_name_));
    ASSERT_TRUE(tcp_client->write(data_));
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
    std::string received_data;
    ASSERT_TRUE(
        fake_upstream_connection->waitForData(data_.size() - bytes_drained, &received_data));
    const std::string expected_data = data_.substr(bytes_drained);
    EXPECT_EQ(expected_data, received_data);
    tcp_client->close();
  }

  // Verify ECDS config dump data.
  bool verifyConfigDumpData(
      envoy::config::core::v3::TypedExtensionConfig filter_config,
      test::integration::filters::TestDrainerNetworkFilterConfig network_filter_config) {
    // There is no ordering. i.e, either foo or bar could be the 1st in the config dump.
    if (filter_config.name() == "foo") {
      EXPECT_EQ(3, network_filter_config.bytes_to_drain());
      return true;
    } else if (filter_config.name() == "bar") {
      EXPECT_EQ(4, network_filter_config.bytes_to_drain());
      return true;
    } else {
      return false;
    }
  }

  // Utilities used for config dump.
  absl::string_view request(const std::string port_key, const std::string method,
                            const std::string endpoint, BufferingStreamDecoderPtr& response) {
    response = IntegrationUtil::makeSingleRequest(lookupPort(port_key), method, endpoint, "",
                                                  Http::CodecType::HTTP1, version_);
    EXPECT_TRUE(response->complete());
    return response->headers().getStatusValue();
  }

  absl::string_view contentType(const BufferingStreamDecoderPtr& response) {
    const Http::HeaderEntry* entry = response->headers().ContentType();
    if (entry == nullptr) {
      return "(null)";
    }
    return entry->value().getStringView();
  }

  const uint32_t default_bytes_to_drain_{2};
  const std::string filter_name_ = "foo";
  const std::string data_ = "HelloWorld";
  const std::string port_name_ = "tcp";
  bool two_connections_{false};

  FakeUpstream& getEcdsFakeUpstream() const { return *fake_upstreams_[1]; }
  FakeUpstream& getLdsFakeUpstream() const { return *fake_upstreams_[2]; }
  FakeUpstream& getEcds2FakeUpstream() const { return *fake_upstreams_[3]; }

  // gRPC LDS set-up
  envoy::config::listener::v3::Listener listener_config_;
  std::string listener_name_{"testing-listener-0"};
  FakeHttpConnectionPtr lds_connection_{nullptr};
  FakeStreamPtr lds_stream_{nullptr};

  // gRPC two ECDS connections set-up.
  FakeHttpConnectionPtr ecds_connection_{nullptr};
  FakeStreamPtr ecds_stream_{nullptr};
  FakeHttpConnectionPtr ecds2_connection_{nullptr};
  FakeStreamPtr ecds2_stream_{nullptr};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, NetworkExtensionDiscoveryIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(NetworkExtensionDiscoveryIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter(filter_name_, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update to have filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(5);

  // Send 2nd config update to have filter drain 3 bytes of data.
  sendXdsResponse(filter_name_, "2", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 2);
  sendDataVerifyResults(3);
}

TEST_P(NetworkExtensionDiscoveryIntegrationTest, BasicSuccessWithTtl) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter(filter_name_, false, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update with TTL 1s, and have network filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5, true);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(5);

  // Wait for configuration expired. Then start a TCP connection.
  // The missing config network filter will be installed to handle the connection.
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 2);

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort(port_name_));
  auto result = tcp_client->write(data_);
  if (result) {
    tcp_client->waitForDisconnect();
  }

  // The network_extension_config_missing stats counter increases by 1.
  test_server_->waitForCounterGe("listener.listener_stat.network_extension_config_missing", 1);

  // Reinstate the configuration.
  sendXdsResponse(filter_name_, "1", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 3);
  sendDataVerifyResults(3);
}

TEST_P(NetworkExtensionDiscoveryIntegrationTest, BasicSuccessWithTtlWithDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter(filter_name_, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update with TTL 1s, and have network filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5, true);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(5);

  // Wait for configuration expired. The default filter will be installed.
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 2);
  // Start a TCP connection. The default filter drains 2 bytes.
  sendDataVerifyResults(2);
}

TEST_P(NetworkExtensionDiscoveryIntegrationTest, BasicFailWithDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter(filter_name_, false, true);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update with invalid config (bytes_to_drain needs to be >=2).
  sendXdsResponse(filter_name_, "1", 1);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_fail", 1);
  // The default filter will be installed. Start a TCP connection. The default filter drain 2 bytes.
  sendDataVerifyResults(2);
}

TEST_P(NetworkExtensionDiscoveryIntegrationTest, BasicFailWithoutDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter(filter_name_, false, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update with invalid config (drain_bytes has to >=2).
  sendXdsResponse(filter_name_, "1", 1);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_fail", 1);

  // New connections will close since there's no valid configuration.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort(port_name_));
  auto result = tcp_client->write(data_);
  if (result) {
    tcp_client->waitForDisconnect();
  }

  // The network_extension_config_missing stats counter increases by 1.
  test_server_->waitForCounterGe("listener.listener_stat.network_extension_config_missing", 1);
}

TEST_P(NetworkExtensionDiscoveryIntegrationTest, BasicWithoutWarming) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter(filter_name_, true);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);

  // Send data without send config update.
  sendDataVerifyResults(default_bytes_to_drain_);

  // Send update should cause a different response.
  sendXdsResponse(filter_name_, "1", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(3);
}

TEST_P(NetworkExtensionDiscoveryIntegrationTest, BasicWithoutWarmingConfigFail) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter(filter_name_, true);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);

  // Send data without send config update.
  sendDataVerifyResults(default_bytes_to_drain_);

  // Send config update with invalid config (drain_bytes has to >=2).
  sendXdsResponse(filter_name_, "1", 1);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_fail", 1);
  sendDataVerifyResults(default_bytes_to_drain_);
}

TEST_P(NetworkExtensionDiscoveryIntegrationTest, TwoSubscriptionsSameName) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter(filter_name_, true);
  addDynamicFilter(filter_name_, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse(filter_name_, "1", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 1);

  // Each filter drain 3 bytes.
  sendDataVerifyResults(6);
}

TEST_P(NetworkExtensionDiscoveryIntegrationTest, TwoSubscriptionsDifferentName) {
  two_connections_ = true;
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter("foo", true);
  addDynamicFilter("bar", false, true, false, true);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update.
  sendXdsResponse("foo", "1", 3);
  sendXdsResponse("bar", "1", 4, false, true);
  test_server_->waitForCounterGe("extension_config_discovery.network_filter.foo.config_reload", 1);
  test_server_->waitForCounterGe("extension_config_discovery.network_filter.bar.config_reload", 1);
  // The two filters drain 3 + 4  bytes.
  sendDataVerifyResults(7);

  // Send 2nd config update.
  sendXdsResponse("foo", "2", 4);
  sendXdsResponse("bar", "2", 5, false, true);
  test_server_->waitForCounterGe("extension_config_discovery.network_filter.foo.config_reload", 2);
  test_server_->waitForCounterGe("extension_config_discovery.network_filter.bar.config_reload", 2);
  // The two filters drain 4 + 5  bytes.
  sendDataVerifyResults(9);
}

// Testing it works with mixed static/dynamic network filter configuration.
TEST_P(NetworkExtensionDiscoveryIntegrationTest, TwoDynamicTwoStaticFilterMixed) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter(filter_name_, false);
  addStaticFilter("bar", 2);
  addDynamicFilter(filter_name_, true);
  addStaticFilter("foobar", 2);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse(filter_name_, "1", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 1);
  // filter drain 3 + 2 + 3 + 2 bytes.
  sendDataVerifyResults(10);
}

TEST_P(NetworkExtensionDiscoveryIntegrationTest, DynamicStaticFilterMixedDifferentOrder) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addStaticFilter("bar", 2);
  addStaticFilter("baz", 2);
  addDynamicFilter(filter_name_, true);
  addDynamicFilter(filter_name_, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse(filter_name_, "1", 2);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 1);
  // filter drain 2 + 2 + 2 + 2 bytes.
  sendDataVerifyResults(8);
}

TEST_P(NetworkExtensionDiscoveryIntegrationTest, DestroyDuringInit) {
  // If rate limiting is enabled on the config source, gRPC mux drainage updates the requests
  // queue size on destruction. The update calls out to stats scope nested under the extension
  // config subscription stats scope. This test verifies that the stats scope outlasts the gRPC
  // subscription.
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter(filter_name_, false, true);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  test_server_.reset();
  auto result = ecds_connection_->waitForDisconnect();
  ASSERT_TRUE(result);
  ecds_connection_.reset();
}

// Validate that a network filter update should fail if the subscribed extension configuration make
// filter terminal but the filter position is not at the last position at filter chain. There would
// be total of 2 filters in the chain: 'foo' and 'tcp_proxy' and both are marked terminal.
TEST_P(NetworkExtensionDiscoveryIntegrationTest, BasicFailTerminalFilterNotAtEndOfFilterChain) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter(filter_name_, false, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse(filter_name_, "1", 5, false, false, true);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_fail", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

  // New connections will close since there's no valid configuration.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort(port_name_));
  auto result = tcp_client->write(data_);
  if (result) {
    tcp_client->waitForDisconnect();
  }

  // The network_extension_config_missing stats counter increases by 1.
  test_server_->waitForCounterGe("listener.listener_stat.network_extension_config_missing", 1);
}

// Basic ECDS config dump test with one filter.
TEST_P(NetworkExtensionDiscoveryIntegrationTest, BasicSuccessWithConfigDump) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin interface.
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter(filter_name_, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update to have network filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 1);

  // Verify ECDS config dump are working correctly.
  BufferingStreamDecoderPtr response;
  EXPECT_EQ("200", request("admin", "GET", "/config_dump", response));
  EXPECT_EQ("application/json", contentType(response));
  Json::ObjectSharedPtr json = Json::Factory::loadFromString(response->body());
  size_t index = 0;
  for (const Json::ObjectSharedPtr& obj_ptr : json->getObjectArray("configs")) {
    EXPECT_TRUE(expected_types[index].compare(obj_ptr->getString("@type")) == 0);
    index++;
  }

  // Validate we can parse as proto.
  envoy::admin::v3::ConfigDump config_dump;
  TestUtility::loadFromJson(response->body(), config_dump);
  EXPECT_EQ(5, config_dump.configs_size());

  // With /config_dump, the response has the format: EcdsConfigDump.
  envoy::admin::v3::EcdsConfigDump ecds_config_dump;
  config_dump.configs(2).UnpackTo(&ecds_config_dump);
  EXPECT_EQ("1", ecds_config_dump.ecds_filters(0).version_info());
  envoy::config::core::v3::TypedExtensionConfig filter_config;
  EXPECT_TRUE(ecds_config_dump.ecds_filters(0).ecds_filter().UnpackTo(&filter_config));
  EXPECT_EQ("foo", filter_config.name());
  test::integration::filters::TestDrainerNetworkFilterConfig network_filter_config;
  filter_config.typed_config().UnpackTo(&network_filter_config);
  EXPECT_EQ(5, network_filter_config.bytes_to_drain());
}

// ECDS config dump test with the filter configuration being removed by TTL expired.
TEST_P(NetworkExtensionDiscoveryIntegrationTest, ConfigDumpWithFilterConfigRemovedByTtl) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin interface.
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter(filter_name_, false, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update with TTL 1s.
  sendXdsResponse(filter_name_, "1", 5, true);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 1);
  // Wait for configuration expired.
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 2);

  BufferingStreamDecoderPtr response;
  EXPECT_EQ("200", request("admin", "GET", "/config_dump?resource=ecds_filters", response));
  envoy::admin::v3::ConfigDump config_dump;
  TestUtility::loadFromJson(response->body(), config_dump);
  // With /config_dump?resource=ecds_filters, the response has the format: EcdsFilterConfig.
  envoy::admin::v3::EcdsConfigDump::EcdsFilterConfig ecds_msg;
  config_dump.configs(0).UnpackTo(&ecds_msg);
  EXPECT_EQ("", ecds_msg.version_info());
  envoy::config::core::v3::TypedExtensionConfig filter_config;
  EXPECT_TRUE(ecds_msg.ecds_filter().UnpackTo(&filter_config));
  EXPECT_EQ("foo", filter_config.name());
  // Verify ECDS config dump doesn't have the filter configuration.
  EXPECT_EQ(false, filter_config.has_typed_config());
}

// ECDS config dump test with two filters.
TEST_P(NetworkExtensionDiscoveryIntegrationTest, TwoSubscriptionsSameFilterTypeWithConfigDump) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin interface.
  two_connections_ = true;
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter("foo", true);
  addDynamicFilter("bar", false, true, false, true);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse("foo", "1", 3);
  sendXdsResponse("bar", "1", 4, false, true);
  test_server_->waitForCounterGe("extension_config_discovery.network_filter.foo.config_reload", 1);
  test_server_->waitForCounterGe("extension_config_discovery.network_filter.bar.config_reload", 1);

  // Verify ECDS config dump are working correctly.
  BufferingStreamDecoderPtr response;
  EXPECT_EQ("200", request("admin", "GET", "/config_dump", response));
  EXPECT_EQ("application/json", contentType(response));
  Json::ObjectSharedPtr json = Json::Factory::loadFromString(response->body());
  size_t index = 0;
  for (const Json::ObjectSharedPtr& obj_ptr : json->getObjectArray("configs")) {
    EXPECT_TRUE(expected_types[index].compare(obj_ptr->getString("@type")) == 0);
    index++;
  }

  envoy::admin::v3::ConfigDump config_dump;
  TestUtility::loadFromJson(response->body(), config_dump);
  EXPECT_EQ(5, config_dump.configs_size());
  envoy::admin::v3::EcdsConfigDump ecds_config_dump;
  config_dump.configs(2).UnpackTo(&ecds_config_dump);
  envoy::config::core::v3::TypedExtensionConfig filter_config;
  test::integration::filters::TestDrainerNetworkFilterConfig network_filter_config;
  // Verify the first filter.
  EXPECT_EQ("1", ecds_config_dump.ecds_filters(0).version_info());
  EXPECT_TRUE(ecds_config_dump.ecds_filters(0).ecds_filter().UnpackTo(&filter_config));
  filter_config.typed_config().UnpackTo(&network_filter_config);
  EXPECT_TRUE(verifyConfigDumpData(filter_config, network_filter_config));
  // Verify the second filter.
  EXPECT_EQ("1", ecds_config_dump.ecds_filters(1).version_info());
  EXPECT_TRUE(ecds_config_dump.ecds_filters(1).ecds_filter().UnpackTo(&filter_config));
  filter_config.typed_config().UnpackTo(&network_filter_config);
  EXPECT_TRUE(verifyConfigDumpData(filter_config, network_filter_config));
}

// ECDS config dump test with specified resource and regex name search.
TEST_P(NetworkExtensionDiscoveryIntegrationTest, TwoSubscriptionsConfigDumpWithResourceAndRegex) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin interface.
  two_connections_ = true;
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter("foo", true);
  addDynamicFilter("bar", false, true, false, true);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse("foo", "1", 3);
  sendXdsResponse("bar", "1", 4, false, true);
  test_server_->waitForCounterGe("extension_config_discovery.network_filter.foo.config_reload", 1);
  test_server_->waitForCounterGe("extension_config_discovery.network_filter.bar.config_reload", 1);
  BufferingStreamDecoderPtr response;
  EXPECT_EQ("200",
            request("admin", "GET", "/config_dump?resource=ecds_filters&name_regex=.a.", response));

  envoy::admin::v3::ConfigDump config_dump;
  TestUtility::loadFromJson(response->body(), config_dump);
  EXPECT_EQ(1, config_dump.configs_size());
  envoy::admin::v3::EcdsConfigDump::EcdsFilterConfig ecds_msg;
  config_dump.configs(0).UnpackTo(&ecds_msg);
  EXPECT_EQ("1", ecds_msg.version_info());
  envoy::config::core::v3::TypedExtensionConfig filter_config;
  EXPECT_TRUE(ecds_msg.ecds_filter().UnpackTo(&filter_config));
  EXPECT_EQ("bar", filter_config.name());
  test::integration::filters::TestDrainerNetworkFilterConfig network_filter_config;
  filter_config.typed_config().UnpackTo(&network_filter_config);
  EXPECT_EQ(4, network_filter_config.bytes_to_drain());
}

TEST_P(NetworkExtensionDiscoveryIntegrationTest, ConfigUpdateDoesNotApplyToExistingConnection) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter(filter_name_, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update to have filter drain 5 bytes of data.
  uint32_t bytes_to_drain = 5;
  sendXdsResponse(filter_name_, "1", bytes_to_drain);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 1);

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort(port_name_));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send 2nd config update to have filter drain 3 bytes of data.
  sendXdsResponse(filter_name_, "2", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 2);

  ASSERT_TRUE(tcp_client->write(data_));
  std::string received_data;
  // Expect drained bytes to be 5 as the 2nd config update was performed after new connection
  // establishment.
  ASSERT_TRUE(fake_upstream_connection->waitForData(data_.size() - bytes_to_drain, &received_data));
  const std::string expected_data = data_.substr(bytes_to_drain);
  EXPECT_EQ(expected_data, received_data);
  tcp_client->close();
}

} // namespace
} // namespace Envoy
