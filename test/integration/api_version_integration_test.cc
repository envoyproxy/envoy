#include "envoy/api/v2/core/config_source.pb.h"

#include "common/common/assert.h"

#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

using Params =
    std::tuple<Network::Address::IpVersion, bool,
               envoy::config::core::v3alpha::ApiConfigSource::ApiType,
               envoy::config::core::v3alpha::ApiVersion, envoy::config::core::v3alpha::ApiVersion>;

class ApiVersionIntegrationTest : public testing::TestWithParam<Params>,
                                  public HttpIntegrationTest {
public:
  ApiVersionIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion()) {
    use_lds_ = false;
    create_xds_upstream_ = true;
    tls_xds_upstream_ = false;
    defer_listener_finalization_ = true;
    skipPortUsageValidation();
  }

  static std::string paramsToString(const testing::TestParamInfo<Params>& p) {
    return fmt::format(
        "{}_{}_{}_Resource_{}_Transport_{}",
        std::get<0>(p.param) == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6",
        std::get<1>(p.param) ? "ADS" : "SingletonXds",
        envoy::config::core::v3alpha::ApiConfigSource::ApiType_Name(std::get<2>(p.param)),
        envoy::config::core::v3alpha::ApiVersion_Name(std::get<3>(p.param)),
        envoy::config::core::v3alpha::ApiVersion_Name(std::get<4>(p.param)));
  }

  Network::Address::IpVersion ipVersion() const { return std::get<0>(GetParam()); }
  bool ads() const { return std::get<1>(GetParam()); }
  envoy::config::core::v3alpha::ApiConfigSource::ApiType apiType() const {
    return std::get<2>(GetParam());
  }
  envoy::config::core::v3alpha::ApiVersion resourceApiVersion() const {
    return std::get<3>(GetParam());
  }
  envoy::config::core::v3alpha::ApiVersion transportApiVersion() const {
    return std::get<4>(GetParam());
  }

  void initialize() override {
    config_helper_.addConfigModifier(
        [this](envoy::config::bootstrap::v3alpha::Bootstrap& bootstrap) {
          auto* xds_cluster = bootstrap.mutable_static_resources()->add_clusters();
          xds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
          xds_cluster->set_name("xds_cluster");
          xds_cluster->mutable_http2_protocol_options();
          if (ads()) {
            auto* api_config_source = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
            api_config_source->set_transport_api_version(transportApiVersion());
            api_config_source->set_api_type(apiType());
            auto* grpc_service = api_config_source->add_grpc_services();
            grpc_service->mutable_envoy_grpc()->set_cluster_name("xds_cluster");
          }
        });
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    HttpIntegrationTest::initialize();
    if (xds_stream_ == nullptr) {
      createXdsConnection();
      AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
      RELEASE_ASSERT(result, result.message());
      result = xds_stream_->waitForHeadersComplete();
      RELEASE_ASSERT(result, result.message());
      endpoint_ = std::string(xds_stream_->headers().Path()->value().getStringView());
      ENVOY_LOG_MISC(debug, "xDS endpoint {}", endpoint_);
    }
  }

  void setupConfigSource(envoy::config::core::v3alpha::ConfigSource& config_source) {
    config_source.set_resource_api_version(resourceApiVersion());
    if (ads()) {
      config_source.mutable_ads();
      return;
    }
    auto* api_config_source = config_source.mutable_api_config_source();
    api_config_source->set_transport_api_version(transportApiVersion());
    api_config_source->set_api_type(apiType());
    if (apiType() == envoy::config::core::v3alpha::ApiConfigSource::REST) {
      api_config_source->add_cluster_names("xds_cluster");
      api_config_source->mutable_refresh_delay()->set_seconds(1);
    } else {
      auto* grpc_service = api_config_source->add_grpc_services();
      grpc_service->mutable_envoy_grpc()->set_cluster_name("xds_cluster");
    }
  }

  AssertionResult validateDiscoveryRequest(const std::string& expected_v2_sotw_endpoint,
                                           const std::string& expected_v2_delta_endpoint,
                                           const std::string& expected_v2_rest_endpoint,
                                           const std::string& expected_v3alpha_sotw_endpoint,
                                           const std::string& expected_v3alpha_delta_endpoint,
                                           const std::string& expected_v3alpha_rest_endpoint,
                                           const std::string& expected_v2_type_url,
                                           const std::string& expected_v3alpha_type_url) {
    // Only with ADS do we allow mixed transport/resource versions.
    if (!ads() && resourceApiVersion() != transportApiVersion()) {
      return AssertionSuccess();
    }
    std::string expected_endpoint;
    std::string expected_type_url;
    std::string actual_type_url;
    const char ads_v2_sotw_endpoint[] =
        "/envoy.service.discovery.v2.AggregatedDiscoveryService/StreamAggregatedResources";
    const char ads_v3alpha_delta_endpoint[] =
        "/envoy.service.discovery.v3alpha.AggregatedDiscoveryService/StreamAggregatedResources";
    switch (transportApiVersion()) {
    case envoy::config::core::v3alpha::ApiVersion::AUTO:
    case envoy::config::core::v3alpha::ApiVersion::V2: {
      switch (apiType()) {
      case envoy::config::core::v3alpha::ApiConfigSource::GRPC: {
        API_NO_BOOST(envoy::api::v2::DiscoveryRequest) discovery_request;
        VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, discovery_request));
        xds_stream_->startGrpcStream();
        actual_type_url = discovery_request.type_url();
        expected_endpoint = ads() ? ads_v2_sotw_endpoint : expected_v2_sotw_endpoint;
        break;
      }
      case envoy::config::core::v3alpha::ApiConfigSource::DELTA_GRPC: {
        API_NO_BOOST(envoy::api::v2::DeltaDiscoveryRequest) delta_discovery_request;
        VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, delta_discovery_request));
        xds_stream_->startGrpcStream();
        actual_type_url = delta_discovery_request.type_url();
        expected_endpoint = expected_v2_delta_endpoint;
        break;
      }
      case envoy::config::core::v3alpha::ApiConfigSource::REST: {
        API_NO_BOOST(envoy::api::v2::DiscoveryRequest) discovery_request;
        VERIFY_ASSERTION(xds_stream_->waitForEndStream(*dispatcher_));
        MessageUtil::loadFromJson(xds_stream_->body().toString(), discovery_request,
                                  ProtobufMessage::getStrictValidationVisitor());
        actual_type_url = discovery_request.type_url();
        expected_endpoint = expected_v2_rest_endpoint;
        break;
      }
      default:
        NOT_REACHED_GCOVR_EXCL_LINE;
        break;
      }
      break;
    }
    case envoy::config::core::v3alpha::ApiVersion::V3ALPHA: {
      switch (apiType()) {
      case envoy::config::core::v3alpha::ApiConfigSource::GRPC: {
        API_NO_BOOST(envoy::api::v2::DiscoveryRequest) discovery_request;
        VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, discovery_request));
        actual_type_url = discovery_request.type_url();
        expected_endpoint = ads() ? ads_v3alpha_delta_endpoint : expected_v3alpha_sotw_endpoint;
        break;
      }
      case envoy::config::core::v3alpha::ApiConfigSource::DELTA_GRPC: {
        API_NO_BOOST(envoy::api::v2::DeltaDiscoveryRequest) delta_discovery_request;
        VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, delta_discovery_request));
        actual_type_url = delta_discovery_request.type_url();
        expected_endpoint = expected_v3alpha_delta_endpoint;
        break;
      }
      case envoy::config::core::v3alpha::ApiConfigSource::REST: {
        API_NO_BOOST(envoy::api::v2::DiscoveryRequest) discovery_request;
        VERIFY_ASSERTION(xds_stream_->waitForEndStream(*dispatcher_));
        MessageUtil::loadFromJson(xds_stream_->body().toString(), discovery_request,
                                  ProtobufMessage::getStrictValidationVisitor());
        actual_type_url = discovery_request.type_url();
        expected_endpoint = expected_v3alpha_rest_endpoint;
        break;
      }
      default:
        NOT_REACHED_GCOVR_EXCL_LINE;
        break;
      }
      break;
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
    switch (resourceApiVersion()) {
    case envoy::config::core::v3alpha::ApiVersion::AUTO:
    case envoy::config::core::v3alpha::ApiVersion::V2:
      expected_type_url = expected_v2_type_url;
      break;
    case envoy::config::core::v3alpha::ApiVersion::V3ALPHA:
      expected_type_url = expected_v3alpha_type_url;
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
    if (endpoint_ != expected_endpoint) {
      return AssertionFailure() << "Expected endpoint " << expected_endpoint << ", got "
                                << endpoint_;
    }
    if (expected_type_url != actual_type_url) {
      return AssertionFailure() << "Expected type URL " << expected_type_url << ", got "
                                << actual_type_url;
    }
    return AssertionSuccess();
  }

  void TearDown() override {
    if (xds_stream_ != nullptr) {
      cleanUpXdsConnection();
    }
    test_server_.reset();
    fake_upstreams_.clear();
  }

  std::string endpoint_;
};

