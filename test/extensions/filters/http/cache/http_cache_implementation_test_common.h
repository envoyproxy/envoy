#pragma once

#include <functional>
#include <string>

#include "source/common/common/utility.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/filters/http/cache/http_cache.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/server_factory_context.h"
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

  virtual void setUp() {}
  virtual void tearDown() {}

  virtual std::shared_ptr<HttpCache> cache() = 0;

  // Specifies whether or not the cache supports validating stale cache entries
  // and updating their headers. If false, tests will expect the cache to return
  // CacheEntryStatus::Unusable for stale entries, instead of
  // RequiresValidation.
  virtual bool validationEnabled() const = 0;

  // May be overridden to, for example, also drain other threads into the dispatcher
  // before draining the dispatcher.
  virtual void beforePumpingDispatcher(){};
  void pumpDispatcher();

  Event::Dispatcher& dispatcher() { return *dispatcher_; }

private:
  Api::ApiPtr api_ = Api::createApiForTest();
  Event::DispatcherPtr dispatcher_ = api_->allocateDispatcher("test_thread");
};

class HttpCacheImplementationTest
    : public Event::TestUsingSimulatedTime,
      public testing::TestWithParam<std::function<std::unique_ptr<HttpCacheTestDelegate>()>> {
public:
  static constexpr absl::Duration kLastValidUpdateMinInterval = absl::Seconds(10);

protected:
  HttpCacheImplementationTest();
  ~HttpCacheImplementationTest() override;

  std::shared_ptr<HttpCache> cache() const { return delegate_->cache(); }
  bool validationEnabled() const { return delegate_->validationEnabled(); }
  void pumpIntoDispatcher() { delegate_->beforePumpingDispatcher(); }
  void pumpDispatcher() { delegate_->pumpDispatcher(); }
  LookupContextPtr lookup(absl::string_view request_path);

  virtual absl::Status
  insert(LookupContextPtr lookup, const Http::TestResponseHeaderMapImpl& headers,
         const absl::string_view body,
         const absl::optional<Http::TestResponseTrailerMapImpl> trailers = absl::nullopt);

  absl::Status insert(absl::string_view request_path,
                      const Http::TestResponseHeaderMapImpl& headers, const absl::string_view body);

  // Returns the headers and a bool for end_stream.
  std::pair<Http::ResponseHeaderMapPtr, bool> getHeaders(LookupContext& context);

  // Returns a body chunk and a bool for end_stream.
  std::pair<std::string, bool> getBody(LookupContext& context, uint64_t start, uint64_t end);

  Http::TestResponseTrailerMapImpl getTrailers(LookupContext& context);

  bool updateHeaders(absl::string_view request_path,
                     const Http::TestResponseHeaderMapImpl& response_headers,
                     const ResponseMetadata& metadata);

  LookupRequest makeLookupRequest(absl::string_view request_path);

  LookupContextPtr lookupContextWithAllParts();

  testing::AssertionResult
  expectLookupSuccessWithHeaders(LookupContext* lookup_context,
                                 const Http::TestResponseHeaderMapImpl& headers);
  testing::AssertionResult
  expectLookupSuccessWithBodyAndTrailers(LookupContext* lookup, absl::string_view body,
                                         Http::TestResponseTrailerMapImpl trailers = {});

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  std::unique_ptr<HttpCacheTestDelegate> delegate_;
  VaryAllowList vary_allow_list_;
  LookupResult lookup_result_;
  bool lookup_end_stream_after_headers_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Event::SimulatedTimeSystem time_system_;
  Event::Dispatcher& dispatcher() { return delegate_->dispatcher(); }
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
