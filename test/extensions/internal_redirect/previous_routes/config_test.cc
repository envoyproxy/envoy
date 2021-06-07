#include "envoy/extensions/internal_redirect/previous_routes/v3/previous_routes_config.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/router/internal_redirect.h"

#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/internal_redirect/previous_routes/config.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {
namespace {

class PreviousRoutesTest : public testing::Test {
protected:
  PreviousRoutesTest() : filter_state_(StreamInfo::FilterState::LifeSpan::FilterChain) {
    factory_ = Registry::FactoryRegistry<Router::InternalRedirectPredicateFactory>::getFactory(
        "envoy.internal_redirect_predicates.previous_routes");
    config_ = factory_->createEmptyConfigProto();
  }

  StreamInfo::FilterStateImpl filter_state_;
  Router::InternalRedirectPredicateFactory* factory_;
  ProtobufTypes::MessagePtr config_;
};

TEST_F(PreviousRoutesTest, TargetIsOnlyTakenOnce) {
  std::string current_route_name = "fake_current_route";
  // Create the predicate for the first time. It should remember nothing in the
  // filter state, so it allows the redirect.
  {
    auto predicate = factory_->createInternalRedirectPredicate(*config_, current_route_name);
    ASSERT(predicate);

    EXPECT_TRUE(predicate->acceptTargetRoute(filter_state_, "route_1", false, false));
    // New filter state data is created with route name.
    EXPECT_TRUE(filter_state_.hasDataWithName(
        "envoy.internal_redirect.previous_routes_predicate_state.fake_current_route"));
  }

  // The second predicate should see the previously taken route.
  {
    auto predicate = factory_->createInternalRedirectPredicate(*config_, current_route_name);
    ASSERT(predicate);

    EXPECT_FALSE(predicate->acceptTargetRoute(filter_state_, "route_1", false, false));
  }
}

TEST_F(PreviousRoutesTest, RoutesAreIndependent) {
  // Create the predicate on route_0.
  {
    auto predicate = factory_->createInternalRedirectPredicate(*config_, "route_0");
    ASSERT(predicate);

    EXPECT_TRUE(predicate->acceptTargetRoute(filter_state_, "route_2", false, false));
    // New filter state data is created with route name.
    EXPECT_TRUE(filter_state_.hasDataWithName(
        "envoy.internal_redirect.previous_routes_predicate_state.route_0"));
  }

  // The predicate created on route_1 should also allow a redirect to route_2
  {
    auto predicate = factory_->createInternalRedirectPredicate(*config_, "route_1");
    ASSERT(predicate);

    EXPECT_TRUE(predicate->acceptTargetRoute(filter_state_, "route_2", false, false));
    // New filter state data is created with route name.
    EXPECT_TRUE(filter_state_.hasDataWithName(
        "envoy.internal_redirect.previous_routes_predicate_state.route_1"));
  }
}

} // namespace
} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