// We manage the permutations below to reduce combinatorial explosion:
// - We only care about testing on one IP version, there should be no
//   material difference between v4/v6.
// - We do care about all the different ApiConfigSource variations.
// - We explicitly give the AUTO versions their own independent test suite,
//   since they are equivalent to v2, so we want to test them once but they are
//   mostly redundant.
// - We treat ADS and singleton xDS differently. ADS doesn't care about REST and
//   doesn't currently support delta xDS.
INSTANTIATE_TEST_SUITE_P(
    SingletonApiConfigSourcesExplicitApiVersions, ApiVersionIntegrationTest,
    testing::Combine(testing::Values(TestEnvironment::getIpVersionsForTest()[0]),
                     testing::Values(false),
                     testing::Values(envoy::config::core::v3alpha::ApiConfigSource::REST,
                                     envoy::config::core::v3alpha::ApiConfigSource::GRPC,
                                     envoy::config::core::v3alpha::ApiConfigSource::DELTA_GRPC),
                     testing::Values(envoy::config::core::v3alpha::ApiVersion::V2,
                                     envoy::config::core::v3alpha::ApiVersion::V3ALPHA),
                     testing::Values(envoy::config::core::v3alpha::ApiVersion::V2,
                                     envoy::config::core::v3alpha::ApiVersion::V3ALPHA)),
    ApiVersionIntegrationTest::paramsToString);

