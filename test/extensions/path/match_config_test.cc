#include "envoy/registry/registry.h"
#include "envoy/router/pattern_template.h"

#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/pattern_template/match/config.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace {

class PathMatchPredicateFactoryConfigTest : public testing::Test {
protected:
  PathMatchPredicateFactoryConfigTest() : filter_state_(StreamInfo::FilterState::LifeSpan::FilterChain) {
    factory_ = Registry::FactoryRegistry<Router::PathMatchPredicateFactory>::getFactory(
        "envoy.path.match.pattern_template.v3.pattern_template_match_predicate");
    config_ = factory_->createEmptyConfigProto();
    ASSERT(config_);
  }

  StreamInfo::FilterStateImpl filter_state_;
  Router::PatternTemplatePredicateFactory* factory_;
  ProtobufTypes::MessagePtr config_;
};

TEST_F(PathMatchPredicateFactoryConfigTest, EmptyCreation) {
  std::string current_route_name = "fake_current_route";
  // Create the predicate for the first time.
  {
    auto predicate =
        factory_->createPathMatchPredicate(config_, "/url_pattern/{TEST}");
    ASSERT(predicate);
  }
}

} // namespace
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
