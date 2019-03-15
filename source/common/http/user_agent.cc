#include "common/http/user_agent.h"

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/network/connection.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/timespan.h"

#include "common/http/headers.h"

namespace Envoy {
namespace Http {

void UserAgent::completeConnectionLength(Stats::Timespan& span) {

  // Note: stats_ and scope_ are set together, so it's assumed that scope_ will be non-nullptr if
  // stats_ is.
  if (!stats_) {
    return;
  }

  scope_->histogram(prefix_ + "downstream_cx_length_ms").recordValue(span.getRawDuration().count());
}

void UserAgent::initializeFromHeaders(const HeaderMap& headers, const std::string& prefix,
                                      Stats::Scope& scope) {
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
    stats_ = std::make_unique<UserAgentStats>(
        UserAgentStats{ALL_USER_AGENTS_STATS(POOL_COUNTER_PREFIX(scope, prefix_))});
    stats_->downstream_cx_total_.inc();
    stats_->downstream_rq_total_.inc();
    scope_ = &scope;
  }
}

void UserAgent::onConnectionDestroy(Network::ConnectionEvent event, bool active_streams) {
  if (!stats_) {
    return;
  }

  if (active_streams && event == Network::ConnectionEvent::RemoteClose) {
    stats_->downstream_cx_destroy_remote_active_rq_.inc();
  }
}

} // namespace Http
} // namespace Envoy
