#pragma once

#include <deque>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/http/ext_proc/v3/processing_mode.pb.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class Filter;

class QueuedChunk {
public:
  // True if this represents the last chunk in the stream
  bool end_stream = false;
  // True if the chunk was actually sent to the gRPC stream
  bool delivered = false;
  Buffer::OwnedImpl data;
};
using QueuedChunkPtr = std::unique_ptr<QueuedChunk>;

class ChunkQueue {
public:
  ChunkQueue() = default;
  ChunkQueue(const ChunkQueue&) = delete;
  ChunkQueue& operator=(const ChunkQueue&) = delete;
  uint32_t bytesEnqueued() const { return bytes_enqueued_; }
  bool empty() const { return queue_.empty(); }
  void push(Buffer::Instance& data, bool end_stream, bool delivered);
  absl::optional<QueuedChunkPtr> pop(bool undelivered_only);
  const QueuedChunk& consolidate(bool delivered);

private:
  // If we are in either streaming mode, store chunks that we received here,
  // and use the "delivered" flag to keep track of which ones were pushed
  // to the external processor. When matching responses come back for these
  // chunks, then they will be removed.
  std::deque<QueuedChunkPtr> queue_;
  // The total size of chunks in the queue.
  uint32_t bytes_enqueued_{};
};

class ProcessorState : public Logger::Loggable<Logger::Id::ext_proc> {
public:
  // This describes whether the filter is waiting for a response to a gRPC message.
  // We use it to determine how to respond to stream messages send back from
  // the external processor.
  enum class CallbackState {
    // Not waiting for anything
    Idle,
    // Waiting for a "headers" response. This may be true even if we started
    // to receive data frames for a message.
    HeadersCallback,
    // Waiting for a "body" response in buffered mode.
    BufferedBodyCallback,
    // Waiting for a "body" response in streaming mode.
    StreamedBodyCallback,
    // Waiting for a "body" response in streaming mode in the special case
    // in which the processing mode was changed while there were outstanding
    // messages sent to the processor.
    StreamedBodyCallbackFinishing,
    // Waiting for a body callback in "buffered partial" mode.
    BufferedPartialBodyCallback,
    // Waiting for a "trailers" response.
    TrailersCallback,
  };

  explicit ProcessorState(Filter& filter,
                          envoy::config::core::v3::TrafficDirection traffic_direction)
      : filter_(filter), watermark_requested_(false), paused_(false), no_body_(false),
        complete_body_available_(false), trailers_available_(false), body_replaced_(false),
        partial_body_processed_(false), traffic_direction_(traffic_direction) {}
  ProcessorState(const ProcessorState&) = delete;
  virtual ~ProcessorState() = default;
  ProcessorState& operator=(const ProcessorState&) = delete;

  envoy::config::core::v3::TrafficDirection trafficDirection() const { return traffic_direction_; }
  CallbackState callbackState() const { return callback_state_; }
  void setPaused(bool paused) { paused_ = paused; }

  bool completeBodyAvailable() const { return complete_body_available_; }
  void setCompleteBodyAvailable(bool d) { complete_body_available_ = d; }
  void setHasNoBody(bool b) { no_body_ = b; }
  void setTrailersAvailable(bool d) { trailers_available_ = d; }
  bool bodyReplaced() const { return body_replaced_; }
  bool partialBodyProcessed() const { return partial_body_processed_; }

  virtual void setProcessingMode(
      const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode& mode) PURE;
  bool sendHeaders() const { return send_headers_; }
  bool sendTrailers() const { return send_trailers_; }
  envoy::extensions::filters::http::ext_proc::v3::ProcessingMode_BodySendMode bodyMode() const {
    return body_mode_;
  }

  void setHeaders(Http::RequestOrResponseHeaderMap* headers) { headers_ = headers; }
  void setTrailers(Http::HeaderMap* trailers) { trailers_ = trailers; }

  void onStartProcessorCall(Event::TimerCb cb, std::chrono::milliseconds timeout,
                            CallbackState callback_state);
  void onFinishProcessorCall(Grpc::Status::GrpcStatus call_status,
                             CallbackState next_state = CallbackState::Idle);
  void stopMessageTimer();

  // Idempotent methods for watermarking the body
  virtual void requestWatermark() PURE;
  virtual void clearWatermark() PURE;

