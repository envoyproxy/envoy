#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/network/connection.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/config/api_version.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"
#include "test/test_common/logging.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/resources.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::StartsWith;

namespace Envoy {
namespace {

class ListenerIntegrationTestBase : public HttpIntegrationTest {
public:
  struct FakeUpstreamInfo {
    FakeHttpConnectionPtr connection_;
    FakeUpstream* upstream_{};
    absl::flat_hash_map<std::string, FakeStreamPtr> stream_by_resource_name_;
  };

  ListenerIntegrationTestBase(Network::Address::IpVersion version, const std::string& config)
      : HttpIntegrationTest(Http::CodecType::HTTP1, version, config) {
    // TODO(ggreenway): add tag extraction rules.
    // Missing stat tag-extraction rule for stat
    // 'listener_manager.lds.grpc.lds_cluster.streams_closed_1' and stat_prefix 'lds_cluster'.
    skip_tag_extraction_rule_check_ = true;
  }

  ~ListenerIntegrationTestBase() override { resetConnections(); }

  virtual void setGrpcServiceHelper(envoy::config::core::v3::GrpcService& grpc_service,
                                    const std::string& cluster_name,
                                    Network::Address::InstanceConstSharedPtr address) PURE;

  void setUpGrpcRds() {
    config_helper_.addConfigModifier(
        [this](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                http_connection_manager) {
          auto* rds_config = http_connection_manager.mutable_rds();
          rds_config->set_route_config_name(route_table_name_);
          rds_config->mutable_config_source()->set_resource_api_version(
              envoy::config::core::v3::ApiVersion::V3);
          envoy::config::core::v3::ApiConfigSource* rds_api_config_source =
              rds_config->mutable_config_source()->mutable_api_config_source();
          rds_api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
          rds_api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
          envoy::config::core::v3::GrpcService* grpc_service =
              rds_api_config_source->add_grpc_services();
          setGrpcServiceHelper(*grpc_service, "rds_cluster", getRdsFakeUpstream().localAddress());
        });
  }

  void setUpGrpcLds() {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      listener_config_.Swap(bootstrap.mutable_static_resources()->mutable_listeners(0));
      listener_config_.set_name(listener_name_);
      bootstrap.mutable_static_resources()->mutable_listeners()->Clear();
      auto* lds_config_source = bootstrap.mutable_dynamic_resources()->mutable_lds_config();
      lds_config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      auto* lds_api_config_source = lds_config_source->mutable_api_config_source();
      lds_api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
      lds_api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
      envoy::config::core::v3::GrpcService* grpc_service =
          lds_api_config_source->add_grpc_services();
      setGrpcServiceHelper(*grpc_service, "lds_cluster", getLdsFakeUpstream().localAddress());
    });
  }

  void initialize() override {
    // We want to use the GRPC based LDS.
    use_lds_ = false;
    setUpstreamCount(1);
    defer_listener_finalization_ = true;

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add the static cluster to serve LDS.
      auto* lds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      lds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      lds_cluster->set_name("lds_cluster");
      ConfigHelper::setHttp2(*lds_cluster);

      // Add the static cluster to serve RDS.
      auto* rds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      rds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      rds_cluster->set_name("rds_cluster");
      ConfigHelper::setHttp2(*rds_cluster);
    });

    setUpGrpcRds();
    // Note this has to be the last modifier as it nuke static_resource listeners.
    setUpGrpcLds();

    HttpIntegrationTest::initialize();
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Create the LDS upstream (fake_upstreams_[1]).
    addFakeUpstream(Http::CodecType::HTTP2);
    // Create the RDS upstream (fake_upstreams_[2]).
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void resetFakeUpstreamInfo(FakeUpstreamInfo* upstream_info) {
    ASSERT(upstream_info->upstream_ != nullptr);

    AssertionResult result = upstream_info->connection_->close();
    RELEASE_ASSERT(result, result.message());
    result = upstream_info->connection_->waitForDisconnect();
    RELEASE_ASSERT(result, result.message());
    upstream_info->connection_.reset();
  }

  void resetConnections() {
    if (rds_upstream_info_.upstream_ != nullptr) {
      resetFakeUpstreamInfo(&rds_upstream_info_);
    }
    resetFakeUpstreamInfo(&lds_upstream_info_);
  }

  FakeUpstream& getLdsFakeUpstream() const { return *fake_upstreams_[1]; }

  FakeUpstream& getRdsFakeUpstream() const { return *fake_upstreams_[2]; }

  void createStream(FakeUpstreamInfo* upstream_info, FakeUpstream& upstream,
                    const std::string& resource_name) {
    if (upstream_info->upstream_ == nullptr) {
      // bind upstream if not yet.
      upstream_info->upstream_ = &upstream;
      AssertionResult result =
          upstream_info->upstream_->waitForHttpConnection(*dispatcher_, upstream_info->connection_);
      RELEASE_ASSERT(result, result.message());
    }
    if (!upstream_info->stream_by_resource_name_.try_emplace(resource_name, nullptr).second) {
      RELEASE_ASSERT(false,
                     fmt::format("stream with resource name '{}' already exists!", resource_name));
    }
    auto result = upstream_info->connection_->waitForNewStream(
        *dispatcher_, upstream_info->stream_by_resource_name_[resource_name]);
    RELEASE_ASSERT(result, result.message());
    upstream_info->stream_by_resource_name_[resource_name]->startGrpcStream();
  }

  void createRdsStream(const std::string& resource_name) {
    createStream(&rds_upstream_info_, getRdsFakeUpstream(), resource_name);
  }

  void createLdsStream() {
    createStream(&lds_upstream_info_, getLdsFakeUpstream(), listener_name_);
  }

  void sendLdsResponse(const std::vector<envoy::config::listener::v3::Listener>& listener_configs,
                       const std::string& version) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url(Config::TypeUrl::get().Listener);
    for (const auto& listener_config : listener_configs) {
      response.add_resources()->PackFrom(listener_config);
    }
    ASSERT(lds_upstream_info_.stream_by_resource_name_[listener_name_] != nullptr);
    lds_upstream_info_.stream_by_resource_name_[listener_name_]->sendGrpcMessage(response);
  }

  void sendLdsResponse(const std::vector<std::string>& listener_configs,
                       const std::string& version) {
    std::vector<envoy::config::listener::v3::Listener> proto_configs;
    proto_configs.reserve(listener_configs.size());
    for (const auto& listener_blob : listener_configs) {
      proto_configs.emplace_back(
          TestUtility::parseYaml<envoy::config::listener::v3::Listener>(listener_blob));
    }
    sendLdsResponse(proto_configs, version);
  }

  void sendRdsResponse(const std::string& route_config, const std::string& version) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url(Config::TypeUrl::get().RouteConfiguration);
    const auto route_configuration =
        TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(route_config);
    response.add_resources()->PackFrom(route_configuration);
    ASSERT(rds_upstream_info_.stream_by_resource_name_[route_configuration.name()] != nullptr);
    rds_upstream_info_.stream_by_resource_name_[route_configuration.name()]->sendGrpcMessage(
        response);
  }
  envoy::config::listener::v3::Listener listener_config_;
  std::string listener_name_{"testing-listener-0"};
  std::string route_table_name_{"testing-route-table-0"};
  FakeUpstreamInfo lds_upstream_info_;
  FakeUpstreamInfo rds_upstream_info_;
};

