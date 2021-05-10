#include "extensions/filters/http/ext_proc/processor_state.h"

#include "extensions/filters/http/ext_proc/ext_proc.h"
#include "extensions/filters/http/ext_proc/mutation_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode;

using envoy::service::ext_proc::v3alpha::BodyResponse;
using envoy::service::ext_proc::v3alpha::HeadersResponse;
using envoy::service::ext_proc::v3alpha::TrailersResponse;

void ProcessorState::startMessageTimer(Event::TimerCb cb, std::chrono::milliseconds timeout) {
  if (!message_timer_) {
    message_timer_ = filter_callbacks_->dispatcher().createTimer(cb);
  }
  message_timer_->enableTimer(timeout);
}

bool ProcessorState::handleHeadersResponse(const HeadersResponse& response) {
  if (callback_state_ == CallbackState::HeadersCallback) {
    ENVOY_LOG(debug, "applying headers response");
    MutationUtils::applyCommonHeaderResponse(response, *headers_);
    if (response.response().clear_route_cache()) {
      filter_callbacks_->clearRouteCache();
    }
    callback_state_ = CallbackState::Idle;
    clearWatermark();
    message_timer_->disableTimer();

    if (body_mode_ == ProcessingMode::BUFFERED) {
      if (complete_body_available_) {
        // If we get here, then all the body data came in before the header message
        // was complete, and the server wants the body. So, don't continue filter
        // processing, but send the buffered request body now.
        ENVOY_LOG(debug, "Sending buffered request body message");
        filter_.sendBufferedData(*this, true);
      }

      // Otherwise, we're not ready to continue processing because then
      // we won't be able to modify the headers any more, so do nothing and
      // let the doData callback handle body chunks until the end is reached.
      return true;
    }

    if (send_trailers_ && trailers_available_) {
      // Trailers came in while we were waiting for this response, and the server
      // is not interested in the body, so send them now.
      filter_.sendTrailers(*this, *trailers_);
      return true;
    }

    // If we got here, then the processor doesn't care about the body or is not ready for
    // trailers, so we can just continue.
    headers_ = nullptr;
    continueProcessing();
    return true;
  }
  return false;
}

bool ProcessorState::handleBodyResponse(const BodyResponse& response) {
  if (callback_state_ == CallbackState::BufferedBodyCallback) {
    ENVOY_LOG(debug, "Applying body response to buffered data");
    modifyBufferedData([this, &response](Buffer::Instance& data) {
      MutationUtils::applyCommonBodyResponse(response, headers_, data);
    });
    if (response.response().clear_route_cache()) {
      filter_callbacks_->clearRouteCache();
    }
    headers_ = nullptr;
    callback_state_ = CallbackState::Idle;
    message_timer_->disableTimer();

    if (send_trailers_ && trailers_available_) {
      // Trailers came in while we were waiting for this response, and the server
      // asked to see them -- send them now.
      filter_.sendTrailers(*this, *trailers_);
      return true;
    }

    continueProcessing();
    return true;
  }
  return false;
}

bool ProcessorState::handleTrailersResponse(const TrailersResponse& response) {
  if (callback_state_ == CallbackState::TrailersCallback) {
    ENVOY_LOG(debug, "Applying response to buffered trailers");
    if (response.has_header_mutation()) {
      MutationUtils::applyHeaderMutations(response.header_mutation(), *trailers_);
    }
    trailers_ = nullptr;
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
    continueProcessing();
    callback_state_ = CallbackState::Idle;
  }
}

void ProcessorState::cleanUpTimer() const {
  if (message_timer_ && message_timer_->enabled()) {
    message_timer_->disableTimer();
  }
}

void DecodingProcessorState::setProcessingModeInternal(const ProcessingMode& mode) {
  // Account for the different default behaviors of headers and trailers --
  // headers are sent by default and trailers are not.
  send_headers_ = mode.request_header_mode() != ProcessingMode::SKIP;
  send_trailers_ = mode.request_trailer_mode() == ProcessingMode::SEND;
  body_mode_ = mode.request_body_mode();
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

void EncodingProcessorState::setProcessingModeInternal(const ProcessingMode& mode) {
  // Account for the different default behaviors of headers and trailers --
  // headers are sent by default and trailers are not.
  send_headers_ = mode.response_header_mode() != ProcessingMode::SKIP;
  send_trailers_ = mode.response_trailer_mode() == ProcessingMode::SEND;
  body_mode_ = mode.response_body_mode();
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
