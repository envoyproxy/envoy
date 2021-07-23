#include "source/extensions/filters/http/ext_proc/processor_state.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"
#include "source/extensions/filters/http/ext_proc/mutation_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode;
using envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode_BodySendMode;

using envoy::service::ext_proc::v3alpha::BodyResponse;
using envoy::service::ext_proc::v3alpha::CommonResponse;
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
    const auto& common_response = response.response();
    MutationUtils::applyCommonHeaderResponse(response, *headers_);
    if (response.response().clear_route_cache()) {
      filter_callbacks_->clearRouteCache();
    }
    callback_state_ = CallbackState::Idle;
    message_timer_->disableTimer();

    if (common_response.status() == CommonResponse::CONTINUE_AND_REPLACE) {
      ENVOY_LOG(debug, "Replacing complete message");
      // Completely replace the body that may already exist.
      if (common_response.has_body_mutation()) {
        // Always remove the content-length header if changing the body.
        // The proxy can restore it later if it needs to.
        headers_->removeContentLength();
        body_replaced_ = true;
        if (bufferedData() == nullptr) {
          Buffer::OwnedImpl new_body;
          MutationUtils::applyBodyMutations(common_response.body_mutation(), new_body);
          addBufferedData(new_body);
        } else {
          modifyBufferedData([&common_response](Buffer::Instance& buf) {
            MutationUtils::applyBodyMutations(common_response.body_mutation(), buf);
          });
        }
      }

      // Once this message is received, we won't send anything more on this request
      // or response to the processor. Clear flags to make sure.
      body_mode_ = ProcessingMode::NONE;
      send_trailers_ = false;
      clearWatermark();

    } else {
      if (no_body_) {
        // Fall through if there was never a body in the first place.
      } else if (body_mode_ == ProcessingMode::BUFFERED) {
        if (complete_body_available_) {
          // If we get here, then all the body data came in before the header message
          // was complete, and the server wants the body. So, don't continue filter
          // processing, but send the buffered request body now.
          ENVOY_LOG(debug, "Sending buffered request body message");
          filter_.sendBufferedData(*this, ProcessorState::CallbackState::BufferedBodyCallback,
                                   true);
        }
        // Otherwise, we're not ready to continue processing because then
        // we won't be able to modify the headers any more, so do nothing and
        // let the doData callback handle body chunks until the end is reached.
        clearWatermark();
        return true;
      } else if (body_mode_ == ProcessingMode::STREAMED) {
        if (complete_body_available_) {
          // All data came in before headers callback, so act just as if we were buffering
          // since effectively this is the same thing.
          ENVOY_LOG(debug, "Sending buffered body data for whole message");
          filter_.sendBufferedData(*this, ProcessorState::CallbackState::BufferedBodyCallback,
                                   true);
          clearWatermark();
          return true;
        }
        if (hasBufferedData()) {
          // We now know that we need to process what we have buffered in streaming mode.
          // Move the current buffer into the queue for remote processing and clear the
          // buffered data.
          Buffer::OwnedImpl buffered_chunk;
          modifyBufferedData(
              [&buffered_chunk](Buffer::Instance& data) { buffered_chunk.move(data); });
          ENVOY_LOG(debug, "Sending first chunk using buffered data");
          filter_.sendBodyChunk(*this, buffered_chunk,
                                ProcessorState::CallbackState::StreamedBodyCallback, false);
          enqueueStreamingChunk(buffered_chunk, false, true);
        }
        if (queueBelowLowLimit()) {
          clearWatermark();
        }
        continueIfNecessary();
        return true;
      }
      if (send_trailers_ && trailers_available_) {
        // Trailers came in while we were waiting for this response, and the server
        // is not interested in the body, so send them now.
        filter_.sendTrailers(*this, *trailers_);
        clearWatermark();
        return true;
      }
    }

    // If we got here, then the processor doesn't care about the body or is not ready for
    // trailers, so we can just continue.
    headers_ = nullptr;
    continueIfNecessary();
    clearWatermark();
    return true;
  }
  return false;
}

