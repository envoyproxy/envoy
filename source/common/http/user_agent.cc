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
    : symbol_table_(symbol_table), pool_(symbol_table),
      downstream_cx_length_ms_(pool_.add("downstream_cx_length_ms")),
      user_agent_(pool_.add("user_agent")),
      user_agent_tag_(pool_.add(Config::TagNames::get().HTTP_USER_AGENT)),
      ios_(pool_.add("user_agent.ios")), android_(pool_.add("user_agent.android")),
      ios_tags_({{user_agent_tag_, pool_.add("ios")}}),
      android_tags_({{user_agent_tag_, pool_.add("android")}}),
      downstream_cx_total_(pool_.add("downstream_cx_total")),
      downstream_cx_destroy_remote_active_rq_(pool_.add("downstream_cx_destroy_remote_active_rq")),
      downstream_rq_total_(pool_.add("downstream_rq_total")) {}

void UserAgent::completeConnectionLength(Stats::Timespan& span) {
  if (stats_ != nullptr) {
    stats_->downstream_cx_length_ms_.recordValue(span.elapsed().count());
  }
}

UserAgentStats::UserAgentStats(Stats::StatName device, Stats::StatNameTagSpan device_tags,
                               Stats::Scope& scope, const UserAgentContext& context)
    : downstream_cx_total_(Stats::Utility::counterFromTaggedPrefix(
          scope, context.user_agent_, device_tags, device, context.downstream_cx_total_)),
      downstream_cx_destroy_remote_active_rq_(
          Stats::Utility::counterFromTaggedPrefix(scope, context.user_agent_, device_tags, device,
                                                  context.downstream_cx_destroy_remote_active_rq_)),
      downstream_rq_total_(Stats::Utility::counterFromTaggedPrefix(
          scope, context.user_agent_, device_tags, device, context.downstream_rq_total_)),
      downstream_cx_length_ms_(Stats::Utility::histogramFromTaggedPrefix(
          scope, context.user_agent_, device_tags, device, context.downstream_cx_length_ms_,
          Stats::Histogram::Unit::Milliseconds)) {
  downstream_cx_total_.inc();
}

void UserAgent::initializeFromHeaders(const RequestHeaderMap& headers, Stats::Scope& scope) {
  // We assume that the user-agent is consistent based on the first request.
  if (stats_ == nullptr && !initialized_) {
    initialized_ = true;

    const absl::string_view user_agent = headers.getUserAgentValue();
    if (!user_agent.empty()) {
      if (user_agent.find("iOS") != absl::string_view::npos) {
        stats_ =
            std::make_unique<UserAgentStats>(context_.ios_, context_.ios_tags_, scope, context_);
      } else if (user_agent.find("android") != absl::string_view::npos) {
        stats_ = std::make_unique<UserAgentStats>(context_.android_, context_.android_tags_, scope,
                                                  context_);
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
