#include "test/extensions/filters/http/dynamic_forward_proxy/test_resolver.h"

namespace Envoy {
namespace Network {

absl::Mutex TestResolver::resolution_mutex_;
std::list<std::function<void()>> TestResolver::blocked_resolutions_;
// getaddrinfo DNS resolver factory
class TestResolverFactory : public DnsResolverFactory, public Logger::Loggable<Logger::Id::dns> {
public:
  std::string name() const override { return {"envoy.network.dns_resolver.test_resolver"}; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::network::dns_resolver::test_resolver::v3::TestResolverConfig()};
  }

  DnsResolverSharedPtr
  createDnsResolver(Event::Dispatcher& dispatcher, Api::Api& api,
                    const envoy::config::core::v3::TypedExtensionConfig&) const override {
    return std::make_shared<TestResolver>(dispatcher, api);
  }
};

REGISTER_FACTORY(TestResolverFactory, DnsResolverFactory);

} // namespace Network
} // namespace Envoy
