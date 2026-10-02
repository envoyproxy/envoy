#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stats/timespan.h"

#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Http {

/**
 * All stats of a user agent. @see stats_macros.h
 */
#define ALL_USER_AGENT_STATS(COUNTER, HISTOGRAM)                                                   \
  COUNTER(downstream_cx_destroy_remote_active_rq)                                                  \
  COUNTER(downstream_cx_total)                                                                     \
  COUNTER(downstream_rq_total)                                                                     \
  HISTOGRAM(downstream_cx_length_ms, Milliseconds)

/**
 * Captures the stat tokens used for recording user-agent stats. These are
 * independent of scope.
 */
struct UserAgentContext {
  UserAgentContext(Stats::SymbolTable& symbol_table);

  Stats::SymbolTable& symbol_table_;
  Stats::StatNamePool pool_;
  // The device is carried by an explicit 'envoy.http_user_agent' tag rather than being recovered
  // from the stat name by a tag extractor: 'user_agent' is the tag-extracted prefix of the
  // per-device stats, ios_ and android_ are the matching flat prefixes, and ios_tags_ and
  // android_tags_ are the tags themselves.
  Stats::StatName user_agent_;
  Stats::StatName user_agent_tag_;
  Stats::StatName ios_;
  Stats::StatName android_;
  Stats::StatNameTagVector ios_tags_;
  Stats::StatNameTagVector android_tags_;
  // The leaf names of the stats.
  ALL_USER_AGENT_STATS(GENERATE_STAT_NAME_STRUCT, GENERATE_STAT_NAME_STRUCT)
};

/**
 * Captures the stats (counters and histograms) for user-agents. These are
 * established within a stats scope. You must supply a UserAgentContext so that
 * none of the symbols have to be looked up in the symbol-table in the
 * request-path.
 */
struct UserAgentStats {
  ALL_USER_AGENT_STATS(GENERATE_COUNTER_STRUCT, GENERATE_HISTOGRAM_STRUCT)
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
   * is assumed to be the same for further requests. Downstream request counter is incremented for
   * for each request.
   * @param headers supplies the request headers.
   * @param scope supplies the backing stat scope, which already carries any enclosing prefix.
   */
  void initializeFromHeaders(const RequestHeaderMap& headers, Stats::Scope& scope);

  /**
   * Called when a connection is being destroyed.
   * @param event supplies the network event that caused destruction.
   * @param active_streams supplies whether there are still active streams at the time of closing.
   */
  void onConnectionDestroy(Network::ConnectionEvent event, bool active_streams);

private:
  const UserAgentContext& context_;
  bool initialized_{false};
  std::unique_ptr<UserAgentStats> stats_;
};

} // namespace Http
} // namespace Envoy
