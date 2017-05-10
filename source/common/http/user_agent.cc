#include "common/http/user_agent.h"

#include <cstdint>
#include <string>

#include "envoy/network/connection.h"
#include "envoy/stats/stats.h"

#include "common/http/headers.h"

namespace Envoy {
namespace Http {

void UserAgent::completeConnectionLength(Stats::Timespan& span) {
  if (!stats_) {
    return;
  }

  span.complete(prefix_ + "downstream_cx_length_ms");
}

void UserAgent::initializeFromHeaders(const HeaderMap& headers, const std::string& prefix,
                                      Stats::Store& stat_store) {
  // We assume that the user-agent is consistent based on the first request.
  if (type_ != Type::NotInitialized) {
    return;
  }

  type_ = Type::Unknown;

  const HeaderEntry* user_agent = headers.UserAgent();
  if (user_agent) {
    prefix_ = prefix;
    if (user_agent->value().find("iOS")) {
      type_ = Type::iOS;
      prefix_ += "user_agent.ios.";
    } else if (user_agent->value().find("android")) {
      type_ = Type::Android;
      prefix_ += "user_agent.android.";
    }
  }

  if (type_ != Type::Unknown) {
    stats_.reset(
        new UserAgentStats{ALL_USER_AGENTS_STATS(POOL_COUNTER_PREFIX(stat_store, prefix_))});
    stats_->downstream_cx_total_.inc();
    stats_->downstream_rq_total_.inc();
  }
}

void UserAgent::onConnectionDestroy(uint32_t events, bool active_streams) {
  if (!stats_) {
    return;
  }

  if (active_streams && (events & Network::ConnectionEvent::RemoteClose)) {
    stats_->downstream_cx_destroy_remote_active_rq_.inc();
  }
}

} // Http
} // Envoy
