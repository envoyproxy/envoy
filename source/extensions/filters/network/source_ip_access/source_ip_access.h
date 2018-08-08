#pragma once

#include <string>
#include <vector>

#include "envoy/config/filter/network/source_ip_access/v2/source_ip_access.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/network/lc_trie.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SourceIpAccess {

/**
 * All source ip access stats. @see stats_macros.h
 */
// clang-format off
#define ALL_SOURCE_IP_ACCESS_STATS(COUNTER)             \
  COUNTER(total)                                        \
  COUNTER(denied)                                       \
  COUNTER(allowed)                                      \
// clang-format on

/**
 * Struct definition for all source ip access stats. @see stats_macros.h
 */
struct InstanceStats {
  ALL_SOURCE_IP_ACCESS_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Global configuration for source ip access filter.
 */
class Config {
public:
  Config(const envoy::config::filter::network::source_ip_access::v2::SourceIpAccess& config, Stats::Scope& scope);

  bool should_allow(const Network::Address::InstanceConstSharedPtr& ip_address) const;
  const InstanceStats& stats() { return stats_; }

private:
  static InstanceStats generateStats(const std::string& name, Stats::Scope& scope);
  const InstanceStats stats_;
  bool allow_by_default;
  std::unique_ptr<Network::LcTrie::LcTrie<bool>> lc_trie_;
};

typedef std::shared_ptr<Config> ConfigSharedPtr;

/**
 * Source IP access filter instance. This filter will allow (continue to next filter) or deny (close the tcp connection) 
 * based on provided config.
 */
class Filter : public Network::ReadFilter {
public:
  Filter(ConfigSharedPtr config)
      : config_(config) {}
  ~Filter() {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance&, bool) override { return Network::FilterStatus::Continue; }
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;
  
private:
  ConfigSharedPtr config_;
  Network::ReadFilterCallbacks* callbacks_{};
};
} // namespace SourceIpAccess
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
