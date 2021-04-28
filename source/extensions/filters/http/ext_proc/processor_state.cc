#include "extensions/filters/http/ext_proc/processor_state.h"

#include "extensions/filters/http/ext_proc/ext_proc.h"
#include "extensions/filters/http/ext_proc/mutation_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode;
using envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode_BodySendMode;

using envoy::service::ext_proc::v3alpha::BodyResponse;
using envoy::service::ext_proc::v3alpha::HeadersResponse;

void ProcessorState::startMessageTimer(Event::TimerCb cb, std::chrono::milliseconds timeout) {
  if (!message_timer_) {
    message_timer_ = filter_callbacks_->dispatcher().createTimer(cb);
  }
  message_timer_->enableTimer(timeout);
}

bool ProcessorState::handleHeadersResponse(const HeadersResponse& response,
                                           ProcessingMode_BodySendMode body_mode) {
  if (callback_state_ == CallbackState::Headers) {
    ENVOY_LOG(debug, "applying headers response");
    MutationUtils::applyCommonHeaderResponse(response, *headers_);
    callback_state_ = CallbackState::Idle;
    clearWatermark();
    message_timer_->disableTimer();

    if (body_mode == ProcessingMode::BUFFERED) {
      if (body_send_deferred_) {
        // If we get here, then all the body data came in before the header message
        // was complete, and the server wants the body. So, don't continue filter
        // processing, but send the buffered request body now.
        ENVOY_LOG(debug, "Sending buffered request body message");
        callback_state_ = CallbackState::BufferedBody;
        startMessageTimer(std::bind(&Filter::onMessageTimeout, &filter_),
                          filter_.config().messageTimeout());
        filter_.sendBufferedData(*this, true);
      }

      // Otherwise, we're not ready to continue processing because then
      // we won't be able to modify the headers any more, so do nothing and
      // let the doData callback handle body chunks until the end is reached.
      return true;
    }

    // If we got here, then the processor doesn't care about the body, so we can just continue.
    headers_ = nullptr;
    continueProcessing();
    return true;
  }
  return false;
}

bool ProcessorState::handleBodyResponse(const BodyResponse& response) {
  if (callback_state_ == CallbackState::BufferedBody) {
    ENVOY_LOG(debug, "Applying body response to buffered data");
    modifyBufferedData([this, &response](Buffer::Instance& data) {
      MutationUtils::applyCommonBodyResponse(response, headers_, data);
    });
    headers_ = nullptr;
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

void DecodingProcessorState::requestWatermark() {
  if (!watermark_requested_) {
    ENVOY_LOG(debug, "Watermark raised on decoding");
    watermark_requested_ = true;
    decoder_callbacks_->onDecoderFilterAboveWriteBufferHighWatermark();
  }
}

void DecodingProcessorState::clearWatermark() {
  if (watermark_requested_) {
    ENVOY_LOG(debug, "Watermark lowered on decoding");
    watermark_requested_ = false;
    decoder_callbacks_->onDecoderFilterBelowWriteBufferLowWatermark();
  }
}

void EncodingProcessorState::requestWatermark() {
  if (!watermark_requested_) {
    ENVOY_LOG(debug, "Watermark raised on encoding");
    watermark_requested_ = true;
    encoder_callbacks_->onEncoderFilterAboveWriteBufferHighWatermark();
  }
}

void EncodingProcessorState::clearWatermark() {
  if (watermark_requested_) {
    ENVOY_LOG(debug, "Watermark lowered on decoding");
    watermark_requested_ = false;
    encoder_callbacks_->onEncoderFilterBelowWriteBufferLowWatermark();
  }
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
