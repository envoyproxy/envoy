#include "source/common/http/filter_chain_helper.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Http {
namespace {

TEST(FilterChainUtilityTest, CreateFilterChainForFactoriesWithRouteDisabled) {
  NiceMock<MockFilterChainFactoryCallbacks> callbacks;
  FilterChainUtility::FilterFactoriesList filter_factories;
  absl::flat_hash_set<std::string> added_filters;

  for (const auto& name : {"filter_0", "filter_1", "filter_2"}) {
    auto provider =
        std::make_unique<Filter::StaticFilterConfigProviderImpl<Filter::HttpFilterFactoryCb>>(
            [name, &added_filters](FilterChainFactoryCallbacks&) { added_filters.insert(name); },
            name);
    filter_factories.push_back({std::move(provider), false});
  }

  {
    // If no filter is disabled explicitly by route, all filters should be added.
    EXPECT_CALL(callbacks, filterDisabled(_)).Times(3).WillRepeatedly(Return(absl::nullopt));
    EXPECT_CALL(callbacks, setFilterConfigName(_)).Times(3);
    FilterChainUtility::createFilterChainForFactories(callbacks, filter_factories);
    EXPECT_EQ(added_filters.size(), 3);
  }

  added_filters.clear();

  {

    EXPECT_CALL(callbacks, filterDisabled("filter_0")).WillOnce(Return(absl::make_optional(true)));
    EXPECT_CALL(callbacks, setFilterConfigName("filter_0")).Times(0);
    EXPECT_CALL(callbacks, filterDisabled("filter_1")).WillOnce(Return(absl::make_optional(false)));
    EXPECT_CALL(callbacks, setFilterConfigName("filter_1"));
    EXPECT_CALL(callbacks, filterDisabled("filter_2")).WillOnce(Return(absl::nullopt));
    EXPECT_CALL(callbacks, setFilterConfigName("filter_2"));

    // 'filter_1' and 'filter_2' should be added.
    FilterChainUtility::createFilterChainForFactories(callbacks, filter_factories);
    EXPECT_TRUE(added_filters.find("filter_1") != added_filters.end());
    EXPECT_TRUE(added_filters.find("filter_2") != added_filters.end());
    EXPECT_EQ(added_filters.size(), 2);
  }
}

TEST(FilterChainUtilityTest, CreateFilterChainForFactoriesWithRouteDisabledAndDefaultDisabled) {
  NiceMock<MockFilterChainFactoryCallbacks> callbacks;
  FilterChainUtility::FilterFactoriesList filter_factories;
  absl::flat_hash_set<std::string> added_filters;

  for (const auto& name : {"filter_0", "filter_1", "filter_2"}) {
    auto provider =
        std::make_unique<Filter::StaticFilterConfigProviderImpl<Filter::HttpFilterFactoryCb>>(
            [name, &added_filters](FilterChainFactoryCallbacks&) { added_filters.insert(name); },
            name);
    filter_factories.push_back({std::move(provider), true});
  }

  {
    // If no filter is enabled explicitly by route, no filter should be added.
    EXPECT_CALL(callbacks, filterDisabled(_)).Times(3).WillRepeatedly(Return(absl::nullopt));
    FilterChainUtility::createFilterChainForFactories(callbacks, filter_factories);
    EXPECT_EQ(added_filters.size(), 0);
  }

  {

    EXPECT_CALL(callbacks, filterDisabled("filter_0")).WillOnce(Return(absl::make_optional(true)));
    EXPECT_CALL(callbacks, setFilterConfigName("filter_0")).Times(0);
    EXPECT_CALL(callbacks, filterDisabled("filter_1")).WillOnce(Return(absl::make_optional(false)));
    EXPECT_CALL(callbacks, setFilterConfigName("filter_1"));
    EXPECT_CALL(callbacks, filterDisabled("filter_2")).WillOnce(Return(absl::nullopt));
    EXPECT_CALL(callbacks, setFilterConfigName("filter_2")).Times(0);

    // Only filter_1 should be added.
    FilterChainUtility::createFilterChainForFactories(callbacks, filter_factories);
    EXPECT_TRUE(added_filters.find("filter_1") != added_filters.end());
    EXPECT_EQ(added_filters.size(), 1);
  }
}

} // namespace
} // namespace Http
} // namespace Envoy
