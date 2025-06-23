#pragma once

#include "envoy/admin/v3/config_dump_shared.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/ads_integration.h"
#include "test/integration/filters/test_network_filter.pb.h"
#include "test/test_common/utility.h"

#include "fake_upstream.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

class FcdsIntegrationTestBase : public AdsIntegrationTest {
public:
  FcdsIntegrationTestBase()
      : AdsIntegrationTest(bootstrapConfig((sotwOrDelta() == Grpc::SotwOrDelta::Sotw) ||
                                                   (sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw)
                                               ? "GRPC"
                                               : "DELTA_GRPC")) {}

  std::string bootstrapConfig(const std::string& api_type) {
    return fmt::format(R"EOF(
      dynamic_resources:
        lds_config:
          ads: {{}}
        ads_config:
          api_type: {0}
          set_node_on_first_message_only: true
      static_resources:
        clusters:
          name: cluster_0
          load_assignment:
            cluster_name: cluster_0
            endpoints:
            - lb_endpoints:
              - endpoint:
                  address:
                    socket_address:
                      address: 127.0.0.1
                      port_value: 0
      admin:
        access_log:
        - name: envoy.access_loggers.file
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
            path: "{1}"
        address:
          socket_address:
            address: 127.0.0.1
            port_value: 0
      )EOF",
                       api_type, Platform::null_device_path);
  }

  struct FilterChainConfig {
    std::string name_;
    absl::optional<std::string> source_ip_match_;
    std::string filter_name_;
    absl::optional<std::string> direct_response_;
  };

  struct ExpectedListenerDump {
    std::string listener_version_;
    std::string filter_chains_version_;
    envoy::config::listener::v3::Listener listener_;
    bool warming_{false};
  };

  envoy::config::listener::v3::Listener
  listenerConfig(const std::string& name, const std::string& fcds_collection_name,
                 bool start_without_warming, absl::optional<std::string> default_response,
                 absl::optional<FilterChainConfig> filter_chain_config,
                 absl::optional<std::vector<std::tuple<std::string, std::string>>> matcher_rules) {
    std::string filter_chain;
    if (filter_chain_config.has_value()) {
      auto& config = filter_chain_config.value();
      std::string match;
      if (config.source_ip_match_.has_value()) {
        match = fmt::format(R"EOF(
        filter_chain_match:
          source_prefix_ranges:
          - address_prefix: {0}
            prefix_len: 32
        )EOF",
                            config.source_ip_match_.value());
      }

      std::string filter_config;
      if (config.direct_response_.has_value()) {
        filter_config = fmt::format(R"EOF(
          typed_config:
            "@type": type.googleapis.com/test.integration.filters.TestDrainerNetworkFilterConfig
            is_terminal_filter: true
            bytes_to_drain: 2
            drain_all_data: true
            direct_response: "{0}"
        )EOF",
                                    config.direct_response_.value());
      } else {
        filter_config = R"EOF(
          config_discovery:
            type_urls:
            - type.googleapis.com/test.integration.filters.TestDrainerNetworkFilterConfig
            config_source:
              resource_api_version: V3
              api_config_source:
                api_type: GRPC
                transport_api_version: V3
                grpc_services:
        )EOF";

        if (clientType() == Grpc::ClientType::EnvoyGrpc) {
          filter_config += fmt::format(R"EOF(
                - envoy_grpc:
                    cluster_name: {0}
          )EOF",
                                       ecds_cluster_name_);
        } else {
          filter_config +=
              fmt::format(R"EOF(
                - google_grpc:
                    target_uri: {0}
                    stat_prefix: {1}
          )EOF",
                          fake_upstreams_[1]->localAddress()->asString(), ecds_cluster_name_);
        }
      }

      filter_chain = fmt::format(R"EOF(
      filter_chains:
      - name: {0}
        {1}
        filters:
        - name: {2}
          {3}
      )EOF",
                                 config.name_, match, config.filter_name_, filter_config);
    }

    std::string fcds_config =
        fmt::format(R"EOF(
      fcds_config:
        resources_locator: xdstp://test/envoy.config.listener.v3.FilterChain/{0}/*
        start_listener_without_warming: {1}
        config_source:
          resource_api_version: V3
          ads: {{}}
    )EOF",
                    fcds_collection_name, start_without_warming ? "true" : "false");

    std::string default_filter_chain;
    if (default_response.has_value()) {
      default_filter_chain = fmt::format(R"EOF(
      default_filter_chain:
        filters:
        - name: direct_response
          typed_config:
            "@type": type.googleapis.com/test.integration.filters.TestDrainerNetworkFilterConfig
            is_terminal_filter: true
            bytes_to_drain: 2
            drain_all_data: true
            direct_response: "{0}"
      )EOF",
                                         default_response.value());
    }

    std::string listener_level_matcher;
    if (matcher_rules.has_value()) {
      listener_level_matcher = R"EOF(
      filter_chain_matcher:
        matcher_tree:
          input:
            name: source_ip_matcher
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.SourceIPInput
          exact_match_map:
            map:
      )EOF";

      for (const auto& [ip, filter_chain] : matcher_rules.value()) {
        listener_level_matcher += fmt::format(R"EOF(
              "{0}":
                action:
                  name: choose_filter_chain
                  typed_config:
                    "@type": type.googleapis.com/google.protobuf.StringValue
                    value: {1}
        )EOF",
                                              ip, filter_chain);
      }
    }

    std::string config =
        fmt::format(R"EOF(
      name: {0}
      stat_prefix: {0}
      address:
        socket_address:
          address: {1}
          port_value: 0
      {2}
      {3}
      {4}
      {5}
    )EOF",
                    name, Network::Test::getLoopbackAddressString(ipVersion()), fcds_config,
                    filter_chain, default_filter_chain, listener_level_matcher);

    envoy::config::listener::v3::Listener listener;
    TestUtility::loadFromYaml(config, listener);
    return listener;
  }

  envoy::config::listener::v3::FilterChain
  filterChainConfig(const std::string& listener_name, const std::string& name,
                    const std::string& filter_name, absl::optional<std::string> direct_response,
                    absl::optional<std::string> source_ip_match) {
    std::string filter_config;
    if (direct_response.has_value()) {
      filter_config = fmt::format(R"EOF(
        typed_config:
          "@type": type.googleapis.com/test.integration.filters.TestDrainerNetworkFilterConfig
          is_terminal_filter: true
          bytes_to_drain: 2
          drain_all_data: true
          direct_response: "{0}"
      )EOF",
                                  direct_response.value());
    } else {
      filter_config = R"EOF(
        config_discovery:
          type_urls:
          - type.googleapis.com/test.integration.filters.TestDrainerNetworkFilterConfig
          config_source:
            resource_api_version: V3
            api_config_source:
              api_type: GRPC
              transport_api_version: V3
              grpc_services:
      )EOF";

      if (clientType() == Grpc::ClientType::EnvoyGrpc) {
        filter_config += fmt::format(R"EOF(
              - envoy_grpc:
                  cluster_name: {0}
        )EOF",
                                     ecds_cluster_name_);
      } else {
        filter_config +=
            fmt::format(R"EOF(
              - google_grpc:
                  target_uri: {0}
                  stat_prefix: {1}
        )EOF",
                        fake_upstreams_[1]->localAddress()->asString(), ecds_cluster_name_);
      }
    }

    std::string filter_chain_match;
    if (source_ip_match.has_value()) {
      filter_chain_match = fmt::format(R"EOF(
      filter_chain_match:
        source_prefix_ranges:
        - address_prefix: {0}
          prefix_len: 32
      )EOF",
                                       source_ip_match.value());
    }

    std::string config = fmt::format(R"EOF(
      name: {0}
      {1}
      filters:
        name: {2}
        {3}
    )EOF",
                                     xdstpResource(listener_name, name), filter_chain_match,
                                     filter_name, filter_config);

    envoy::config::listener::v3::FilterChain filter_chain;
    TestUtility::loadFromYaml(config, filter_chain);
    return filter_chain;
  }

  envoy::config::core::v3::TypedExtensionConfig
  extensionConfig(const std::string& name, const std::string& direct_response) {
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name(name);
    auto configuration = test::integration::filters::TestDrainerNetworkFilterConfig();
    configuration.set_is_terminal_filter(true);
    configuration.set_direct_response(direct_response);
    configuration.set_bytes_to_drain(2); // Not used in this test. Overridden by drain_all_data.
    configuration.set_drain_all_data(true);
    typed_config.mutable_typed_config()->PackFrom(configuration);
    return typed_config;
  }

  std::string xdstpResource(const std::string& listener_name,
                            const std::string& filter_chain_name) {
    return fmt::format("xdstp://test/envoy.config.listener.v3.FilterChain/{}/{}", listener_name,
                       filter_chain_name);
  }

  IntegrationTcpClientPtr sendDataVerifyResponse(const std::string& port_name,
                                                 const std::string& expected_response,
                                                 const std::string& source_ip,
                                                 bool keep_open = false) {
    registerTestServerPorts({l0_port_, l1_port_});
    EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
    Network::Address::InstanceConstSharedPtr source_address =
        Network::Utility::parseInternetAddressNoThrow(source_ip);
    IntegrationTcpClientPtr tcp_client =
        makeTcpConnection(lookupPort(port_name), nullptr, source_address);
    EXPECT_TRUE(tcp_client->write("ping", false, false));
    EXPECT_TRUE(tcp_client->waitForData(expected_response.length()));
    EXPECT_EQ(expected_response, tcp_client->data());
    if (!keep_open) {
      tcp_client->close();
    }

    return tcp_client;
  }

  FakeStreamPtr waitForNewEcdsStream() {
    if (ecds_connection_ == nullptr) {
      EXPECT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, ecds_connection_));
    }

    FakeStreamPtr ecds_stream;
    EXPECT_TRUE(ecds_connection_->waitForNewStream(*dispatcher_, ecds_stream));
    ecds_stream->startGrpcStream();
    return ecds_stream;
  }

  void expectFilterChainUpdateStats(const std::string& listener, int listener_updates,
                                    int total_updates, int total_rejected = 0) {
    test_server_->waitForCounterEq("listener_manager.listener_dynamic_filter_chains_update",
                                   total_updates);
    test_server_->waitForCounterEq("listener." + listener + ".fcds.update_success",
                                   listener_updates);
    test_server_->waitForCounterEq("listener." + listener + ".fcds.update_rejected",
                                   total_rejected);
  }

  void expectDrainingFilterChains(int count) {
    test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", count);
  }

  void expectWarmingListeners(int listener_count) {
    test_server_->waitForGaugeGe("listener_manager.total_listeners_active", listener_count);
  }

  void expectDrainingListeners(int listener_count) {
    test_server_->waitForGaugeEq("listener_manager.total_listeners_draining", listener_count);
  }

  void expectListenersUpdateStats(int total_updates, int active_listeners, int total_created) {
    test_server_->waitForCounterEq("listener_manager.lds.update_success", total_updates);
    test_server_->waitForGaugeEq("listener_manager.workers_started", 1);
    test_server_->waitForCounterEq("listener_manager.listener_create_success", total_created);
    test_server_->waitForGaugeEq("listener_manager.total_listeners_active", active_listeners);
    EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  }

  void expectExtensionReloadStats(const std::string& name, int updates) {
    test_server_->waitForCounterEq(
        "extension_config_discovery.network_filter." + name + ".config_reload", updates);
  }

  void expectListenersModified(int count) {
    test_server_->waitForCounterEq("listener_manager.listener_modified", count);
  }

  void expectInitializing() {
    EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  }

  AssertionResult expectLdsSubscription() {
    return compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {});
  }

  AssertionResult expectFcdsSubscription(const std::string& resource_names) {
    return compareDiscoveryRequest(Config::TypeUrl::get().FilterChain, "", {}, {resource_names},
                                   {});
  }

  AssertionResult expectExtensionSubscription(const std::string& resource_name,
                                              FakeStreamPtr& stream) {
    return compareSotwDiscoveryRequest(Config::TypeUrl::get().TypedExtension, "1", {resource_name},
                                       true, Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                       stream.get());
  }

  AssertionResult expectFcdsUnsubscribe(const std::string& resource_names) {
    return compareDiscoveryRequest(Config::TypeUrl::get().FilterChain, "", {}, {},
                                   {resource_names});
  }

  void expectFcdsFailure(const std::string& filter_name, bool removed = false) {
    const std::string error_message_substring =
        fmt::format("filter chain '{}' cannot be {} as it was not added by filter chain discovery",
                    filter_name, removed ? "removed" : "updated");
    ASSERT_TRUE(compareDeltaDiscoveryRequest(
        Config::TypeUrl::get().FilterChain, {}, {}, xds_stream_.get(),
        Grpc::Status::WellKnownGrpcStatus::Internal, error_message_substring));
  }

  void expectLdsAck() {
    envoy::service::discovery::v3::DeltaDiscoveryRequest request;
    EXPECT_TRUE(xds_stream_->waitForGrpcMessage(*dispatcher_, request));
    EXPECT_EQ(request.type_url(), Config::TypeUrl::get().Listener);
    EXPECT_FALSE(request.response_nonce().empty());
  }

  void expectFcdsAck() {
    envoy::service::discovery::v3::DeltaDiscoveryRequest request;
    EXPECT_TRUE(xds_stream_->waitForGrpcMessage(*dispatcher_, request));
    EXPECT_EQ(request.type_url(), Config::TypeUrl::get().FilterChain);
    EXPECT_FALSE(request.response_nonce().empty());
  }

  void sendLdsResponse(envoy::config::listener::v3::Listener listeners,
                       const std::string& version) {
    return sendLdsResponse(std::vector{listeners}, version);
  }

  void sendLdsResponse(std::vector<envoy::config::listener::v3::Listener> listeners,
                       const std::string& version) {
    return sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
        Config::TypeUrl::get().Listener, listeners, listeners, {}, version);
  }

  void sendFcdsResponse(const std::string& version,
                        envoy::config::listener::v3::FilterChain filter_chains,
                        std::vector<std::string> removed_filter_chains = {}) {
    return sendFcdsResponse(version, std::vector{filter_chains}, removed_filter_chains);
  }

  void sendFcdsResponse(const std::string& version,
                        std::vector<envoy::config::listener::v3::FilterChain> filter_chains,
                        std::vector<std::string> removed_filter_chains = {}) {
    return sendDiscoveryResponse<envoy::config::listener::v3::FilterChain>(
        Config::TypeUrl::get().FilterChain, filter_chains, filter_chains, removed_filter_chains,
        version);
  }

  void sendExtensionResponse(envoy::config::core::v3::TypedExtensionConfig extension_config,
                             FakeStreamPtr& stream, const std::string& version) {
    sendExtensionResponse(std::vector{extension_config}, stream, version);
  }

  void
  sendExtensionResponse(std::vector<envoy::config::core::v3::TypedExtensionConfig> extension_config,
                        FakeStreamPtr& stream, const std::string& version) {
    sendSotwDiscoveryResponse(Config::TypeUrl::get().TypedExtension, extension_config, version,
                              stream.get(), {});
  }

  std::vector<envoy::config::listener::v3::FilterChain> noFilterChains() { return {}; }

  void expectConfigDump(const std::string& listener_version,
                        const std::string& filter_chains_version,
                        const envoy::config::listener::v3::Listener& listeners,
                        bool warming = false) {
    expectConfigDump(
        {ExpectedListenerDump{listener_version, filter_chains_version, listeners, warming}});
  }

  void expectConfigDump(std::vector<ExpectedListenerDump> config) {
    expectListenerConfigDump(config);
  }

  void expectListenerConfigDump(std::vector<ExpectedListenerDump> config) {
    envoy::admin::v3::ListenersConfigDump expected_listeners;
    expected_listeners.set_version_info("system_version_info_this_is_a_test");

    auto actual_listeners = getListenersConfigDump();
    absl::flat_hash_map<std::string, envoy::admin::v3::ListenersConfigDump_DynamicListener*>
        listeners_map;
    for (const auto& [listener_version, filter_chains_Version, listener, warming] : config) {
      envoy::admin::v3::ListenersConfigDump_DynamicListener* listener_dump;
      if (listeners_map.contains(listener.name())) {
        listener_dump = listeners_map[listener.name()];
      } else {
        listener_dump = expected_listeners.add_dynamic_listeners();
        listeners_map[listener.name()] = listener_dump;
      }

      listener_dump->set_name(listener.name());
      envoy::admin::v3::ListenersConfigDump_DynamicListenerState* state;
      if (warming) {
        state = listener_dump->mutable_warming_state();
      } else {
        state = listener_dump->mutable_active_state();
      }

      state->set_version_info(listener_version);
      state->mutable_listener()->PackFrom(listener);
    }

    for (auto& listener : *actual_listeners.mutable_dynamic_listeners()) {
      if (listener.has_warming_state()) {
        listener.mutable_warming_state()->clear_last_updated();
      }
      if (listener.has_active_state()) {
        listener.mutable_active_state()->clear_last_updated();
      }
    }

    ASSERT_TRUE(TestUtility::protoEqual(expected_listeners, actual_listeners, true))
        << "Listeners config dump mismatch."
        << "\nExpected:\n"
        << expected_listeners.DebugString() << "\nActual:\n"
        << actual_listeners.DebugString();
  }

  void addEcdsCluster(const std::string& cluster_name) {
    config_helper_.addConfigModifier(
        [cluster_name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* ecds_cluster = bootstrap.mutable_static_resources()->add_clusters();
          ecds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
          ecds_cluster->set_name(cluster_name);
          ConfigHelper::setHttp2(*ecds_cluster);
        });
  }

  void initialize() override {
    addEcdsCluster(ecds_cluster_name_);
    addFakeUpstream(Http::CodecType::HTTP2);
    AdsIntegrationTest::initialize();
  }

  void resetStream(FakeStreamPtr& stream) {
    if (stream != nullptr) {
      stream->encodeResetStream();
    }
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

  ~FcdsIntegrationTestBase() override {
    resetStream(ecds_stream_);
    resetStream(ecds_stream_2_);
    fake_upstreams_[1]->cleanUp();
    resetConnection(ecds_connection_);
    resetConnection(xds_connection_);
  }

  const std::string ecds_cluster_name_ = "ecds_cluster";
  FakeStreamPtr ecds_stream_{nullptr};
  FakeStreamPtr ecds_stream_2_{nullptr};
  FakeHttpConnectionPtr ecds_connection_{nullptr};
  const std::string all_listeners_ = "all_listeners";
  const std::string l0_name_ = "listener_0";
  const std::string l1_name_ = "listener_1";
  const std::string l0_port_ = "listener_0_tcp";
  const std::string l1_port_ = "listener_1_tcp";
  const std::string fc0_name_ = "filter_chain_0";
  const std::string fc1_name_ = "filter_chain_1";
  const std::string fc2_name_ = "filter_chain_2";
  const std::string ip_2_ = "127.0.0.2";
  const std::string ip_3_ = "127.0.0.3";
  const std::string ip_4_ = "127.0.0.4";
  const std::string ip_5_ = "127.0.0.5";
  const std::string filter_name_ = "direct_response";
  const std::string listeners_v1_ = "listener_0_v1";
  const std::string listeners_v2_ = "listener_0_v2";
  const std::string filter_chains_v1_ = "filter_chains_v1";
  const std::string filter_chains_v2_ = "filter_chains_v2";
};

} // namespace
} // namespace Envoy
