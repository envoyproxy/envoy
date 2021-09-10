#pragma once

#include "envoy/extensions/rbac/matchers/upstream_port/v3/upstream_port_matcher.pb.validate.h"

#include "source/common/common/logger.h"
#include "source/common/network/cidr_range.h"
#include "source/extensions/filters/common/rbac/matcher_extension.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Matchers {

// RBAC matcher extension for matching upstream's port number. It matches the port range provided by
// the `envoy::extensions::rbac::matchers::upstream_port::v3::UpstreamPortMatcher`
// configuration with the resolved upstream's port.
class UpstreamPortMatcher : public Filters::Common::RBAC::Matcher,
                            public Logger::Loggable<Logger::Id::rbac> {
public:
  UpstreamPortMatcher(
      const envoy::extensions::rbac::matchers::upstream_port::v3::UpstreamPortMatcher& proto)
      : start_(proto.upstream_port_range().start()), end_(proto.upstream_port_range().end()) {}

  // Matcher interface.
  bool matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
               const StreamInfo::StreamInfo&) const override;

private:
  const uint32_t start_;
  const uint32_t end_;
};

// Extension factory for UpstreamPortMatcher.
class UpstreamPortMatcherFactory
    : public Filters::Common::RBAC::BaseMatcherExtensionFactory<
          UpstreamPortMatcher,
          envoy::extensions::rbac::matchers::upstream_port::v3::UpstreamPortMatcher> {
public:
  std::string name() const override { return "envoy.filters.common.rbac.upstream_port"; }
};

} // namespace Matchers
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
