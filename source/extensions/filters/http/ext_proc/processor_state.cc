#include "source/extensions/filters/http/ext_proc/processor_state.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"
#include "source/extensions/filters/http/ext_proc/mutation_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode_BodySendMode;

using envoy::service::ext_proc::v3::BodyResponse;
using envoy::service::ext_proc::v3::CommonResponse;
using envoy::service::ext_proc::v3::HeadersResponse;
using envoy::service::ext_proc::v3::TrailersResponse;

void ProcessorState::onStartProcessorCall(Event::TimerCb cb, std::chrono::milliseconds timeout,
                                          CallbackState callback_state) {
  ENVOY_LOG(debug, "Start external processing call");
  callback_state_ = callback_state;

  // Skip starting timer For FULL_DUPLEX_STREAMED body mode.
  if (bodyMode() != ProcessingMode::FULL_DUPLEX_STREAMED) {
    if (message_timer_ == nullptr) {
      message_timer_ = filter_callbacks_->dispatcher().createTimer(cb);
    }
    message_timer_->enableTimer(timeout);
    ENVOY_LOG(debug, "Traffic direction {}: {} ms timer enabled", trafficDirectionDebugStr(),
              timeout.count());
  }

  call_start_time_ = filter_callbacks_->dispatcher().timeSource().monotonicTime();
  new_timeout_received_ = false;
}

void ProcessorState::onFinishProcessorCall(Grpc::Status::GrpcStatus call_status,
                                           CallbackState next_state) {
  ENVOY_LOG(debug, "Finish external processing call");
  filter_.logGrpcStreamInfo();

  stopMessageTimer();

  if (call_start_time_.has_value()) {
    std::chrono::microseconds duration = std::chrono::duration_cast<std::chrono::microseconds>(
        filter_callbacks_->dispatcher().timeSource().monotonicTime() - call_start_time_.value());
    ExtProcLoggingInfo* logging_info = filter_.loggingInfo();
    if (logging_info != nullptr) {
      logging_info->recordGrpcCall(duration, call_status, callback_state_, trafficDirection());
    }
    call_start_time_ = absl::nullopt;
  }
  callback_state_ = next_state;
  new_timeout_received_ = false;
}

void ProcessorState::stopMessageTimer() {
  if (message_timer_) {
    ENVOY_LOG(debug, "Traffic direction {}: timer disabled", trafficDirectionDebugStr());
    message_timer_->disableTimer();
  }
}

// Server sends back response to stop the original timer and start a new timer.
// Do not change call_start_time_ since that call has not been responded yet.
// Do not change callback_state_ either.
bool ProcessorState::restartMessageTimer(const uint32_t message_timeout_ms) {
  if (message_timer_ && message_timer_->enabled() && !new_timeout_received_) {
    ENVOY_LOG(debug,
              "Traffic direction {}: Server needs more time to process the request, start a "
              "new timer with timeout {} ms",
              trafficDirectionDebugStr(), message_timeout_ms);
    message_timer_->disableTimer();
    message_timer_->enableTimer(std::chrono::milliseconds(message_timeout_ms));
    // Setting this flag to true to make sure Envoy ignore the future such
    // messages when in the same state.
    new_timeout_received_ = true;
    return true;
  } else {
    ENVOY_LOG(debug,
              "Traffic direction {}: Ignoring server new timeout message {} ms due to timer not "
              "enabled or not the 1st such message",
              trafficDirectionDebugStr(), message_timeout_ms);
    return false;
  }
}

void ProcessorState::sendBufferedDataInStreamedMode(bool end_stream) {
  // Process the data being buffered in streaming mode.
  // Move the current buffer into the queue for remote processing and clear the buffered data.
  if (hasBufferedData()) {
    Buffer::OwnedImpl buffered_chunk;
    modifyBufferedData([&buffered_chunk](Buffer::Instance& data) { buffered_chunk.move(data); });
    ENVOY_LOG(debug, "Sending a chunk of buffered data ({})", buffered_chunk.length());
    // Need to first enqueue the data into the chunk queue before sending.
    auto req = filter_.setupBodyChunk(*this, buffered_chunk, end_stream);
    enqueueStreamingChunk(buffered_chunk, end_stream);
    filter_.sendBodyChunk(*this, ProcessorState::CallbackState::StreamedBodyCallback, req);
  }
  if (queueBelowLowLimit()) {
    clearWatermark();
  }
}

