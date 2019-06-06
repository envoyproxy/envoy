#include "extensions/filters/http/cache/http_cache.h"

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

using Event::SimulatedTimeSystem;
using Http::HeaderMapPtr;
using Http::makeHeaderMap;
using Http::TestHeaderMapImpl;

class LookupRequestTest : public testing::Test {
protected:
  SimulatedTimeSystem time_source_;
  SystemTime current_time_ = time_source_.systemTime();
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  const TestHeaderMapImpl request_headers_{
      {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"}};
  const LookupRequest lookup_request_{request_headers_, current_time_};
};

TEST_F(LookupRequestTest, makeLookupResult) {
  HeaderMapPtr response_headers = makeHeaderMap(
      {{"date", formatter_.fromTime(current_time_)}, {"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response =
      lookup_request_.makeLookupResult(std::move(response_headers), 0);
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status)
      << formatter_.fromTime(current_time_);
}

TEST_F(LookupRequestTest, PrivateResponse) {
  HeaderMapPtr response_headers = makeHeaderMap({{"age", "2"},
                                                 {"cache-control", "private, max-age=3600"},
                                                 {"date", formatter_.fromTime(current_time_)}});
  const LookupResult lookup_response =
      lookup_request_.makeLookupResult(std::move(response_headers), 0);

  // We must make sure at cache insertion time, private responses must not be
  // inserted. However, if the insertion did happen, it would be served at the
  // time of lookup. (Nothing should rely on this.)
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status)
      << formatter_.fromTime(current_time_);
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
