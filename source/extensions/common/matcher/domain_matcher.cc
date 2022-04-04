#include "source/extensions/common/matcher/domain_matcher.h"

#include "envoy/http/filter.h"
#include "envoy/network/filter.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {

namespace {
bool isWildcardServerName(const std::string& name) {
  return absl::StartsWith(name, "*.") || name == "*";
}
} // namespace

void DomainMatcherUtility::validateServerName(const std::string& server_name) {
  if (server_name.find('*') != std::string::npos && !isWildcardServerName(server_name)) {
    throw EnvoyException(fmt::format("invalid domain wildcard: {}", server_name));
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
