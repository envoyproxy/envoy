#pragma once

#include <functional>
#include <string>

#include "source/common/common/utility.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/filters/http/cache/http_cache.h"

#include "test/extensions/filters/http/cache/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

// Delegate class for holding the cache. Needed because TEST_P can't be used
// with an abstract test fixture, even if the tests are only instantiated with
// concrete subclasses.
class HttpCacheTestDelegate {
public:
  virtual ~HttpCacheTestDelegate() = default;

  virtual void setUp(Event::MockDispatcher& dispatcher) { dispatcher_ = &dispatcher; }
  virtual void tearDown() {}
  virtual std::shared_ptr<HttpCache> cache() = 0;

  // Specifies whether or not the cache supports validating stale cache entries
  // and updating their headers. If false, tests will expect the cache to return
  // CacheEntryStatus::Unusable for stale entries, instead of
  // RequiresValidation.
  virtual bool validationEnabled() const = 0;

  Event::MockDispatcher& dispatcher() { return *dispatcher_; }

private:
  Event::MockDispatcher* dispatcher_ = nullptr;
};

class HttpCacheImplementationTest
    : public testing::TestWithParam<std::function<std::unique_ptr<HttpCacheTestDelegate>()>> {
public:
  static constexpr absl::Duration kLastValidUpdateMinInterval = absl::Seconds(10);

protected:
  HttpCacheImplementationTest();
  ~HttpCacheImplementationTest() override;

  std::shared_ptr<HttpCache> cache() const { return delegate_->cache(); }
  bool validationEnabled() const { return delegate_->validationEnabled(); }
  LookupContextPtr lookup(absl::string_view request_path);

  absl::Status insert(LookupContextPtr lookup, const Http::TestResponseHeaderMapImpl& headers,
                      const absl::string_view body,
                      std::chrono::milliseconds timeout = std::chrono::seconds(1));

  virtual absl::Status insert(LookupContextPtr lookup,
                              const Http::TestResponseHeaderMapImpl& headers,
                              const absl::string_view body,
                              const absl::optional<Http::TestResponseTrailerMapImpl> trailers,
                              std::chrono::milliseconds timeout = std::chrono::seconds(1));

  absl::Status insert(absl::string_view request_path,
                      const Http::TestResponseHeaderMapImpl& headers, const absl::string_view body,
                      std::chrono::milliseconds timeout = std::chrono::seconds(1));

  Http::ResponseHeaderMapPtr getHeaders(LookupContext& context);

  std::string getBody(LookupContext& context, uint64_t start, uint64_t end);

  Http::TestResponseTrailerMapImpl getTrailers(LookupContext& context);

  bool updateHeaders(absl::string_view request_path,
                     const Http::TestResponseHeaderMapImpl& response_headers,
                     const ResponseMetadata& metadata);

  LookupRequest makeLookupRequest(absl::string_view request_path);

  testing::AssertionResult
  expectLookupSuccessWithHeaders(LookupContext* lookup_context,
                                 const Http::TestResponseHeaderMapImpl& headers);
  testing::AssertionResult
  expectLookupSuccessWithBodyAndTrailers(LookupContext* lookup, absl::string_view body,
                                         Http::TestResponseTrailerMapImpl trailers = {});

  std::unique_ptr<HttpCacheTestDelegate> delegate_;
  VaryAllowList vary_allow_list_;
  LookupResult lookup_result_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Event::SimulatedTimeSystem time_system_;
  Event::MockDispatcher dispatcher_;
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
