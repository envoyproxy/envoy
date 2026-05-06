#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/base64.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/protobuf/utility.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

class ExtAuthzCacheIntegrationTest : public HttpIntegrationTest,
                                     public testing::Test {
public:
  ExtAuthzCacheIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Create fake upstream for ext_authz gRPC service
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void initializeConfig() {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add ext_authz cluster
      auto* ext_authz_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ext_authz_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ext_authz_cluster->set_name("ext_authz_cluster");
      ConfigHelper::setHttp2(*ext_authz_cluster);

      // 1. Setup header_to_metadata filter config (Cache Simulator)
      envoy::extensions::filters::http::header_to_metadata::v3::Config h2m_proto;
      const std::string h2m_yaml = R"YAML(
        request_rules:
        - header: x-simulate-cache
          on_header_present:
            metadata_namespace: envoy.filters.http.ext_authz
            key: authz_cache
            type: STRING
          remove: true
      )YAML";
      TestUtility::loadFromYaml(h2m_yaml, h2m_proto);

      envoy::config::core::v3::TypedExtensionConfig header_to_metadata_filter;
      header_to_metadata_filter.set_name("envoy.filters.http.header_to_metadata");
      header_to_metadata_filter.mutable_typed_config()->PackFrom(h2m_proto);

      // 2. Setup ext_authz filter config
      envoy::extensions::filters::http::ext_authz::v3::ExtAuthz ext_authz_proto;
      ext_authz_proto.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("ext_authz_cluster");
      ext_authz_proto.set_check_response_metadata_key("authz_cache");
      ext_authz_proto.set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);

      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter ext_authz_filter;
      ext_authz_filter.set_name("envoy.filters.http.ext_authz");
      ext_authz_filter.mutable_typed_config()->PackFrom(ext_authz_proto);

      // Prepend filters to HCM (header_to_metadata first, then ext_authz)
      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_authz_filter));
      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(header_to_metadata_filter));
    });
  }

  std::string serializeAndEncode(const envoy::service::auth::v3::CheckResponse& response) {
    std::string serialized;
    RELEASE_ASSERT(response.SerializeToString(&serialized), "Failed to serialize CheckResponse");
    return Base64::encode(serialized.data(), serialized.size());
  }
};

TEST_F(ExtAuthzCacheIntegrationTest, CacheHitOKBypassesRPC) {
  initializeConfig();
  HttpIntegrationTest::initialize();

  // 1. Prepare cached OK response with header mutations
  envoy::service::auth::v3::CheckResponse cached_response;
  cached_response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);
  auto* header_to_add = cached_response.mutable_ok_response()->add_headers();
  header_to_add->mutable_header()->set_key("x-cached-header");
  header_to_add->mutable_header()->set_value("cache-value-ok");

  std::string base64_cached_response = serializeAndEncode(cached_response);

  // 2. Client connection and request
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/test"},
      {":scheme", "http"},
      {":authority", "host"},
      {"x-simulate-cache", base64_cached_response}
  };

  auto response = codec_client_->makeHeaderOnlyRequest(headers);

  // 3. Verify request goes upstream with injected headers, and gRPC bypasses
  AssertionResult result = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
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
  initializeConfig();
  HttpIntegrationTest::initialize();

  // 1. Prepare cached Denied response (403 Forbidden)
  envoy::service::auth::v3::CheckResponse cached_response;
  cached_response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);
  auto* denied_response = cached_response.mutable_denied_response();
  denied_response->mutable_status()->set_code(static_cast<envoy::type::v3::StatusCode>(enumToInt(Http::Code::Forbidden)));
  denied_response->set_body("Cache Denied Body");

  std::string base64_cached_response = serializeAndEncode(cached_response);

  // 2. Client request
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/test"},
      {":scheme", "http"},
      {":authority", "host"},
      {"x-simulate-cache", base64_cached_response}
  };

  auto response = codec_client_->makeHeaderOnlyRequest(headers);

  // 3. Verify client receives 403 local reply immediately, and upstream never sees request
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
  EXPECT_EQ("Cache Denied Body", response->body());
}

} // namespace Envoy