class ListenerIntegrationTest : public ListenerIntegrationTestBase,
                                public Grpc::GrpcClientIntegrationParamTest {
public:
  ListenerIntegrationTest()
      : ListenerIntegrationTestBase(ipVersion(),
                                    ConfigHelper::httpProxyConfig(/*downstream_use_quic=*/false,
                                                                  /*multiple_addresses=*/false)) {}

  void setGrpcServiceHelper(envoy::config::core::v3::GrpcService& grpc_service,
                            const std::string& cluster_name,
                            Network::Address::InstanceConstSharedPtr address) override {
    setGrpcService(grpc_service, cluster_name, address);
  }
};

class ListenerMultiAddressesIntegrationTest : public ListenerIntegrationTestBase,
                                              public Grpc::GrpcClientIntegrationParamTest {
public:
  ListenerMultiAddressesIntegrationTest()
      : ListenerIntegrationTestBase(ipVersion(),
                                    ConfigHelper::httpProxyConfig(/*downstream_use_quic=*/false,
                                                                  /*multiple_addresses=*/true)) {}

  void setGrpcServiceHelper(envoy::config::core::v3::GrpcService& grpc_service,
                            const std::string& cluster_name,
                            Network::Address::InstanceConstSharedPtr address) override {
    setGrpcService(grpc_service, cluster_name, address);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsAndGrpcTypes, ListenerIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

INSTANTIATE_TEST_SUITE_P(IpVersionsAndGrpcTypes, ListenerMultiAddressesIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// Tests that an update with an unknown filter config proto is rejected.
TEST_P(ListenerIntegrationTest, CleanlyRejectsUnknownFilterConfigProto) {
  on_server_init_function_ = [&]() {
    createLdsStream();
    envoy::config::listener::v3::Listener listener =
        TestUtility::parseYaml<envoy::config::listener::v3::Listener>(R"EOF(
    name: fake_listener
    address:
      socket_address:
        address: "::"
        port_value: 4242
    filter_chains:
      - filters:
        - name: "filter_name"
        )EOF");
    auto* typed_config =
        listener.mutable_filter_chains(0)->mutable_filters(0)->mutable_typed_config();
    typed_config->set_type_url("type.googleapis.com/unknown.type.url");
    typed_config->set_value("non-empty config contents");
    sendLdsResponse({listener}, "1");
  };
  initialize();
  registerTestServerPorts({listener_name_});
  test_server_->waitForCounterGe("listener_manager.lds.update_rejected", 1);
}

TEST_P(ListenerIntegrationTest, RejectsUnsupportedTypedPerFilterConfig) {
  on_server_init_function_ = [&]() {
    createLdsStream();
    envoy::config::listener::v3::Listener listener =
        TestUtility::parseYaml<envoy::config::listener::v3::Listener>(R"EOF(
      name: fake_listener
      address:
        socket_address:
          address: 127.0.0.1
          port_value: 0
      filter_chains:
        - filters:
          - name: http
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
              codec_type: HTTP2
              stat_prefix: config_test
              route_config:
                name: route_config_0
                virtual_hosts:
                  - name: integration
                    domains:
                      - "*"
                    routes:
                      - match:
                          prefix: /
                        route:
                          cluster: cluster_0
                    typed_per_filter_config:
                      set-response-code:
                        "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
                        code: 403
              http_filters:
                - name: set-response-code
                  typed_config:
                    "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
                    code: 402
          - name: envoy.filters.http.router
        )EOF");
    sendLdsResponse({listener}, "2");
  };
  initialize();
  registerTestServerPorts({listener_name_});
  test_server_->waitForCounterGe("listener_manager.lds.update_rejected", 1);
}

TEST_P(ListenerIntegrationTest, RejectsUnknownHttpFilter) {
  on_server_init_function_ = [&]() {
    createLdsStream();
    envoy::config::listener::v3::Listener listener =
        TestUtility::parseYaml<envoy::config::listener::v3::Listener>(R"EOF(
      name: fake_listener
      address:
        socket_address:
          address: 127.0.0.1
          port_value: 0
      filter_chains:
        - filters:
          - name: http
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
              codec_type: HTTP2
              stat_prefix: config_test
              route_config:
                name: route_config_0
                virtual_hosts:
                  - name: integration
                    domains:
                      - "*"
                    routes:
                      - match:
                          prefix: /
                        route:
                          cluster: cluster_0
              http_filters:
                - name: filter.unknown
                - name: envoy.filters.http.router
        )EOF");
    sendLdsResponse({listener}, "2");
  };
  initialize();
  registerTestServerPorts({listener_name_});
  test_server_->waitForCounterGe("listener_manager.lds.update_rejected", 1);
}

TEST_P(ListenerIntegrationTest, IgnoreUnknownOptionalHttpFilter) {
  on_server_init_function_ = [&]() {
    createLdsStream();
    envoy::config::listener::v3::Listener listener =
        TestUtility::parseYaml<envoy::config::listener::v3::Listener>(R"EOF(
      name: fake_listener
      address:
        socket_address:
          address: "::"
          port_value: 0
      filter_chains:
        - filters:
          - name: http
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
              codec_type: HTTP2
              stat_prefix: config_test
              route_config:
                name: route_config_0
                virtual_hosts:
                  - name: integration
                    domains:
                      - "*"
                    routes:
                      - match:
                          prefix: /
                        route:
                          cluster: cluster_0
              http_filters:
                - name: filter.unknown
                  is_optional: true
                - name: envoy.filters.http.router
        )EOF");
    sendLdsResponse({listener}, "2");
  };
  initialize();
  registerTestServerPorts({listener_name_});
  test_server_->waitForCounterGe("listener_manager.lds.update_rejected", 0);
}

// Tests that a LDS deletion before Server initManager been initialized will not block the Server
// from starting.
TEST_P(ListenerIntegrationTest, RemoveLastUninitializedListener) {
  on_server_init_function_ = [&]() {
    createLdsStream();
    sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "1");
    createRdsStream(route_table_name_);
  };
  initialize();
  registerTestServerPorts({listener_name_});
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  // testing-listener-0 is not initialized as we haven't push any RDS yet.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  // Workers not started, the LDS added listener 0 is in active_listeners_ list.
  EXPECT_EQ(test_server_->server().listenerManager().listeners().size(), 1);

  // This actually deletes the only listener.
  sendLdsResponse(std::vector<std::string>{}, "2");
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 2);
  EXPECT_EQ(test_server_->server().listenerManager().listeners().size(), 0);
  // Server instance is ready now because the listener's destruction marked the listener
  // initialized.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
}

TEST_P(ListenerMultiAddressesIntegrationTest, BasicSuccessWithMultiAddresses) {
  on_server_init_function_ = [&]() {
    createLdsStream();
    sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "1");
    createRdsStream(route_table_name_);
  };
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  // testing-listener-0 is not initialized as we haven't pushed any RDS yet.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  // Workers not started, the LDS added listener 0 is in active_listeners_ list.
  EXPECT_EQ(test_server_->server().listenerManager().listeners().size(), 1);
  registerTestServerPorts({"address1", "address2"});

  const std::string route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
)EOF";
  sendRdsResponse(fmt::format(route_config_tmpl, route_table_name_, "cluster_0"), "1");
  test_server_->waitForCounterGe(
      fmt::format("http.config_test.rds.{}.update_success", route_table_name_), 1);
  // Now testing-listener-0 finishes initialization, Server initManager will be ready.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

  test_server_->waitUntilListenersReady();
  // NOTE: The line above doesn't tell you if listener is up and listening.
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  // Request is sent to cluster_0.

  int response_size = 800;
  int request_size = 10;
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"server_id", "cluster_0, backend_0"}};

  codec_client_ = makeHttpConnection(lookupPort("address1"));
  auto response = sendRequestAndWaitForResponse(
      Http::TestResponseHeaderMapImpl{
          {":method", "GET"}, {":path", "/"}, {":authority", "host"}, {":scheme", "http"}},
      request_size, response_headers, response_size, /*cluster_0*/ 0);
  verifyResponse(std::move(response), "200", response_headers, std::string(response_size, 'a'));
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_size, upstream_request_->bodyLength());
  codec_client_->close();
  // Wait for the client to be disconnected.
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  codec_client_ = makeHttpConnection(lookupPort("address2"));
  auto response2 = sendRequestAndWaitForResponse(
      Http::TestResponseHeaderMapImpl{
          {":method", "GET"}, {":path", "/"}, {":authority", "host"}, {":scheme", "http"}},
      request_size, response_headers, response_size, /*cluster_0*/ 0);
  verifyResponse(std::move(response2), "200", response_headers, std::string(response_size, 'a'));
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_size, upstream_request_->bodyLength());
}

