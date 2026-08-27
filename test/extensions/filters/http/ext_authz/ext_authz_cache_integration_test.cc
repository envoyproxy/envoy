#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
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
#include "absl/synchronization/mutex.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

struct SimpleInMemoryCacheStorage {
  absl::Mutex mutex;
  absl::flat_hash_map<std::string, Filters::Common::ExtAuthz::ResponseSharedPtr>
      map ABSL_GUARDED_BY(mutex);
  bool hold_lookup ABSL_GUARDED_BY(mutex){false};
  uint32_t cancel_count ABSL_GUARDED_BY(mutex){0};
  uint32_t lookup_count ABSL_GUARDED_BY(mutex){0};

  void setHoldLookup(bool hold) {
    absl::MutexLock lock(&mutex);
    hold_lookup = hold;
  }

  uint32_t cancelCount() {
    absl::MutexLock lock(&mutex);
    return cancel_count;
  }

  uint32_t lookupCount() {
    absl::MutexLock lock(&mutex);
    return lookup_count;
  }

  bool waitForLookup(absl::Duration timeout = absl::Seconds(5)) {
    auto predicate = [this]() ABSL_SHARED_LOCKS_REQUIRED(mutex) {
      mutex.AssertHeld();
      return lookup_count > 0;
    };
    absl::MutexLock lock(&mutex);
    return mutex.AwaitWithTimeout(absl::Condition(&predicate), timeout);
  }

  bool waitForCancel(absl::Duration timeout = absl::Seconds(5)) {
    auto predicate = [this]() ABSL_SHARED_LOCKS_REQUIRED(mutex) {
      mutex.AssertHeld();
      return cancel_count > 0;
    };
    absl::MutexLock lock(&mutex);
    return mutex.AwaitWithTimeout(absl::Condition(&predicate), timeout);
  }
};

class SimpleLookupRequest : public AuthCacheSession::LookupRequest {
public:
  SimpleLookupRequest(Event::Dispatcher& dispatcher, AuthCacheSession::LookupCallback cb,
                      Filters::Common::ExtAuthz::ResponseSharedPtr response,
                      std::shared_ptr<SimpleInMemoryCacheStorage> storage, bool hold_lookup)
      : cb_(std::move(cb)), response_(std::move(response)), storage_(std::move(storage)) {
    if (!hold_lookup) {
      timer_ = dispatcher.createTimer([this]() {
        if (!cancelled_) {
          cb_(std::move(response_));
        }
      });
      timer_->enableTimer(std::chrono::milliseconds(0));
    }
  }

  ~SimpleLookupRequest() override {
    if (timer_ != nullptr) {
      timer_->disableTimer();
      timer_.reset();
    }
  }

  void cancel() override {
    cancelled_ = true;
    if (timer_ != nullptr) {
      timer_->disableTimer();
      timer_.reset();
    }
    absl::MutexLock lock(&storage_->mutex);
    storage_->cancel_count++;
  }

private:
  AuthCacheSession::LookupCallback cb_;
  Filters::Common::ExtAuthz::ResponseSharedPtr response_;
  std::shared_ptr<SimpleInMemoryCacheStorage> storage_;
  Event::TimerPtr timer_;
  bool cancelled_{false};
};

class SimpleInMemoryCacheSession : public AuthCacheSession {
public:
  SimpleInMemoryCacheSession(std::shared_ptr<SimpleInMemoryCacheStorage> storage, bool async_lookup)
      : storage_(std::move(storage)), async_lookup_(async_lookup) {}

  LookupRequest* lookup(Http::StreamDecoderFilterCallbacks& decoder_callbacks,
                        const RequestAttributes& attributes, LookupCallback&& cb) override {
    current_key_ = std::string(attributes.headers_.getPathValue());
    if (current_key_.empty()) {
      if (async_lookup_) {
        bool hold = false;
        {
          absl::MutexLock lock(&storage_->mutex);
          hold = storage_->hold_lookup;
        }
        active_request_ = std::make_unique<SimpleLookupRequest>(
            decoder_callbacks.dispatcher(), std::move(cb), nullptr, storage_, hold);
        return active_request_.get();
      }
      cb(nullptr);
      return nullptr;
    }

    Filters::Common::ExtAuthz::ResponseSharedPtr response = nullptr;
    bool hold = false;
    {
      absl::MutexLock lock(&storage_->mutex);
      storage_->lookup_count++;
      auto it = storage_->map.find(current_key_);
      if (it != storage_->map.end()) {
        response = it->second;
      }
      hold = storage_->hold_lookup;
    }

    if (async_lookup_) {
      active_request_ = std::make_unique<SimpleLookupRequest>(
          decoder_callbacks.dispatcher(), std::move(cb), std::move(response), storage_, hold);
      return active_request_.get();
    }

    cb(std::move(response));
    return nullptr;
  }

