#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/base64.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

// A simple test-only HTTP filter that acts as the "Fake Cache" caching layer in our integration
// test. It intercepts requests with 'x-simulate-cache' header, Base64-decodes and deserializes it
// into a strongly-typed CheckResponse proto, and writes it directly to dynamic typed metadata under
// the configured cache namespace before stripping the header.
class CacheSimulatorFilter : public Http::PassThroughFilter {
public:
  CacheSimulatorFilter() = default;

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    const auto simulate_header = headers.get(Http::LowerCaseString("x-simulate-cache"));
    if (!simulate_header.empty()) {
      std::string base64_response = std::string(simulate_header[0]->value().getStringView());
      std::string decoded = Base64::decode(base64_response);
      if (!decoded.empty()) {
        envoy::service::auth::v3::CheckResponse check_response;
        if (check_response.ParseFromString(decoded)) {
          Protobuf::Any typed_metadata;
          typed_metadata.PackFrom(check_response);

          // Store direct CheckResponse Any under the configured typed metadata cache namespace
          decoder_callbacks_->streamInfo().setDynamicTypedMetadata(
              "envoy.filters.http.ext_authz.cache", typed_metadata);
        }
      }
      headers.remove(Http::LowerCaseString("x-simulate-cache"));
    }
    return Http::FilterHeadersStatus::Continue;
  }
};

class CacheSimulatorFilterConfig
    : public Extensions::HttpFilters::Common::EmptyHttpDualFilterConfig {
public:
  CacheSimulatorFilterConfig() : EmptyHttpDualFilterConfig("cache-simulator-filter") {}
  absl::StatusOr<Http::FilterFactoryCb>
  createDualFilter(const std::string&, Server::Configuration::ServerFactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<CacheSimulatorFilter>());
    };
  }
};

// Perform static registration so Envoy's bootstrap configuration can resolve it
static Registry::RegisterFactory<CacheSimulatorFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_cache_simulator_filter_;

class ExtAuthzCacheIntegrationTest : public HttpIntegrationTest, public testing::Test {
public:
  ExtAuthzCacheIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, Network::Address::IpVersion::v4) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Allocate secondary dynamic ports to satisfy ConfigHelper configuration finalize
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add ext_authz cluster, dynamic endpoint will be mapped automatically by ConfigHelper
      auto* ext_authz_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ext_authz_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ext_authz_cluster->set_name("ext_authz_cluster");
      ConfigHelper::setHttp2(*ext_authz_cluster);

      // 1. Set up CacheSimulatorFilter (Fake Cache) using empty config
      envoy::config::core::v3::TypedExtensionConfig cache_simulator_config;
      cache_simulator_config.set_name("cache-simulator-filter");
      cache_simulator_config.mutable_typed_config()->PackFrom(Protobuf::Struct());

      // 2. Set up ext_authz filter config, bypass namespace set
      envoy::extensions::filters::http::ext_authz::v3::ExtAuthz ext_authz_proto;
      ext_authz_proto.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name(
          "ext_authz_cluster");
      ext_authz_proto.set_check_response_typed_metadata_namespace(
          "envoy.filters.http.ext_authz.cache");
      ext_authz_proto.set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);

      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter ext_authz_filter;
      ext_authz_filter.set_name("envoy.filters.http.ext_authz");
      ext_authz_filter.mutable_typed_config()->PackFrom(ext_authz_proto);

      // Prepend filters to HCM (cache_simulator_filter first, then ext_authz)
      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_authz_filter));
      config_helper_.prependFilter(
          MessageUtil::getJsonStringFromMessageOrError(cache_simulator_config));
    });

    HttpIntegrationTest::initialize();
  }

  std::string serializeAndEncode(const envoy::service::auth::v3::CheckResponse& response) {
    std::string serialized;
    RELEASE_ASSERT(response.SerializeToString(&serialized), "Failed to serialize CheckResponse");
    return Base64::encode(serialized.data(), serialized.size());
  }
};

TEST_F(ExtAuthzCacheIntegrationTest, CacheHitOKBypassesRPC) {
  initialize();

  // 1. Prepare cached OK response with header mutations
  envoy::service::auth::v3::CheckResponse cached_response;
  cached_response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);
  auto* header_to_add = cached_response.mutable_ok_response()->add_headers();
  header_to_add->mutable_header()->set_key("x-cached-header");
  header_to_add->mutable_header()->set_value("cache-value-ok");

  std::string base64_cached_response = serializeAndEncode(cached_response);

  // 2. Client connection and request
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                         {":path", "/test"},
                                         {":scheme", "http"},
                                         {":authority", "host"},
                                         {"x-simulate-cache", base64_cached_response}};

  auto response = codec_client_->makeHeaderOnlyRequest(headers);

  // 3. Verify request goes upstream with injected headers, and gRPC bypasses
  AssertionResult result =
      fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
  RELEASE_ASSERT(result, result.message());
  result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
  RELEASE_ASSERT(result, result.message());
  result = upstream_request_->waitForEndStream(*dispatcher_);
  RELEASE_ASSERT(result, result.message());

  // Verify mutated header is present upstream
  EXPECT_THAT(upstream_request_->headers(), ContainsHeader("x-cached-header", "cache-value-ok"));
  // Verify simulation header was stripped
  EXPECT_TRUE(upstream_request_->headers().get(Http::LowerCaseString("x-simulate-cache")).empty());

  // Send response from upstream
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  // Client receives response
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_F(ExtAuthzCacheIntegrationTest, CacheHitDeniedBypassesRPC) {
  initialize();

  // 1. Prepare cached Denied response (403 Forbidden)
  envoy::service::auth::v3::CheckResponse cached_response;
  cached_response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);
  auto* denied_response = cached_response.mutable_denied_response();
  denied_response->mutable_status()->set_code(
      static_cast<envoy::type::v3::StatusCode>(enumToInt(Http::Code::Forbidden)));
  denied_response->set_body("Cache Denied Body");

  std::string base64_cached_response = serializeAndEncode(cached_response);

  // 2. Client request
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                         {":path", "/test"},
                                         {":scheme", "http"},
                                         {":authority", "host"},
                                         {"x-simulate-cache", base64_cached_response}};

  auto response = codec_client_->makeHeaderOnlyRequest(headers);

  // 3. Verify client receives 403 local reply immediately, and upstream never sees request
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
  EXPECT_EQ("Cache Denied Body", response->body());
}

} // namespace Envoy
