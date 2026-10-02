#include "source/common/http/user_agent.h"

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/network/connection.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/timespan.h"

#include "source/common/config/well_known_names.h"
#include "source/common/http/headers.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"

namespace Envoy {
namespace Http {

UserAgentContext::UserAgentContext(Stats::SymbolTable& symbol_table)
    : symbol_table_(symbol_table), pool_(symbol_table), user_agent_(pool_.add("user_agent")),
      user_agent_tag_(pool_.add(Config::TagNames::get().HTTP_USER_AGENT)),
      ios_(pool_.add("user_agent.ios")), android_(pool_.add("user_agent.android")),
      ios_tags_({{user_agent_tag_, pool_.add("ios")}}),
      android_tags_({{user_agent_tag_, pool_.add("android")}})
          ALL_USER_AGENT_STATS(GENERATE_STAT_NAME_INIT, GENERATE_STAT_NAME_INIT) {}

void UserAgent::completeConnectionLength(Stats::Timespan& span) {
  if (stats_ != nullptr) {
    stats_->downstream_cx_length_ms_.recordValue(span.elapsed().count());
  }
}

namespace {

// Create the stats from the names pre-encoded in the context, so that none of the symbols have
// to be looked up in the symbol table in the request path.
#define USER_AGENT_COUNTER(NAME) helper.counterFromStatName(context.NAME##_),
#define USER_AGENT_HISTOGRAM(NAME, UNIT)                                                           \
  helper.histogramFromStatName(context.NAME##_, Stats::Histogram::Unit::UNIT),

// device is the flat 'user_agent.<device>' prefix of the stats and device_tags are the tags
// describing that same device.
std::unique_ptr<UserAgentStats> createStats(Stats::StatName device,
                                            Stats::StatNameTagSpan device_tags, Stats::Scope& scope,
                                            const UserAgentContext& context) {
  Stats::ScopeHelper helper(scope, context.user_agent_, device_tags, device);
  auto stats = std::make_unique<UserAgentStats>(
      UserAgentStats{ALL_USER_AGENT_STATS(USER_AGENT_COUNTER, USER_AGENT_HISTOGRAM)});
  stats->downstream_cx_total_.inc();
  return stats;
}

} // namespace

void UserAgent::initializeFromHeaders(const RequestHeaderMap& headers, Stats::Scope& scope) {
  // We assume that the user-agent is consistent based on the first request.
  if (stats_ == nullptr && !initialized_) {
    initialized_ = true;

    const absl::string_view user_agent = headers.getUserAgentValue();
    if (!user_agent.empty()) {
      if (user_agent.find("iOS") != absl::string_view::npos) {
        stats_ = createStats(context_.ios_, context_.ios_tags_, scope, context_);
      } else if (user_agent.find("android") != absl::string_view::npos) {
        stats_ = createStats(context_.android_, context_.android_tags_, scope, context_);
      }
    }
  }
  if (stats_ != nullptr) {
    stats_->downstream_rq_total_.inc();
  }
}

void UserAgent::onConnectionDestroy(Network::ConnectionEvent event, bool active_streams) {
  if (stats_ != nullptr && active_streams && event == Network::ConnectionEvent::RemoteClose) {
    stats_->downstream_cx_destroy_remote_active_rq_.inc();
  }
}

} // namespace Http
} // namespace Envoy
