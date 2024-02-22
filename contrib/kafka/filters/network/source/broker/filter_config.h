#pragma once

#include <utility>

#include "absl/types/optional.h"
#include "contrib/envoy/extensions/filters/network/kafka_broker/v3/kafka_broker.pb.h"
#include "contrib/envoy/extensions/filters/network/kafka_broker/v3/kafka_broker.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

using KafkaBrokerProtoConfig = envoy::extensions::filters::network::kafka_broker::v3::KafkaBroker;

using HostAndPort = std::pair<std::string, uint32_t>;

// Represents a rule matching a broker with given id.
struct RewriteRule {

  uint32_t id_;

  std::string host_;
  uint32_t port_;

  RewriteRule(const uint32_t id, const std::string host, const uint32_t port)
      : id_{id}, host_{host}, port_{port} {};

  bool matches(const uint32_t broker_id) const { return id_ == broker_id; }
};

// Minor helper object that contains broker filter configuration.
class BrokerFilterConfig {
public:
  virtual ~BrokerFilterConfig() = default;

  BrokerFilterConfig(const KafkaBrokerProtoConfig& proto_config);

  // Visible for testing.
  BrokerFilterConfig(const std::string& stat_prefix, const bool force_response_rewrite,
                     const std::vector<RewriteRule>& broker_address_rewrite_rules);

  /**
   * Returns the prefix for stats.
   */
  virtual const std::string& statPrefix() const;

  /**
   * Whether this configuration means that rewrite should be happening.
   */
  virtual bool needsResponseRewrite() const;

  /**
   * Returns override address for a broker.
   */
  virtual absl::optional<HostAndPort> findBrokerAddressOverride(const uint32_t broker_id) const;

private:
  std::string stat_prefix_;
  bool force_response_rewrite_;
  std::vector<RewriteRule> broker_address_rewrite_rules_;
};

using BrokerFilterConfigSharedPtr = std::shared_ptr<BrokerFilterConfig>;

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
