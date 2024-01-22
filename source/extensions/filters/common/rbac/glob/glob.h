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

// RBAC matcher extension for matching URL path according to glob standard.
class GlobTemplateMatcher : public Filters::Common::RBAC::Matcher,
                            public Logger::Loggable<Logger::Id::rbac> {
public:
  GlobTemplateMatcher(
      const envoy::extensions::path::match::uri_template::v3::UriTemplateMatchConfig& proto)
      : glob_matcher_(proto){};

  bool matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo&) const override;

private:
  const Envoy::Extensions::UriTemplate::Match::UriTemplateMatcher glob_matcher_;
};

// Extension factory for GlobTemplateMatcher.
class GlobTemplateMatcherFactory
    : public Filters::Common::RBAC::BaseMatcherExtensionFactory<
          GlobTemplateMatcher,
          envoy::extensions::path::match::uri_template::v3::UriTemplateMatchConfig> {
public:
  std::string name() const override { return "envoy.rbac.matchers.glob_matcher"; }
};

} // namespace Matchers
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
