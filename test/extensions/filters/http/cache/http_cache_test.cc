#include "extensions/filters/http/cache/http_cache.h"

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class RawByteRangeTest : public testing::Test {};


using RawByteRangeDeathTest = RawByteRangeTest;


TEST_F(RawByteRangeTest, IsSuffix) {
  auto r = RawByteRange(UINT64_MAX, 4);
  ASSERT_TRUE(r.isSuffix());
}

TEST_F(RawByteRangeTest, IsNotSuffix) {
  auto r = RawByteRange(3, 4);
  ASSERT_FALSE(r.isSuffix());
}

TEST_F(RawByteRangeTest, IllegalByteRange) {
  ASSERT_DEATH(RawByteRange(5, 4), "Illegal byte range");
}

TEST_F(RawByteRangeTest, IllegalByteRangeButIsSuffix) {
  // no death here
  RawByteRange(UINT64_MAX, 3);
  ASSERT_TRUE(true);
}

TEST_F(RawByteRangeTest, FirstBytePos) {
  auto r = RawByteRange(3, 4);
  ASSERT_EQ(3, r.firstBytePos());
}

TEST_F(RawByteRangeDeathTest, FirstBytePosIfSuffix) {
  auto r = RawByteRange(UINT64_MAX, 4);
  ASSERT_DEATH(r.firstBytePos(), "!isSuffix()");
}

TEST_F(RawByteRangeTest, LastBytePos) {
  auto r = RawByteRange(3, 4);
  ASSERT_EQ(4, r.lastBytePos());
}

TEST_F(RawByteRangeTest, LastBytePosIfSuffix) {
  auto r = RawByteRange(UINT64_MAX, UINT64_MAX);
  ASSERT_DEATH(r.lastBytePos(), "!isSuffix()");
}

TEST_F(RawByteRangeDeathTest, SuffixLengthOfNotSuffix) {
  auto r = RawByteRange(3, 4);
  ASSERT_DEATH(r.suffixLength(), "isSuffix()");
}

TEST_F(RawByteRangeTest, suffixLength) {
  auto r = RawByteRange(UINT64_MAX, 4);
  ASSERT_EQ(4, r.suffixLength());
}

class AdjustedByteRangeTest : public testing::Test {};

TEST_F(AdjustedByteRangeTest, IllegalByteRange) {
  ASSERT_DEATH(AdjustedByteRange(5, 4), "Illegal byte range.");
}

TEST_F(AdjustedByteRangeTest, Length) {
  auto a = AdjustedByteRange(3, 6);
  ASSERT_EQ(3, a.length());
}

TEST_F(AdjustedByteRangeTest, TrimFront) {
  auto a = AdjustedByteRange(3, 6);
  a.trimFront(2);
  ASSERT_EQ(5, a.firstBytePos());
}

TEST_F(AdjustedByteRangeTest, TrimFrontTooMuch) {
  auto a = AdjustedByteRange(3, 6);
  ASSERT_DEATH(a.trimFront(5), "Attempt to trim too much from range.");
}

TEST_F(AdjustedByteRangeTest, MaxLength) {
  auto a = AdjustedByteRange(0, UINT64_MAX);
  ASSERT_EQ(UINT64_MAX, a.length());
}

TEST_F(AdjustedByteRangeTest, MaxTrim) {
  auto a = AdjustedByteRange(0, UINT64_MAX);
  a.trimFront(UINT64_MAX);
  ASSERT_EQ(UINT64_MAX, a.firstBytePos());
  ASSERT_EQ(0, a.length());
}

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

TEST_F(LookupRequestTest, makeLookupResultNoDate) {
  Http::HeaderMapPtr response_headers =
      Http::makeHeaderMap({{"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response =
      lookup_request_.makeLookupResult(std::move(response_headers), 0);
  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status)
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
