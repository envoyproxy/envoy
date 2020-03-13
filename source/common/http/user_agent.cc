#include "common/http/user_agent.h"

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/network/connection.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/timespan.h"

#include "common/http/headers.h"
#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Http {

UserAgentContext::UserAgentContext(Stats::SymbolTable& symbol_table)
    : symbol_table_(symbol_table), pool_(symbol_table),
      downstream_cx_length_ms_(pool_.add("downstream_cx_length_ms")),
      ios_(pool_.add("user_agent.ios")), android_(pool_.add("user_agent.android")),
      downstream_cx_total_(pool_.add("downstream_cx_total")),
      downstream_cx_destroy_remote_active_rq_(pool_.add("downstream_cx_destroy_remote_active_rq")),
      downstream_rq_total_(pool_.add("downstream_rq_total")) {}

void UserAgent::completeConnectionLength(Stats::Timespan& span) {
  if (stats_ != nullptr) {
    stats_->downstream_cx_length_ms_.recordValue(span.elapsed().count());
  }
}

UserAgentStats::UserAgentStats(Stats::StatName prefix, Stats::StatName device, Stats::Scope& scope,
                               const UserAgentContext& context)
    : downstream_cx_total_(scope.counterFromStatName(Stats::StatName(
          context.symbol_table_.join({prefix, device, context.downstream_cx_total_}).get()))),
      downstream_cx_destroy_remote_active_rq_(scope.counterFromStatName(Stats::StatName(
          context.symbol_table_
              .join({prefix, device, context.downstream_cx_destroy_remote_active_rq_})
              .get()))),
      downstream_rq_total_(scope.counterFromStatName(Stats::StatName(
          context.symbol_table_.join({prefix, device, context.downstream_rq_total_}).get()))),
      downstream_cx_length_ms_(scope.histogramFromStatName(
          Stats::StatName(
              context.symbol_table_.join({prefix, device, context.downstream_cx_length_ms_}).get()),
          Stats::Histogram::Unit::Milliseconds)) {
  downstream_cx_total_.inc();
  downstream_rq_total_.inc();
}

void UserAgent::initializeFromHeaders(const RequestHeaderMap& headers, Stats::StatName prefix,
                                      Stats::Scope& scope) {
  // We assume that the user-agent is consistent based on the first request.
  if (stats_ == nullptr && !initialized_) {
    initialized_ = true;

    const HeaderEntry* user_agent = headers.UserAgent();
    if (user_agent != nullptr) {
      if (user_agent->value().getStringView().find("iOS") != absl::string_view::npos) {
        stats_ = std::make_unique<UserAgentStats>(prefix, context_.ios_, scope, context_);
      } else if (user_agent->value().getStringView().find("android") != absl::string_view::npos) {
        stats_ = std::make_unique<UserAgentStats>(prefix, context_.android_, scope, context_);
      }
    }
  }
}

void UserAgent::onConnectionDestroy(Network::ConnectionEvent event, bool active_streams) {
  if (stats_ != nullptr && active_streams && event == Network::ConnectionEvent::RemoteClose) {
    stats_->downstream_cx_destroy_remote_active_rq_.inc();
  }
}

} // namespace Http
} // namespace Envoy