// Tests that a LDS adding listener works as expected.
TEST_P(ListenerIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [&]() {
    createLdsStream();
    sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "1");
    createRdsStream(route_table_name_);
  };
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  // testing-listener-0 is not initialized as we haven't pushed any RDS yet.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  // Workers not started, the LDS added listener 0 is in active_listeners_ list.
  EXPECT_EQ(test_server_->server().listenerManager().listeners().size(), 1);
  registerTestServerPorts({listener_name_});

  const std::string route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
)EOF";
  sendRdsResponse(fmt::format(route_config_tmpl, route_table_name_, "cluster_0"), "1");
  test_server_->waitForCounterGe(
      fmt::format("http.config_test.rds.{}.update_success", route_table_name_), 1);
  // Now testing-listener-0 finishes initialization, Server initManager will be ready.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

  test_server_->waitUntilListenersReady();
  // NOTE: The line above doesn't tell you if listener is up and listening.
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  // Request is sent to cluster_0.

  codec_client_ = makeHttpConnection(lookupPort(listener_name_));
  int response_size = 800;
  int request_size = 10;
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"server_id", "cluster_0, backend_0"}};
  auto response = sendRequestAndWaitForResponse(
      Http::TestResponseHeaderMapImpl{
          {":method", "GET"}, {":path", "/"}, {":authority", "host"}, {":scheme", "http"}},
      request_size, response_headers, response_size, /*cluster_0*/ 0);
  verifyResponse(std::move(response), "200", response_headers, std::string(response_size, 'a'));
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_size, upstream_request_->bodyLength());
}

// This test updates the same listener multiple times to catch problems that
// may exist in the shared ListenSocketFactory. e.g. ListenSocketFactory holding
// stale reference to members of the first listener.
TEST_P(ListenerIntegrationTest, MultipleLdsUpdatesSharingListenSocketFactory) {
  on_server_init_function_ = [&]() {
    createLdsStream();
    sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "1");
    createRdsStream(route_table_name_);
  };
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  // testing-listener-0 is not initialized as we haven't pushed any RDS yet.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  // Workers not started, the LDS added listener 0 is in active_listeners_ list.
  EXPECT_EQ(test_server_->server().listenerManager().listeners().size(), 1);
  registerTestServerPorts({listener_name_});

  const std::string route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
)EOF";
  sendRdsResponse(fmt::format(route_config_tmpl, route_table_name_, "cluster_0"), "1");
  test_server_->waitForCounterGe(
      fmt::format("http.config_test.rds.{}.update_success", route_table_name_), 1);
  // Now testing-listener-0 finishes initialization, Server initManager will be ready.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

  test_server_->waitUntilListenersReady();
  // NOTE: The line above doesn't tell you if listener is up and listening.
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  // Make a connection to the listener from version 1.
  codec_client_ = makeHttpConnection(lookupPort(listener_name_));

  for (int version = 2; version <= 10; version++) {
    // Touch the metadata to get a different hash.
    (*(*listener_config_.mutable_metadata()->mutable_filter_metadata())["random_filter_name"]
          .mutable_fields())["random_key"]
        .set_number_value(version);
    sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)},
                    absl::StrCat(version));
    sendRdsResponse(fmt::format(route_config_tmpl, route_table_name_, "cluster_0"),
                    absl::StrCat(version));

    test_server_->waitForCounterGe("listener_manager.listener_create_success", version);

    // Wait for the client to be disconnected.
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    // Make a new connection to the new listener.
    codec_client_ = makeHttpConnection(lookupPort(listener_name_));
    const uint32_t response_size = 800;
    const uint32_t request_size = 10;
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"server_id", "cluster_0, backend_0"}};
    auto response = sendRequestAndWaitForResponse(
        Http::TestResponseHeaderMapImpl{
            {":method", "GET"}, {":path", "/"}, {":authority", "host"}, {":scheme", "http"}},
        request_size, response_headers, response_size, /*cluster_0*/ 0);
    verifyResponse(std::move(response), "200", response_headers, std::string(response_size, 'a'));
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(request_size, upstream_request_->bodyLength());
  }
}

// This is multiple addresses version test for the above one.
TEST_P(ListenerMultiAddressesIntegrationTest,
       MultipleLdsUpdatesSharingListenSocketFactoryWithMultiAddresses) {
  on_server_init_function_ = [&]() {
    createLdsStream();
    sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "1");
    createRdsStream(route_table_name_);
  };
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  // testing-listener-0 is not initialized as we haven't pushed any RDS yet.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  // Workers not started, the LDS added listener 0 is in active_listeners_ list.
  EXPECT_EQ(test_server_->server().listenerManager().listeners().size(), 1);
  registerTestServerPorts({"address1", "address2"});

  const std::string route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
)EOF";
  sendRdsResponse(fmt::format(route_config_tmpl, route_table_name_, "cluster_0"), "1");
  test_server_->waitForCounterGe(
      fmt::format("http.config_test.rds.{}.update_success", route_table_name_), 1);
  // Now testing-listener-0 finishes initialization, Server initManager will be ready.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

  test_server_->waitUntilListenersReady();
  // NOTE: The line above doesn't tell you if listener is up and listening.
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  // Make a connection to the listener from version 1.
  codec_client_ = makeHttpConnection(lookupPort("address1"));

  for (int version = 2; version <= 10; version++) {
    // Touch the metadata to get a different hash.
    (*(*listener_config_.mutable_metadata()->mutable_filter_metadata())["random_filter_name"]
          .mutable_fields())["random_key"]
        .set_number_value(version);
    sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)},
                    absl::StrCat(version));
    sendRdsResponse(fmt::format(route_config_tmpl, route_table_name_, "cluster_0"),
                    absl::StrCat(version));

    test_server_->waitForCounterGe("listener_manager.listener_create_success", version);

    // Wait for the client to be disconnected.
    ASSERT_TRUE(codec_client_->waitForDisconnect());

    const uint32_t response_size = 800;
    const uint32_t request_size = 10;
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"server_id", "cluster_0, backend_0"}};

    // Make a new connection to the new listener's first address.
    codec_client_ = makeHttpConnection(lookupPort("address1"));
    auto response = sendRequestAndWaitForResponse(
        Http::TestResponseHeaderMapImpl{
            {":method", "GET"}, {":path", "/"}, {":authority", "host"}, {":scheme", "http"}},
        request_size, response_headers, response_size, /*cluster_0*/ 0);
    verifyResponse(std::move(response), "200", response_headers, std::string(response_size, 'a'));
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(request_size, upstream_request_->bodyLength());
    codec_client_->close();
    // Wait for the client to be disconnected.
    ASSERT_TRUE(codec_client_->waitForDisconnect());

    // Make a new connection to the new listener's second address.
    codec_client_ = makeHttpConnection(lookupPort("address2"));
    auto response2 = sendRequestAndWaitForResponse(
        Http::TestResponseHeaderMapImpl{
            {":method", "GET"}, {":path", "/"}, {":authority", "host"}, {":scheme", "http"}},
        request_size, response_headers, response_size, /*cluster_0*/ 0);
    verifyResponse(std::move(response2), "200", response_headers, std::string(response_size, 'a'));
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(request_size, upstream_request_->bodyLength());
  }
}

