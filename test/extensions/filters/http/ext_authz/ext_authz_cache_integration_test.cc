#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/base64.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/ext_authz/auth_cache.h"

#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

// Shared storage for our simple cache.
struct SimpleInMemoryCacheStorage {
  absl::Mutex mutex;
  // Map path -> Response.
  absl::flat_hash_map<std::string, Filters::Common::ExtAuthz::Response> map ABSL_GUARDED_BY(mutex);
};

class SimpleInMemoryCache : public AuthCache {
public:
  SimpleInMemoryCache(std::shared_ptr<SimpleInMemoryCacheStorage> storage) : storage_(storage) {}

  void lookup(const envoy::service::auth::v3::CheckRequest& request, LookupCallback&& cb,
              Tracing::Span&, const StreamInfo::StreamInfo&) override {
    if (request.has_attributes() && request.attributes().has_request() &&
        request.attributes().request().has_http()) {
      current_key_ = request.attributes().request().http().path();
    }

    if (current_key_.empty()) {
      cb(nullptr);
      return;
    }

    absl::ReaderMutexLock lock(&storage_->mutex);
    auto it = storage_->map.find(current_key_);
    if (it != storage_->map.end()) {
      // Hit! Copy the response.
      auto copy = std::make_unique<Filters::Common::ExtAuthz::Response>(it->second);
      cb(std::move(copy));
      return;
    }
    cb(nullptr);
  }

  void insert(const Filters::Common::ExtAuthz::Response& response, Tracing::Span&,
              const StreamInfo::StreamInfo&) override {
    if (current_key_.empty()) {
      return;
    }

    absl::WriterMutexLock lock(&storage_->mutex);
    storage_->map[current_key_] = response;
  }

  void onDestroy() override {}

private:
  std::shared_ptr<SimpleInMemoryCacheStorage> storage_;
  std::string current_key_;
};

class SimpleInMemoryCacheFactory : public AuthCacheFactory {
public:
  SimpleInMemoryCacheFactory() : storage_(std::make_shared<SimpleInMemoryCacheStorage>()) {}

  AuthCachePtr createAuthCache(const Protobuf::Message&,
                               Server::Configuration::ServerFactoryContext&) override {
    return std::make_unique<SimpleInMemoryCache>(storage_);
  }

  std::string name() const override {
    return "envoy.filters.http.ext_authz.cache.simple_in_memory";
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  void clear() {
    absl::WriterMutexLock lock(&storage_->mutex);
    storage_->map.clear();
  }

private:
  std::shared_ptr<SimpleInMemoryCacheStorage> storage_;
};

class ExtAuthzCacheIntegrationTest : public HttpIntegrationTest, public testing::Test {
public:
  ExtAuthzCacheIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, Network::Address::IpVersion::v4) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Add fake upstream for ext_authz (gRPC)
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void initialize() override {
    cache_factory_.clear();
    inject_cache_factory_ =
        std::make_unique<Registry::InjectFactory<AuthCacheFactory>>(cache_factory_);

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add ext_authz cluster by merging from cluster_0 (backend)
      auto* ext_authz_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ext_authz_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ext_authz_cluster->set_name("ext_authz_cluster");
      ConfigHelper::setHttp2(*ext_authz_cluster);

      // Set up ext_authz filter config
      envoy::extensions::filters::http::ext_authz::v3::ExtAuthz ext_authz_proto;
      ext_authz_proto.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name(
          "ext_authz_cluster");
      ext_authz_proto.set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);

      // Configure the cache
      auto* cache_config = ext_authz_proto.mutable_cache();
      cache_config->set_name("envoy.filters.http.ext_authz.cache.simple_in_memory");
      cache_config->mutable_typed_config()->PackFrom(Protobuf::Struct());

      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter ext_authz_filter;
      ext_authz_filter.set_name("envoy.filters.http.ext_authz");
      ext_authz_filter.mutable_typed_config()->PackFrom(ext_authz_proto);

      // Prepend filter to HCM
      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_authz_filter));
    });

    HttpIntegrationTest::initialize();
  }

  void TearDown() override { cleanup(); }

  void cleanup() {
    if (fake_ext_authz_connection_ != nullptr) {
      AssertionResult result = fake_ext_authz_connection_->close();
      RELEASE_ASSERT(result, result.message());
    }
    cleanupUpstreamAndDownstream();
  }

  SimpleInMemoryCacheFactory cache_factory_;
  std::unique_ptr<Registry::InjectFactory<AuthCacheFactory>> inject_cache_factory_;

  FakeHttpConnectionPtr fake_ext_authz_connection_;
  FakeStreamPtr ext_authz_request_;
};

TEST_F(ExtAuthzCacheIntegrationTest, CacheMissThenHit) {
  initialize();

  // --- Request 1: Cache Miss ---
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/cache-me"}, {":scheme", "http"}, {":authority", "host"}};
  auto response1 = codec_client_->makeHeaderOnlyRequest(headers);

  // Wait for ext_authz check request on fake authz upstream (fake_upstreams_[1])
  AssertionResult result =
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_ext_authz_connection_);
  ASSERT_TRUE(result) << result.message();
  result = fake_ext_authz_connection_->waitForNewStream(*dispatcher_, ext_authz_request_);
  ASSERT_TRUE(result) << result.message();

  // Verify it is a check request
  envoy::service::auth::v3::CheckRequest check_request;
  result = ext_authz_request_->waitForGrpcMessage(*dispatcher_, check_request);
  ASSERT_TRUE(result) << result.message();
  EXPECT_EQ("/cache-me", check_request.attributes().request().http().path());

  // Send OK response from authz service
  ext_authz_request_->startGrpcStream();
  envoy::service::auth::v3::CheckResponse check_response;
  check_response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);
  auto* header_to_add = check_response.mutable_ok_response()->add_headers();
  header_to_add->mutable_header()->set_key("x-authed-header");
  header_to_add->mutable_header()->set_value("auth-value");

  ext_authz_request_->sendGrpcMessage(check_response);
  ext_authz_request_->finishGrpcStream(Grpc::Status::Ok);

  // Wait for request upstream on backend (fake_upstreams_[0])
  result = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
  ASSERT_TRUE(result) << result.message();
  result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
  ASSERT_TRUE(result) << result.message();
  result = upstream_request_->waitForEndStream(*dispatcher_);
  ASSERT_TRUE(result) << result.message();

  // Verify backend received the mutated header
  EXPECT_THAT(upstream_request_->headers(), ContainsHeader("x-authed-header", "auth-value"));

  // Send response from backend
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  // Client receives response
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_TRUE(response1->complete());
  EXPECT_EQ("200", response1->headers().getStatusValue());

  // Clean up stream pointers for next request
  upstream_request_.reset();

  // --- Request 2: Cache Hit ---
  auto response2 = codec_client_->makeHeaderOnlyRequest(headers);

  // It should bypass authz and go straight to backend
  result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
  ASSERT_TRUE(result) << result.message();
  result = upstream_request_->waitForEndStream(*dispatcher_);
  ASSERT_TRUE(result) << result.message();

  // Verify backend received the mutated header (from cache!)
  EXPECT_THAT(upstream_request_->headers(), ContainsHeader("x-authed-header", "auth-value"));

  // Send response from backend
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  // Client receives response
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("200", response2->headers().getStatusValue());

  // Verify stats: 2 OKs total
  uint64_t ok_counter = test_server_->counter("http.config_test.ext_authz.ok")->value();
  EXPECT_EQ(2U, ok_counter);
}

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