INSTANTIATE_TEST_SUITE_P(
    SingletonApiConfigSourcesAutoApiVersions, ApiVersionIntegrationTest,
    testing::Combine(testing::Values(TestEnvironment::getIpVersionsForTest()[0]),
                     testing::Values(false),
                     testing::Values(envoy::config::core::v3alpha::ApiConfigSource::REST,
                                     envoy::config::core::v3alpha::ApiConfigSource::GRPC,
                                     envoy::config::core::v3alpha::ApiConfigSource::DELTA_GRPC),
                     testing::Values(envoy::config::core::v3alpha::ApiVersion::AUTO),
                     testing::Values(envoy::config::core::v3alpha::ApiVersion::AUTO)),
    ApiVersionIntegrationTest::paramsToString);

INSTANTIATE_TEST_SUITE_P(
    AdsApiConfigSourcesExplicitApiVersions, ApiVersionIntegrationTest,
    testing::Combine(testing::Values(TestEnvironment::getIpVersionsForTest()[0]),
                     testing::Values(true),
                     testing::Values(envoy::config::core::v3alpha::ApiConfigSource::GRPC),
                     testing::Values(envoy::config::core::v3alpha::ApiVersion::V2,
                                     envoy::config::core::v3alpha::ApiVersion::V3ALPHA),
                     testing::Values(envoy::config::core::v3alpha::ApiVersion::V2,
                                     envoy::config::core::v3alpha::ApiVersion::V3ALPHA)),
    ApiVersionIntegrationTest::paramsToString);

TEST_P(ApiVersionIntegrationTest, Lds) {
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3alpha::Bootstrap& bootstrap) {
    setupConfigSource(*bootstrap.mutable_dynamic_resources()->mutable_lds_config());
  });
  initialize();
  ASSERT_TRUE(validateDiscoveryRequest(
      "/envoy.api.v2.ListenerDiscoveryService/StreamListeners",
      "/envoy.api.v2.ListenerDiscoveryService/DeltaListeners", "/v2/discovery:listeners",
      "/envoy.service.listener.v3alpha.ListenerDiscoveryService/StreamListeners",
      "/envoy.service.listener.v3alpha.ListenerDiscoveryService/DeltaListeners",
      "/v3alpha/discovery:listeners", "type.googleapis.com/envoy.api.v2.Listener",
      "type.googleapis.com/envoy.config.listener.v3alpha.Listener"));
}

