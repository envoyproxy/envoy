#include "extensions/filters/http/cache/http_cache.h"

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class LookupRequestTest : public testing::Test {
protected:
  Event::SimulatedTimeSystem time_source_;
  SystemTime current_time_ = time_source_.systemTime();
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  const Http::TestHeaderMapImpl request_headers_{
      {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"}};
  const LookupRequest lookup_request_{request_headers_, current_time_};
};

TEST_F(LookupRequestTest, makeLookupResult) {
  Http::HeaderMapPtr response_headers = Http::makeHeaderMap(
      {{"date", formatter_.fromTime(current_time_)}, {"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response =
      lookup_request_.makeLookupResult(std::move(response_headers), 0);
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status)
      << formatter_.fromTime(current_time_);
}

TEST_F(LookupRequestTest, PrivateResponse) {
  Http::HeaderMapPtr response_headers =
      Http::makeHeaderMap({{"age", "2"},
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

TEST_F(LookupRequestTest, Expired) {
  Http::HeaderMapPtr response_headers = Http::makeHeaderMap(
      {{"cache-control", "public, max-age=3600"}, {"date", "Thu, 01 Jan 2019 00:00:00 GMT"}});
  const LookupResult lookup_response =
      lookup_request_.makeLookupResult(std::move(response_headers), 0);

  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status);
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
