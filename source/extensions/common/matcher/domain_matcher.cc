#include "source/extensions/common/matcher/domain_matcher.h"

#include "envoy/http/filter.h"
#include "envoy/network/filter.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {

void DomainMatcherUtility::validateServerName(const std::string& server_name) {
  auto pos = server_name.rfind('*');
  if (pos != std::string::npos) {
    if (pos != 0) {
      throw EnvoyException(fmt::format("wildcard only allowed in the prefix: {}", server_name));
    }
    if (server_name != "*" && !absl::StartsWith(server_name, "*.")) {
      throw EnvoyException(fmt::format("wildcard must be the first domain part: {}", server_name));
    }
  }
  // Reject internationalized domains to avoid ambiguity with case sensitivity.
  for (char c : server_name) {
    if (!absl::ascii_isascii(c)) {
      throw EnvoyException(fmt::format("non-ASCII domains are not supported: {}", server_name));
    }
  }
}

void DomainMatcherUtility::duplicateServerNameError(const std::string& server_name) {
  throw EnvoyException(fmt::format("duplicate domain: {}", server_name));
}

class NetworkDomainMatcherFactory : public DomainMatcherFactoryBase<Network::MatchingData> {};
class UdpNetworkDomainMatcherFactory : public DomainMatcherFactoryBase<Network::UdpMatchingData> {};
class HttpDomainMatcherFactory : public DomainMatcherFactoryBase<Http::HttpMatchingData> {};

REGISTER_FACTORY(NetworkDomainMatcherFactory,
                 ::Envoy::Matcher::CustomMatcherFactory<Network::MatchingData>);
REGISTER_FACTORY(UdpNetworkDomainMatcherFactory,
                 ::Envoy::Matcher::CustomMatcherFactory<Network::UdpMatchingData>);
REGISTER_FACTORY(HttpDomainMatcherFactory,
                 ::Envoy::Matcher::CustomMatcherFactory<Http::HttpMatchingData>);

} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
