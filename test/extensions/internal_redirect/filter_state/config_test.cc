#include "envoy/extensions/internal_redirect/filter_state/v3/filter_state_config.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/router/internal_redirect.h"

#include "source/common/router/string_accessor_impl.h"
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

enum class Polarity { Allow, Deny };

class FilterStateTest : public testing::Test {
protected:
  FilterStateTest() : filter_state_(StreamInfo::FilterState::LifeSpan::FilterChain) {
    factory_ = Registry::FactoryRegistry<Router::InternalRedirectPredicateFactory>::getFactory(
        "envoy.internal_redirect_predicates.filter_state");
  }

  // Builds a predicate from an explicit config. `polarity` selects which oneof
  // arm (allow_value vs deny_value) carries `compare_value`.
  Router::InternalRedirectPredicateSharedPtr
  makePredicate(Polarity polarity, absl::string_view compare_value, bool allow_if_absent) {
    envoy::extensions::internal_redirect::filter_state::v3::FilterStateConfig config;
    config.set_filter_state_key(std::string(kGateKey));
    if (polarity == Polarity::Allow) {
      config.set_allow_value(std::string(compare_value));
    } else {
      config.set_deny_value(std::string(compare_value));
    }
    config.set_allow_if_absent(allow_if_absent);
    return factory_->createInternalRedirectPredicate(config, "fake_current_route");
  }

  void setGate(absl::string_view value) {
    filter_state_.setData(std::string(kGateKey),
                          std::make_shared<Router::StringAccessorImpl>(value),
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
  EXPECT_NE(dynamic_cast<envoy::extensions::internal_redirect::filter_state::v3::FilterStateConfig*>(
                proto.get()),
            nullptr);
}

TEST_F(FilterStateTest, PredicateName) {
  auto predicate = makePredicate(Polarity::Allow, "allow", false);
  EXPECT_EQ(predicate->name(), "envoy.internal_redirect_predicates.filter_state");
}

// allow_value: follow only when the gate equals the configured value.
TEST_F(FilterStateTest, AllowValueMatchesFollows) {
  setGate("allow");
  auto predicate = makePredicate(Polarity::Allow, "allow", false);
  EXPECT_TRUE(predicate->acceptTargetRoute(filter_state_, "any_route", false, false));
}

TEST_F(FilterStateTest, AllowValueDiffersRejects) {
  setGate("deny");
  auto predicate = makePredicate(Polarity::Allow, "allow", false);
  EXPECT_FALSE(predicate->acceptTargetRoute(filter_state_, "any_route", false, false));
}

// deny_value: follow unless the gate equals the configured value.
TEST_F(FilterStateTest, DenyValueMatchesRejects) {
  setGate("block");
  auto predicate = makePredicate(Polarity::Deny, "block", /*allow_if_absent=*/true);
  EXPECT_FALSE(predicate->acceptTargetRoute(filter_state_, "any_route", false, false));
}

TEST_F(FilterStateTest, DenyValueDiffersFollows) {
  setGate("something-else");
  auto predicate = makePredicate(Polarity::Deny, "block", /*allow_if_absent=*/true);
  EXPECT_TRUE(predicate->acceptTargetRoute(filter_state_, "any_route", false, false));
}

// Absent object honors allow_if_absent.
TEST_F(FilterStateTest, AbsentRejectsByDefault) {
  auto predicate = makePredicate(Polarity::Allow, "allow", /*allow_if_absent=*/false);
  EXPECT_FALSE(predicate->acceptTargetRoute(filter_state_, "any_route", false, false));
}

TEST_F(FilterStateTest, AbsentAllowedWhenConfigured) {
  auto predicate = makePredicate(Polarity::Allow, "allow", /*allow_if_absent=*/true);
  EXPECT_TRUE(predicate->acceptTargetRoute(filter_state_, "any_route", false, false));
}

} // namespace
} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