absl::Status ProcessorState::processHeaderMutation(const CommonResponse& common_response) {
  ENVOY_LOG(debug, "Applying header mutations");
  const auto mut_status = MutationUtils::applyHeaderMutations(
      common_response.header_mutation(), *headers_,
      common_response.status() == CommonResponse::CONTINUE_AND_REPLACE,
      filter_.config().mutationChecker(), filter_.stats().rejected_header_mutations_,
      shouldRemoveContentLength());
  return mut_status;
}

ProcessorState::CallbackState
ProcessorState::getCallbackStateAfterHeaderResp(const CommonResponse& common_response) const {
  if (common_response.status() == CommonResponse::CONTINUE_AND_REPLACE) {
    return ProcessorState::CallbackState::Idle;
  }

  if ((bodyMode() == ProcessingMode::STREAMED &&
       filter_.config().sendBodyWithoutWaitingForHeaderResponse()) &&
      !chunk_queue_.empty()) {
    return ProcessorState::CallbackState::StreamedBodyCallback;
  }

  if (bodyMode() == ProcessingMode::FULL_DUPLEX_STREAMED) {
    if (bodyReceived()) {
      return ProcessorState::CallbackState::StreamedBodyCallback;
    }
    if (trailers_available_) {
      return ProcessorState::CallbackState::TrailersCallback;
    }
  }

  return ProcessorState::CallbackState::Idle;
}

absl::Status ProcessorState::handleHeadersResponse(const HeadersResponse& response) {
  if (callback_state_ != CallbackState::HeadersCallback) {
    return absl::FailedPreconditionError("spurious message");
  }

  ENVOY_LOG(debug, "applying headers response. body mode = {}",
            ProcessingMode::BodySendMode_Name(body_mode_));

  const auto& common_response = response.response();

  // Process header mutations if present
  if (common_response.has_header_mutation()) {
    const auto mut_status = processHeaderMutation(common_response);
    if (!mut_status.ok()) {
      return mut_status;
    }
  }

  clearRouteCache(common_response);
  onFinishProcessorCall(Grpc::Status::Ok, getCallbackStateAfterHeaderResp(common_response));

  if (common_response.status() == CommonResponse::CONTINUE_AND_REPLACE) {
    return handleHeadersResponseWithContinueAndReplace(common_response);
  }

  return handleHeadersResponseWithContinue();
}

absl::Status
ProcessorState::handleHeadersResponseWithContinueAndReplace(const CommonResponse& common_response) {
  ENVOY_LOG(debug, "Replacing complete message");

  if (common_response.has_body_mutation()) {
    applyReplacementBodyMutation(common_response);
  }

  // Clear any leftover data and disable further processing
  clearStreamingChunk();
  body_mode_ = ProcessingMode::NONE;
  send_trailers_ = false;
  clearWatermark();

  return finalizeResponse();
}

