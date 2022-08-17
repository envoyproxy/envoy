#include "source/extensions/filters/http/cache/cache_filter_logging_info.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

TEST(Coverage, LookupStatusToString) {
  EXPECT_EQ(lookupStatusToString(LookupStatus::Unknown), "Unknown");
  EXPECT_EQ(lookupStatusToString(LookupStatus::CacheHit), "CacheHit");
  EXPECT_EQ(lookupStatusToString(LookupStatus::CacheMiss), "CacheMiss");
  EXPECT_EQ(lookupStatusToString(LookupStatus::StaleHitWithSuccessfulValidation),
            "StaleHitWithSuccessfulValidation");
  EXPECT_EQ(lookupStatusToString(LookupStatus::StaleHitWithFailedValidation),
            "StaleHitWithFailedValidation");
  EXPECT_EQ(lookupStatusToString(LookupStatus::NotModifiedHit), "NotModifiedHit");
  EXPECT_EQ(lookupStatusToString(LookupStatus::RequestNotCacheable), "RequestNotCacheable");
  EXPECT_EQ(lookupStatusToString(LookupStatus::RequestIncomplete), "RequestIncomplete");
  EXPECT_EQ(lookupStatusToString(LookupStatus::LookupError), "LookupError");
  EXPECT_ENVOY_BUG(lookupStatusToString(static_cast<LookupStatus>(99)), "Unexpected LookupStatus");
}

TEST(Coverage, InsertStatusToString) {
  EXPECT_EQ(insertStatusToString(InsertStatus::InsertSucceeded), "InsertSucceeded");
  EXPECT_EQ(insertStatusToString(InsertStatus::InsertAbortedByCache), "InsertAbortedByCache");
  EXPECT_EQ(insertStatusToString(InsertStatus::InsertAbortedCacheCongested),
            "InsertAbortedCacheCongested");
  EXPECT_EQ(insertStatusToString(InsertStatus::InsertAbortedResponseIncomplete),
            "InsertAbortedResponseIncomplete");
  EXPECT_EQ(insertStatusToString(InsertStatus::HeaderUpdate), "HeaderUpdate");
  EXPECT_EQ(insertStatusToString(InsertStatus::NoInsertCacheHit), "NoInsertCacheHit");
  EXPECT_EQ(insertStatusToString(InsertStatus::NoInsertRequestNotCacheable),
            "NoInsertRequestNotCacheable");
  EXPECT_EQ(insertStatusToString(InsertStatus::NoInsertResponseNotCacheable),
            "NoInsertResponseNotCacheable");
  EXPECT_EQ(insertStatusToString(InsertStatus::NoInsertRequestIncomplete),
            "NoInsertRequestIncomplete");
  EXPECT_EQ(insertStatusToString(InsertStatus::NoInsertResponseValidatorsMismatch),
            "NoInsertResponseValidatorsMismatch");
  EXPECT_EQ(insertStatusToString(InsertStatus::NoInsertResponseVaryMismatch),
            "NoInsertResponseVaryMismatch");
  EXPECT_EQ(insertStatusToString(InsertStatus::NoInsertResponseVaryDisallowed),
            "NoInsertResponseVaryDisallowed");
  EXPECT_EQ(insertStatusToString(InsertStatus::NoInsertLookupError), "NoInsertLookupError");
  EXPECT_ENVOY_BUG(insertStatusToString(static_cast<InsertStatus>(99)), "Unexpected InsertStatus");
}

TEST(Coverage, StatusStream) {
  std::ostringstream stream;
  stream << LookupStatus::Unknown;
  EXPECT_EQ(stream.str(), "Unknown");
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
