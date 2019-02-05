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

  ~ScopedRdsIntegrationTest() {
    resetConnections();
    cleanupUpstreamAndDownstream();
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
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
          // RDS must be enabled along when SRDS is enabled.
          auto* rds = http_connection_manager.mutable_rds();
          rds->set_scoped_rds_template(true);
          auto* api_config_source = rds->mutable_config_source()->mutable_api_config_source();
          api_config_source->set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
          auto* grpc_service = api_config_source->add_grpc_services();
          setGrpcService(*grpc_service, "rds_cluster", fake_upstreams_[2]->localAddress());

          // Configure SRDS.
          auto* scoped_rds = http_connection_manager.mutable_scoped_rds();
          scoped_rds->set_scoped_routes_config_set_name("foo_scope_set");
          api_config_source = scoped_rds->mutable_config_source()->mutable_api_config_source();
          api_config_source->set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
          grpc_service = api_config_source->add_grpc_services();
          setGrpcService(*grpc_service, "srds_cluster", fake_upstreams_[1]->localAddress());
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

  void sendScopedRdsResponse(const std::string& response_yaml, const std::string& version) {
    ASSERT(scoped_rds_upstream_info_.stream_ != nullptr);

    envoy::api::v2::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url(Config::TypeUrl::get().ScopedRouteConfigurationsSet);

    envoy::api::v2::ScopedRouteConfigurationsSet scoped_routes_proto;
    MessageUtil::loadFromYaml(response_yaml, scoped_routes_proto);
    response.add_resources()->PackFrom(scoped_routes_proto);

    scoped_rds_upstream_info_.stream_->sendGrpcMessage(response);
  }

  FakeUpstreamInfo scoped_rds_upstream_info_;
  FakeUpstreamInfo rds_upstream_info_;
};

INSTANTIATE_TEST_CASE_P(IpVersionsAndGrpcTypes, ScopedRdsIntegrationTest,
                        GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(ScopedRdsIntegrationTest, BasicSuccess) {
  const std::string response_yaml = R"EOF(
name: foo_scope_set
scope_key_builder:
  fragments:
    - header_value_extractor: { name: element }
scopes:
  - route_configuration_name: foo_routes
    key:
      fragments:
        - string_key: x-foo-key
)EOF";

  pre_worker_start_test_steps_ = [this, &response_yaml]() {
    createScopedRdsStream();
    sendScopedRdsResponse(response_yaml, "1");
  };
  initialize();

  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo_scope_set.update_attempt", 1);
  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo_scope_set.update_success", 1);
  // The version gauge should be set to xxHash64("1").
  test_server_->waitForGaugeEq("http.config_test.scoped_rds.foo_scope_set.version",
                               13237225503670494420UL);

  const std::string response_yaml2 = R"EOF(
name: foo_scope_set
scope_key_builder:
  fragments:
    - header_value_extractor: { name: element }
scopes:
  - route_configuration_name: foo_routes2
    key:
      fragments:
        - string_key: x-baz-key
)EOF";
  sendScopedRdsResponse(response_yaml, "2");

  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo_scope_set.update_attempt", 2);
  test_server_->waitForCounterGe("http.config_test.scoped_rds.foo_scope_set.update_success", 2);
  test_server_->waitForGaugeEq("http.config_test.scoped_rds.foo_scope_set.version",
                               6927017134761466251UL);

  // TODO(AndresGuedez): test actual scoped routing logic; only the config hanlding is implemented
  // at this point.
}

} // namespace
} // namespace Envoy
