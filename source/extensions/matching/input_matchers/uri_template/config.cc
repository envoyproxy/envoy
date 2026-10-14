#include "source/extensions/matching/input_matchers/uri_template/config.h"

#include "envoy/common/exception.h"
#include "envoy/registry/registry.h"

#include "source/common/common/thread.h"
#include "source/extensions/matching/input_matchers/uri_template/matcher.h"
#include "source/extensions/path/match/uri_template/config.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace UriTemplate {

Envoy::Matcher::InputMatcherFactoryCb UriTemplateInputMatcherFactory::createInputMatcherFactoryCb(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext&) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  const auto path_matcher = THROW_OR_RETURN_VALUE(
      Extensions::UriTemplate::Match::UriTemplateMatcherFactory().createPathMatcher(config),
      Router::PathMatcherSharedPtr);
  return [path_matcher]() { return std::make_unique<UriTemplateInputMatcher>(path_matcher); };
}

REGISTER_FACTORY(UriTemplateInputMatcherFactory, Envoy::Matcher::InputMatcherFactory);

} // namespace UriTemplate
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
