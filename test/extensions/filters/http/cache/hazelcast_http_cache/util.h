#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_http_cache.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

class HazelcastTestUtil {
public:

  static constexpr int TEST_PARTITION_SIZE = 10;
  static constexpr int TEST_MAX_BODY_SIZE = TEST_PARTITION_SIZE * 10;

  static const std::string& abortedBodyResponse()
  {
    static std::string response("NULL_BODY");
    return response;
  }

  static HazelcastHttpCacheConfig getTestConfig(bool unified) {
    HazelcastHttpCacheConfig hc;
    hc.set_group_name("dev");
    hc.set_group_password("dev-pass");
    hc.set_ip("127.0.0.1");
    hc.set_port(5701);
    hc.set_body_partition_size(TEST_PARTITION_SIZE);
    hc.set_app_prefix("test");
    hc.set_unified(unified);
    hc.set_max_body_size(TEST_MAX_BODY_SIZE);
    return hc;
  }

  static void setRequestHeaders(Http::TestRequestHeaderMapImpl& headers) {
    headers.setMethod("GET");
    headers.setHost("hazelcast.com");
    headers.setForwardedProto("https");
    headers.setCacheControl("max-age=3600");
  }

};

// TODO: If running a Hazelcast server is not possible during tests,
//  this base will serve mock cache for testing.
class HazelcastHttpCacheTestBase : public testing::Test {
protected:

  HazelcastHttpCacheTestBase() {
    HazelcastTestUtil::setRequestHeaders(request_headers_);
  }

  static void TearDownTestSuite() {
    delete hz_cache_;
    hz_cache_ = nullptr;
  }

  // Performs a cache lookup.
  LookupContextPtr lookup(absl::string_view request_path) {
    LookupRequest request = makeLookupRequest(request_path);
    LookupContextPtr context = hz_cache_->makeLookupContext(std::move(request));
    context->getHeaders([this](LookupResult&& result) {lookup_result_ = std::move(result); });
    return context;
  }

  // Inserts a value into the cache.
  void insert(LookupContextPtr lookup, const Http::TestResponseHeaderMapImpl& response_headers,
              const absl::string_view response_body) {
    InsertContextPtr inserter = hz_cache_->makeInsertContext(move(lookup));
    inserter->insertHeaders(response_headers, false);
    inserter->insertBody(Buffer::OwnedImpl(response_body), nullptr, true);
  }

  void insert(absl::string_view request_path,
              const Http::TestResponseHeaderMapImpl& response_headers,
              const absl::string_view response_body) {
    insert(lookup(request_path), response_headers, response_body);
  }

  // Makes getBody requests until requested range is satisfied.
  // Returns the bod on success, HazelcastTestUtil::abortedBodyResponse() on
  // abortion by cache itself.
  std::string getBody(LookupContext& context, uint64_t start, uint64_t end) {
    std::string full_body, body_chunk;
    uint64_t offset = start;
    bool aborted = false;
    while (full_body.length() != end - start) {
      if (aborted) {
        return HazelcastTestUtil::abortedBodyResponse();
      }
      AdjustedByteRange range(offset, end);
      context.getBody(range, [&aborted, &body_chunk, &offset,
          &full_body](Buffer::InstancePtr&& data) {
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

  LookupRequest makeLookupRequest(absl::string_view request_path) {
    request_headers_.setPath(request_path);
    return LookupRequest(request_headers_, current_time_);
  }

  AssertionResult expectLookupSuccessWithBody(LookupContext* lookup_context,
                                              absl::string_view body) {
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
      return AssertionFailure() << "Expected body == " << body <<
                                "\n  Actual:  " << actual_body;
    }
    return AssertionSuccess();
  }

  Http::TestResponseHeaderMapImpl getResponseHeaders() {
    return Http::TestResponseHeaderMapImpl{
      {"date", formatter_.fromTime(current_time_)},
      {"cache-control", "public,max-age=3600"}};
  }

  static HazelcastHttpCache* hz_cache_;
  LookupResult lookup_result_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Event::SimulatedTimeSystem time_source_;
  SystemTime current_time_ = time_source_.systemTime();
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};

  // Helpers for test environment.
  static void clearMaps() {
    if (hz_cache_->unified_) {
      hz_cache_->getResponseMap().clear();
    } else {
      hz_cache_->getBodyMap().clear();
      hz_cache_->getHeaderMap().clear();
    }
  }

  void removeBody(uint64_t key, uint64_t order) {
    ASSERT(!hz_cache_->unified_);
    hz_cache_->getBodyMap().remove(hz_cache_->orderedMapKey(key, order));
  }

  IMap<int64_t, HazelcastHeaderEntry> testHeaderMap() {
    ASSERT(!hz_cache_->unified_);
    return hz_cache_->getHeaderMap();
  };

  IMap<std::string, HazelcastBodyEntry> testBodyMap() {
    ASSERT(!hz_cache_->unified_);
    return hz_cache_->getBodyMap();
  };

  IMap<int64_t, HazelcastResponseEntry> testResponseMap() {
    ASSERT(hz_cache_->unified_);
    return hz_cache_->getResponseMap();
  };

  bool isUnified() {
    return hz_cache_->unified_;
  }

  std::string getBodyKey(uint64_t key, uint64_t order){
    return hz_cache_->orderedMapKey(key, order);
  }

  int64_t mapKey(uint64_t key){
    return hz_cache_->mapKey(key);
  }
};

// Since creating the cache (connecting to the cluster),
// is not light weight, use a single cache through the test environment.
// TODO: Check for parallel tests. Using a random app_prefix per test might solve
//  the concurrent request issues. However, destroying cache is still a problem.
HazelcastHttpCache* HazelcastHttpCacheTestBase::hz_cache_ = nullptr;

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
