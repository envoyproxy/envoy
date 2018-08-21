#include "extensions/filters/network/ext_authz/ext_authz.h"

#include <cstdint>
#include <string>

#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtAuthz {

InstanceStats Config::generateStats(const std::string& name, Stats::Scope& scope) {
  const std::string final_prefix = fmt::format("ext_authz.{}.", name);
  return {ALL_TCP_EXT_AUTHZ_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                  POOL_GAUGE_PREFIX(scope, final_prefix))};
}

void Filter::callCheck() {
  Filters::Common::ExtAuthz::CheckRequestUtils::createTcpCheck(filter_callbacks_, check_request_);

  status_ = Status::Calling;
  config_->stats().active_.inc();
  config_->stats().total_.inc();

  calling_check_ = true;
  client_->check(*this, check_request_, Tracing::NullSpan::instance());
  calling_check_ = false;
}

Network::FilterStatus Filter::onData(Buffer::Instance&, bool /* end_stream */) {
  if (status_ == Status::NotStarted) {
    // By waiting to invoke the check at onData() the call to authorization service will have
    // sufficient information to fillout the checkRequest_.
    callCheck();
  }
  return filter_return_ == FilterReturn::Stop ? Network::FilterStatus::StopIteration
                                              : Network::FilterStatus::Continue;
}

Network::FilterStatus Filter::onNewConnection() {
  // Wait till onData() happens.
  return Network::FilterStatus::Continue;
}

void Filter::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    if (status_ == Status::Calling) {
      // Make sure that any pending request in the client is cancelled. This will be NOP if the
      // request already completed.
      client_->cancel();
      config_->stats().active_.dec();
    }
  }
}

void Filter::onComplete(Filters::Common::ExtAuthz::ResponsePtr&& response) {
  status_ = Status::Complete;
  config_->stats().active_.dec();

  switch (response->status) {
  case Filters::Common::ExtAuthz::CheckStatus::OK:
    config_->stats().ok_.inc();
    break;
  case Filters::Common::ExtAuthz::CheckStatus::Error:
    config_->stats().error_.inc();
    break;
  case Filters::Common::ExtAuthz::CheckStatus::Denied:
    config_->stats().denied_.inc();
    break;
  }

  // Fail open only if configured to do so and if the check status was a error.
  if (response->status == Filters::Common::ExtAuthz::CheckStatus::Denied ||
      (response->status == Filters::Common::ExtAuthz::CheckStatus::Error &&
       !config_->failureModeAllow())) {
    config_->stats().cx_closed_.inc();
    filter_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
  } else {
    // Let the filter chain continue.
    filter_return_ = FilterReturn::Continue;
    if (config_->failureModeAllow() &&
        response->status == Filters::Common::ExtAuthz::CheckStatus::Error) {
      // Status is Error and yet we are configured to allow traffic. Click a counter.
      config_->stats().failure_mode_allowed_.inc();
    }
    // We can get completion inline, so only call continue if that isn't happening.
    if (!calling_check_) {
      filter_callbacks_->continueReading();
    }
  }
}

} // namespace ExtAuthz
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
