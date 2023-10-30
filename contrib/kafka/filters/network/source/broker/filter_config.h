#pragma once

#include <utility>

#include "source/common/common/assert.h"

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

struct IdRule {

  uint32_t id_;
  std::string host_;
  uint32_t port_;

  IdRule(const uint32_t id, const std::string host, const uint32_t port)
      : id_{id}, host_{host}, port_{port} {};

  bool matches(const uint32_t broker_id) const { return id_ == broker_id; }
};

static std::vector<IdRule> extractRewriteRules(const KafkaBrokerProtoConfig& proto_config) {
  if (proto_config.has_id_based_broker_address_rewrite_spec()) {
    std::vector<IdRule> result;
    const auto& spec = proto_config.id_based_broker_address_rewrite_spec();
    for (const auto& rule : spec.rules()) {
      result.emplace_back(rule.id(), rule.host(), rule.port());
    }
    return result;
  } else {
    return {};
  }
}

// Minor helper structure that contains broker filter configuration.
struct BrokerFilterConfig {

  BrokerFilterConfig(const KafkaBrokerProtoConfig& proto_config)
      : BrokerFilterConfig{proto_config.stat_prefix(), proto_config.force_response_rewrite(),
                           extractRewriteRules(proto_config)} {}

  // Visible for testing.
  BrokerFilterConfig(const std::string& stat_prefix, const bool force_response_rewrite,
                     const std::vector<IdRule>& broker_address_rewrite_rules)
      : stat_prefix_{stat_prefix}, force_response_rewrite_{force_response_rewrite},
        broker_address_rewrite_rules_{broker_address_rewrite_rules} {
    ASSERT(!stat_prefix_.empty());
  };

  /**
   * Whether this configuration means that rewrite should be happening.
   */
  bool needsResponseRewrite() const {
    return force_response_rewrite_ || !broker_address_rewrite_rules_.empty();
  }

  /**
   * Returns override address for a broker.
   */
  absl::optional<HostAndPort> findBrokerAddressOverride(const uint32_t broker_id) const {
    for (const auto& rule : broker_address_rewrite_rules_) {
      if (rule.matches(broker_id)) {
        const HostAndPort hp = {rule.host_, rule.port_};
        return {hp};
      }
    }
    return absl::nullopt;
  }

  std::string stat_prefix_;
  bool force_response_rewrite_;
  std::vector<IdRule> broker_address_rewrite_rules_;
};

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
