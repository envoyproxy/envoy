#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.validate.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "test/extensions/filters/udp/udp_proxy/session_filters/drainer_filter.h"
#include "test/extensions/filters/udp/udp_proxy/session_filters/drainer_filter.pb.h"
#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"

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

class UdpSessionExtensionDiscoveryIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                                    public BaseIntegrationTest {
public:
  UdpSessionExtensionDiscoveryIntegrationTest()
      : BaseIntegrationTest(ipVersion(), ConfigHelper::baseUdpListenerConfig()) {
    skip_tag_extraction_rule_check_ = true;
  }

  void setupUdpProxyFilter() {
    config_helper_.addListenerFilter(R"EOF(
name: udp_proxy
typed_config:
  '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.UdpProxyConfig
  stat_prefix: foo
  matcher:
    on_no_match:
      action:
        name: route
        typed_config:
          '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
          cluster: cluster_0
)EOF");
  }

  void addDynamicFilter(const std::string& name, bool apply_without_warming,
                        bool set_default_config = true, bool rate_limit = false,
                        bool second_connection = false) {
    config_helper_.addConfigModifier([name, apply_without_warming, set_default_config, rate_limit,
                                      second_connection,
                                      this](ConfigHelper::UdpProxyConfig& udp_proxy) {
      auto* session_filter = udp_proxy.add_session_filters();
      session_filter->set_name(name);

      auto* discovery = session_filter->mutable_config_discovery();
      discovery->add_type_urls(
          "type.googleapis.com/"
          "test.extensions.filters.udp.udp_proxy.session_filters.DrainerUdpSessionFilterConfig");

      if (set_default_config) {
        auto default_configuration =
            Extensions::UdpFilters::UdpProxy::SessionFilters::DrainerConfig();
        default_configuration.set_downstream_bytes_to_drain(default_bytes_to_drain_);
        default_configuration.set_upstream_bytes_to_drain(default_bytes_to_drain_);
        default_configuration.set_stop_iteration_on_new_session(false);
        default_configuration.set_stop_iteration_on_first_read(false);
        default_configuration.set_continue_filter_chain(false);
        default_configuration.set_stop_iteration_on_first_write(false);
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
        [name, bytes_to_drain](ConfigHelper::UdpProxyConfig& udp_proxy) {
          auto* session_filter = udp_proxy.add_session_filters();
          session_filter->set_name(name);

          auto configuration = Extensions::UdpFilters::UdpProxy::SessionFilters::DrainerConfig();
          configuration.set_downstream_bytes_to_drain(bytes_to_drain);
          configuration.set_upstream_bytes_to_drain(bytes_to_drain);
          configuration.set_stop_iteration_on_new_session(false);
          configuration.set_stop_iteration_on_first_read(false);
          configuration.set_continue_filter_chain(false);
          configuration.set_stop_iteration_on_first_write(false);

          session_filter->mutable_typed_config()->PackFrom(configuration);
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

    FakeUpstreamConfig::UdpConfig config;
    config.max_rx_datagram_size_ = Network::DEFAULT_UDP_MAX_DATAGRAM_SIZE;
    setUdpFakeUpstream(config);

    addEcdsCluster(std::string(EcdsClusterName));

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

  ~UdpSessionExtensionDiscoveryIntegrationTest() override {
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

  void sendLdsResponse(const std::string& version) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url(Config::TypeUrl::get().Listener);
    response.add_resources()->PackFrom(listener_config_);
    lds_stream_->sendGrpcMessage(response);
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

  void sendXdsResponse(const std::string& name, const std::string& version, uint32_t bytes_to_drain,
                       bool ttl = false, bool second_connection = false) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url("type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig");
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name(name);
    envoy::service::discovery::v3::Resource resource;
    resource.set_name(name);

    auto configuration = Extensions::UdpFilters::UdpProxy::SessionFilters::DrainerConfig();
    configuration.set_downstream_bytes_to_drain(bytes_to_drain);
    configuration.set_upstream_bytes_to_drain(bytes_to_drain);
    configuration.set_stop_iteration_on_new_session(false);
    configuration.set_stop_iteration_on_first_read(false);
    configuration.set_continue_filter_chain(false);
    configuration.set_stop_iteration_on_first_write(false);
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

  void updateStatsForSuccessfulSession(std::string request, std::string expected_request,
                                       std::string response, std::string expected_response) {
    ds_rx_bytes_ += request.size();
    ds_tx_bytes_ += expected_response.size();
    us_rx_bytes_ += response.size();
    us_tx_bytes_ += expected_request.size();
    expected_datagrams_ += 1;
    total_sessions_ += 1;
    active_sessions_ += 1;
  }

  void verifyStats() {
    test_server_->waitForCounterEq("udp.foo.downstream_sess_rx_bytes", ds_rx_bytes_);
    test_server_->waitForCounterEq("udp.foo.downstream_sess_rx_datagrams", expected_datagrams_);
    test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_tx_bytes_total", us_tx_bytes_);
    test_server_->waitForCounterEq("cluster.cluster_0.udp.sess_tx_datagrams", expected_datagrams_);

    test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_rx_bytes_total", us_rx_bytes_);
    test_server_->waitForCounterEq("cluster.cluster_0.udp.sess_rx_datagrams", expected_datagrams_);

    test_server_->waitForCounterEq("udp.foo.downstream_sess_tx_bytes", ds_tx_bytes_);
    test_server_->waitForCounterEq("udp.foo.downstream_sess_tx_datagrams", expected_datagrams_);

    test_server_->waitForCounterEq("udp.foo.downstream_sess_total", total_sessions_);
    test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", active_sessions_);
  }

  void requestResponseWithListenerAddress(const Network::Address::Instance& listener_address,
                                          std::string request, std::string expected_request,
                                          std::string response, std::string expected_response) {
    // Send datagram to be proxied.
    Network::Test::UdpSyncPeer client(version_, Network::DEFAULT_UDP_MAX_DATAGRAM_SIZE);
    client.write(request, listener_address);

    // Wait for the upstream datagram.
    Network::UdpRecvData request_datagram;
    ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
    EXPECT_EQ(expected_request, request_datagram.buffer_->toString());

    // Respond from the upstream.
    fake_upstreams_[0]->sendUdpDatagram(response, request_datagram.addresses_.peer_);
    Network::UdpRecvData response_datagram;
    client.recv(response_datagram);
    EXPECT_EQ(expected_response, response_datagram.buffer_->toString());
    EXPECT_EQ(listener_address.asString(), response_datagram.addresses_.peer_->asString());

    updateStatsForSuccessfulSession(request, expected_request, response, expected_response);
    verifyStats();
  }

  void sendDataVerifyResults(uint32_t bytes_drained) {
    test_server_->waitUntilListenersReady();
    test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
    EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

    const uint32_t port = lookupPort(port_name_);
    const auto listener_address = *Network::Utility::resolveUrl(
        fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

    std::string request = "hellohellohello";
    std::string response = "worldworldworld";

    requestResponseWithListenerAddress(*listener_address, request, request.substr(bytes_drained),
                                       response, response.substr(bytes_drained));
  }

  void sendDataExpectSessionFailure() {
    const uint32_t port = lookupPort(port_name_);
    const auto listener_address = *Network::Utility::resolveUrl(
        fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));
    Network::Test::UdpSyncPeer client(version_, Network::DEFAULT_UDP_MAX_DATAGRAM_SIZE);
    client.write("hello", *listener_address);

    // The new datagram is expected to create a session that will be destroyed, since the session
    // filter configuration is missing. Expect that the follow stat will increase.
    test_server_->waitForCounterGe("udp.foo.session_filter_config_missing", 1);
    total_sessions_ += 1;
    verifyStats();
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

  // Verify ECDS config dump data.
  bool verifyConfigDumpData(
      envoy::config::core::v3::TypedExtensionConfig filter_config,
      Extensions::UdpFilters::UdpProxy::SessionFilters::DrainerConfig udp_session_filter_config) {
    // There is no ordering. i.e, either foo or bar could be the 1st in the config dump.
    if (filter_config.name() == "foo") {
      EXPECT_EQ(3, udp_session_filter_config.downstream_bytes_to_drain());
      EXPECT_EQ(3, udp_session_filter_config.upstream_bytes_to_drain());
      return true;
    } else if (filter_config.name() == "bar") {
      EXPECT_EQ(4, udp_session_filter_config.downstream_bytes_to_drain());
      EXPECT_EQ(4, udp_session_filter_config.upstream_bytes_to_drain());
      return true;
    } else {
      return false;
    }
  }

  int ds_rx_bytes_{0};
  int us_tx_bytes_{0};
  int us_rx_bytes_{0};
  int ds_tx_bytes_{0};
  int expected_datagrams_{0};
  int total_sessions_{0};
  int active_sessions_{0};

  const uint32_t default_bytes_to_drain_{2};
  const std::string filter_name_ = "foo";
  const std::string port_name_ = "udp";
  bool two_connections_{false};

  FakeUpstream& getEcdsFakeUpstream() const { return *fake_upstreams_[1]; }
  FakeUpstream& getLdsFakeUpstream() const { return *fake_upstreams_[2]; }
  FakeUpstream& getEcds2FakeUpstream() const { return *fake_upstreams_[3]; }

  // gRPC LDS set-up
  envoy::config::listener::v3::Listener listener_config_;
  std::string listener_name_{"listener_0"};
  FakeHttpConnectionPtr lds_connection_{nullptr};
  FakeStreamPtr lds_stream_{nullptr};

  // gRPC two ECDS connections set-up.
  FakeHttpConnectionPtr ecds_connection_{nullptr};
  FakeStreamPtr ecds_stream_{nullptr};
  FakeHttpConnectionPtr ecds2_connection_{nullptr};
  FakeStreamPtr ecds2_stream_{nullptr};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, UdpSessionExtensionDiscoveryIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(UdpSessionExtensionDiscoveryIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  setupUdpProxyFilter();
  addDynamicFilter(filter_name_, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update to have filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5);
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(5);

  // Send 2nd config update to have filter drain 3 bytes of data.
  sendXdsResponse(filter_name_, "2", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_reload", 2);
  sendDataVerifyResults(3);
}

TEST_P(UdpSessionExtensionDiscoveryIntegrationTest, BasicSuccessWithTtl) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  setupUdpProxyFilter();
  addDynamicFilter(filter_name_, false, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update with TTL 1s, and have the filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5, true);
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(5);

  // Wait for configuration expired.
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_reload", 2);

  sendDataExpectSessionFailure();

  // Reinstate the configuration.
  sendXdsResponse(filter_name_, "1", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_reload", 3);
  sendDataVerifyResults(3);
}

TEST_P(UdpSessionExtensionDiscoveryIntegrationTest, BasicSuccessWithTtlWithDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  setupUdpProxyFilter();
  addDynamicFilter(filter_name_, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update with TTL 1s, and have the filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5, true);
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(5);

  // Wait for configuration expired. The default filter will be installed.
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_reload", 2);

  // Start a new session. The default filter drains 2 bytes.
  sendDataVerifyResults(default_bytes_to_drain_);
}

TEST_P(UdpSessionExtensionDiscoveryIntegrationTest, BasicFailWithDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  setupUdpProxyFilter();
  addDynamicFilter(filter_name_, false, true);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update with invalid config (bytes_to_drain needs to be <= 20).
  sendXdsResponse(filter_name_, "1", 21);
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_fail", 1);

  // The default filter will be used. Start a UDP session. The default filter drain 2 bytes.
  sendDataVerifyResults(default_bytes_to_drain_);
}

TEST_P(UdpSessionExtensionDiscoveryIntegrationTest, BasicFailWithoutDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  setupUdpProxyFilter();
  addDynamicFilter(filter_name_, false, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update with invalid config (bytes_to_drain needs to be <= 20).
  sendXdsResponse(filter_name_, "1", 21);
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_fail", 1);

  sendDataExpectSessionFailure();
}

TEST_P(UdpSessionExtensionDiscoveryIntegrationTest, BasicWithoutWarming) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  setupUdpProxyFilter();
  addDynamicFilter(filter_name_, true);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);

  // Send data without send config update.
  sendDataVerifyResults(default_bytes_to_drain_);

  // Send update should cause a different response.
  sendXdsResponse(filter_name_, "1", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(3);
}

TEST_P(UdpSessionExtensionDiscoveryIntegrationTest, BasicWithoutWarmingConfigFail) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  setupUdpProxyFilter();
  addDynamicFilter(filter_name_, true);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);

  // Send data without send config update.
  sendDataVerifyResults(default_bytes_to_drain_);

  // Send config update with invalid config (drain_bytes has to be <= 21).
  sendXdsResponse(filter_name_, "1", 21);
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_fail", 1);
  sendDataVerifyResults(default_bytes_to_drain_);
}

TEST_P(UdpSessionExtensionDiscoveryIntegrationTest, BasicWithoutWarmingNoDefaultConfig) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  setupUdpProxyFilter();
  addDynamicFilter(filter_name_, true, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);

  // No default configuration and no warming, expect new session to fail due to missing config.
  sendDataExpectSessionFailure();
}

TEST_P(UdpSessionExtensionDiscoveryIntegrationTest, TwoSubscriptionsSameName) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  setupUdpProxyFilter();
  addDynamicFilter(filter_name_, true);
  addDynamicFilter(filter_name_, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse(filter_name_, "1", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_reload", 1);

  // Each filter drain 3 bytes.
  sendDataVerifyResults(6);
}

TEST_P(UdpSessionExtensionDiscoveryIntegrationTest, TwoSubscriptionsDifferentName) {
  two_connections_ = true;
  on_server_init_function_ = [&]() { waitXdsStream(); };
  setupUdpProxyFilter();
  addDynamicFilter("foo", true);
  addDynamicFilter("bar", false, true, false, true);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update.
  sendXdsResponse("foo", "1", 3);
  sendXdsResponse("bar", "1", 4, false, true);
  test_server_->waitForCounterGe("extension_config_discovery.udp_session_filter.foo.config_reload",
                                 1);
  test_server_->waitForCounterGe("extension_config_discovery.udp_session_filter.bar.config_reload",
                                 1);
  // The two filters drain 3 + 4  bytes.
  sendDataVerifyResults(7);

  // Send 2nd config update.
  sendXdsResponse("foo", "2", 4);
  sendXdsResponse("bar", "2", 5, false, true);
  test_server_->waitForCounterGe("extension_config_discovery.udp_session_filter.foo.config_reload",
                                 2);
  test_server_->waitForCounterGe("extension_config_discovery.udp_session_filter.bar.config_reload",
                                 2);
  // The two filters drain 4 + 5  bytes.
  sendDataVerifyResults(9);
}

TEST_P(UdpSessionExtensionDiscoveryIntegrationTest, TwoDynamicTwoStaticFilterMixed) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  setupUdpProxyFilter();
  addDynamicFilter(filter_name_, false);
  addStaticFilter("bar", 2);
  addDynamicFilter(filter_name_, true);
  addStaticFilter("foobar", 2);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse(filter_name_, "1", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_reload", 1);
  // filter drain 3 + 2 + 3 + 2 bytes.
  sendDataVerifyResults(10);
}

TEST_P(UdpSessionExtensionDiscoveryIntegrationTest, DynamicStaticFilterMixedDifferentOrder) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  setupUdpProxyFilter();
  addStaticFilter("bar", 2);
  addStaticFilter("baz", 2);
  addDynamicFilter(filter_name_, true);
  addDynamicFilter(filter_name_, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse(filter_name_, "1", 2);
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_reload", 1);
  // filter drain 2 + 2 + 2 + 2 bytes.
  sendDataVerifyResults(8);
}

TEST_P(UdpSessionExtensionDiscoveryIntegrationTest, BasicSuccessWithConfigDump) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin interface.
  on_server_init_function_ = [&]() { waitXdsStream(); };
  setupUdpProxyFilter();
  addDynamicFilter(filter_name_, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update to have network filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5);
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_reload", 1);

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
  Extensions::UdpFilters::UdpProxy::SessionFilters::DrainerConfig udp_session_filter_config;
  filter_config.typed_config().UnpackTo(&udp_session_filter_config);
  EXPECT_EQ(5, udp_session_filter_config.downstream_bytes_to_drain());
  EXPECT_EQ(5, udp_session_filter_config.upstream_bytes_to_drain());
}

// ECDS config dump test with the filter configuration being removed by TTL expired.
TEST_P(UdpSessionExtensionDiscoveryIntegrationTest, ConfigDumpWithFilterConfigRemovedByTtl) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin interface.
  on_server_init_function_ = [&]() { waitXdsStream(); };
  setupUdpProxyFilter();
  addDynamicFilter(filter_name_, false, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update with TTL 1s.
  sendXdsResponse(filter_name_, "1", 5, true);
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_reload", 1);
  // Wait for configuration expired.
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_reload", 2);

  BufferingStreamDecoderPtr response;
  EXPECT_EQ("200", request("admin", "GET", "/config_dump?resource=ecds_filters", response));
  envoy::admin::v3::ConfigDump config_dump;
  TestUtility::loadFromJson(response->body(), config_dump);
  // With /config_dump?resource=ecds_filters, the response has the format: EcdsFilterConfig.
  // The number of current ECDS configurations is zero because the ECDS resources have been deleted
  // due to expiration.
  EXPECT_EQ(0, config_dump.configs_size());
}

TEST_P(UdpSessionExtensionDiscoveryIntegrationTest, TwoSubscriptionsSameFilterTypeWithConfigDump) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin interface.
  two_connections_ = true;
  on_server_init_function_ = [&]() { waitXdsStream(); };
  setupUdpProxyFilter();
  addDynamicFilter("foo", true);
  addDynamicFilter("bar", false, true, false, true);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse("foo", "1", 3);
  sendXdsResponse("bar", "1", 4, false, true);
  test_server_->waitForCounterGe("extension_config_discovery.udp_session_filter.foo.config_reload",
                                 1);
  test_server_->waitForCounterGe("extension_config_discovery.udp_session_filter.bar.config_reload",
                                 1);

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

  Extensions::UdpFilters::UdpProxy::SessionFilters::DrainerConfig udp_session_filter_config;
  // Verify the first filter.
  EXPECT_EQ("1", ecds_config_dump.ecds_filters(0).version_info());
  EXPECT_TRUE(ecds_config_dump.ecds_filters(0).ecds_filter().UnpackTo(&filter_config));
  filter_config.typed_config().UnpackTo(&udp_session_filter_config);
  EXPECT_TRUE(verifyConfigDumpData(filter_config, udp_session_filter_config));
  // Verify the second filter.
  EXPECT_EQ("1", ecds_config_dump.ecds_filters(1).version_info());
  EXPECT_TRUE(ecds_config_dump.ecds_filters(1).ecds_filter().UnpackTo(&filter_config));
  filter_config.typed_config().UnpackTo(&udp_session_filter_config);
  EXPECT_TRUE(verifyConfigDumpData(filter_config, udp_session_filter_config));
}

// ECDS config dump test with specified resource and regex name search.
TEST_P(UdpSessionExtensionDiscoveryIntegrationTest,
       TwoSubscriptionsConfigDumpWithResourceAndRegex) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin interface.
  two_connections_ = true;
  on_server_init_function_ = [&]() { waitXdsStream(); };
  setupUdpProxyFilter();
  addDynamicFilter("foo", true);
  addDynamicFilter("bar", false, true, false, true);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse("foo", "1", 3);
  sendXdsResponse("bar", "1", 4, false, true);
  test_server_->waitForCounterGe("extension_config_discovery.udp_session_filter.foo.config_reload",
                                 1);
  test_server_->waitForCounterGe("extension_config_discovery.udp_session_filter.bar.config_reload",
                                 1);
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
  Extensions::UdpFilters::UdpProxy::SessionFilters::DrainerConfig udp_session_filter_config;
  filter_config.typed_config().UnpackTo(&udp_session_filter_config);
  EXPECT_EQ(4, udp_session_filter_config.downstream_bytes_to_drain());
  EXPECT_EQ(4, udp_session_filter_config.upstream_bytes_to_drain());
}

TEST_P(UdpSessionExtensionDiscoveryIntegrationTest, ConfigUpdateDoesNotApplyToExistingSession) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  setupUdpProxyFilter();
  addDynamicFilter(filter_name_, false);
  initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update to have filter drain 5 bytes of data.
  uint32_t bytes_to_drain = 5;
  sendXdsResponse(filter_name_, "1", bytes_to_drain);
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_reload", 1);

  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

  const uint32_t port = lookupPort(port_name_);
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  std::string request = "hellohellohello";
  std::string response = "worldworldworld";
  std::string expected_request = "hellohello";
  std::string expected_response = "worldworld";

  // Send datagram to be proxied.
  Network::Test::UdpSyncPeer client(version_, Network::DEFAULT_UDP_MAX_DATAGRAM_SIZE);
  client.write(request, *listener_address);

  // Wait for the upstream datagram.
  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ(expected_request, request_datagram.buffer_->toString());

  // Respond from the upstream.
  fake_upstreams_[0]->sendUdpDatagram(response, request_datagram.addresses_.peer_);
  Network::UdpRecvData response_datagram;
  client.recv(response_datagram);
  EXPECT_EQ(expected_response, response_datagram.buffer_->toString());
  EXPECT_EQ(listener_address->asString(), response_datagram.addresses_.peer_->asString());

  updateStatsForSuccessfulSession(request, expected_request, response, expected_response);
  verifyStats();

  // Send 2nd config update to have filter drain 3 bytes of data.
  sendXdsResponse(filter_name_, "2", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.udp_session_filter." + filter_name_ + ".config_reload", 2);

  // Using the same client to send another datagram. It should not create a new session, and the
  // number of bytes drained should not change, as the new configuration does not apply to the
  // existing session.
  client.write(request, *listener_address);
  Network::UdpRecvData request_datagram2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram2));
  EXPECT_EQ(expected_request, request_datagram2.buffer_->toString());
  fake_upstreams_[0]->sendUdpDatagram(response, request_datagram.addresses_.peer_);
  Network::UdpRecvData response_datagram2;
  client.recv(response_datagram2);
  EXPECT_EQ(expected_response, response_datagram2.buffer_->toString());
  EXPECT_EQ(listener_address->asString(), response_datagram2.addresses_.peer_->asString());
}

} // namespace
} // namespace Envoy
