#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/config/api_version.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"
#include "test/test_common/printers.h"
#include "test/test_common/resources.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

class ScopedRdsIntegrationTest : public HttpIntegrationTest,
                                 public Grpc::DeltaSotwIntegrationParamTest {
protected:
  struct FakeUpstreamInfo {
    FakeHttpConnectionPtr connection_;
    FakeUpstream* upstream_{};
    absl::flat_hash_map<std::string, FakeStreamPtr> stream_by_resource_name_;
  };

  ScopedRdsIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, ipVersion()) {
    // TODO(ggreenway): add tag extraction rules.
    // Missing stat tag-extraction rule for stat
    // 'http.scoped_rds.foo-scoped-routes.grpc.srds_cluster.streams_closed_16' and stat_prefix
    // 'srds_cluster'.
    skip_tag_extraction_rule_check_ = true;

    config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux",
                                      (sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw ||
                                       sotwOrDelta() == Grpc::SotwOrDelta::UnifiedDelta)
                                          ? "true"
                                          : "false");
  }

  ~ScopedRdsIntegrationTest() override { resetConnections(); }

  void setupModifications() {
    if (modifications_set_up_) {
      return;
    }

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add another static cluster for sending HTTP requests.
      auto* cluster_1 = bootstrap.mutable_static_resources()->add_clusters();
      cluster_1->MergeFrom(bootstrap.static_resources().clusters()[0]);
      cluster_1->set_name("cluster_1");

      // Add the static cluster to serve SRDS.
      auto* scoped_rds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      scoped_rds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      scoped_rds_cluster->set_name("srds_cluster");
      ConfigHelper::setHttp2(*scoped_rds_cluster);

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
          const std::string& scope_key_builder_config_yaml = R"EOF(
fragments:
  - header_value_extractor:
      name: Addr
      element_separator: ;
      element:
        key: x-foo-key
        separator: =
)EOF";
          envoy::extensions::filters::network::http_connection_manager::v3::ScopedRoutes::
              ScopeKeyBuilder scope_key_builder;
          TestUtility::loadFromYaml(scope_key_builder_config_yaml, scope_key_builder);
          auto* scoped_routes = http_connection_manager.mutable_scoped_routes();
          scoped_routes->set_name(srds_config_name_);
          *scoped_routes->mutable_scope_key_builder() = scope_key_builder;

          // Set resource api version for rds.
          envoy::config::core::v3::ConfigSource* rds_config_source =
              scoped_routes->mutable_rds_config_source();
          rds_config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);

          // Set transport api version for rds.
          envoy::config::core::v3::ApiConfigSource* rds_api_config_source =
              rds_config_source->mutable_api_config_source();
          rds_api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);

          // Add grpc service for rds.
          rds_api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
          envoy::config::core::v3::GrpcService* grpc_service =
              rds_api_config_source->add_grpc_services();
          setGrpcService(*grpc_service, "rds_cluster", getRdsFakeUpstream().localAddress());

          // Set resource api version for scoped rds.
          envoy::config::core::v3::ConfigSource* srds_config_source =
              scoped_routes->mutable_scoped_rds()->mutable_scoped_rds_config_source();
          srds_config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
          if (!srds_resources_locator_.empty()) {
            scoped_routes->mutable_scoped_rds()->set_srds_resources_locator(
                srds_resources_locator_);
          }

          // Set Transport api version for scoped_rds.
          envoy::config::core::v3::ApiConfigSource* srds_api_config_source =
              srds_config_source->mutable_api_config_source();
          srds_api_config_source->set_transport_api_version(
              envoy::config::core::v3::ApiVersion::V3);

          // Add grpc service for scoped rds.
          if (isDelta()) {
            srds_api_config_source->set_api_type(
                envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
          } else {
            srds_api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
          }
          srds_api_config_source->set_transport_api_version(
              envoy::config::core::v3::ApiVersion::V3);
          grpc_service = srds_api_config_source->add_grpc_services();
          setGrpcService(*grpc_service, "srds_cluster", getScopedRdsFakeUpstream().localAddress());
        });
    modifications_set_up_ = true;
  }

  void initialize() override {
    // Setup two upstream hosts, one for each cluster.
    setUpstreamCount(2);
    setupModifications();
    HttpIntegrationTest::initialize();
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Create the SRDS upstream.
    srds_upstream_idx_ = fake_upstreams_.size();
    addFakeUpstream(Http::CodecType::HTTP2);
    // Create the RDS upstream.
    rds_upstream_idx_ = fake_upstreams_.size();
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void resetFakeUpstreamInfo(FakeUpstreamInfo* upstream_info) {
    if (upstream_info->upstream_ == nullptr) {
      return;
    }

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

  FakeUpstream& getRdsFakeUpstream() const {
    ASSERT(rds_upstream_idx_ < fake_upstreams_.size());
    return *fake_upstreams_[rds_upstream_idx_];
  }

  FakeUpstream& getScopedRdsFakeUpstream() const {
    ASSERT(srds_upstream_idx_ < fake_upstreams_.size());
    return *fake_upstreams_[srds_upstream_idx_];
  }

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

  void createScopedRdsStream() {
    createStream(&scoped_rds_upstream_info_, getScopedRdsFakeUpstream(), srds_config_name_);
  }

  void sendRdsResponse(const std::string& route_config, const std::string& version) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    std::string route_conguration_type_url =
        "type.googleapis.com/envoy.config.route.v3.RouteConfiguration";
    response.set_version_info(version);
    response.set_type_url(route_conguration_type_url);
    auto route_configuration =
        TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(route_config);
    response.add_resources()->PackFrom(route_configuration);
    ASSERT(rds_upstream_info_.stream_by_resource_name_[route_configuration.name()] != nullptr);
    rds_upstream_info_.stream_by_resource_name_[route_configuration.name()]->sendGrpcMessage(
        response);
  }

  void sendSrdsResponse(const std::vector<std::string>& sotw_list,
                        const std::vector<std::string>& to_add_list,
                        const std::vector<std::string>& to_delete_list,
                        const std::string& version) {
    if (isDelta()) {
      sendDeltaScopedRdsResponse(to_add_list, to_delete_list, version);
    } else {
      sendSotwScopedRdsResponse(sotw_list, version);
    }
  }

  void sendDeltaScopedRdsResponse(const std::vector<std::string>& to_add_list,
                                  const std::vector<std::string>& to_delete_list,
                                  const std::string& version) {
    ASSERT(scoped_rds_upstream_info_.stream_by_resource_name_[srds_config_name_] != nullptr);
    std::string scoped_route_configuration_type_url =
        "type.googleapis.com/envoy.config.route.v3.ScopedRouteConfiguration";
    envoy::service::discovery::v3::DeltaDiscoveryResponse response;
    response.set_system_version_info(version);
    response.set_type_url(scoped_route_configuration_type_url);

    for (const auto& scope_name : to_delete_list) {
      *response.add_removed_resources() = scope_name;
    }
    for (const auto& resource_proto : to_add_list) {
      envoy::config::route::v3::ScopedRouteConfiguration scoped_route_proto;
      TestUtility::loadFromYaml(resource_proto, scoped_route_proto);
      auto resource = response.add_resources();
      resource->set_name(scoped_route_proto.name());
      resource->set_version(version);
      resource->mutable_resource()->PackFrom(scoped_route_proto);
    }
    scoped_rds_upstream_info_.stream_by_resource_name_[srds_config_name_]->sendGrpcMessage(
        response);
  }

  void sendSotwScopedRdsResponse(const std::vector<std::string>& resource_protos,
                                 const std::string& version) {
    ASSERT(scoped_rds_upstream_info_.stream_by_resource_name_[srds_config_name_] != nullptr);

    std::string scoped_route_configuration_type_url =
        "type.googleapis.com/envoy.config.route.v3.ScopedRouteConfiguration";
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url(scoped_route_configuration_type_url);

    for (const auto& resource_proto : resource_protos) {
      envoy::config::route::v3::ScopedRouteConfiguration scoped_route_proto;
      TestUtility::loadFromYaml(resource_proto, scoped_route_proto);
      response.add_resources()->PackFrom(scoped_route_proto);
    }
    scoped_rds_upstream_info_.stream_by_resource_name_[srds_config_name_]->sendGrpcMessage(
        response);
  }

  bool isDelta() {
    return sotwOrDelta() == Grpc::SotwOrDelta::Delta ||
           sotwOrDelta() == Grpc::SotwOrDelta::UnifiedDelta;
  }

  const std::string srds_config_name_{"foo-scoped-routes"};
  std::size_t rds_upstream_idx_ = -1;
  std::size_t srds_upstream_idx_ = -1;
  FakeUpstreamInfo scoped_rds_upstream_info_;
  FakeUpstreamInfo rds_upstream_info_;
  std::string srds_resources_locator_;
  bool modifications_set_up_ = false;
};

} // namespace Envoy
