#include "envoy/extensions/internal_redirect/safe_cross_scheme/v3/safe_cross_scheme_config.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/router/internal_redirect.h"

#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/internal_redirect/safe_cross_scheme/config.h"
#include "source/extensions/internal_redirect/safe_cross_scheme/safe_cross_scheme.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {
namespace {

class SafeCrossSchemeTest : public testing::Test {
protected:
  SafeCrossSchemeTest() : filter_state_(StreamInfo::FilterState::LifeSpan::FilterChain) {
    factory_ = Registry::FactoryRegistry<Router::InternalRedirectPredicateFactory>::getFactory(
        "envoy.internal_redirect_predicates.safe_cross_scheme");
    config_ = factory_->createEmptyConfigProto();
  }

  StreamInfo::FilterStateImpl filter_state_;
  Router::InternalRedirectPredicateFactory* factory_;
  ProtobufTypes::MessagePtr config_;
};

TEST_F(SafeCrossSchemeTest, PredicateTest) {
  auto predicate = factory_->createInternalRedirectPredicate(*config_, "route_0");
  ASSERT(predicate);

  // Test when downstream is HTTPS and target is HTTPS (allowed)
  EXPECT_TRUE(predicate->acceptTargetRoute(filter_state_, "route_1", true, true));

  // Test when downstream is HTTPS and target is HTTP (allowed)
  EXPECT_TRUE(predicate->acceptTargetRoute(filter_state_, "route_1", true, false));

  // Test when downstream is HTTP and target is HTTP (allowed)
  EXPECT_TRUE(predicate->acceptTargetRoute(filter_state_, "route_1", false, false));

  // Test when downstream is HTTP and target is HTTPS (not allowed)
  EXPECT_FALSE(predicate->acceptTargetRoute(filter_state_, "route_1", false, true));
}

TEST_F(SafeCrossSchemeTest, VerifyPredicateNameFunction) {
  auto predicate = factory_->createInternalRedirectPredicate(*config_, "route_0");
  ASSERT(predicate);
  EXPECT_EQ("envoy.internal_redirect_predicates.safe_cross_scheme", predicate->name());
}

} // namespace
} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
