#include "source/extensions/matching/input_matchers/uri_template/config.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/matching/input_matchers/uri_template/v3/uri_template.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/thread.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/matching/input_matchers/uri_template/matcher.h"
#include "source/extensions/path/uri_template_lib/uri_template.h"

#include "re2/re2.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace UriTemplate {

Envoy::Matcher::InputMatcherFactoryCb UriTemplateInputMatcherFactory::createInputMatcherFactoryCb(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& factory_context) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  const auto& uri_template_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::matching::input_matchers::uri_template::v3::UriTemplate&>(
      config, factory_context.messageValidationVisitor());
  const std::string& path_template = uri_template_config.path_template();
  const absl::StatusOr<std::string> regex_pattern =
      Extensions::UriTemplate::convertPathPatternSyntaxToRegex(path_template);
  if (!regex_pattern.ok()) {
    throw EnvoyException(fmt::format("path_template {} is invalid: {}", path_template,
                                     regex_pattern.status().message()));
  }
  const auto regex = std::make_shared<const RE2>(*regex_pattern, RE2::Quiet);
  if (!regex->ok()) {
    throw EnvoyException(
        fmt::format("path_template {} is invalid: {}", path_template, regex->error()));
  }
  return [regex]() { return std::make_unique<UriTemplateInputMatcher>(regex); };
}

REGISTER_FACTORY(UriTemplateInputMatcherFactory, Envoy::Matcher::InputMatcherFactory);

} // namespace UriTemplate
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
