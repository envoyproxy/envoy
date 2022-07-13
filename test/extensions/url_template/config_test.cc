#include "envoy/registry/registry.h"
#include "envoy/router/url_template.h"

#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/url_template/config.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace UrlTemplate {
namespace {

class UrlTemplateConfigTest : public testing::Test {
protected:
  UrlTemplateConfigTest() : filter_state_(StreamInfo::FilterState::LifeSpan::FilterChain) {
    factory_ = Registry::FactoryRegistry<Router::UrlTemplatePredicateFactory>::getFactory(
        "envoy.url_template_predicates");
    config_ = factory_->createEmptyConfigProto();
  }

  StreamInfo::FilterStateImpl filter_state_;
  Router::UrlTemplatePredicateFactory* factory_;
  ProtobufTypes::MessagePtr config_;
};

TEST_F(UrlTemplateConfigTest, EmptyCreation) {
  std::string current_route_name = "fake_current_route";
  // Create the predicate for the first time.
  {
    auto predicate = factory_->createUrlTemplatePredicate("/url_pattern/{TEST}", "rewrite_pattern");
    ASSERT(predicate);
  }
}

} // namespace
} // namespace UrlTemplate
} // namespace Extensions
} // namespace Envoy
