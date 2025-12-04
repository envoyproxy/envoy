#pragma once

#include <list>
#include <memory>
#include <string>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/dns.h"
#include "envoy/network/dns_resolver.h"

#include "test/common/mocks/dns/mock_dns_resolver.pb.h"

namespace Envoy {
namespace Test {

// A simple synchronous mock DNS resolver for tests.
class MockDnsResolver : public Network::DnsResolver {
public:
  explicit MockDnsResolver(const std::vector<std::string>& non_existent_domains)
      : non_existent_domains_(non_existent_domains) {}

  Network::ActiveDnsQuery* resolve(const std::string& dns_name,
                                   Network::DnsLookupFamily /*dns_lookup_family*/,
                                   ResolveCb callback) override;

  void resetNetworking() override {}

private:
  std::vector<std::string> non_existent_domains_;
};

// Factory to create MockDnsResolver from a typed config.
class MockDnsResolverFactory : public Envoy::Network::DnsResolverFactory {
public:
  absl::StatusOr<Envoy::Network::DnsResolverSharedPtr>
  createDnsResolver(Envoy::Event::Dispatcher& /*dispatcher*/, Envoy::Api::Api& /*api*/,
                    const envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config)
      const override;

  std::string name() const override { return "envoy.test.mock_dns_resolver"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

DECLARE_FACTORY(MockDnsResolverFactory);

} // namespace Test
} // namespace Envoy
