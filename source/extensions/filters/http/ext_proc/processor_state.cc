#include "extensions/filters/http/ext_proc/processor_state.h"

#include "extensions/filters/http/ext_proc/mutation_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::service::ext_proc::v3alpha::BodyResponse;
using envoy::service::ext_proc::v3alpha::HeadersResponse;

void ProcessorState::startMessageTimer(Event::TimerCb cb, std::chrono::milliseconds timeout) {
  if (!message_timer_) {
    message_timer_ = filter_callbacks_->dispatcher().createTimer(cb);
  }
  message_timer_->enableTimer(timeout);
}

bool ProcessorState::handleHeadersResponse(const HeadersResponse& response) {
  if (callback_state_ == CallbackState::Headers) {
    ENVOY_LOG(debug, "applying headers response");
    MutationUtils::applyCommonHeaderResponse(response, *headers_);
    headers_ = nullptr;
    callback_state_ = CallbackState::Idle;
    message_timer_->disableTimer();
    continueProcessing();
    return true;
  }
  return false;
}

bool ProcessorState::handleBodyResponse(const BodyResponse& response) {
  if (callback_state_ == CallbackState::BufferedBody) {
    ENVOY_LOG(debug, "Applying body response to buffered data");
    modifyBufferedData([&response](Buffer::Instance& data) {
      MutationUtils::applyCommonBodyResponse(response, data);
    });
    callback_state_ = CallbackState::Idle;
    message_timer_->disableTimer();
    continueProcessing();
    return true;
  }
  return false;
}

void ProcessorState::clearAsyncState() {
  cleanUpTimer();
  if (callback_state_ != CallbackState::Idle) {
    callback_state_ = CallbackState::Idle;
    continueProcessing();
  }
}

void ProcessorState::cleanUpTimer() const {
  if (message_timer_ && message_timer_->enabled()) {
    message_timer_->disableTimer();
  }
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
