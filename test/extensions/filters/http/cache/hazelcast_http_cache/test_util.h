#pragma once

#include "extensions/filters/http/cache/hazelcast_http_cache/util.h"

#include "test/extensions/filters/http/cache/hazelcast_http_cache/test_accessors.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

class HazelcastTestUtil {
public:
  static constexpr int TEST_PARTITION_SIZE = 10;
  static constexpr int TEST_MAX_BODY_SIZE = TEST_PARTITION_SIZE * 20;

  static Runtime::RandomGeneratorImpl& randomGenerator() {
    static Runtime::RandomGeneratorImpl rand;
    return rand;
  }

  static const std::string& abortedBodyResponse() {
    static std::string response("NULL_BODY");
    return response;
  }

  static envoy::extensions::filters::http::cache::v3alpha::CacheConfig getTestCacheConfig() {
    envoy::extensions::filters::http::cache::v3alpha::CacheConfig cache_config;
    cache_config.set_max_body_bytes(TEST_MAX_BODY_SIZE);
    return cache_config;
  }

  static HazelcastHttpCacheConfig getTestTypedConfig(bool unified) {
    HazelcastHttpCacheConfig typed_config;
    typed_config.set_group_name("dev");
    typed_config.set_group_password("dev-pass");
    envoy::config::core::v3::SocketAddress* member_address = typed_config.add_addresses();
    member_address->set_address("127.0.0.1");
    member_address->set_port_value(5701);
    typed_config.set_invocation_timeout(1);
    typed_config.set_body_partition_size(TEST_PARTITION_SIZE);
    // During parallel tests, if caches do not have different prefixes, the entries
    // and hence the results will be different than the expected.
    typed_config.set_app_prefix(randomGenerator().uuid());
    typed_config.set_unified(unified);
    return typed_config;
  }

  static StorageAccessorPtr getTestRemoteAccessor(HazelcastHttpCache& cache) {
    HazelcastHttpCacheConfig typed_config = getTestTypedConfig(cache.unified());
    ClientConfig client_config = ConfigUtil::getClientConfig(typed_config);
    StorageAccessorPtr accessor = std::make_unique<RemoteTestAccessor>(
        cache, std::move(client_config), cache.prefix(), cache.bodySizePerEntry());
    return accessor;
  }

  static void setRequestHeaders(Http::TestRequestHeaderMapImpl& headers) {
    headers.setMethod("GET");
    headers.setHost("hazelcast.com");
    headers.setForwardedProto("https");
    headers.setCacheControl("max-age=3600");
  }
};

/**
 * The base test environment for DIVIDED and UNIFIED cache mode tests.
 *
 * A similar environment to SimpleHttpCacheTest is applied and
 * some functions & fields are derived directly.
 *
 */
class HazelcastHttpCacheTestBase : public testing::Test {
protected:
  HazelcastHttpCacheTestBase() { HazelcastTestUtil::setRequestHeaders(request_headers_); }

  int64_t mapKey(const uint64_t key_hash) { return cache_->mapKey(key_hash); }

  TestAccessor& getTestAccessor() { return dynamic_cast<TestAccessor&>(*cache_->accessor_); }

  std::string orderedMapKey(const uint64_t key_hash, const uint64_t order) {
    return cache_->orderedMapKey(key_hash, order);
  }

  // Makes getBody requests until requested range is satisfied.
  // Returns the body on success; HazelcastTestUtil::abortedBodyResponse() on
  // abortion by cache.
  std::string getBody(LookupContext& context, uint64_t start, uint64_t end) {
    std::string full_body, body_chunk;
    uint64_t offset = start;
    bool aborted = false;
    while (full_body.length() != end - start) {
      if (aborted) {
        return HazelcastTestUtil::abortedBodyResponse();
      }
      AdjustedByteRange range(offset, end);
      context.getBody(range,
                      [&aborted, &body_chunk, &offset, &full_body](Buffer::InstancePtr&& data) {
                        if (data) {
                          body_chunk = data->toString();
                          full_body.append(body_chunk);
                          offset += body_chunk.length();
                        } else {
                          aborted = true;
                        }
                      });
    }
    return full_body;
  }

  Http::TestResponseHeaderMapImpl getResponseHeaders() {
    return Http::TestResponseHeaderMapImpl{{"date", formatter_.fromTime(current_time_)},
                                           {"cache-control", "public, max-age=3600"}};
  }

  /// from SimpleHttpCacheTest

  LookupContextPtr lookup(absl::string_view request_path) {
    LookupRequest request = makeLookupRequest(request_path);
    LookupContextPtr context = cache_->makeLookupContext(std::move(request));
    context->getHeaders([this](LookupResult&& result) { lookup_result_ = std::move(result); });
    return context;
  }

  void insert(LookupContextPtr lookup, const Http::TestResponseHeaderMapImpl& response_headers,
              const absl::string_view response_body) {
    InsertContextPtr insert_context = cache_->makeInsertContext(move(lookup));
    insert_context->insertHeaders(response_headers, response_body == nullptr);
    if (response_body == nullptr) {
      return;
    }
    insert_context->insertBody(Buffer::OwnedImpl(response_body), nullptr, true);
  }

  void insert(absl::string_view request_path,
              const Http::TestResponseHeaderMapImpl& response_headers,
              const absl::string_view response_body) {
    insert(lookup(request_path), response_headers, response_body);
  }

  LookupRequest makeLookupRequest(absl::string_view request_path) {
    request_headers_.setPath(request_path);
    return LookupRequest(request_headers_, current_time_);
  }

  AssertionResult expectLookupSuccessWithFullBody(LookupContext* lookup_context,
                                                  absl::string_view body) {
    if (lookup_result_.content_length_ != body.size()) {
      return AssertionFailure() << "Expected: lookup_result_.content_length_"
                                   " == "
                                << body.size() << "\n  Actual: " << lookup_result_.content_length_;
    }
    // From SimpleHttpCacheTest
    if (lookup_result_.cache_entry_status_ != CacheEntryStatus::Ok) {
      return AssertionFailure() << "Expected: lookup_result_.cache_entry_status"
                                   " == CacheEntryStatus::Ok\n  Actual: "
                                << lookup_result_.cache_entry_status_;
    }
    if (!lookup_result_.headers_) {
      return AssertionFailure() << "Expected nonnull lookup_result_.headers";
    }
    if (!lookup_context) {
      return AssertionFailure() << "Expected nonnull lookup_context";
    }
    const std::string actual_body = getBody(*lookup_context, 0, body.size());
    if (body != actual_body) {
      return AssertionFailure() << "Expected body == " << body << "\n  Actual:  " << actual_body;
    }
    return AssertionSuccess();
  }

  std::unique_ptr<HazelcastHttpCache> cache_;
  LookupResult lookup_result_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Event::SimulatedTimeSystem time_source_;
  SystemTime current_time_ = time_source_.systemTime();
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
};

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
