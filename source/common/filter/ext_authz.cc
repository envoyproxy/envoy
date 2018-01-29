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
  std::string final_prefix = fmt::format("ext_authz.{}.", name);
  return {ALL_TCP_EXT_AUTHZ_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                  POOL_GAUGE_PREFIX(scope, final_prefix))};
}

void Instance::setCheckReqGenerator(CheckRequestGenerator* crg) {
  ASSERT(check_req_generator_ == nullptr);
  check_req_generator_ = CheckRequestGeneratorPtr{std::move(crg)};
}

void Instance::callCheck() {
  envoy::service::auth::v2::CheckRequest request;
  if (check_req_generator_ == nullptr) {
    setCheckReqGenerator(new ExtAuthzCheckRequestGenerator());
  }
  check_req_generator_->createTcpCheck(filter_callbacks_, request);

  status_ = Status::Calling;
  filter_callbacks_->connection().readDisable(true);
  config_->stats().active_.inc();
  config_->stats().total_.inc();

  calling_check_ = true;
  client_->check(*this, request, Tracing::NullSpan::instance());
  calling_check_ = false;
}

Network::FilterStatus Instance::onData(Buffer::Instance&) {
  if (status_ == Status::NotStarted) {
    // If the ssl handshake was not done and data is the next event!
    callCheck();
  }
  return status_ == Status::Calling ? Network::FilterStatus::StopIteration
                                    : Network::FilterStatus::Continue;
}

Network::FilterStatus Instance::onNewConnection() {
  // Wait till the next event occurs.
  return Network::FilterStatus::Continue;
}

void Instance::onEvent(Network::ConnectionEvent event) {
  // Make sure that any pending request in the client is cancelled. This will be NOP if the
  // request already completed.
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    if (status_ == Status::Calling) {
      client_->cancel();
      config_->stats().active_.dec();
    }
  } else {
    // SSL connection is post TCP newConnection. Therefore the ext_authz check in onEvent.
    // if the ssl handshake was successful then it will invoke the
    // Network::ConnectionEvent::Connected.
    if (status_ == Status::NotStarted) {
      callCheck();
    }
  }
}

void Instance::onComplete(CheckStatus status) {
  status_ = Status::Complete;
  filter_callbacks_->connection().readDisable(false);
  config_->stats().active_.dec();

  switch (status) {
  case CheckStatus::OK:
    config_->stats().ok_.inc();
    break;
  case CheckStatus::Error:
    config_->stats().error_.inc();
    break;
  case CheckStatus::Denied:
    config_->stats().unauthz_.inc();
    break;
  }

  // We fail open if there is an error contacting the service.
  if (status == CheckStatus::Denied || (status == CheckStatus::Error && !config_->failOpen())) {
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