  void insert(const Filters::Common::ExtAuthz::Response& response) override {
    if (current_key_.empty()) {
      return;
    }

    absl::MutexLock lock(&storage_->mutex);
    storage_->map[current_key_] = std::make_shared<Filters::Common::ExtAuthz::Response>(response);
  }

private:
  std::shared_ptr<SimpleInMemoryCacheStorage> storage_;
  const bool async_lookup_;
  std::string current_key_;
  std::unique_ptr<SimpleLookupRequest> active_request_;
};

class SimpleInMemoryCache : public AuthCache {
public:
  SimpleInMemoryCache(bool async_lookup, std::shared_ptr<SimpleInMemoryCacheStorage> storage)
      : storage_(std::move(storage)), async_lookup_(async_lookup) {}

  AuthCacheSessionPtr createSession() override {
    return std::make_unique<SimpleInMemoryCacheSession>(storage_, async_lookup_);
  }

  void clear() {
    absl::MutexLock lock(&storage_->mutex);
    storage_->map.clear();
  }

private:
  std::shared_ptr<SimpleInMemoryCacheStorage> storage_;
  const bool async_lookup_;
};

class SimpleInMemoryCacheFactory : public AuthCacheFactory {
public:
  SimpleInMemoryCacheFactory(bool async_lookup, std::shared_ptr<SimpleInMemoryCacheStorage> storage)
      : async_lookup_(async_lookup), storage_(std::move(storage)) {}

  AuthCachePtr createAuthCache(const Protobuf::Message&,
                               Server::Configuration::ServerFactoryContext&) override {
    return std::make_unique<SimpleInMemoryCache>(async_lookup_, storage_);
  }

  std::string name() const override {
    return "envoy.filters.http.ext_authz.cache.simple_in_memory";
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

private:
  const bool async_lookup_;
  std::shared_ptr<SimpleInMemoryCacheStorage> storage_;
};

class ExtAuthzCacheIntegrationTest : public HttpIntegrationTest,
                                     public testing::TestWithParam<bool> {
public:
  ExtAuthzCacheIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, Network::Address::IpVersion::v4),
        storage_(std::make_shared<SimpleInMemoryCacheStorage>()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Add fake upstream for ext_authz (gRPC)
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void initialize() override {
    cache_factory_ = std::make_unique<SimpleInMemoryCacheFactory>(GetParam(), storage_);
    inject_cache_factory_ =
        std::make_unique<Registry::InjectFactory<AuthCacheFactory>>(*cache_factory_);

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
      EXPECT_TRUE(cache_config->mutable_typed_config()->PackFrom(Protobuf::Struct()));

      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter ext_authz_filter;
      ext_authz_filter.set_name("envoy.filters.http.ext_authz");
      EXPECT_TRUE(ext_authz_filter.mutable_typed_config()->PackFrom(ext_authz_proto));

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

  std::shared_ptr<SimpleInMemoryCacheStorage> storage_;
  std::unique_ptr<SimpleInMemoryCacheFactory> cache_factory_;
  std::unique_ptr<Registry::InjectFactory<AuthCacheFactory>> inject_cache_factory_;

  FakeHttpConnectionPtr fake_ext_authz_connection_;
  FakeStreamPtr ext_authz_request_;
};

INSTANTIATE_TEST_SUITE_P(CacheLookupMode, ExtAuthzCacheIntegrationTest, testing::Bool(),
                         [](const ::testing::TestParamInfo<bool>& params) {
                           return params.param ? "AsyncLookup" : "SyncLookup";
                         });

TEST_P(ExtAuthzCacheIntegrationTest, CacheMissThenHit) {
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
  // Verify cluster stats: only incremented on cache miss, not on cache hit.
  uint64_t cluster_ok_counter = test_server_->counter("cluster.cluster_0.ext_authz.ok")->value();
  EXPECT_EQ(1U, cluster_ok_counter);
}

TEST_P(ExtAuthzCacheIntegrationTest, CancelledLookup) {
  if (!GetParam()) {
    return; // Cancellation only applies to asynchronous cache lookups.
  }

  storage_->setHoldLookup(true);
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/cancel-me"}, {":scheme", "http"}, {":authority", "host"}};
  auto encoder_decoder = codec_client_->startRequest(headers);

  // Wait until the cache lookup has started in ext_authz filter.
  ASSERT_TRUE(storage_->waitForLookup());

  // Reset the downstream stream while cache lookup is in progress.
  codec_client_->sendReset(encoder_decoder.first);

  // Verify that the cache lookup was cancelled.
  ASSERT_TRUE(storage_->waitForCancel());
  EXPECT_EQ(1U, storage_->cancelCount());

  // Verify stats: no OK or Denied responses recorded.
  EXPECT_EQ(0U, test_server_->counter("http.config_test.ext_authz.ok")->value());
  EXPECT_EQ(0U, test_server_->counter("http.config_test.ext_authz.denied")->value());

  codec_client_->close();
}

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
