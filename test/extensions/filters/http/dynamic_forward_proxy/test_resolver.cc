#include "test/extensions/filters/http/dynamic_forward_proxy/test_resolver.h"

namespace Envoy {
namespace Network {

absl::Mutex TestResolver::resolution_mutex_;
std::list<absl::AnyInvocable<void(absl::optional<std::string> dns_override)>>
    TestResolver::blocked_resolutions_;

REGISTER_FACTORY(TestResolverFactory, DnsResolverFactory);

} // namespace Network
} // namespace Envoy
