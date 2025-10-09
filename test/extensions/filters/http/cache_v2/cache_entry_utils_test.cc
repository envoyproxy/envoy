#include "source/extensions/filters/http/cache_v2/cache_entry_utils.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace {

TEST(Coverage, CacheEntryStatusString) {
  EXPECT_EQ(cacheEntryStatusString(CacheEntryStatus::Hit), "Hit");
  EXPECT_EQ(cacheEntryStatusString(CacheEntryStatus::Follower), "Follower");
  EXPECT_EQ(cacheEntryStatusString(CacheEntryStatus::Miss), "Miss");
  EXPECT_EQ(cacheEntryStatusString(CacheEntryStatus::Uncacheable), "Uncacheable");
  EXPECT_EQ(cacheEntryStatusString(CacheEntryStatus::Validated), "Validated");
  EXPECT_EQ(cacheEntryStatusString(CacheEntryStatus::ValidatedFree), "ValidatedFree");
  EXPECT_EQ(cacheEntryStatusString(CacheEntryStatus::FailedValidation), "FailedValidation");
  EXPECT_EQ(cacheEntryStatusString(CacheEntryStatus::FoundNotModified), "FoundNotModified");
  EXPECT_EQ(cacheEntryStatusString(CacheEntryStatus::LookupError), "LookupError");
  EXPECT_EQ(cacheEntryStatusString(CacheEntryStatus::UpstreamReset), "UpstreamReset");
  EXPECT_ENVOY_BUG(cacheEntryStatusString(static_cast<CacheEntryStatus>(99)),
                   "Unexpected CacheEntryStatus");
}

TEST(Coverage, CacheEntryStatusStream) {
  std::ostringstream stream;
  stream << CacheEntryStatus::Hit;
  EXPECT_EQ(stream.str(), "Hit");
}

TEST(CacheEntryUtils, ApplyHeaderUpdateReplacesMultiValues) {
  Http::TestResponseHeaderMapImpl headers{
      {"test_header", "test_value"},
      {"second_header", "second_value"},
      {"second_header", "additional_value"},
  };
  Http::TestResponseHeaderMapImpl new_headers{
      {"second_header", "new_second_value"},
  };
  applyHeaderUpdate(new_headers, headers);
  Http::TestResponseHeaderMapImpl expected{
      {"test_header", "test_value"},
      {"second_header", "new_second_value"},
  };
  EXPECT_THAT(&headers, HeaderMapEqualIgnoreOrder(&expected));
}

TEST(CacheEntryUtils, ApplyHeaderUpdateAppliesMultiValues) {
  Http::TestResponseHeaderMapImpl headers{
      {"test_header", "test_value"},
      {"second_header", "second_value"},
  };
  Http::TestResponseHeaderMapImpl new_headers{
      {"second_header", "new_second_value"},
      {"second_header", "another_new_second_value"},
  };
  applyHeaderUpdate(new_headers, headers);
  Http::TestResponseHeaderMapImpl expected{
      {"test_header", "test_value"},
      {"second_header", "new_second_value"},
      {"second_header", "another_new_second_value"},
  };
  EXPECT_THAT(&headers, HeaderMapEqualIgnoreOrder(&expected));
}

TEST(CacheEntryUtils, ApplyHeaderUpdateIgnoresIgnoredValues) {
  Http::TestResponseHeaderMapImpl headers{
      {"test_header", "test_value"}, {"etag", "original_etag"}, {"content-length", "123456"},
      {"content-range", "654321"},   {"vary", "original_vary"},
  };
  Http::TestResponseHeaderMapImpl new_headers{
      {"etag", "updated_etag"},
      {"content-length", "999999"},
      {"content-range", "999999"},
      {"vary", "updated_vary"},
  };
  applyHeaderUpdate(new_headers, headers);
  Http::TestResponseHeaderMapImpl expected{
      {"test_header", "test_value"}, {"etag", "original_etag"}, {"content-length", "123456"},
      {"content-range", "654321"},   {"vary", "original_vary"},
  };
  EXPECT_THAT(&headers, HeaderMapEqualIgnoreOrder(&expected));
}

TEST(CacheEntryUtils, ApplyHeaderUpdateCorrectlyMixesOverwriteIgnoreAddAndPersist) {
  Http::TestResponseHeaderMapImpl headers{
      {"persisted_header", "1"},
      {"persisted_header", "2"},
      {"overwritten_header", "old"},
  };
  Http::TestResponseHeaderMapImpl new_headers{
      {"overwritten_header", "new"},
      {"added_header", "also_new"},
      {"etag", "ignored"},
  };
  applyHeaderUpdate(new_headers, headers);
  Http::TestResponseHeaderMapImpl expected{
      {"persisted_header", "1"},
      {"persisted_header", "2"},
      {"overwritten_header", "new"},
      {"added_header", "also_new"},
  };
  EXPECT_THAT(&headers, HeaderMapEqualIgnoreOrder(&expected));
}

} // namespace
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