TEST_P(ListenerMultiAddressesIntegrationTest, MultipleAddressesListenerInPlaceUpdate) {
  on_server_init_function_ = [&]() {
    createLdsStream();
    sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "1");
    createRdsStream(route_table_name_);
  };
  setDrainTime(std::chrono::seconds(30));
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  // testing-listener-0 is not initialized as we haven't pushed any RDS yet.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  // Workers not started, the LDS added listener 0 is in active_listeners_ list.
  EXPECT_EQ(test_server_->server().listenerManager().listeners().size(), 1);
  registerTestServerPorts({"address1", "address2"});

  const std::string route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
)EOF";
  sendRdsResponse(fmt::format(route_config_tmpl, route_table_name_, "cluster_0"), "1");
  test_server_->waitForCounterGe(
      fmt::format("http.config_test.rds.{}.update_success", route_table_name_), 1);
  // Now testing-listener-0 finishes initialization, Server initManager will be ready.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

  test_server_->waitUntilListenersReady();
  // NOTE: The line above doesn't tell you if listener is up and listening.
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);

  // Trigger a listener in-place updating.
  listener_config_.mutable_filter_chains(0)->mutable_filters(0)->set_name("http_filter");
  sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "2");
  sendRdsResponse(fmt::format(route_config_tmpl, route_table_name_, "cluster_0"), "2");

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);
  test_server_->waitForCounterEq("listener_manager.listener_in_place_updated", 1);
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 1);

  const uint32_t response_size = 800;
  const uint32_t request_size = 10;
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"server_id", "cluster_0, backend_0"}};

  // Make a new connection to the new listener's first address.
  codec_client_ = makeHttpConnection(lookupPort("address1"));

  auto response1 = sendRequestAndWaitForResponse(
      Http::TestResponseHeaderMapImpl{
          {":method", "GET"}, {":path", "/"}, {":authority", "host"}, {":scheme", "http"}},
      request_size, response_headers, response_size, /*cluster_0*/ 0);
  verifyResponse(std::move(response1), "200", response_headers, std::string(response_size, 'a'));
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_size, upstream_request_->bodyLength());
  codec_client_->close();
  // Wait for the client to be disconnected.
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Make a new connection to the new listener's second address.
  codec_client_ = makeHttpConnection(lookupPort("address2"));

  auto response2 = sendRequestAndWaitForResponse(
      Http::TestResponseHeaderMapImpl{
          {":method", "GET"}, {":path", "/"}, {":authority", "host"}, {":scheme", "http"}},
      request_size, response_headers, response_size, /*cluster_0*/ 0);
  verifyResponse(std::move(response2), "200", response_headers, std::string(response_size, 'a'));
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_size, upstream_request_->bodyLength());
}

// Create a listener, then do an in-place update for the listener.
// Remove the listener before the filter chain draining is done,
// then expect the connection will be reset.
TEST_P(ListenerIntegrationTest, RemoveListenerAfterInPlaceUpdate) {
  on_server_init_function_ = [&]() {
    createLdsStream();
    sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "1");
    createRdsStream(route_table_name_);
  };
  setDrainTime(std::chrono::seconds(30));
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  // testing-listener-0 is not initialized as we haven't pushed any RDS yet.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  // Workers not started, the LDS added listener 0 is in active_listeners_ list.
  EXPECT_EQ(test_server_->server().listenerManager().listeners().size(), 1);
  registerTestServerPorts({listener_name_});

  const std::string route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
)EOF";
  sendRdsResponse(fmt::format(route_config_tmpl, route_table_name_, "cluster_0"), "1");
  test_server_->waitForCounterGe(
      fmt::format("http.config_test.rds.{}.update_success", route_table_name_), 1);
  // Now testing-listener-0 finishes initialization, Server initManager will be ready.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

  test_server_->waitUntilListenersReady();
  // NOTE: The line above doesn't tell you if listener is up and listening.
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);

  // Trigger a listener in-place updating.
  listener_config_.mutable_filter_chains(0)->mutable_filters(0)->set_name("http_filter");
  sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "2");
  sendRdsResponse(fmt::format(route_config_tmpl, route_table_name_, "cluster_0"), "2");

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);
  test_server_->waitForCounterEq("listener_manager.listener_in_place_updated", 1);
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 1);

  // Make a new connection to the new listener.
  codec_client_ = makeHttpConnection(lookupPort(listener_name_));
  const uint32_t response_size = 800;
  const uint32_t request_size = 10;
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"server_id", "cluster_0, backend_0"}};
  auto response = sendRequestAndWaitForResponse(
      Http::TestResponseHeaderMapImpl{
          {":method", "GET"}, {":path", "/"}, {":authority", "host"}, {":scheme", "http"}},
      request_size, response_headers, response_size, /*cluster_0*/ 0);
  verifyResponse(std::move(response), "200", response_headers, std::string(response_size, 'a'));
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_size, upstream_request_->bodyLength());
  codec_client_->close();

  // Remove the active listener.
  sendLdsResponse(std::vector<std::string>{}, "3");
  test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 0);
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 1);

  // All the listen socket are closed. include the sockets in the active listener and
  // the sockets in the filter chain draining listener. The new connection should be reset.
  auto codec1 =
      makeRawHttpConnection(makeClientConnection(lookupPort(listener_name_)), absl::nullopt);
  // The socket are closed asynchronously, so waiting the connection closed here.
  ASSERT_TRUE(codec1->waitForDisconnect());

  // Test the connection again to ensure the socket is closed.
  auto codec2 =
      makeRawHttpConnection(makeClientConnection(lookupPort(listener_name_)), absl::nullopt);
  EXPECT_FALSE(codec2->connected());
  EXPECT_THAT(codec2->connection()->transportFailureReason(), StartsWith("delayed connect error"));

  // Ensure the old listener is still in filter chain draining.
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 1);
}