bool ProcessorState::handleBodyResponse(const BodyResponse& response) {
  bool should_continue = false;
  if (callback_state_ == CallbackState::BufferedBodyCallback ||
      callback_state_ == CallbackState::StreamedBodyCallback ||
      callback_state_ == CallbackState::StreamedBodyCallbackFinishing) {
    ENVOY_LOG(debug, "Processing body response");
    if (callback_state_ == CallbackState::BufferedBodyCallback) {
      ENVOY_LOG(debug, "Applying body response to buffered data. State = {}", callback_state_);
      modifyBufferedData([this, &response](Buffer::Instance& data) {
        MutationUtils::applyCommonBodyResponse(response, headers_, data);
      });
      clearWatermark();
      callback_state_ = CallbackState::Idle;
      should_continue = true;

    } else if (callback_state_ == CallbackState::StreamedBodyCallback ||
               callback_state_ == CallbackState::StreamedBodyCallbackFinishing) {
      bool delivered_one = false;
      while (auto queued_chunk = dequeueStreamingChunk(delivered_one)) {
        // Loop through queue in case some of it is chunks that were never
        // delivered because the processing mode changed.
        auto chunk = std::move(*queued_chunk);
        if (chunk->delivered) {
          ENVOY_LOG(debug, "Applying body response to chunk of data. Size = {}",
                    chunk->data.length());
          MutationUtils::applyCommonBodyResponse(response, nullptr, chunk->data);
          delivered_one = true;
          // After we have delivered one chunk, don't process anything
          // more from the queue unless it was never sent to the server.
        }
        should_continue = chunk->end_stream;
        if (chunk->data.length() > 0) {
          ENVOY_LOG(trace, "Injecting {} bytes of data to filter stream", chunk->data.length());
          injectDataToFilterChain(chunk->data, false);
        }
      }
      if (queueBelowLowLimit()) {
        clearWatermark();
      }
      if (chunks_for_processing_.empty()) {
        callback_state_ = CallbackState::Idle;
      }
    }

    if (response.response().clear_route_cache()) {
      filter_callbacks_->clearRouteCache();
    }
    message_timer_->disableTimer();
    headers_ = nullptr;

    if (send_trailers_ && trailers_available_) {
      // Trailers came in while we were waiting for this response, and the server
      // asked to see them -- send them now.
      filter_.sendTrailers(*this, *trailers_);
      return true;
    }

    if (should_continue) {
      continueIfNecessary();
    }
    return true;
  }

  return false;
}

bool ProcessorState::handleTrailersResponse(const TrailersResponse& response) {
  if (callback_state_ == CallbackState::TrailersCallback) {
    ENVOY_LOG(debug, "Applying response to buffered trailers");
    if (response.has_header_mutation()) {
      MutationUtils::applyHeaderMutations(response.header_mutation(), *trailers_, false);
    }
    trailers_ = nullptr;
    callback_state_ = CallbackState::Idle;
    message_timer_->disableTimer();
    continueIfNecessary();
    return true;
  }
  return false;
}

void ProcessorState::enqueueStreamingChunk(Buffer::Instance& data, bool end_stream,
                                           bool delivered) {
  bytes_enqueued_ += data.length();
  auto next_chunk = std::make_unique<QueuedChunk>();
  next_chunk->data.move(data);
  next_chunk->end_stream = end_stream;
  next_chunk->delivered = delivered;
  chunks_for_processing_.push_back(std::move(next_chunk));
  if (queueOverHighLimit()) {
    requestWatermark();
  }
}

absl::optional<QueuedChunkPtr> ProcessorState::dequeueStreamingChunk(bool undelivered_only) {
  if (chunks_for_processing_.empty()) {
    return absl::nullopt;
  }
  if (undelivered_only && chunks_for_processing_.front()->delivered) {
    return absl::nullopt;
  }
  QueuedChunkPtr chunk = std::move(chunks_for_processing_.front());
  chunks_for_processing_.pop_front();
  bytes_enqueued_ -= chunk->data.length();
  return chunk;
}

void ProcessorState::clearAsyncState() {
  cleanUpTimer();
  while (auto queued_chunk = dequeueStreamingChunk(false)) {
    auto chunk = std::move(*queued_chunk);
    ENVOY_LOG(trace, "Injecting leftover buffer of {} bytes", chunk->data.length());
    injectDataToFilterChain(chunk->data, false);
  }
  clearWatermark();
  continueIfNecessary();
  callback_state_ = CallbackState::Idle;
}

void ProcessorState::cleanUpTimer() const {
  if (message_timer_ && message_timer_->enabled()) {
    message_timer_->disableTimer();
  }
}

void ProcessorState::setBodyMode(ProcessingMode_BodySendMode body_mode) {
  body_mode_ = body_mode;
  if (callback_state_ == CallbackState::StreamedBodyCallback &&
      body_mode != ProcessingMode::STREAMED) {
    // Special handling for when the processing mode is changed while
    // streaming.
    callback_state_ = CallbackState::StreamedBodyCallbackFinishing;
  }
}

void ProcessorState::continueIfNecessary() {
  if (paused_) {
    ENVOY_LOG(debug, "Continuing processing");
    paused_ = false;
    continueProcessing();
  }
}

void DecodingProcessorState::setProcessingModeInternal(const ProcessingMode& mode) {
  // Account for the different default behaviors of headers and trailers --
  // headers are sent by default and trailers are not.
  send_headers_ = mode.request_header_mode() != ProcessingMode::SKIP;
  send_trailers_ = mode.request_trailer_mode() == ProcessingMode::SEND;
  setBodyMode(mode.request_body_mode());
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
  setBodyMode(mode.response_body_mode());
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