  absl::Status handleHeadersResponse(const envoy::service::ext_proc::v3::HeadersResponse& response);
  absl::Status handleBodyResponse(const envoy::service::ext_proc::v3::BodyResponse& response);
  absl::Status
  handleTrailersResponse(const envoy::service::ext_proc::v3::TrailersResponse& response);

  virtual const Buffer::Instance* bufferedData() const PURE;
  bool hasBufferedData() const { return bufferedData() != nullptr && bufferedData()->length() > 0; }
  virtual void addBufferedData(Buffer::Instance& data) const PURE;
  virtual void modifyBufferedData(std::function<void(Buffer::Instance&)> cb) const PURE;
  virtual void injectDataToFilterChain(Buffer::Instance& data, bool end_stream) PURE;
  virtual uint32_t bufferLimit() const PURE;

  // Move the contents of "data" into a QueuedChunk object on the streaming queue.
  void enqueueStreamingChunk(Buffer::Instance& data, bool end_stream, bool delivered);
  // If the queue has chunks, return the head of the queue.
  absl::optional<QueuedChunkPtr> dequeueStreamingChunk(bool undelivered_only) {
    return chunk_queue_.pop(undelivered_only);
  }
  // Consolidate all the chunks on the queue into a single one and return a reference.
  const QueuedChunk& consolidateStreamedChunks(bool delivered) {
    return chunk_queue_.consolidate(delivered);
  }
  bool queueOverHighLimit() const { return chunk_queue_.bytesEnqueued() > bufferLimit(); }
  bool queueBelowLowLimit() const { return chunk_queue_.bytesEnqueued() < bufferLimit() / 2; }

  virtual Http::HeaderMap* addTrailers() PURE;

  virtual void continueProcessing() const PURE;
  void continueIfNecessary();
  void clearAsyncState();

  virtual envoy::service::ext_proc::v3::HttpHeaders*
  mutableHeaders(envoy::service::ext_proc::v3::ProcessingRequest& request) const PURE;
  virtual envoy::service::ext_proc::v3::HttpBody*
  mutableBody(envoy::service::ext_proc::v3::ProcessingRequest& request) const PURE;
  virtual envoy::service::ext_proc::v3::HttpTrailers*
  mutableTrailers(envoy::service::ext_proc::v3::ProcessingRequest& request) const PURE;

protected:
  void setBodyMode(
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode_BodySendMode body_mode);

  Filter& filter_;
  Http::StreamFilterCallbacks* filter_callbacks_;
  CallbackState callback_state_ = CallbackState::Idle;

  // Keep track of whether we requested a watermark.
  bool watermark_requested_ : 1;
  // Keep track of whether we paused processing and may require
  // a "continue."
  bool paused_ : 1;

  // If true, then there is not going to be a body
  bool no_body_ : 1;
  // If true, then the filter received the complete body
  bool complete_body_available_ : 1;
  // If true, then the filter received the trailers
  bool trailers_available_ : 1;
  // If true, then a CONTINUE_AND_REPLACE status was used on a response
  bool body_replaced_ : 1;
  // If true, we are in "buffered partial" mode and we already reached the buffer
  // limit, sent the body in a message, and got back a reply.
  bool partial_body_processed_ : 1;

  // If true, the server wants to see the headers
  bool send_headers_ : 1;
  // If true, the server wants to see the trailers
  bool send_trailers_ : 1;

  // The specific mode for body handling
  envoy::extensions::filters::http::ext_proc::v3::ProcessingMode_BodySendMode body_mode_;

  Http::RequestOrResponseHeaderMap* headers_ = nullptr;
  Http::HeaderMap* trailers_ = nullptr;
  Event::TimerPtr message_timer_;
  ChunkQueue chunk_queue_;
  absl::optional<MonotonicTime> call_start_time_ = absl::nullopt;
  const envoy::config::core::v3::TrafficDirection traffic_direction_;
};

