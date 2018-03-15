#include "common/filter/ext_authz.h"

#include <cstdint>
#include <string>

#include "common/common/assert.h"
#include "common/tracing/http_tracer_impl.h"

#include "fmt/format.h"

namespace Envoy {
namespace ExtAuthz {
namespace TcpFilter {

InstanceStats Config::generateStats(const std::string& name, Stats::Scope& scope) {
  const std::string final_prefix = fmt::format("ext_authz.{}.", name);
  return {ALL_TCP_EXT_AUTHZ_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                  POOL_GAUGE_PREFIX(scope, final_prefix))};
}

void Instance::callCheck() {
  CheckRequestUtils::createTcpCheck(filter_callbacks_, check_request_);

  status_ = Status::Calling;
  config_->stats().active_.inc();
  config_->stats().total_.inc();

  calling_check_ = true;
  client_->check(*this, check_request_, Tracing::NullSpan::instance());
  calling_check_ = false;
}

Network::FilterStatus Instance::onData(Buffer::Instance&, bool /* end_stream */) {
  if (status_ == Status::NotStarted) {
    // By waiting to invoke the check at onData() the call to authorization service will have
    // sufficient information to fillout the checkRequest_.
    callCheck();
  }
  return status_ == Status::Calling ? Network::FilterStatus::StopIteration
                                    : Network::FilterStatus::Continue;
}

Network::FilterStatus Instance::onNewConnection() {
  // Wait till onData() happens.
  return Network::FilterStatus::Continue;
}

void Instance::onEvent(Network::ConnectionEvent event) {
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

void Instance::onComplete(CheckStatus status) {
  status_ = Status::Complete;
  config_->stats().active_.dec();

  switch (status) {
  case CheckStatus::OK:
    config_->stats().ok_.inc();
    break;
  case CheckStatus::Error:
    config_->stats().error_.inc();
    break;
  case CheckStatus::Denied:
    config_->stats().denied_.inc();
    break;
  }

  // Fail open only if configured to do so and if the check status was a error.
  if (status == CheckStatus::Denied ||
      (status == CheckStatus::Error && !config_->failureModeAllow())) {
    config_->stats().cx_closed_.inc();
    filter_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
  } else {
    // We can get completion inline, so only call continue if that isn't happening.
    if (!calling_check_) {
      filter_callbacks_->continueReading();
    }
  }
}

} // namespace TcpFilter
} // namespace ExtAuthz
} // namespace Envoy
