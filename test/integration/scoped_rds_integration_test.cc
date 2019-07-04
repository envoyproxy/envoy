#include "envoy/api/v2/srds.pb.h"

#include "common/config/resources.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

class ScopedRdsIntegrationTest : public HttpIntegrationTest,
                                 public Grpc::GrpcClientIntegrationParamTest {
protected:
  struct FakeUpstreamInfo {
    FakeHttpConnectionPtr connection_;
    FakeUpstream* upstream_{};
    FakeStreamPtr stream_;
  };

  ScopedRdsIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, ipVersion(), realTime()) {}

  ~ScopedRdsIntegrationTest() override {
    resetConnections();
    cleanupUpstreamAndDownstream();
  }

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      // Add the static cluster to serve SRDS.
      auto* scoped_rds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      scoped_rds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      scoped_rds_cluster->set_name("srds_cluster");
      scoped_rds_cluster->mutable_http2_protocol_options();

      // Add the static cluster to serve RDS.
      auto* rds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      rds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      rds_cluster->set_name("rds_cluster");
      rds_cluster->mutable_http2_protocol_options();
    });

    config_helper_.addConfigModifier(
        [this](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
                   http_connection_manager) {
          const std::string& scope_key_builder_config_yaml = R"EOF(
fragments:
  - header_value_extractor: { name: X-Google-VIP }
)EOF";
          envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes::ScopeKeyBuilder
              scope_key_builder;
          TestUtility::loadFromYaml(scope_key_builder_config_yaml, scope_key_builder);
          auto* scoped_routes = http_connection_manager.mutable_scoped_routes();
          scoped_routes->set_name("foo-scoped-routes");
          *scoped_routes->mutable_scope_key_builder() = scope_key_builder;

          envoy::api::v2::core::ApiConfigSource* rds_api_config_source =
              scoped_routes->mutable_rds_config_source()->mutable_api_config_source();
          rds_api_config_source->set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
          envoy::api::v2::core::GrpcService* grpc_service =
              rds_api_config_source->add_grpc_services();
          setGrpcService(*grpc_service, "rds_cluster", getRdsFakeUpstream().localAddress());

          envoy::api::v2::core::ApiConfigSource* srds_api_config_source =
              scoped_routes->mutable_scoped_rds()
                  ->mutable_scoped_rds_config_source()
                  ->mutable_api_config_source();
          srds_api_config_source->set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
          grpc_service = srds_api_config_source->add_grpc_services();
          setGrpcService(*grpc_service, "srds_cluster", getScopedRdsFakeUpstream().localAddress());
        });

    HttpIntegrationTest::initialize();
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Create the SRDS upstream.
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_,
                                                  timeSystem(), enable_half_close_));
    // Create the RDS upstream.
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_,
                                                  timeSystem(), enable_half_close_));
  }

  void resetFakeUpstreamInfo(FakeUpstreamInfo* upstream_info) {
    ASSERT(upstream_info->upstream_ != nullptr);

    upstream_info->upstream_->set_allow_unexpected_disconnects(true);
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
    resetFakeUpstreamInfo(&scoped_rds_upstream_info_);
  }

  FakeUpstream& getRdsFakeUpstream() const { return *fake_upstreams_[2]; }

  FakeUpstream& getScopedRdsFakeUpstream() const { return *fake_upstreams_[1]; }

  void createStream(FakeUpstreamInfo* upstream_info, FakeUpstream& upstream) {
    upstream_info->upstream_ = &upstream;
    AssertionResult result =
        upstream_info->upstream_->waitForHttpConnection(*dispatcher_, upstream_info->connection_);
    RELEASE_ASSERT(result, result.message());
    result = upstream_info->connection_->waitForNewStream(*dispatcher_, upstream_info->stream_);
    RELEASE_ASSERT(result, result.message());
    upstream_info->stream_->startGrpcStream();
  }

  void createRdsStream() { createStream(&rds_upstream_info_, getRdsFakeUpstream()); }

  void createScopedRdsStream() {
    createStream(&scoped_rds_upstream_info_, getScopedRdsFakeUpstream());
  }

  void sendScopedRdsResponse(const std::vector<std::string>& resource_protos,
                             const std::string& version) {
    ASSERT(scoped_rds_upstream_info_.stream_ != nullptr);

    envoy::api::v2::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url(Config::TypeUrl::get().ScopedRouteConfiguration);

    for (const auto& resource_proto : resource_protos) {
      envoy::api::v2::ScopedRouteConfiguration scoped_route_proto;
      TestUtility::loadFromYaml(resource_proto, scoped_route_proto);
      response.add_resources()->PackFrom(scoped_route_proto);
    }

    scoped_rds_upstream_info_.stream_->sendGrpcMessage(response);
  }

  FakeUpstreamInfo scoped_rds_upstream_info_;
  FakeUpstreamInfo rds_upstream_info_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsAndGrpcTypes, ScopedRdsIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// Test that a SRDS DiscoveryResponse is successfully processed.
TEST_P(ScopedRdsIntegrationTest, BasicSuccess) {
  const std::string scope_route1 = R"EOF(
name: foo_scope1
route_configuration_name: foo_route1
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  const std::string scope_route2 = R"EOF(
name: foo_scope2
route_configuration_name: foo_route2
key:
  fragments:
    - string_key: x-foo-key
)EOF";

  on_server_init_function_ = [this, &scope_route1, &scope_route2]() {
    createScopedRdsStream();
    sendScopedRdsResponse({scope_route1, scope_route2}, "1");
  };
  initialize();

  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_attempt", 1);
  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_success", 1);
  // The version gauge should be set to xxHash64("1").
  test_server_->waitForGaugeEq("http.config_test.scoped_rds.foo-scoped-routes.version",
                               13237225503670494420UL);

  const std::string scope_route3 = R"EOF(
name: foo_scope3
route_configuration_name: foo_route3
key:
  fragments:
    - string_key: x-baz-key
)EOF";
  sendScopedRdsResponse({scope_route3}, "2");

  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_attempt", 2);
  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_success", 2);
  test_server_->waitForGaugeEq("http.config_test.scoped_rds.foo-scoped-routes.version",
                               6927017134761466251UL);

  // TODO(AndresGuedez): test actual scoped routing logic; only the config handling is implemented
  // at this point.
}

// Test that a bad config update updates the corresponding stats.
TEST_P(ScopedRdsIntegrationTest, ConfigUpdateFailure) {
  // 'name' will fail to validate due to empty string.
  const std::string scope_route1 = R"EOF(
name:
route_configuration_name: foo_route1
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  on_server_init_function_ = [this, &scope_route1]() {
    createScopedRdsStream();
    sendScopedRdsResponse({scope_route1}, "1");
  };
  initialize();

  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo-scoped-routes.update_rejected",
                                 1);
}

} // namespace
} // namespace Envoy