class DecodingProcessorState : public ProcessorState {
public:
  explicit DecodingProcessorState(
      Filter& filter, const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode& mode)
      : ProcessorState(filter, envoy::config::core::v3::TrafficDirection::INBOUND) {
    setProcessingModeInternal(mode);
  }
  DecodingProcessorState(const DecodingProcessorState&) = delete;
  DecodingProcessorState& operator=(const DecodingProcessorState&) = delete;

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
    decoder_callbacks_ = &callbacks;
    filter_callbacks_ = &callbacks;
  }

  const Buffer::Instance* bufferedData() const override {
    return decoder_callbacks_->decodingBuffer();
  }

  void addBufferedData(Buffer::Instance& data) const override {
    decoder_callbacks_->addDecodedData(data, false);
  }

  void modifyBufferedData(std::function<void(Buffer::Instance&)> cb) const override {
    decoder_callbacks_->modifyDecodingBuffer(cb);
  }

  void injectDataToFilterChain(Buffer::Instance& data, bool end_stream) override {
    decoder_callbacks_->injectDecodedDataToFilterChain(data, end_stream);
  }

  uint32_t bufferLimit() const override { return decoder_callbacks_->decoderBufferLimit(); }

  Http::HeaderMap* addTrailers() override {
    trailers_ = &decoder_callbacks_->addDecodedTrailers();
    return trailers_;
  }

  void continueProcessing() const override { decoder_callbacks_->continueDecoding(); }

  envoy::service::ext_proc::v3::HttpHeaders*
  mutableHeaders(envoy::service::ext_proc::v3::ProcessingRequest& request) const override {
    return request.mutable_request_headers();
  }

  envoy::service::ext_proc::v3::HttpBody*
  mutableBody(envoy::service::ext_proc::v3::ProcessingRequest& request) const override {
    return request.mutable_request_body();
  }

  envoy::service::ext_proc::v3::HttpTrailers*
  mutableTrailers(envoy::service::ext_proc::v3::ProcessingRequest& request) const override {
    return request.mutable_request_trailers();
  }

  void setProcessingMode(
      const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode& mode) override {
    setProcessingModeInternal(mode);
  }

  void requestWatermark() override;
  void clearWatermark() override;

private:
  void setProcessingModeInternal(
      const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode& mode);

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
};

class EncodingProcessorState : public ProcessorState {
public:
  explicit EncodingProcessorState(
      Filter& filter, const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode& mode)
      : ProcessorState(filter, envoy::config::core::v3::TrafficDirection::OUTBOUND) {
    setProcessingModeInternal(mode);
  }
  EncodingProcessorState(const EncodingProcessorState&) = delete;
  EncodingProcessorState& operator=(const EncodingProcessorState&) = delete;

  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) {
    encoder_callbacks_ = &callbacks;
    filter_callbacks_ = &callbacks;
  }

  const Buffer::Instance* bufferedData() const override {
    return encoder_callbacks_->encodingBuffer();
  }

  void addBufferedData(Buffer::Instance& data) const override {
    encoder_callbacks_->addEncodedData(data, false);
  }

  void modifyBufferedData(std::function<void(Buffer::Instance&)> cb) const override {
    encoder_callbacks_->modifyEncodingBuffer(cb);
  }

  void injectDataToFilterChain(Buffer::Instance& data, bool end_stream) override {
    encoder_callbacks_->injectEncodedDataToFilterChain(data, end_stream);
  }

  uint32_t bufferLimit() const override { return encoder_callbacks_->encoderBufferLimit(); }

  Http::HeaderMap* addTrailers() override {
    trailers_ = &encoder_callbacks_->addEncodedTrailers();
    return trailers_;
  }

  void continueProcessing() const override { encoder_callbacks_->continueEncoding(); }

  envoy::service::ext_proc::v3::HttpHeaders*
  mutableHeaders(envoy::service::ext_proc::v3::ProcessingRequest& request) const override {
    return request.mutable_response_headers();
  }

  envoy::service::ext_proc::v3::HttpBody*
  mutableBody(envoy::service::ext_proc::v3::ProcessingRequest& request) const override {
    return request.mutable_response_body();
  }

  envoy::service::ext_proc::v3::HttpTrailers*
  mutableTrailers(envoy::service::ext_proc::v3::ProcessingRequest& request) const override {
    return request.mutable_response_trailers();
  }

  void setProcessingMode(
      const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode& mode) override {
    setProcessingModeInternal(mode);
  }

  void requestWatermark() override;
  void clearWatermark() override;

private:
  void setProcessingModeInternal(
      const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode& mode);

  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