// Create a listener, then do two in-place updates for the listener.
// Remove the listener before the filter chain draining is done,
// then expect the connection will be reset.
TEST_P(ListenerIntegrationTest, RemoveListenerAfterMultipleInPlaceUpdate) {
  on_server_init_function_ = [&]() {
    createLdsStream();
    sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "1");
    createRdsStream(route_table_name_);
  };
  setDrainTime(std::chrono::seconds(30));
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  // testing-listener-0 is not initialized as we haven't pushed any RDS yet.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  // Workers not started, the LDS added listener 0 is in active_listeners_ list.
  EXPECT_EQ(test_server_->server().listenerManager().listeners().size(), 1);
  registerTestServerPorts({listener_name_});

  const std::string route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
)EOF";
  sendRdsResponse(fmt::format(route_config_tmpl, route_table_name_, "cluster_0"), "1");
  test_server_->waitForCounterGe(
      fmt::format("http.config_test.rds.{}.update_success", route_table_name_), 1);
  // Now testing-listener-0 finishes initialization, Server initManager will be ready.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

  test_server_->waitUntilListenersReady();
  // NOTE: The line above doesn't tell you if listener is up and listening.
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);

  // Trigger a listener in-place updating.
  listener_config_.mutable_filter_chains(0)->mutable_filters(0)->set_name("http_filter");
  sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "2");
  sendRdsResponse(fmt::format(route_config_tmpl, route_table_name_, "cluster_0"), "2");

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);
  test_server_->waitForCounterEq("listener_manager.listener_in_place_updated", 1);
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 1);

  // Trigger second listener in-place updating.
  listener_config_.mutable_filter_chains(0)->mutable_filters(0)->set_name("http_filter2");
  sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "2");
  sendRdsResponse(fmt::format(route_config_tmpl, route_table_name_, "cluster_0"), "2");

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 3);
  test_server_->waitForCounterEq("listener_manager.listener_in_place_updated", 2);
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 2);

  // Make a new connection to the new listener.
  codec_client_ = makeHttpConnection(lookupPort(listener_name_));
  const uint32_t response_size = 800;
  const uint32_t request_size = 10;
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"server_id", "cluster_0, backend_0"}};
  auto response = sendRequestAndWaitForResponse(
      Http::TestResponseHeaderMapImpl{
          {":method", "GET"}, {":path", "/"}, {":authority", "host"}, {":scheme", "http"}},
      request_size, response_headers, response_size, /*cluster_0*/ 0);
  verifyResponse(std::move(response), "200", response_headers, std::string(response_size, 'a'));
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_size, upstream_request_->bodyLength());
  codec_client_->close();

  // Remove the active listener.
  sendLdsResponse(std::vector<std::string>{}, "3");
  test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 0);
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 2);

  // All the listen socket are closed. include the sockets in the active listener and
  // the sockets in the filter chain draining listener. The new connection should be reset.
  auto codec1 =
      makeRawHttpConnection(makeClientConnection(lookupPort(listener_name_)), absl::nullopt);
  // The socket are closed asynchronously, so waiting the connection closed here.
  ASSERT_TRUE(codec1->waitForDisconnect());

  // Test the connection again to ensure the socket is closed.
  auto codec2 =
      makeRawHttpConnection(makeClientConnection(lookupPort(listener_name_)), absl::nullopt);
  EXPECT_FALSE(codec2->connected());
  EXPECT_THAT(codec2->connection()->transportFailureReason(), StartsWith("delayed connect error"));

  // Ensure the old listener is still in filter chain draining.
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 2);
}

TEST_P(ListenerIntegrationTest, ChangeListenerAddress) {
  on_server_init_function_ = [&]() {
    createLdsStream();
    sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "1");
    createRdsStream(route_table_name_);
  };
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  // testing-listener-0 is not initialized as we haven't pushed any RDS yet.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  // Workers not started, the LDS added listener 0 is in active_listeners_ list.
  EXPECT_EQ(test_server_->server().listenerManager().listeners().size(), 1);
  registerTestServerPorts({listener_name_});

  const std::string route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
)EOF";
  sendRdsResponse(fmt::format(route_config_tmpl, route_table_name_, "cluster_0"), "1");
  test_server_->waitForCounterGe(
      fmt::format("http.config_test.rds.{}.update_success", route_table_name_), 1);
  // Now testing-listener-0 finishes initialization, Server initManager will be ready.
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

  test_server_->waitUntilListenersReady();
  // NOTE: The line above doesn't tell you if listener is up and listening.
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  const uint32_t old_port = lookupPort(listener_name_);
  // Make a connection to the listener from version 1.
  codec_client_ = makeHttpConnection(old_port);

  // Change the listener address from loopback to wildcard.
  const std::string new_address = Network::Test::getAnyAddressString(ipVersion());
  listener_config_.mutable_address()->mutable_socket_address()->set_address(new_address);
  sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "2");
  sendRdsResponse(fmt::format(route_config_tmpl, route_table_name_, "cluster_0"), "2");

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);
  registerTestServerPorts({listener_name_});
  // Verify that the listener was updated and that the next connection will be to the new listener.
  // (Note that connecting to 127.0.0.1 works whether the listener address is 127.0.0.1 or 0.0.0.0.)
  const uint32_t new_port = lookupPort(listener_name_);
  EXPECT_NE(old_port, new_port);

  // Wait for the client to be disconnected.
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  // Make a new connection to the new listener.
  codec_client_ = makeHttpConnection(new_port);
  const uint32_t response_size = 800;
  const uint32_t request_size = 10;
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"server_id", "cluster_0, backend_0"}};
  auto response = sendRequestAndWaitForResponse(
      Http::TestResponseHeaderMapImpl{
          {":method", "GET"}, {":path", "/"}, {":authority", "host"}, {":scheme", "http"}},
      request_size, response_headers, response_size, /*cluster_0*/ 0);
  verifyResponse(std::move(response), "200", response_headers, std::string(response_size, 'a'));
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_size, upstream_request_->bodyLength());
}

struct PerConnection {
  std::string response_;
  std::unique_ptr<RawConnectionDriver> client_conn_;
  FakeRawConnectionPtr upstream_conn_;
};

class ListenerFilterIntegrationTest : public BaseIntegrationTest,
                                      public Grpc::GrpcClientIntegrationParamTest {
public:
  ListenerFilterIntegrationTest()
      : BaseIntegrationTest(ipVersion(), ConfigHelper::baseConfig() + R"EOF(
    filter_chains:
    - filters:
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp_stats
          cluster: cluster_0
)EOF") {
    // TODO(ggreenway): add tag extraction rules.
    // Missing stat tag-extraction rule for stat
    // 'listener_manager.lds.grpc.lds_cluster.streams_closed_1' and stat_prefix 'lds_cluster'.
    skip_tag_extraction_rule_check_ = true;
  }

  ~ListenerFilterIntegrationTest() override {
    if (lds_connection_ != nullptr) {
      AssertionResult result = lds_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = lds_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      lds_connection_.reset();
    }
  }

  void createLdsStream() {
    AssertionResult result =
        fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, lds_connection_);
    EXPECT_TRUE(result);

    auto result2 = lds_connection_->waitForNewStream(*dispatcher_, lds_stream_);
    EXPECT_TRUE(result2);
    lds_stream_->startGrpcStream();
  }

  void sendLdsResponse(const std::vector<envoy::config::listener::v3::Listener>& listener_configs,
                       const std::string& version) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url(Config::TypeUrl::get().Listener);
    for (const auto& listener_config : listener_configs) {
      response.add_resources()->PackFrom(listener_config);
    }
    ASSERT_NE(nullptr, lds_stream_);
    lds_stream_->sendGrpcMessage(response);
  }

  void sendLdsResponse(const std::vector<std::string>& listener_configs,
                       const std::string& version) {
    std::vector<envoy::config::listener::v3::Listener> proto_configs;
    proto_configs.reserve(listener_configs.size());
    for (const auto& listener_blob : listener_configs) {
      proto_configs.emplace_back(
          TestUtility::parseYaml<envoy::config::listener::v3::Listener>(listener_blob));
    }
    sendLdsResponse(proto_configs, version);
  }

  void createUpstreams() override {
    BaseIntegrationTest::createUpstreams();
    if (!use_lds_) {
      // Create the LDS upstream (fake_upstreams_[1]).
      addFakeUpstream(Http::CodecType::HTTP2);
    }
  }

  envoy::config::listener::v3::Listener listener_config_;
  FakeHttpConnectionPtr lds_connection_;
  FakeStreamPtr lds_stream_{};
};

