#pragma once

// #include "envoy/extensions/path/match/uri_template/v3/uri_template_match.pb.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/common/rbac/matcher_extension.h"
#include "source/extensions/path/match/uri_template/uri_template_match.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Matchers {

// RBAC matcher extension for matching URI templates.
class UriTemplateMatcher : public Filters::Common::RBAC::Matcher,
                           public Logger::Loggable<Logger::Id::rbac> {
public:
  UriTemplateMatcher(
      const envoy::extensions::path::match::uri_template::v3::UriTemplateMatchConfig& proto)
      : uri_template_matcher_(proto){};

  bool matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo&) const override;

private:
  const Envoy::Extensions::UriTemplate::Match::UriTemplateMatcher uri_template_matcher_;
};

// Extension factory for UriTemplateMatcher.
class UriTemplateMatcherFactory
    : public Filters::Common::RBAC::BaseMatcherExtensionFactory<
          UriTemplateMatcher,
          envoy::extensions::path::match::uri_template::v3::UriTemplateMatchConfig> {
public:
  std::string name() const override { return "envoy.rbac.uri_template.uri_template_matcher"; }
};

} // namespace Matchers
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
