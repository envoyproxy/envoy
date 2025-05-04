#include "envoy/extensions/internal_redirect/allow_listed_routes/v3/allow_listed_routes_config.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/router/internal_redirect.h"

#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/internal_redirect/allow_listed_routes/allow_listed_routes.h"
#include "source/extensions/internal_redirect/allow_listed_routes/config.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {
namespace {

class AllowListedRoutesTest : public testing::Test {
protected:
  AllowListedRoutesTest() : filter_state_(StreamInfo::FilterState::LifeSpan::FilterChain) {
    factory_ = Registry::FactoryRegistry<Router::InternalRedirectPredicateFactory>::getFactory(
        "envoy.internal_redirect_predicates.allow_listed_routes");
    config_ = factory_->createEmptyConfigProto();

    envoy::extensions::internal_redirect::allow_listed_routes::v3::AllowListedRoutesConfig*
        typed_config = dynamic_cast<envoy::extensions::internal_redirect::allow_listed_routes::v3::
                                        AllowListedRoutesConfig*>(config_.get());
    typed_config->add_allowed_route_names("route_2");
    typed_config->add_allowed_route_names("route_3");
  }

  StreamInfo::FilterStateImpl filter_state_;
  Router::InternalRedirectPredicateFactory* factory_;
  ProtobufTypes::MessagePtr config_;
};

TEST_F(AllowListedRoutesTest, PredicateTest) {
  auto predicate = factory_->createInternalRedirectPredicate(*config_, "route_0");
  ASSERT(predicate);

  // Test route that is in the allow list (allowed)
  EXPECT_TRUE(predicate->acceptTargetRoute(filter_state_, "route_2", false, false));

  // Test another route that is in the allow list (allowed)
  EXPECT_TRUE(predicate->acceptTargetRoute(filter_state_, "route_3", false, false));

  // Test route that is not in the allow list (not allowed)
  EXPECT_FALSE(predicate->acceptTargetRoute(filter_state_, "route_1", false, false));
}

TEST_F(AllowListedRoutesTest, VerifyPredicateNameFunction) {
  auto predicate = factory_->createInternalRedirectPredicate(*config_, "route_0");
  ASSERT(predicate);
  EXPECT_EQ("envoy.internal_redirect_predicates.allow_listed_routes", predicate->name());
}

} // namespace
} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
