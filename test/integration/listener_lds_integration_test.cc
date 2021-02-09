#include "envoy/api/v2/discovery.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "common/config/api_version.h"
#include "common/config/version_converter.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/test_common/printers.h"
#include "test/test_common/resources.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

class ListenerIntegrationTest : public HttpIntegrationTest,
                                public Grpc::GrpcClientIntegrationParamTest {
protected:
  struct FakeUpstreamInfo {
    FakeHttpConnectionPtr connection_;
    FakeUpstream* upstream_{};
    absl::flat_hash_map<std::string, FakeStreamPtr> stream_by_resource_name_;
  };

  ListenerIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, ipVersion(), realTime()) {}

  ~ListenerIntegrationTest() override { resetConnections(); }

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
          setGrpcService(*grpc_service, "rds_cluster", getRdsFakeUpstream().localAddress());
        });

    // Note this has to be the last modifier as it nuke static_resource listeners.
    setUpGrpcLds();
    HttpIntegrationTest::initialize();
  }
  void setUpGrpcLds() {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      listener_config_.Swap(bootstrap.mutable_static_resources()->mutable_listeners(0));
      listener_config_.set_name(listener_name_);
      ENVOY_LOG_MISC(error, "listener config: {}", listener_config_.DebugString());
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
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Create the LDS upstream (fake_upstreams_[1]).
    addFakeUpstream(FakeHttpConnection::Type::HTTP2);
    // Create the RDS upstream (fake_upstreams_[2]).
    addFakeUpstream(FakeHttpConnection::Type::HTTP2);
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
    API_NO_BOOST(envoy::api::v2::DiscoveryResponse) response;
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
    API_NO_BOOST(envoy::api::v2::DiscoveryResponse) response;
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

INSTANTIATE_TEST_SUITE_P(IpVersionsAndGrpcTypes, ListenerIntegrationTest,
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
    // Set reuse_port so that a new socket is created by the
    // ListenSocketFactory.
    listener_config_.set_reuse_port(true);
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

} // namespace
} // namespace Envoy
