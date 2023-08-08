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

class UpstreamNetworkFiltersIntegrationTestBase : public BaseIntegrationTest {
public:
  UpstreamNetworkFiltersIntegrationTestBase(Network::Address::IpVersion ip_version,
                                            std::string base_config)
      : BaseIntegrationTest(ip_version, base_config) {
    skip_tag_extraction_rule_check_ = true;
  }

  void addFilterChain() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      listener->set_stat_prefix("listener_stat");
      listener->add_filter_chains();
    });
  }

  void addStaticFilter(const std::string& name, uint32_t bytes_to_drain) {
    config_helper_.addConfigModifier(
        [name, bytes_to_drain](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
          auto* filter = cluster->add_filters();
          filter->set_name(name);
          auto configuration = test::integration::filters::TestDrainerUpstreamNetworkFilterConfig();
          configuration.set_bytes_to_drain(bytes_to_drain);
          filter->mutable_typed_config()->PackFrom(configuration);
        });
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

  const uint32_t default_bytes_to_drain_{2};
  const std::string filter_name_ = "foo";
  const std::string data_ = "HelloWorld";
  const std::string port_name_ = "tcp";
};

class StaticUpstreamNetworkFilterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public UpstreamNetworkFiltersIntegrationTestBase {
public:
  StaticUpstreamNetworkFilterIntegrationTest()
      : UpstreamNetworkFiltersIntegrationTestBase(GetParam(), ConfigHelper::baseConfig()) {}

  void initialize() override {
    setUpstreamCount(1);

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

    BaseIntegrationTest::initialize();
    registerTestServerPorts({port_name_});
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, StaticUpstreamNetworkFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(StaticUpstreamNetworkFilterIntegrationTest, BasicSuccess) {
  addFilterChain();
  addStaticFilter(filter_name_, 5);
  initialize();

  sendDataVerifyResults(5);
}

TEST_P(StaticUpstreamNetworkFilterIntegrationTest, TwoStaticFilters) {
  addFilterChain();
  addStaticFilter(filter_name_, 5);
  addStaticFilter(filter_name_, 3);
  initialize();

  sendDataVerifyResults(8);
}

class UpstreamNetworkExtensionDiscoveryIntegrationTest
    : public Grpc::GrpcClientIntegrationParamTest,
      public UpstreamNetworkFiltersIntegrationTestBase {
public:
  UpstreamNetworkExtensionDiscoveryIntegrationTest()
      : UpstreamNetworkFiltersIntegrationTestBase(ipVersion(), ConfigHelper::baseConfig()) {
    skip_tag_extraction_rule_check_ = true;
  }

  void addDynamicFilter(const std::string& name, bool apply_without_warming,
                        bool set_default_config = true, bool rate_limit = false,
                        bool second_connection = false) {
    config_helper_.addConfigModifier([name, apply_without_warming, set_default_config, rate_limit,
                                      second_connection,
                                      this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      auto* filter = cluster->add_filters();
      filter->set_name(name);

      auto* discovery = filter->mutable_config_discovery();
      discovery->add_type_urls(
          "type.googleapis.com/test.integration.filters.TestDrainerUpstreamNetworkFilterConfig");
      if (set_default_config) {
        auto default_configuration =
            test::integration::filters::TestDrainerUpstreamNetworkFilterConfig();
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

  void addEcdsCluster(const std::string& cluster_name) {
    // Add an xDS cluster for extension config discovery.
    config_helper_.addConfigModifier(
        [cluster_name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* ecds_cluster = bootstrap.mutable_static_resources()->add_clusters();
          ecds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
          ecds_cluster->set_name(cluster_name);
          ecds_cluster->mutable_filters()->Clear();
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
      lds_cluster->mutable_filters()->Clear();
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

  ~UpstreamNetworkExtensionDiscoveryIntegrationTest() override {
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
                       bool ttl = false, bool second_connection = false) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url("type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig");
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name(name);
    envoy::service::discovery::v3::Resource resource;
    resource.set_name(name);
    auto configuration = test::integration::filters::TestDrainerUpstreamNetworkFilterConfig();
    configuration.set_bytes_to_drain(bytes_to_drain);
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

  // Verify ECDS config dump data.
  bool verifyConfigDumpData(
      envoy::config::core::v3::TypedExtensionConfig filter_config,
      test::integration::filters::TestDrainerUpstreamNetworkFilterConfig network_filter_config) {
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

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, UpstreamNetworkExtensionDiscoveryIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(UpstreamNetworkExtensionDiscoveryIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter(filter_name_, false);
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  registerTestServerPorts({port_name_});
  // Send 1st config update to have filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5);
  test_server_->waitForCounterGe(
      "extension_config_discovery.upstream_network_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(5);
  // Send 2nd config update to have filter drain 3 bytes of data.
  sendXdsResponse(filter_name_, "2", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.upstream_network_filter." + filter_name_ + ".config_reload", 2);
  sendDataVerifyResults(3);
}

} // namespace
} // namespace Envoy
