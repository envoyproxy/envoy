#include "test/common/mocks/dns/mock_dns_resolver.h"

#include "envoy/network/address.h"

#include "source/common/network/utility.h"

#include "test/common/mocks/dns/mock_dns_resolver.pb.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"

namespace Envoy {
namespace Test {

namespace {
bool isLocalhost(const std::string& host) {
  std::string lower = absl::AsciiStrToLower(host);
  return absl::StartsWith(lower, "localhost") || absl::StartsWith(lower, "127.0.0.1") ||
         absl::StartsWith(lower, "::1");
}

bool isNonExistent(const std::vector<std::string>& list, const std::string& host) {
  std::string lower = absl::AsciiStrToLower(host);
  for (const auto& d : list) {
    if (absl::AsciiStrToLower(d) == lower) {
      return true;
    }
  }
  return false;
}
} // namespace

Network::ActiveDnsQuery* MockDnsResolver::resolve(const std::string& dns_name,
                                                  Network::DnsLookupFamily dns_lookup_family,
                                                  ResolveCb callback) {
  std::list<Network::DnsResponse> responses;

  if (isLocalhost(dns_name)) {
    if (dns_lookup_family == Network::DnsLookupFamily::V6Only ||
        dns_lookup_family == Network::DnsLookupFamily::All ||
        dns_lookup_family == Network::DnsLookupFamily::Auto) {
      auto address =
          Network::Utility::parseInternetAddressNoThrow("::1", /*port=*/0, /*v6only=*/true);
      responses.emplace_back(address, std::chrono::seconds(360));
    }
    if (dns_lookup_family == Network::DnsLookupFamily::V4Only ||
        dns_lookup_family == Network::DnsLookupFamily::All ||
        dns_lookup_family == Network::DnsLookupFamily::Auto ||
        dns_lookup_family == Network::DnsLookupFamily::V4Preferred) {
      auto address =
          Network::Utility::parseInternetAddressNoThrow("127.0.0.1", /*port=*/0, /*v6only=*/true);
      responses.emplace_back(address, std::chrono::seconds(360));
    }

    callback(Network::DnsResolver::ResolutionStatus::Completed, "mock dns: localhost",
             std::move(responses));
    return nullptr;
  }

  if (isNonExistent(non_existent_domains_, dns_name)) {
    callback(Network::DnsResolver::ResolutionStatus::Failure, "mock dns: nxdomain",
             std::move(responses));
    return nullptr;
  }

  callback(Network::DnsResolver::ResolutionStatus::Failure, "mock dns: failure",
           std::move(responses));
  return nullptr;
}

absl::StatusOr<Envoy::Network::DnsResolverSharedPtr> MockDnsResolverFactory::createDnsResolver(
    Envoy::Event::Dispatcher& /*dispatcher*/, Envoy::Api::Api& /*api*/,
    const envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) const {
  envoy::test::mock_dns_resolver::v3::MockDnsResolverConfig config;
  if (!typed_dns_resolver_config.has_typed_config()) {
    return absl::InvalidArgumentError("typed_config missing for MockDnsResolver");
  }

  if (!typed_dns_resolver_config.typed_config().UnpackTo(&config)) {
    return absl::InvalidArgumentError("failed to unpack MockDnsResolver config");
  }

  std::vector<std::string> domains(config.non_existent_domains().begin(),
                                   config.non_existent_domains().end());
  return std::make_shared<MockDnsResolver>(domains);
}

ProtobufTypes::MessagePtr MockDnsResolverFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::test::mock_dns_resolver::v3::MockDnsResolverConfig>();
}

REGISTER_FACTORY(MockDnsResolverFactory, Network::DnsResolverFactory);

} // namespace Test
} // namespace Envoy
