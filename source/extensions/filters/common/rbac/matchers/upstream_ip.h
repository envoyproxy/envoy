#pragma once

#include "envoy/extensions/rbac/matchers/upstream_ip/v3/upstream_ip_matcher.pb.validate.h"

#include "source/common/common/logger.h"
#include "source/common/network/cidr_range.h"
#include "source/extensions/filters/common/rbac/matcher_extension.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Matchers {

// RBAC matcher extension for matching upstream's IP address. It matches the CIDR range provided by
// the `envoy::extensions::rbac::matchers::upstream_ip::v3::UpstreamIpMatcher`
// configuration with the resolved upstream IP (v4 and v6).
class UpstreamIpMatcher : public Filters::Common::RBAC::Matcher,
                          public Logger::Loggable<Logger::Id::rbac> {
public:
  UpstreamIpMatcher(
      const envoy::extensions::rbac::matchers::upstream_ip::v3::UpstreamIpMatcher& proto)
      : range_(Network::Address::CidrRange::create(proto.upstream_ip())) {}

  // Matcher interface.
  bool matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
               const StreamInfo::StreamInfo&) const override;

private:
  const Network::Address::CidrRange range_;
};

// Extension factory for UpstreamIpMatcher.
class UpstreamIpMatcherFactory
    : public Filters::Common::RBAC::BaseMatcherExtensionFactory<
          UpstreamIpMatcher,
          envoy::extensions::rbac::matchers::upstream_ip::v3::UpstreamIpMatcher> {
public:
  std::string name() const override { return "envoy.rbac.matchers.upstream.upstream_ip"; }
};

} // namespace Matchers
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
