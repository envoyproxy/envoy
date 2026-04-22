#pragma once

#include <functional>
#include <string>

#include "source/common/common/utility.h"
#include "source/extensions/filters/http/cache_v2/cache_headers_utils.h"
#include "source/extensions/filters/http/cache_v2/http_cache.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

// Delegate class for holding the cache. Needed because TEST_P can't be used
// with an abstract test fixture, even if the tests are only instantiated with
// concrete subclasses.
class HttpCacheTestDelegate {
public:
  virtual ~HttpCacheTestDelegate() = default;

  virtual void setUp() {}
  virtual void tearDown() {}

  virtual HttpCache& cache() PURE;

  // May be overridden to, for example, also drain other threads into the dispatcher
  // before draining the dispatcher.
  virtual void beforePumpingDispatcher() {};
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

  HttpCache& cache() const { return delegate_->cache(); }
  void pumpIntoDispatcher() { delegate_->beforePumpingDispatcher(); }
  void pumpDispatcher() { delegate_->pumpDispatcher(); }
  LookupResult lookup(absl::string_view request_path);

  virtual CacheReaderPtr
  insert(Key key, const Http::TestResponseHeaderMapImpl& headers, const absl::string_view body,
         const absl::optional<Http::TestResponseTrailerMapImpl> trailers = absl::nullopt);

  CacheReaderPtr
  insert(absl::string_view request_path, const Http::TestResponseHeaderMapImpl& headers,
         const absl::string_view body,
         const absl::optional<Http::TestResponseTrailerMapImpl> trailers = absl::nullopt);

  std::pair<std::string, EndStream> getBody(CacheReader& reader, uint64_t start, uint64_t end);

  void evict(absl::string_view request_path);

  void updateHeaders(absl::string_view request_path,
                     const Http::TestResponseHeaderMapImpl& response_headers,
                     const ResponseMetadata& metadata);

  Key simpleKey(absl::string_view request_path) const;
  LookupRequest makeLookupRequest(absl::string_view request_path) const;

  std::unique_ptr<HttpCacheTestDelegate> delegate_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Event::SimulatedTimeSystem time_system_;
  Event::Dispatcher& dispatcher() const { return delegate_->dispatcher(); }
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
};

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
