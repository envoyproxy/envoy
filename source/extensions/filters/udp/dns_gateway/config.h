#pragma once

#include "envoy/extensions/filters/udp/dns_gateway/v3/dns_gateway.pb.h"
#include "envoy/extensions/filters/udp/dns_gateway/v3/dns_gateway.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/extensions/filters/udp/dns_gateway/dns_gateway_filter.h"

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

#include <atomic>
#include <memory>

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsGateway {

/**
 * Config registration for the DNS Gateway Filter.
 */
class DnsGatewayConfigFactory : public Server::Configuration::NamedUdpListenerFilterConfigFactory {
public:
  // Type alias for shared counter
  using SharedCounterPtr = std::shared_ptr<std::atomic<uint32_t>>;

  // NamedUdpListenerFilterConfigFactory
  Network::UdpListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               Server::Configuration::ListenerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;

  // Get or create a shared counter for a policy
  // Key is derived from policy configuration to ensure uniqueness
  static SharedCounterPtr getOrCreateSharedCounter(const std::string& policy_key);

private:
  // Shared state across all filter instances (survives worker thread creation)
  // Maps policy key -> shared atomic counter
  static absl::flat_hash_map<std::string, SharedCounterPtr> shared_counters_ ABSL_GUARDED_BY(mutex_);
  static absl::Mutex mutex_;
};

} // namespace DnsGateway
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
