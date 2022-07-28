#include "envoy/registry/registry.h"
#include "envoy/router/pattern_template.h"

#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/pattern_template/rewrite/config.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace {

class PatternTemplateRewriteConfigTest : public testing::Test {
protected:
  PatternTemplateRewriteConfigTest() : filter_state_(StreamInfo::FilterState::LifeSpan::FilterChain) {
    factory_ = Registry::FactoryRegistry<Router::PatternTemplatePredicateRewriteFactory>::getFactory(
        "envoy.pattern_template.pattern_template_rewrite_predicate");
    config_ = factory_->createEmptyConfigProto();
  }

  StreamInfo::FilterStateImpl filter_state_;
  Router::PatternTemplatePredicateFactory* factory_;
  ProtobufTypes::MessagePtr config_;
};

TEST_F(PatternTemplateRewriteConfigTest, EmptyCreation) {
  std::string current_route_name = "fake_current_route";
  // Create the predicate for the first time.
  {
    auto predicate =
        factory_->createUrlTemplateRewritePredicate("/url_pattern/{TEST}", "rewrite_pattern");
    ASSERT(predicate);
  }
}

} // namespace
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
