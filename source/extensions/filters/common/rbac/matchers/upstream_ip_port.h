#pragma once

#include "envoy/extensions/rbac/matchers/upstream_ip_port/v3/upstream_ip_port_matcher.pb.validate.h"

#include "source/common/common/logger.h"
#include "source/common/network/cidr_range.h"
#include "source/extensions/filters/common/rbac/matcher_extension.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Matchers {

// RBAC matcher extension for matching upstream's IP address (and port range if configured).
// configuration with the resolved upstream IP (v4 and v6).
class UpstreamIpPortMatcher : public Filters::Common::RBAC::Matcher,
                              public Logger::Loggable<Logger::Id::rbac> {
public:
  UpstreamIpPortMatcher(
      const envoy::extensions::rbac::matchers::upstream_ip_port::v3::UpstreamIpPortMatcher& proto);

  // Matcher interface.
  bool matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
               const StreamInfo::StreamInfo&) const override;

private:
  absl::optional<Envoy::Network::Address::CidrRange> cidr_;
  absl::optional<envoy::type::v3::Int64Range> port_;
};

// Extension factory for UpstreamIpPortMatcher.
class UpstreamIpPortMatcherFactory
    : public Filters::Common::RBAC::BaseMatcherExtensionFactory<
          UpstreamIpPortMatcher,
          envoy::extensions::rbac::matchers::upstream_ip_port::v3::UpstreamIpPortMatcher> {
public:
  std::string name() const override { return "envoy.rbac.matchers.upstream_ip_port"; }
};

} // namespace Matchers
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
