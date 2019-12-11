#include "extensions/filters/http/cache/http_cache.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
TEST(RawByteRangeTest, isSuffix) {
  auto r = RawByteRange(UINT64_MAX, 4);
  ASSERT_TRUE(r.isSuffix());
}

TEST(RawByteRangeTest, IsNotSuffix) {
  auto r = RawByteRange(3, 4);
  ASSERT_FALSE(r.isSuffix());
}

TEST(RawByteRangeTest, FirstBytePos) {
  auto r = RawByteRange(3, 4);
  ASSERT_EQ(3, r.firstBytePos());
}

TEST(RawByteRangeTest, LastBytePos) {
  auto r = RawByteRange(3, 4);
  ASSERT_EQ(4, r.lastBytePos());
}

TEST(RawByteRangeTest, suffixLength) {
  auto r = RawByteRange(UINT64_MAX, 4);
  ASSERT_EQ(4, r.suffixLength());
}

TEST(AdjustedByteRangeTest, Length) {
  auto a = AdjustedByteRange(3, 6);
  ASSERT_EQ(3, a.length());
}

TEST(AdjustedByteRangeTest, TrimFront) {
  auto a = AdjustedByteRange(3, 6);
  a.trimFront(2);
  ASSERT_EQ(5, a.begin());
}

TEST(AdjustedByteRangeTest, MaxLength) {
  auto a = AdjustedByteRange(0, UINT64_MAX);
  ASSERT_EQ(UINT64_MAX, a.length());
}

TEST(AdjustedByteRangeTest, MaxTrim) {
  auto a = AdjustedByteRange(0, UINT64_MAX);
  a.trimFront(UINT64_MAX);
  ASSERT_EQ(0, a.length());
}

class LookupRequestTest : public testing::Test {
protected:
  Event::SimulatedTimeSystem time_source_;
  SystemTime current_time_ = time_source_.systemTime();
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  Http::TestHeaderMapImpl request_headers_{
      {":path", "/"}, {"x-forwarded-proto", "https"}, {":authority", "example.com"}};
};

LookupResult makeLookupResult(const LookupRequest& lookup_request,
                              const Http::TestHeaderMapImpl& response_headers,
                              uint64_t content_length = 0) {
  return lookup_request.makeLookupResult(
      std::make_unique<Http::TestHeaderMapImpl>(response_headers), content_length);
}

TEST_F(LookupRequestTest, makeLookupResultNoBody) {
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(current_time_)}, {"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  ASSERT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status);
  ASSERT_TRUE(lookup_response.headers);
  EXPECT_THAT(*lookup_response.headers, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length, 0);
  EXPECT_TRUE(lookup_response.response_ranges.empty());
  EXPECT_FALSE(lookup_response.has_trailers);
}

TEST_F(LookupRequestTest, makeLookupResultBody) {
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(current_time_)}, {"cache-control", "public, max-age=3600"}});
  const uint64_t content_length = 5;
  const LookupResult lookup_response =
      makeLookupResult(lookup_request, response_headers, content_length);
  ASSERT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status);
  ASSERT_TRUE(lookup_response.headers);
  EXPECT_THAT(*lookup_response.headers, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length, content_length);
  EXPECT_TRUE(lookup_response.response_ranges.empty());
  EXPECT_FALSE(lookup_response.has_trailers);
}

TEST_F(LookupRequestTest, makeLookupResultNoDate) {
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestHeaderMapImpl response_headers({{"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status);
  ASSERT_TRUE(lookup_response.headers);
  EXPECT_THAT(*lookup_response.headers, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length, 0);
  EXPECT_TRUE(lookup_response.response_ranges.empty());
  EXPECT_FALSE(lookup_response.has_trailers);
}

TEST_F(LookupRequestTest, PrivateResponse) {
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestHeaderMapImpl response_headers({{"age", "2"},
                                                  {"cache-control", "private, max-age=3600"},
                                                  {"date", formatter_.fromTime(current_time_)}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  // We must make sure at cache insertion time, private responses must not be
  // inserted. However, if the insertion did happen, it would be served at the
  // time of lookup. (Nothing should rely on this.)
  ASSERT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status);
  ASSERT_TRUE(lookup_response.headers);
  EXPECT_THAT(*lookup_response.headers, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length, 0);
  EXPECT_TRUE(lookup_response.response_ranges.empty());
  EXPECT_FALSE(lookup_response.has_trailers);
}

TEST_F(LookupRequestTest, Expired) {
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestHeaderMapImpl response_headers(
      {{"cache-control", "public, max-age=3600"}, {"date", "Thu, 01 Jan 2019 00:00:00 GMT"}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status);
  ASSERT_TRUE(lookup_response.headers);
  EXPECT_THAT(*lookup_response.headers, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length, 0);
  EXPECT_TRUE(lookup_response.response_ranges.empty());
  EXPECT_FALSE(lookup_response.has_trailers);
}

TEST_F(LookupRequestTest, ExpiredViaFallbackheader) {
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestHeaderMapImpl response_headers(
      {{"expires", formatter_.fromTime(current_time_ - std::chrono::seconds(5))},
       {"date", formatter_.fromTime(current_time_)}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status);
}

TEST_F(LookupRequestTest, NotExpiredViaFallbackheader) {
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestHeaderMapImpl response_headers(
      {{"expires", formatter_.fromTime(current_time_ + std::chrono::seconds(5))},
       {"date", formatter_.fromTime(current_time_)}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status);
}

TEST_F(LookupRequestTest, FullRange) {
  request_headers_.addCopy("Range", "0-99");
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestHeaderMapImpl response_headers({{"date", formatter_.fromTime(current_time_)},
                                                  {"cache-control", "public, max-age=3600"},
                                                  {"content-length", "4"}});
  const uint64_t content_length = 4;
  const LookupResult lookup_response =
      makeLookupResult(lookup_request, response_headers, content_length);
  ASSERT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status);
  ASSERT_TRUE(lookup_response.headers);
  EXPECT_THAT(*lookup_response.headers, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length, 4);
  EXPECT_TRUE(lookup_response.response_ranges.empty());
  EXPECT_FALSE(lookup_response.has_trailers);
}

TEST(AdjustByteRangeSet, FullRange) {
  std::vector<AdjustedByteRange> result;
  EXPECT_TRUE(adjustByteRangeSet(result, {{0, 99}}, 4));
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(result[0].length(), 4);
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
