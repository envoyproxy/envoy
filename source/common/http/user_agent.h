#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stats/timespan.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Http {

struct UserAgentContext {
  UserAgentContext(Stats::SymbolTable& symbol_table);

  Stats::SymbolTable& symbol_table_;
  Stats::StatNamePool pool_;
  Stats::StatName downstream_cx_length_ms_;
  Stats::StatName ios_;
  Stats::StatName android_;
  Stats::StatName downstream_cx_total_;
  Stats::StatName downstream_cx_destroy_remote_active_rq_;
  Stats::StatName downstream_rq_total_;
};

struct UserAgentStats {
  UserAgentStats(Stats::StatName prefix, Stats::StatName device, Stats::Scope& scope,
                 const UserAgentContext& context);

  Stats::Counter& downstream_cx_total_;
  Stats::Counter& downstream_cx_destroy_remote_active_rq_;
  Stats::Counter& downstream_rq_total_;
  Stats::Histogram& downstream_cx_length_ms_;
};

/**
 * Stats support for specific user agents.
 */
class UserAgent {
public:
  UserAgent(const UserAgentContext& context) : context_(context) {}

  /**
   * Complete a connection length timespan for the target user agent.
   * @param span supplies the timespan to complete.
   */
  void completeConnectionLength(Stats::Timespan& span);

  /**
   * Initialize the user agent from request headers. This is only done once and the user-agent
   * is assumed to be the same for further requests.
   * @param headers supplies the request headers.
   * @param prefix supplies the stat prefix for the UA stats.
   * @param scope supplies the backing stat scope.
   */
  void initializeFromHeaders(const HeaderMap& headers, Stats::StatName prefix, Stats::Scope& scope);

  /**
   * Called when a connection is being destroyed.
   * @param event supplies the network event that caused destruction.
   * @param active_streams supplies whether there are still active streams at the time of closing.
   */
  void onConnectionDestroy(Network::ConnectionEvent event, bool active_streams);

  void initStats(Stats::StatName prefix, Stats::StatName device, Stats::Scope& scope);

private:
  const UserAgentContext& context_;
  bool initialized_{false};
  std::unique_ptr<UserAgentStats> stats_;
};

} // namespace Http
} // namespace Envoy