TEST_P(ListenerFilterIntegrationTest, InspectDataFilterDrainData) {
  config_helper_.addListenerFilter(R"EOF(
      name: inspect_data1
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.InspectDataListenerFilterConfig
        max_read_bytes: 2
        close_connection: false
        drain: true
        )EOF");
  config_helper_.addListenerFilter(R"EOF(
      name: inspect_data2
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.InspectDataListenerFilterConfig
        max_read_bytes: 5
        close_connection: false
        )EOF");
  std::string data = "hello";
  std::string data_after_drain = data.substr(2, std::string::npos);
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(data));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(data.size() - 2, &data_after_drain));
  tcp_client->close();
}

TEST_P(ListenerFilterIntegrationTest, InspectDataFilterChangeMaxReadBytes) {
  config_helper_.addListenerFilter(R"EOF(
      name: inspect_data1
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.InspectDataListenerFilterConfig
        max_read_bytes: 2
        close_connection: false
        new_max_read_bytes: 10
        )EOF");
  config_helper_.addListenerFilter(R"EOF(
      name: inspect_data2
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.InspectDataListenerFilterConfig
        max_read_bytes: 5
        close_connection: false
        )EOF");
  std::string data = "hello";
  std::string data2 = "world";
  std::string expected_data = data + data2;
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(data));
  ASSERT_TRUE(tcp_client->write(data2));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(expected_data.size(), &expected_data));
  tcp_client->close();
}

TEST_P(ListenerFilterIntegrationTest, MultipleInspectDataFilters) {
  config_helper_.addListenerFilter(R"EOF(
      name: inspect_data1
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.InspectDataListenerFilterConfig
        max_read_bytes: 2
        close_connection: false
        )EOF");
  config_helper_.addListenerFilter(R"EOF(
      name: inspect_data2
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.InspectDataListenerFilterConfig
        max_read_bytes: 5
        close_connection: false
        )EOF");
  std::string data = "hello";
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(data));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(data.size(), &data));
  tcp_client->close();
}

TEST_P(ListenerFilterIntegrationTest, MultipleInspectDataFilters2) {
  config_helper_.addListenerFilter(R"EOF(
      name: inspect_data1
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.InspectDataListenerFilterConfig
        max_read_bytes: 5
        close_connection: false
        )EOF");
  config_helper_.addListenerFilter(R"EOF(
      name: inspect_data2
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.InspectDataListenerFilterConfig
        max_read_bytes: 2
        close_connection: false
        )EOF");
  std::string data = "hello";
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(data));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(data.size(), &data));
  tcp_client->close();
}

TEST_P(ListenerFilterIntegrationTest, ListenerFiltersCloseConnectionOnAccept) {
  config_helper_.addListenerFilter(R"EOF(
      name: inspect_data1
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.InspectDataListenerFilterConfig
        max_read_bytes: 0
        close_connection: true
        )EOF");
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->waitForDisconnect();
}

TEST_P(ListenerFilterIntegrationTest, InspectDataFiltersCloseConnectionAfterGetData) {
  config_helper_.addListenerFilter(R"EOF(
      name: inspect_data1
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.InspectDataListenerFilterConfig
        max_read_bytes: 5
        close_connection: true
        )EOF");
  std::string data = "hello";
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  auto result = tcp_client->write(data);
  // The connection could be closed when writing or after write.
  if (result == true) {
    tcp_client->waitForDisconnect();
  }
}

TEST_P(ListenerFilterIntegrationTest, MixNoInspectDataFilterAndInspectDataFilters) {
  config_helper_.addListenerFilter(R"EOF(
      name: inspect_data1
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.InspectDataListenerFilterConfig
        max_read_bytes: 0
        close_connection: false
        )EOF");
  config_helper_.addListenerFilter(R"EOF(
      name: inspect_data2
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.InspectDataListenerFilterConfig
        max_read_bytes: 5
        close_connection: false
        )EOF");
  std::string data = "hello";
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(data));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(data.size(), &data));
  tcp_client->close();
}

TEST_P(ListenerFilterIntegrationTest, InspectDataFiltersClientCloseConnectionWithFewData) {
// This is required `EV_FEATURE_EARLY_CLOSE` feature for libevent, and this feature is
// only supported with `epoll`. But `MacOS` uses the `kqueue`.
// https://libevent.org/doc/event_8h.html#a98f643f9c9063a4cbf410f519eb61e55
#if !defined(__APPLE__)
  config_helper_.addListenerFilter(R"EOF(
      name: inspect_data1
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.InspectDataListenerFilterConfig
        max_read_bytes: 10
        close_connection: false
        )EOF");

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_static_resources()
        ->mutable_listeners(0)
        ->set_continue_on_listener_filters_timeout(false);
    bootstrap.mutable_static_resources()
        ->mutable_listeners(0)
        ->mutable_listener_filters_timeout()
        ->MergeFrom(ProtobufUtil::TimeUtil::MillisecondsToDuration(1000000));
    bootstrap.mutable_static_resources()->mutable_listeners(0)->set_stat_prefix("listener_0");
  });

  std::string data = "hello";
  initialize();
  enableHalfClose(true);
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  auto result = tcp_client->write(data, true);
  // The connection could be closed when writing or after write.
  if (result == true) {
    tcp_client->waitForDisconnect();
  }
  test_server_->waitForCounterEq("listener.listener_0.downstream_listener_filter_remote_close", 1);
#endif
}