TEST_P(ApiVersionIntegrationTest, Cds) {
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3alpha::Bootstrap& bootstrap) {
    setupConfigSource(*bootstrap.mutable_dynamic_resources()->mutable_cds_config());
  });
  initialize();
  ASSERT_TRUE(validateDiscoveryRequest(
      "/envoy.api.v2.ClusterDiscoveryService/StreamClusters",
      "/envoy.api.v2.ClusterDiscoveryService/DeltaClusters", "/v2/discovery:clusters",
      "/envoy.service.cluster.v3alpha.ClusterDiscoveryService/StreamClusters",
      "/envoy.service.cluster.v3alpha.ClusterDiscoveryService/DeltaClusters",
      "/v3alpha/discovery:clusters", "type.googleapis.com/envoy.api.v2.Cluster",
      "type.googleapis.com/envoy.config.cluster.v3alpha.Cluster"));
}

TEST_P(ApiVersionIntegrationTest, Eds) {
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3alpha::Bootstrap& bootstrap) {
    auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
    cluster->MergeFrom(bootstrap.static_resources().clusters(0));
    cluster->set_name("some_cluster");
    cluster->set_type(envoy::config::cluster::v3alpha::Cluster::EDS);
    setupConfigSource(*cluster->mutable_eds_cluster_config()->mutable_eds_config());
  });
  initialize();
  ASSERT_TRUE(validateDiscoveryRequest(
      "/envoy.api.v2.EndpointDiscoveryService/StreamEndpoints",
      "/envoy.api.v2.EndpointDiscoveryService/DeltaEndpoints", "/v2/discovery:endpoints",
      "/envoy.service.endpoint.v3alpha.EndpointDiscoveryService/StreamEndpoints",
      "/envoy.service.endpoint.v3alpha.EndpointDiscoveryService/DeltaEndpoints",
      "/v3alpha/discovery:endpoints", "type.googleapis.com/envoy.api.v2.ClusterLoadAssignment",
      "type.googleapis.com/envoy.config.endpoint.v3alpha.ClusterLoadAssignment"));
}

TEST_P(ApiVersionIntegrationTest, Rtds) {
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3alpha::Bootstrap& bootstrap) {
    auto* admin_layer = bootstrap.mutable_layered_runtime()->add_layers();
    admin_layer->set_name("admin layer");
    admin_layer->mutable_admin_layer();
    auto* rtds_layer = bootstrap.mutable_layered_runtime()->add_layers();
    rtds_layer->set_name("rtds_layer");
    setupConfigSource(*rtds_layer->mutable_rtds_layer()->mutable_rtds_config());
  });
  initialize();
  ASSERT_TRUE(validateDiscoveryRequest(
      "/envoy.service.discovery.v2.RuntimeDiscoveryService/StreamRuntime",
      "/envoy.service.discovery.v2.RuntimeDiscoveryService/DeltaRuntime", "/v2/discovery:runtime",
      "/envoy.service.runtime.v3alpha.RuntimeDiscoveryService/StreamRuntime",
      "/envoy.service.runtime.v3alpha.RuntimeDiscoveryService/DeltaRuntime",
      "/v3alpha/discovery:runtime", "type.googleapis.com/envoy.service.discovery.v2.Runtime",
      "type.googleapis.com/envoy.service.runtime.v3alpha.Runtime"));
}

