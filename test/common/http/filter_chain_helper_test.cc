#include "source/common/http/filter_chain_helper.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Http {
namespace {

class MockFilterChainOptions : public FilterChainOptions {
public:
  MockFilterChainOptions() = default;

  MOCK_METHOD(absl::optional<bool>, filterDisabled, (absl::string_view), (const));
};

TEST(FilterChainUtilityTest, CreateFilterChainForFactoriesWithRouteDisabled) {
  NiceMock<MockFilterChainManager> manager;
  NiceMock<MockFilterChainOptions> options;
  FilterChainUtility::FilterFactoriesList filter_factories;

  for (const auto& name : {"filter_0", "filter_1", "filter_2"}) {
    auto provider =
        std::make_unique<Filter::StaticFilterConfigProviderImpl<Filter::HttpFilterFactoryCb>>(
            [](FilterChainFactoryCallbacks&) {}, name);
    filter_factories.push_back({std::move(provider), false});
  }

  {
    // If empty filter chain options is provided, all filters should be added.
    EXPECT_CALL(manager, applyFilterFactoryCb(_, _)).Times(3);
    FilterChainUtility::createFilterChainForFactories(manager, Http::EmptyFilterChainOptions{},
                                                      filter_factories);
  }

  {

    EXPECT_CALL(options, filterDisabled("filter_0")).WillOnce(Return(absl::make_optional(true)));
    EXPECT_CALL(options, filterDisabled("filter_1")).WillOnce(Return(absl::make_optional(false)));
    EXPECT_CALL(options, filterDisabled("filter_2")).WillOnce(Return(absl::nullopt));

    // 'filter_1' and 'filter_2' should be added.
    EXPECT_CALL(manager, applyFilterFactoryCb(_, _)).Times(2);
    FilterChainUtility::createFilterChainForFactories(manager, options, filter_factories);
  }
}

TEST(FilterChainUtilityTest, CreateFilterChainForFactoriesWithRouteDisabledAndDefaultDisabled) {
  NiceMock<MockFilterChainManager> manager;
  NiceMock<MockFilterChainOptions> options;
  FilterChainUtility::FilterFactoriesList filter_factories;

  for (const auto& name : {"filter_0", "filter_1", "filter_2"}) {
    auto provider =
        std::make_unique<Filter::StaticFilterConfigProviderImpl<Filter::HttpFilterFactoryCb>>(
            [](FilterChainFactoryCallbacks&) {}, name);
    filter_factories.push_back({std::move(provider), true});
  }

  {
    // If empty filter chain options is provided, all filters should not be added because they are
    // all disabled by default.
    EXPECT_CALL(manager, applyFilterFactoryCb(_, _)).Times(0);
    FilterChainUtility::createFilterChainForFactories(manager, Http::EmptyFilterChainOptions{},
                                                      filter_factories);
  }

  {

    EXPECT_CALL(options, filterDisabled("filter_0")).WillOnce(Return(absl::make_optional(true)));
    EXPECT_CALL(options, filterDisabled("filter_1")).WillOnce(Return(absl::make_optional(false)));
    EXPECT_CALL(options, filterDisabled("filter_2")).WillOnce(Return(absl::nullopt));

    // Only filter_1 should be added.
    EXPECT_CALL(manager, applyFilterFactoryCb(_, _));
    FilterChainUtility::createFilterChainForFactories(manager, options, filter_factories);
  }
}

} // namespace
} // namespace Http
} // namespace Envoy