// Only update the order of listener filters, ensure the listener filters
// was update.
TEST_P(ListenerFilterIntegrationTest, UpdateListenerFilterOrder) {
  // Add two listener filters. The first filter will peek 5 bytes data,
  // the second filter will drain 2 bytes data. Expect the upstream will
  // receive 3 bytes data.
  config_helper_.addListenerFilter(R"EOF(
      name: inspect_data1
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.InspectDataListenerFilterConfig
        max_read_bytes: 2
        close_connection: false
        drain: true
        )EOF");
  config_helper_.addListenerFilter(R"EOF(
      name: inspect_data2
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.InspectDataListenerFilterConfig
        max_read_bytes: 5
        close_connection: false
        )EOF");
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Add the static cluster to serve LDS.
    auto* lds_cluster = bootstrap.mutable_static_resources()->add_clusters();
    lds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
    lds_cluster->set_name("lds_cluster");
    ConfigHelper::setHttp2(*lds_cluster);
  });
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    listener_config_.Swap(bootstrap.mutable_static_resources()->mutable_listeners(0));
    listener_config_.set_name("test_listener");
    listener_config_.set_continue_on_listener_filters_timeout(false);
    listener_config_.mutable_listener_filters_timeout()->MergeFrom(
        ProtobufUtil::TimeUtil::MillisecondsToDuration(1000));
    ENVOY_LOG_MISC(debug, "listener config: {}", listener_config_.DebugString());
    bootstrap.mutable_static_resources()->mutable_listeners()->Clear();
    auto* lds_config_source = bootstrap.mutable_dynamic_resources()->mutable_lds_config();
    lds_config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    auto* lds_api_config_source = lds_config_source->mutable_api_config_source();
    lds_api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    lds_api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
    envoy::config::core::v3::GrpcService* grpc_service = lds_api_config_source->add_grpc_services();
    setGrpcService(*grpc_service, "lds_cluster", fake_upstreams_[1]->localAddress());
  });
  on_server_init_function_ = [&]() {
    createLdsStream();
    sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "1");
  };
  use_lds_ = false;
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  test_server_->waitUntilListenersReady();
  // NOTE: The line above doesn't tell you if listener is up and listening.
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  // Workers not started, the LDS added test_listener is in active_listeners_ list.
  EXPECT_EQ(test_server_->server().listenerManager().listeners().size(), 1);
  registerTestServerPorts({"test_listener"});

  std::string data = "hello";
  std::string data_after_drain = data.substr(2, std::string::npos);
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("test_listener"));
  ASSERT_TRUE(tcp_client->write(data));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(data.size() - 2, &data_after_drain));
  tcp_client->close();

  // Switch the order of two listener filters. The first filter will drain 2 bytes data.
  // Then the second filter expects 5 bytes data, since the client only send 5 bytes data
  // and 2 bytes was drained. Then only 3 bytes data available. In the end, the listener
  // filter will timeout.
  listener_config_.mutable_listener_filters()->SwapElements(0, 1);
  ENVOY_LOG_MISC(debug, "listener config: {}", listener_config_.DebugString());
  sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "2");
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);
  test_server_->waitForCounterEq("listener_manager.listener_in_place_updated", 1);

  IntegrationTcpClientPtr tcp_client2 = makeTcpConnection(lookupPort("test_listener"));
  ASSERT_TRUE(tcp_client2->write(data));
  tcp_client2->waitForDisconnect();

  // Then ensure the whole listener works as expect with enough data.
  std::string long_data = "helloworld";
  std::string long_data_after_drain = long_data.substr(2, std::string::npos);
  IntegrationTcpClientPtr tcp_client3 = makeTcpConnection(lookupPort("test_listener"));
  ASSERT_TRUE(tcp_client3->write(long_data));
  FakeRawConnectionPtr fake_upstream_connection2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection2));
  ASSERT_TRUE(fake_upstream_connection2->waitForData(long_data.size() - 2, &long_data_after_drain));
  tcp_client3->close();
}

#ifdef __linux__
TEST_P(ListenerFilterIntegrationTest, UpdateListenerWithDifferentSocketOptions) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Add the static cluster to serve LDS.
    auto* lds_cluster = bootstrap.mutable_static_resources()->add_clusters();
    lds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
    lds_cluster->set_name("lds_cluster");
    ConfigHelper::setHttp2(*lds_cluster);
  });
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    listener_config_.Swap(bootstrap.mutable_static_resources()->mutable_listeners(0));
    listener_config_.set_name("listener_foo");
    auto* socket_option = listener_config_.add_socket_options();
    socket_option->set_level(IPPROTO_IP);
    socket_option->set_name(IP_TOS);
    socket_option->set_int_value(8); // IPTOS_THROUGHPUT
    socket_option->set_state(envoy::config::core::v3::SocketOption::STATE_LISTENING);
    ENVOY_LOG_MISC(debug, "listener config: {}", listener_config_.DebugString());
    bootstrap.mutable_static_resources()->mutable_listeners()->Clear();
    auto* lds_config_source = bootstrap.mutable_dynamic_resources()->mutable_lds_config();
    lds_config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    auto* lds_api_config_source = lds_config_source->mutable_api_config_source();
    lds_api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    lds_api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
    envoy::config::core::v3::GrpcService* grpc_service = lds_api_config_source->add_grpc_services();
    setGrpcService(*grpc_service, "lds_cluster", fake_upstreams_[1]->localAddress());
  });
  on_server_init_function_ = [&]() {
    createLdsStream();
    sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "1");
  };
  use_lds_ = false;
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  test_server_->waitUntilListenersReady();
  // NOTE: The line above doesn't tell you if listener is up and listening.
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  // Workers not started, the LDS added test_listener is in active_listeners_ list.
  EXPECT_EQ(test_server_->server().listenerManager().listeners().size(), 1);
  registerTestServerPorts({"test_listener"});
  std::string data = "hello";
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("test_listener"));
  ASSERT_TRUE(tcp_client->write(data));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(data.size(), &data));
  tcp_client->close();

  int opt_value = 0;
  socklen_t opt_len = sizeof(opt_value);
  EXPECT_TRUE(getSocketOption("listener_foo", IPPROTO_IP, IP_TOS, &opt_value, &opt_len));
  EXPECT_EQ(opt_len, sizeof(opt_value));
  EXPECT_EQ(8, opt_value);

  listener_config_.mutable_socket_options(0)->set_int_value(4);
  ENVOY_LOG_MISC(debug, "listener config: {}", listener_config_.DebugString());
  sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "2");
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);

  // We want to test update listener with same address but different socket options. But
  // the test is using zero port address. It turns out when create a new socket on zero
  // port address, kernel will generate new port for it. So we have to register the
  // port again here. IPTOS_RELIABILITY	= 4.
  registerTestServerPorts({"test_listener"});
  IntegrationTcpClientPtr tcp_client2 = makeTcpConnection(lookupPort("test_listener"));
  ASSERT_TRUE(tcp_client2->write(data));
  FakeRawConnectionPtr fake_upstream_connection2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection2));
  ASSERT_TRUE(fake_upstream_connection2->waitForData(data.size(), &data));
  tcp_client2->close();
  ASSERT_TRUE(fake_upstream_connection2->waitForDisconnect());

  int opt_value2 = 0;
  socklen_t opt_len2 = sizeof(opt_value);
  EXPECT_TRUE(getSocketOption("listener_foo", IPPROTO_IP, IP_TOS, &opt_value2, &opt_len2));
  EXPECT_EQ(opt_len2, sizeof(opt_value2));
  EXPECT_EQ(4, opt_value2);
}
#endif

