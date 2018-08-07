#include "extensions/filters/network/source_ip_access/source_ip_access.h"

#include <string>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SourceIpAccess {

InstanceStats Config::generateStats(const std::string& name, Stats::Scope& scope) {
  const std::string final_prefix = fmt::format("source_ip_access.{}.", name);
  return {ALL_SOURCE_IP_ACCESS_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

Config::Config(const envoy::config::filter::network::source_ip_access::v2::SourceIpAccess& config,
               Stats::Scope& scope)
    : stats_(generateStats(config.stat_prefix(), scope)),
      allow_by_default(config.allow_by_default()) {
  std::vector<Network::Address::CidrRange> cidrs;
  for (const auto& cidr : config.exception_prefixes()) {
    Network::Address::CidrRange cidr_entry = Network::Address::CidrRange::create(cidr);
    if (!cidr_entry.isValid()) {
      throw EnvoyException(
          fmt::format("invalid ip/mask combo '{}/{}' (format is <ip>/<# mask bits>)",
                      cidr.address_prefix(), cidr.prefix_len().value()));
    }

    cidrs.emplace_back(cidr_entry);
  }

  std::vector<std::pair<bool, std::vector<Network::Address::CidrRange>>> data{
      std::pair<bool, std::vector<Network::Address::CidrRange>>(true, cidrs)};

  lc_trie_ = std::make_unique<Network::LcTrie::LcTrie<bool>>(data);
}

bool Config::should_allow(const Network::Address::InstanceConstSharedPtr& ip_address) const {
  bool found = !lc_trie_->getData(ip_address).empty();
  return allow_by_default ? !found : found;
}

Network::FilterStatus Filter::onNewConnection() {
  auto& source_ip = callbacks_->connection().remoteAddress();

  config_->stats().total_.inc();
  if (config_->should_allow(source_ip)) {
    config_->stats().allowed_.inc();
    return Network::FilterStatus::Continue;
  } else {
    config_->stats().denied_.inc();
    callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }
}

void Filter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}
} // namespace SourceIpAccess
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
