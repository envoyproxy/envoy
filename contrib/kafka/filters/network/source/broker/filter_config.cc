#include "contrib/kafka/filters/network/source/broker/filter_config.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

static std::vector<RewriteRule> extractRewriteRules(const KafkaBrokerProtoConfig& proto_config) {
  if (proto_config.has_id_based_broker_address_rewrite_spec()) {
    std::vector<RewriteRule> result;
    const auto& spec = proto_config.id_based_broker_address_rewrite_spec();
    for (const auto& rule : spec.rules()) {
      result.emplace_back(rule.id(), rule.host(), rule.port());
    }
    return result;
  } else {
    return {};
  }
}

template <typename Iter> static absl::flat_hash_set<int16_t> extractApiKeySet(const Iter& arg) {
  return {arg.begin(), arg.end()};
}

BrokerFilterConfig::BrokerFilterConfig(const KafkaBrokerProtoConfig& proto_config)
    : BrokerFilterConfig{proto_config.stat_prefix(), proto_config.force_response_rewrite(),
                         extractRewriteRules(proto_config),
                         extractApiKeySet(proto_config.api_keys_allowed()),
                         extractApiKeySet(proto_config.api_keys_denied())} {}

BrokerFilterConfig::BrokerFilterConfig(const std::string& stat_prefix,
                                       const bool force_response_rewrite,
                                       const std::vector<RewriteRule>& broker_address_rewrite_rules,
                                       const absl::flat_hash_set<int16_t>& api_keys_allowed,
                                       const absl::flat_hash_set<int16_t>& api_keys_denied)
    : stat_prefix_{stat_prefix}, force_response_rewrite_{force_response_rewrite},
      broker_address_rewrite_rules_{broker_address_rewrite_rules},
      api_keys_allowed_{api_keys_allowed}, api_keys_denied_{api_keys_denied} {

  ASSERT(!stat_prefix_.empty());
};

const std::string& BrokerFilterConfig::statPrefix() const { return stat_prefix_; }

bool BrokerFilterConfig::needsResponseRewrite() const {
  return force_response_rewrite_ || !broker_address_rewrite_rules_.empty();
}

absl::optional<HostAndPort>
BrokerFilterConfig::findBrokerAddressOverride(const uint32_t broker_id) const {
  for (const auto& rule : broker_address_rewrite_rules_) {
    if (rule.matches(broker_id)) {
      const HostAndPort hp = {rule.host_, rule.port_};
      return {hp};
    }
  }
  return absl::nullopt;
}

absl::flat_hash_set<int16_t> BrokerFilterConfig::apiKeysAllowed() const {
  return api_keys_allowed_;
}

absl::flat_hash_set<int16_t> BrokerFilterConfig::apiKeysDenied() const { return api_keys_denied_; }

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
