#include "source/extensions/filters/udp/dns_gateway/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/common/synthetic_ip/cache_manager.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsGateway {

namespace {
// Helper to create a unique key for each policy
std::string makePolicyKey(absl::string_view domain_pattern, absl::string_view cidr_prefix,
                          uint32_t prefix_len) {
  return absl::StrCat(domain_pattern, ":", cidr_prefix, "/", prefix_len);
}
} // namespace

// Initialize static members
absl::flat_hash_map<std::string, DnsGatewayConfigFactory::SharedCounterPtr>
    DnsGatewayConfigFactory::shared_counters_;
absl::Mutex DnsGatewayConfigFactory::mutex_;

DnsGatewayConfigFactory::SharedCounterPtr
DnsGatewayConfigFactory::getOrCreateSharedCounter(const std::string& policy_key) {
  absl::MutexLock lock(&mutex_);

  auto it = shared_counters_.find(policy_key);
  if (it != shared_counters_.end()) {
    return it->second;
  }

  // Create new shared counter starting at 1 (skip network address for most CIDRs)
  auto counter = std::make_shared<std::atomic<uint32_t>>(1);
  shared_counters_[policy_key] = counter;

  ENVOY_LOG(info, "Created shared counter for policy: {}", policy_key);
  return counter;
}

Network::UdpListenerFilterFactoryCb DnsGatewayConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, Server::Configuration::ListenerFactoryContext& context) {

  auto config = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::udp::dns_gateway::v3::DnsGateway&>(
      proto_config, context.messageValidationVisitor());

  // Get or create singleton cache manager
  // We use the singleton manager to ensure only one cache manager exists per server instance
  auto cache_manager =
      context.serverFactoryContext()
          .singletonManager()
          .getTyped<Common::SyntheticIp::SyntheticIpCacheManager>(
              "synthetic_ip_cache_manager_singleton", [&context]() {
                return std::make_shared<Common::SyntheticIp::SyntheticIpCacheManager>(
                    context.serverFactoryContext().threadLocal(),
                    context.serverFactoryContext().mainThreadDispatcher());
              });

  return [config, cache_manager, &context](Network::UdpListenerFilterManager& filter_manager,
                                           Network::UdpReadFilterCallbacks& callbacks) -> void {
    // Build shared counters map for policies using LINEAR strategy
    absl::flat_hash_map<std::string, SharedCounterPtr> policy_counters;

    for (const auto& policy_proto : config.policies()) {
      if (policy_proto.allocation_strategy() ==
          envoy::extensions::filters::udp::dns_gateway::v3::LINEAR) {

        std::string policy_key = makePolicyKey(policy_proto.domain_pattern(),
                                               policy_proto.cidr_block().address_prefix(),
                                               policy_proto.cidr_block().prefix_len().value());

        policy_counters[policy_key] = getOrCreateSharedCounter(policy_key);
      }
    }

    filter_manager.addReadFilter(std::make_unique<DnsGatewayFilter>(
        callbacks, config, cache_manager, policy_counters,
        context.serverFactoryContext().api().timeSource(),
        context.serverFactoryContext().api().randomGenerator()));
  };
}

ProtobufTypes::MessagePtr DnsGatewayConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::filters::udp::dns_gateway::v3::DnsGateway>();
}

std::string DnsGatewayConfigFactory::name() const {
  return "envoy.filters.udp_listener.dns_gateway";
}

/**
 * Static registration for the DNS Gateway Filter config factory.
 */
REGISTER_FACTORY(DnsGatewayConfigFactory,
                 Server::Configuration::NamedUdpListenerFilterConfigFactory);

} // namespace DnsGateway
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