TEST_P(ListenerFilterIntegrationTest,
       UpdateListenerWithDifferentSocketOptionsWhenReusePortDisabled) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Add the static cluster to serve LDS.
    auto* lds_cluster = bootstrap.mutable_static_resources()->add_clusters();
    lds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
    lds_cluster->set_name("lds_cluster");
    ConfigHelper::setHttp2(*lds_cluster);
  });
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    listener_config_.Swap(bootstrap.mutable_static_resources()->mutable_listeners(0));
    listener_config_.set_name("listener_foo");
    listener_config_.mutable_enable_reuse_port()->set_value(false);
    ENVOY_LOG_MISC(debug, "listener config: {}", listener_config_.DebugString());
    bootstrap.mutable_static_resources()->mutable_listeners()->Clear();
    auto* lds_config_source = bootstrap.mutable_dynamic_resources()->mutable_lds_config();
    lds_config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    auto* lds_api_config_source = lds_config_source->mutable_api_config_source();
    lds_api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    lds_api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
    envoy::config::core::v3::GrpcService* grpc_service = lds_api_config_source->add_grpc_services();
    setGrpcService(*grpc_service, "lds_cluster", fake_upstreams_[1]->localAddress());
  });
  on_server_init_function_ = [&]() {
    createLdsStream();
    sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "1");
  };
  use_lds_ = false;
  initialize();
  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  test_server_->waitUntilListenersReady();
  // NOTE: The line above doesn't tell you if listener is up and listening.
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  // Workers not started, the LDS added test_listener is in active_listeners_ list.
  EXPECT_EQ(test_server_->server().listenerManager().listeners().size(), 1);
  registerTestServerPorts({"test_listener"});
  std::string data = "hello";
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("test_listener"));
  ASSERT_TRUE(tcp_client->write(data));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(data.size(), &data));
  tcp_client->close();

  auto* socket_option = listener_config_.add_socket_options();
  socket_option->set_level(IPPROTO_IP);
  socket_option->set_name(IP_TOS);
  socket_option->set_int_value(8); // IPTOS_THROUGHPUT
  socket_option->set_state(envoy::config::core::v3::SocketOption::STATE_LISTENING);
  ENVOY_LOG_MISC(debug, "listener config: {}", listener_config_.DebugString());

  EXPECT_LOG_CONTAINS(
      "warning",
      "gRPC config for type.googleapis.com/envoy.config.listener.v3.Listener rejected: Error "
      "adding/updating listener(s) listener_foo: Listener listener_foo: doesn't support update any "
      "socket options when the reuse port isn't enabled",
      {
        sendLdsResponse({MessageUtil::getYamlStringFromMessage(listener_config_)}, "2");
        test_server_->waitForCounterGe("listener_manager.lds.update_rejected", 1);
        IntegrationTcpClientPtr tcp_client2 = makeTcpConnection(lookupPort("test_listener"));
        ASSERT_TRUE(tcp_client2->write(data));
        FakeRawConnectionPtr fake_upstream_connection2;
        ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection2));
        ASSERT_TRUE(fake_upstream_connection2->waitForData(data.size(), &data));
        tcp_client2->close();
        ASSERT_TRUE(fake_upstream_connection2->waitForDisconnect());
      });
}

INSTANTIATE_TEST_SUITE_P(IpVersionsAndGrpcTypes, ListenerFilterIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

class RebalancerTest : public testing::TestWithParam<Network::Address::IpVersion>,
                       public BaseIntegrationTest {
public:
  RebalancerTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseConfig() + R"EOF(
    listener_filters:
    # The inspect data filter is used for test the file event reset after
    # rebalance the request.
    - name: envoy.filters.listener.inspect_data
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.InspectDataListenerFilterConfig
        max_read_bytes: 5
        close_connection: false
    filter_chains:
    - filters:
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp_stats
          cluster: cluster_0
)EOF") {}

  void initialize() override {
    config_helper_.renameListener("tcp");
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          auto& src_listener_config = *bootstrap.mutable_static_resources()->mutable_listeners(0);
          src_listener_config.mutable_use_original_dst()->set_value(true);
          // Note that the below original_dst is replaced by FakeOriginalDstListenerFilter at the
          // link time.
          src_listener_config.add_listener_filters()->set_name(
              "envoy.filters.listener.original_dst");
          auto& virtual_listener_config = *bootstrap.mutable_static_resources()->add_listeners();
          virtual_listener_config = src_listener_config;
          virtual_listener_config.mutable_use_original_dst()->set_value(false);
          virtual_listener_config.clear_listener_filters();
          virtual_listener_config.mutable_bind_to_port()->set_value(false);
          virtual_listener_config.set_name("balanced_target_listener");
          virtual_listener_config.mutable_connection_balance_config()->mutable_exact_balance();
          *virtual_listener_config.mutable_stat_prefix() = target_listener_prefix_;
          virtual_listener_config.mutable_address()->mutable_socket_address()->set_port_value(80);
        });
    BaseIntegrationTest::initialize();
  }

  std::unique_ptr<RawConnectionDriver> createConnectionAndWrite(const std::string& request,
                                                                std::string& response) {
    Buffer::OwnedImpl buffer(request);
    return std::make_unique<RawConnectionDriver>(
        lookupPort("tcp"), buffer,
        [&response](Network::ClientConnection&, const Buffer::Instance& data) -> void {
          response.append(data.toString());
        },
        version_, *dispatcher_);
  }

  void verifyBalance(uint32_t repeats = 10) {
    // The balancer is balanced as per active connection instead of total connection.
    // The below vector maintains all the connections alive.
    std::vector<PerConnection> connections;
    for (uint32_t i = 0; i < repeats * concurrency_; ++i) {
      connections.emplace_back();
      connections.back().client_conn_ =
          createConnectionAndWrite("dummy", connections.back().response_);
      connections.back().client_conn_->waitForConnection();
      ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(connections.back().upstream_conn_));
    }
    for (auto& conn : connections) {
      conn.client_conn_->close();
      while (!conn.client_conn_->closed()) {
        dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
      }
    }
    ASSERT_EQ(TestUtility::findCounter(test_server_->statStore(),
                                       absl::StrCat("listener.", target_listener_prefix_,
                                                    ".worker_0.downstream_cx_total"))
                  ->value(),
              repeats);
    ASSERT_EQ(TestUtility::findCounter(test_server_->statStore(),
                                       absl::StrCat("listener.", target_listener_prefix_,
                                                    ".worker_1.downstream_cx_total"))
                  ->value(),
              repeats);
  }

  // The stats prefix that shared by ipv6 and ipv4 listener.
  std::string target_listener_prefix_{"balanced_listener"};
};

TEST_P(RebalancerTest, BindToPortUpdate) {
  concurrency_ = 2;
  initialize();

  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));

  new_config_helper.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap)
                                          -> void {
    // This virtual listener need updating.
    auto& virtual_listener_config = *bootstrap.mutable_static_resources()->mutable_listeners(1);
    *virtual_listener_config.mutable_address()->mutable_socket_address()->mutable_address() =
        bootstrap.static_resources().listeners(0).address().socket_address().address();
    (*(*virtual_listener_config.mutable_metadata()->mutable_filter_metadata())["random_filter_name"]
          .mutable_fields())["random_key"]
        .set_number_value(2);
  });
  // Create an LDS response with the new config, and reload config.
  new_config_helper.setLds("1");

  test_server_->waitForCounterEq("listener_manager.listener_modified", 1);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_draining", 0);

  verifyBalance();
}

// Verify the connections are distributed evenly on the 2 worker threads of the redirected
// listener.
// Currently flaky because the virtual listener create listen socket anyway despite the socket is
// never used. Will enable this test once https://github.com/envoyproxy/envoy/pull/16259 is merged.
TEST_P(RebalancerTest, DISABLED_RedirectConnectionIsBalancedOnDestinationListener) {
  auto ip_address_str =
      Network::Test::getLoopbackAddressUrlString(TestEnvironment::getIpVersionsForTest().front());
  concurrency_ = 2;
  initialize();
  verifyBalance();
}

INSTANTIATE_TEST_SUITE_P(IpVersions, RebalancerTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace
} // namespace Envoy