void ProcessorState::applyReplacementBodyMutation(const CommonResponse& common_response) {
  // Remove content length since it won't match the new body
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

absl::Status ProcessorState::handleHeadersResponseWithContinue() {
  if (no_body_) {
    ENVOY_LOG(debug, "The message had no body");
    return finalizeContinueResponse();
  }

  if (complete_body_available_ && body_mode_ != ProcessingMode::NONE) {
    return handleCompleteBodyAvailable();
  }

  switch (body_mode_) {
  case ProcessingMode::BUFFERED:
    return handleBufferedMode();
  case ProcessingMode::STREAMED:
    return handleStreamedMode();
  case ProcessingMode::FULL_DUPLEX_STREAMED:
    return handleFullDuplexStreamedMode();
  case ProcessingMode::BUFFERED_PARTIAL:
    return handleBufferedPartialMode();
  default:
    return finalizeContinueResponse();
  }
}

absl::Status ProcessorState::handleCompleteBodyAvailable() {
  if (callback_state_ == CallbackState::Idle) {
    if (bufferedData()) {
      auto req = filter_.setupBodyChunk(*this, *bufferedData(), !trailers_available_);
      filter_.sendBodyChunk(*this, ProcessorState::CallbackState::BufferedBodyCallback, req);
      clearWatermark();
      return absl::OkStatus();
    }
  } else {
    sendBufferedDataInStreamedMode(!trailers_available_);
  }
  return absl::OkStatus();
}

absl::Status ProcessorState::handleBufferedMode() {
  clearWatermark();
  return absl::OkStatus();
}

absl::Status ProcessorState::handleStreamedMode() {
  sendBufferedDataInStreamedMode(false);
  continueIfNecessary();
  return absl::OkStatus();
}

absl::Status ProcessorState::handleFullDuplexStreamedMode() {
  continueIfNecessary();
  return absl::OkStatus();
}

absl::Status ProcessorState::handleBufferedPartialMode() {
  if (hasBufferedData()) {
    enqueueBufferedDataToChunkQueue();
  }

  if (queueOverHighLimit()) {
    return sendBufferedPartialData();
  }

  clearWatermark();
  return absl::OkStatus();
}

void ProcessorState::enqueueBufferedDataToChunkQueue() {
  ENVOY_LOG(debug, "Enqueuing body data buffered so far");
  Buffer::OwnedImpl buffered_chunk;
  modifyBufferedData([&buffered_chunk](Buffer::Instance& data) { buffered_chunk.move(data); });
  enqueueStreamingChunk(buffered_chunk, false);
}

absl::Status ProcessorState::sendBufferedPartialData() {
  const auto& all_data = consolidateStreamedChunks();
  ENVOY_LOG(debug,
            "Sending {} bytes of data end_stream {} in buffered partial mode before end stream",
            chunkQueue().receivedData().length(), all_data.end_stream);
  auto req = filter_.setupBodyChunk(*this, chunkQueue().receivedData(), false);
  filter_.sendBodyChunk(*this, ProcessorState::CallbackState::BufferedPartialBodyCallback, req);
  return absl::OkStatus();
}

absl::Status ProcessorState::finalizeContinueResponse() {
  if (send_trailers_ && trailers_available_) {
    filter_.sendTrailers(*this, *trailers_);
    clearWatermark();
    return absl::OkStatus();
  }

  return finalizeResponse();
}

absl::Status ProcessorState::finalizeResponse() {
  ENVOY_LOG(trace, "Clearing stored headers");
  headers_ = nullptr;
  continueIfNecessary();
  clearWatermark();
  return absl::OkStatus();
}

// TODO(#37048) Refactoring this function by adding one helper function for each callback state.
absl::Status ProcessorState::handleBodyResponse(const BodyResponse& response) {
  bool should_continue = false;
  const auto& common_response = response.response();
  if (callback_state_ == CallbackState::BufferedBodyCallback ||
      callback_state_ == CallbackState::StreamedBodyCallback ||
      callback_state_ == CallbackState::BufferedPartialBodyCallback) {
    ENVOY_LOG(debug, "Processing body response");
    if (callback_state_ == CallbackState::BufferedBodyCallback) {
      if (common_response.has_header_mutation()) {
        if (headers_ != nullptr) {
          const auto mut_status = processHeaderMutation(common_response);
          if (!mut_status.ok()) {
            return mut_status;
          }
        } else {
          ENVOY_LOG(debug, "Response had header mutations but headers aren't available");
        }
      }

      if (common_response.has_body_mutation()) {
        if (headers_ != nullptr && headers_->ContentLength() != nullptr) {
          size_t content_length = 0;
          // When body mutation by external processor is enabled, content-length header is only
          // allowed in BUFFERED mode. If its value doesn't match the length of mutated body, the
          // corresponding body mutation will be rejected and local reply will be sent with an error
          // message.
          if (absl::SimpleAtoi(headers_->getContentLengthValue(), &content_length) &&
              content_length != common_response.body_mutation().body().size()) {
            return absl::InternalError(
                "mismatch between content length and the length of the mutated body");
          }
        }
        ENVOY_LOG(debug, "Applying body response to buffered data. State = {}",
                  static_cast<int>(callback_state_));
        modifyBufferedData([&common_response](Buffer::Instance& data) {
          MutationUtils::applyBodyMutations(common_response.body_mutation(), data);
        });
      }
      clearWatermark();
      onFinishProcessorCall(Grpc::Status::Ok);
      should_continue = true;
    } else if (callback_state_ == CallbackState::StreamedBodyCallback) {
      absl::StatusOr<bool> result = handleBodyInStreamedState(common_response);
      if (!result.ok()) {
        return result.status();
      }
      should_continue = *result;
    } else if (callback_state_ == CallbackState::BufferedPartialBodyCallback) {
      // Apply changes to the buffer that we sent to the server
      Buffer::OwnedImpl chunk_data;
      auto chunk = dequeueStreamingChunk(chunk_data);
      ENVOY_BUG(chunk != nullptr, "Bad partial body callback state");
      if (common_response.has_header_mutation()) {
        if (headers_ != nullptr) {
          const auto mut_status = processHeaderMutation(common_response);
          if (!mut_status.ok()) {
            return mut_status;
          }
        } else {
          ENVOY_LOG(debug, "Response had header mutations but headers aren't available");
        }
      }
      if (common_response.has_body_mutation()) {
        MutationUtils::applyBodyMutations(common_response.body_mutation(), chunk_data);
      }
      if (chunk_data.length() > 0) {
        ENVOY_LOG(trace, "Injecting {} bytes of processed data to filter stream",
                  chunk_data.length());
        injectDataToFilterChain(chunk_data, chunk->end_stream);
      }
      should_continue = true;
      clearWatermark();
      onFinishProcessorCall(Grpc::Status::Ok);
      partial_body_processed_ = true;

      // If anything else is left on the queue, inject it too
      if (chunkQueue().receivedData().length() > 0) {
        const auto& all_data = consolidateStreamedChunks();
        ENVOY_LOG(trace, "Injecting {} bytes of leftover data to filter stream",
                  chunkQueue().receivedData().length());
        injectDataToFilterChain(chunkQueue().receivedData(), all_data.end_stream);
      }
    } else {
      // Fake a grpc error when processor state and received message type doesn't match, beware this
      // is not an error from grpc.
      onFinishProcessorCall(Grpc::Status::FailedPrecondition);
    }

    clearRouteCache(common_response);
    headers_ = nullptr;

    // Send trailers if they are available and no data pending for processing.
    if (send_trailers_ && trailers_available_ && chunk_queue_.empty()) {
      filter_.sendTrailers(*this, *trailers_);
      return absl::OkStatus();
    }

    if (should_continue || (trailers_available_ && chunk_queue_.empty())) {
      continueIfNecessary();
    }
    return absl::OkStatus();
  }

  return absl::FailedPreconditionError("spurious message");
}

// If the body mode is FULL_DUPLEX_STREAMED, then the trailers response may come back when
// the state is still waiting for body response.
absl::Status ProcessorState::handleTrailersResponse(const TrailersResponse& response) {
  if (callback_state_ == CallbackState::TrailersCallback ||
      bodyMode() == ProcessingMode::FULL_DUPLEX_STREAMED) {
    ENVOY_LOG(debug, "Applying response to buffered trailers, body_mode_ {}",
              ProcessingMode::BodySendMode_Name(body_mode_));
    if (response.has_header_mutation()) {
      auto mut_status = MutationUtils::applyHeaderMutations(
          response.header_mutation(), *trailers_, false, filter_.config().mutationChecker(),
          filter_.stats().rejected_header_mutations_);
      if (!mut_status.ok()) {
        return mut_status;
      }
    }
    trailers_ = nullptr;
    onFinishProcessorCall(Grpc::Status::Ok);
    continueIfNecessary();
    return absl::OkStatus();
  }
  return absl::FailedPreconditionError("spurious message");
}

void ProcessorState::enqueueStreamingChunk(Buffer::Instance& data, bool end_stream) {
  chunk_queue_.push(data, end_stream);
  if (queueOverHighLimit()) {
    requestWatermark();
  }
}

QueuedChunkPtr ProcessorState::dequeueStreamingChunk(Buffer::OwnedImpl& out_data) {
  return chunk_queue_.pop(out_data);
}

void ProcessorState::clearAsyncState() {
  onFinishProcessorCall(Grpc::Status::Aborted);
  if (chunkQueue().receivedData().length() > 0) {
    const auto& all_data = consolidateStreamedChunks();
    ENVOY_LOG(trace, "Injecting leftover buffer of {} bytes", chunkQueue().receivedData().length());
    injectDataToFilterChain(chunkQueue().receivedData(), all_data.end_stream);
  }
  clearWatermark();
  continueIfNecessary();
}

void ProcessorState::setBodyMode(ProcessingMode_BodySendMode body_mode) { body_mode_ = body_mode; }

void ProcessorState::continueIfNecessary() {
  if (paused_) {
    ENVOY_LOG(debug, "Continuing processing");
    paused_ = false;
    continueProcessing();
  }
}

bool ProcessorState::handleStreamedBodyResponse(const CommonResponse& common_response) {
  Buffer::OwnedImpl chunk_data;
  QueuedChunkPtr chunk = dequeueStreamingChunk(chunk_data);
  ENVOY_BUG(chunk != nullptr, "Bad streamed body callback state");
  if (common_response.has_body_mutation()) {
    ENVOY_LOG(debug, "Applying body response to chunk of data. Size = {}", chunk->length);
    MutationUtils::applyBodyMutations(common_response.body_mutation(), chunk_data);
  }
  bool should_continue = chunk->end_stream;
  if (chunk_data.length() > 0) {
    ENVOY_LOG(trace, "Injecting {} bytes of data to filter stream", chunk_data.length());
    injectDataToFilterChain(chunk_data, chunk->end_stream);
  }

  if (queueBelowLowLimit()) {
    clearWatermark();
  }
  if (chunk_queue_.empty()) {
    onFinishProcessorCall(Grpc::Status::Ok);
  } else {
    onFinishProcessorCall(Grpc::Status::Ok, callback_state_);
  }

  return should_continue;
}

bool ProcessorState::handleDuplexStreamedBodyResponse(const CommonResponse& common_response) {
  const envoy::service::ext_proc::v3::StreamedBodyResponse& streamed_response =
      common_response.body_mutation().streamed_response();
  const std::string& body = streamed_response.body();
  const bool end_of_stream = streamed_response.end_of_stream();

  if (body.size() > 0) {
    Buffer::OwnedImpl buffer;
    buffer.add(body);
    ENVOY_LOG(trace,
              "Injecting {} bytes of data to filter stream in FULL_DUPLEX_STREAMED mode. "
              "end_of_stream is {}",
              buffer.length(), end_of_stream);
    injectDataToFilterChain(buffer, end_of_stream);
  }

  if (end_of_stream) {
    onFinishProcessorCall(Grpc::Status::Ok);
  } else {
    // Set the state to CallbackState::StreamedBodyCallback to wait for more bodies.
    // However, this could be the last chunk of body, and trailers are right after it.
    // The function to handle trailers response needs to consider this.
    onFinishProcessorCall(Grpc::Status::Ok, CallbackState::StreamedBodyCallback);
  }
  // If end_of_stream is true, Envoy should continue the filter chain operations.
  return end_of_stream;
}

absl::StatusOr<bool>
ProcessorState::handleBodyInStreamedState(const CommonResponse& common_response) {
  if (common_response.has_body_mutation() &&
      common_response.body_mutation().has_streamed_response()) {
    ENVOY_LOG(debug, "FULL_DUPLEX_STREAMED body response is received and body_mode_: {} ",
              ProcessingMode::BodySendMode_Name(body_mode_));
    // streamed_response will only be supported if the ext_proc filter has body_mode set to
    // FULL_DUPLEX_STREAMED.
    if (body_mode_ != ProcessingMode::FULL_DUPLEX_STREAMED) {
      return absl::FailedPreconditionError(
          "spurious message: streamed_response is received while body_mode_ is not "
          "FULL_DUPLEX_STREAMED");
    }
    return handleDuplexStreamedBodyResponse(common_response);
  } else {
    if (body_mode_ == ProcessingMode::FULL_DUPLEX_STREAMED) {
      return absl::FailedPreconditionError(
          "spurious message: Normal body mutation response is received while body_mode_ is "
          "FULL_DUPLEX_STREAMED");
    }
    return handleStreamedBodyResponse(common_response);
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

void DecodingProcessorState::clearRouteCache(const CommonResponse& common_response) {
  bool response_clear_route_cache = common_response.clear_route_cache();
  if (filter_.config().isUpstream()) {
    if (response_clear_route_cache) {
      filter_.stats().clear_route_cache_upstream_ignored_.inc();
      ENVOY_LOG(debug, "NOT clearing route cache. The filter is in upstream filter chain.");
    }
    return;
  }

  if (!common_response.has_header_mutation()) {
    if (response_clear_route_cache) {
      filter_.stats().clear_route_cache_ignored_.inc();
      ENVOY_LOG(debug, "NOT clearing route cache. No header mutation in the response");
    }
    return;
  }

  // Filter is in downstream and response has header mutation.
  switch (filter_.config().routeCacheAction()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor::DEFAULT:
    if (response_clear_route_cache) {
      ENVOY_LOG(debug, "Clearing route cache due to the filter RouterCacheAction is configured "
                       "with DEFAULT and response has clear_route_cache set.");
      decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
    }
    break;
  case envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor::CLEAR:
    ENVOY_LOG(debug,
              "Clearing route cache due to the filter RouterCacheAction is configured with CLEAR");
    decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
    break;
  case envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor::RETAIN:
    if (response_clear_route_cache) {
      filter_.stats().clear_route_cache_disabled_.inc();
      ENVOY_LOG(debug, "NOT clearing route cache, it is disabled by the filter config");
    }
    break;
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
    ENVOY_LOG(debug, "Watermark lowered on encoding");
    watermark_requested_ = false;
    encoder_callbacks_->onEncoderFilterBelowWriteBufferLowWatermark();
  }
}

void ChunkQueue::push(Buffer::Instance& data, bool end_stream) {
  // Adding the chunk into the queue.
  auto next_chunk = std::make_unique<QueuedChunk>();
  next_chunk->length = data.length();
  next_chunk->end_stream = end_stream;
  queue_.push_back(std::move(next_chunk));
  bytes_enqueued_ += data.length();

  // Adding the data to the buffer.
  received_data_.move(data);
}

QueuedChunkPtr ChunkQueue::pop(Buffer::OwnedImpl& out_data) {
  if (queue_.empty()) {
    return nullptr;
  }

  QueuedChunkPtr chunk = std::move(queue_.front());
  queue_.pop_front();
  bytes_enqueued_ -= chunk->length;

  // Move the corresponding data out.
  out_data.move(received_data_, chunk->length);
  return chunk;
}

const QueuedChunk& ChunkQueue::consolidate() {
  if (queue_.size() > 1) {
    auto new_chunk = std::make_unique<QueuedChunk>();
    new_chunk->end_stream = queue_.back()->end_stream;
    new_chunk->length = bytes_enqueued_;
    queue_.clear();
    queue_.push_front(std::move(new_chunk));
  }
  auto& chunk = *(queue_.front());
  return chunk;
}

void ChunkQueue::clear() {
  if (queue_.size() > 1) {
    received_data_.drain(received_data_.length());
    queue_.clear();
  }
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
