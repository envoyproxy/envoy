#include "envoy/extensions/internal_redirect/filter_state/v3/filter_state_config.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/router/internal_redirect.h"

#include "source/common/stream_info/bool_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/internal_redirect/filter_state/config.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {
namespace {

constexpr absl::string_view kGateKey = "envoy.internal_redirect.gate";

class FilterStateTest : public testing::Test {
protected:
  FilterStateTest() : filter_state_(StreamInfo::FilterState::LifeSpan::FilterChain) {
    factory_ = Registry::FactoryRegistry<Router::InternalRedirectPredicateFactory>::getFactory(
        "envoy.internal_redirect_predicates.filter_state");
  }

  Router::InternalRedirectPredicateSharedPtr makePredicate(bool redirect_if_absent) {
    envoy::extensions::internal_redirect::filter_state::v3::FilterStateConfig config;
    config.set_redirect_enabled_key(std::string(kGateKey));
    config.set_redirect_if_absent(redirect_if_absent);
    return factory_->createInternalRedirectPredicate(config, "fake_current_route");
  }

  void setGate(bool value) {
    filter_state_.setData(std::string(kGateKey),
                          std::make_shared<StreamInfo::BoolAccessorImpl>(value),
                          StreamInfo::FilterState::LifeSpan::FilterChain);
  }

  StreamInfo::FilterStateImpl filter_state_;
  Router::InternalRedirectPredicateFactory* factory_;
};

TEST_F(FilterStateTest, FactoryRegistered) {
  ASSERT_NE(factory_, nullptr);
  EXPECT_EQ(factory_->name(), "envoy.internal_redirect_predicates.filter_state");
}

TEST_F(FilterStateTest, FactoryCreatesEmptyConfigProto) {
  auto proto = factory_->createEmptyConfigProto();
  ASSERT_NE(proto, nullptr);
  EXPECT_EQ(proto->GetTypeName(),
            "envoy.extensions.internal_redirect.filter_state.v3.FilterStateConfig");
}

TEST_F(FilterStateTest, PredicateName) {
  auto predicate = makePredicate(false);
  EXPECT_EQ(predicate->name(), "envoy.internal_redirect_predicates.filter_state");
}

// When the boolean is true, the redirect should be followed.
TEST_F(FilterStateTest, TrueValueFollowsRedirect) {
  setGate(true);
  auto predicate = makePredicate(false);
  EXPECT_TRUE(predicate->acceptTargetRoute(filter_state_, "any_route", false, false));
}

// When the boolean is false, the redirect should not be followed.
TEST_F(FilterStateTest, FalseValueRejectsRedirect) {
  setGate(false);
  auto predicate = makePredicate(false);
  EXPECT_FALSE(predicate->acceptTargetRoute(filter_state_, "any_route", false, false));
}

// When the object is absent and redirect_if_absent is false, reject.
TEST_F(FilterStateTest, AbsentRejectsByDefault) {
  auto predicate = makePredicate(/*redirect_if_absent=*/false);
  EXPECT_FALSE(predicate->acceptTargetRoute(filter_state_, "any_route", false, false));
}

// When the object is absent and redirect_if_absent is true, follow.
TEST_F(FilterStateTest, AbsentFollowsWhenConfigured) {
  auto predicate = makePredicate(/*redirect_if_absent=*/true);
  EXPECT_TRUE(predicate->acceptTargetRoute(filter_state_, "any_route", false, false));
}

} // namespace
} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