TEST_P(ApiVersionIntegrationTest, Rds) {
  // TODO(htuch): this segfaults, this is likely some untested existing issue.
  if (apiType() == envoy::config::core::v3alpha::ApiConfigSource::DELTA_GRPC) {
    return;
  }
  config_helper_.addConfigModifier(
      [this](envoy::extensions::filters::network::http_connection_manager::v3alpha::
                 HttpConnectionManager& http_connection_manager) {
        auto* rds = http_connection_manager.mutable_rds();
        rds->set_route_config_name("rds");
        setupConfigSource(*rds->mutable_config_source());
      });
  initialize();
  ASSERT_TRUE(validateDiscoveryRequest(
      "/envoy.api.v2.RouteDiscoveryService/StreamRoutes",
      "/envoy.api.v2.RouteDiscoveryService/DeltaRoutes", "/v2/discovery:routes",
      "/envoy.service.route.v3alpha.RouteDiscoveryService/StreamRoutes",
      "/envoy.service.route.v3alpha.RouteDiscoveryService/DeltaRoutes", "/v3alpha/discovery:routes",
      "type.googleapis.com/envoy.api.v2.RouteConfiguration",
      "type.googleapis.com/envoy.config.route.v3alpha.RouteConfiguration"));
}

// TODO(htuch): add VHDS tests once VHDS lands.
// TEST_P(ApiVersionIntegrationTest, Vhds) {
// }

TEST_P(ApiVersionIntegrationTest, Srds) {
  config_helper_.addConfigModifier(
      [this](envoy::extensions::filters::network::http_connection_manager::v3alpha::
                 HttpConnectionManager& http_connection_manager) {
        auto* scoped_routes = http_connection_manager.mutable_scoped_routes();
        scoped_routes->set_name("scoped_routes");
        const std::string& scope_key_builder_config_yaml = R"EOF(
fragments:
  - header_value_extractor:
      name: Addr
      element_separator: ;
      element:
        key: x-foo-key
        separator: =
)EOF";
        envoy::extensions::filters::network::http_connection_manager::v3alpha::ScopedRoutes::
            ScopeKeyBuilder scope_key_builder;
        TestUtility::loadFromYaml(scope_key_builder_config_yaml,
                                  *scoped_routes->mutable_scope_key_builder());
        setupConfigSource(*scoped_routes->mutable_scoped_rds()->mutable_scoped_rds_config_source());
        setupConfigSource(*scoped_routes->mutable_rds_config_source());
      });
  initialize();
  ASSERT_TRUE(validateDiscoveryRequest(
      "/envoy.api.v2.ScopedRoutesDiscoveryService/StreamScopedRoutes",
      "/envoy.api.v2.ScopedRoutesDiscoveryService/DeltaScopedRoutes", "/v2/discovery:scoped-routes",
      "/envoy.service.route.v3alpha.ScopedRoutesDiscoveryService/StreamScopedRoutes",
      "/envoy.service.route.v3alpha.ScopedRoutesDiscoveryService/DeltaScopedRoutes",
      "/v3alpha/discovery:scoped-routes",
      "type.googleapis.com/envoy.api.v2.ScopedRouteConfiguration",
      "type.googleapis.com/envoy.config.route.v3alpha.ScopedRouteConfiguration"));
}

TEST_P(ApiVersionIntegrationTest, Sds) {
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3alpha::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* transport_socket = listener->mutable_filter_chains(0)->mutable_transport_socket();
    envoy::extensions::transport_sockets::tls::v3alpha::DownstreamTlsContext tls_context;
    auto* common_tls_context = tls_context.mutable_common_tls_context();
    auto* secret_config = common_tls_context->add_tls_certificate_sds_secret_configs();
    secret_config->set_name("sds");
    setupConfigSource(*secret_config->mutable_sds_config());
    transport_socket->set_name("envoy.transport_sockets.tls");
    transport_socket->mutable_typed_config()->PackFrom(tls_context);
  });
  initialize();
  ASSERT_TRUE(validateDiscoveryRequest(
      "/envoy.service.discovery.v2.SecretDiscoveryService/StreamSecrets",
      "/envoy.service.discovery.v2.SecretDiscoveryService/DeltaSecrets", "/v2/discovery:secrets",
      "/envoy.service.secret.v3alpha.SecretDiscoveryService/StreamSecrets",
      "/envoy.service.secret.v3alpha.SecretDiscoveryService/DeltaSecrets",
      "/v3alpha/discovery:secrets", "type.googleapis.com/envoy.api.v2.auth.Secret",
      "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3alpha.Secret"));
}

} // namespace
} // namespace Envoy
