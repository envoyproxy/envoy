#pragma once

#include <deque>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/processing_mode.pb.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

#include "absl/status/status.h"
#include "matching_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class Filter;

class QueuedChunk {
public:
  // True if this represents the last chunk in the stream
  bool end_stream = false;
  uint32_t length = 0;
};
using QueuedChunkPtr = std::unique_ptr<QueuedChunk>;

class ChunkQueue {
public:
  ChunkQueue() = default;
  ChunkQueue(const ChunkQueue&) = delete;
  ChunkQueue& operator=(const ChunkQueue&) = delete;
  uint32_t bytesEnqueued() const { return bytes_enqueued_; }
  bool empty() const { return queue_.empty(); }
  void push(Buffer::Instance& data, bool end_stream);
  void clear();
  QueuedChunkPtr pop(Buffer::OwnedImpl& out_data);
  const QueuedChunk& consolidate();
  Buffer::OwnedImpl& receivedData() { return received_data_; }
  const std::deque<QueuedChunkPtr>& queue() const { return queue_; }

private:
  std::deque<QueuedChunkPtr> queue_;
  // The total size of chunks in the queue.
  uint32_t bytes_enqueued_{};
  // The received data that had not been sent to downstream/upstream.
  Buffer::OwnedImpl received_data_;
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
    // Waiting for a body callback in "buffered partial" mode.
    BufferedPartialBodyCallback,
    // Waiting for a "trailers" response.
    TrailersCallback,
  };

  explicit ProcessorState(Filter& filter,
                          envoy::config::core::v3::TrafficDirection traffic_direction,
                          const std::vector<std::string>& untyped_forwarding_namespaces,
                          const std::vector<std::string>& typed_forwarding_namespaces,
                          const std::vector<std::string>& untyped_receiving_namespaces)
      : filter_(filter), traffic_direction_(traffic_direction),
        untyped_forwarding_namespaces_(&untyped_forwarding_namespaces),
        typed_forwarding_namespaces_(&typed_forwarding_namespaces),
        untyped_receiving_namespaces_(&untyped_receiving_namespaces) {}
  ProcessorState(const ProcessorState&) = delete;
  virtual ~ProcessorState() = default;
  ProcessorState& operator=(const ProcessorState&) = delete;

  envoy::config::core::v3::TrafficDirection trafficDirection() const { return traffic_direction_; }
  const std::string trafficDirectionDebugStr() const {
    if (trafficDirection() == envoy::config::core::v3::TrafficDirection::INBOUND) {
      return "INBOUND";
    }
    return "OUTBOUND";
  }
  CallbackState callbackState() const { return callback_state_; }
  void setPaused(bool paused) { paused_ = paused; }

  bool completeBodyAvailable() const { return complete_body_available_; }
  void setCompleteBodyAvailable(bool d) { complete_body_available_ = d; }
  bool hasNoBody() const { return no_body_; }
  void setHasNoBody(bool b) { no_body_ = b; }
  bool bodyReplaced() const { return body_replaced_; }
  bool bodyReceived() const { return body_received_; }
  void setBodyReceived(bool b) { body_received_ = b; }
  bool partialBodyProcessed() const { return partial_body_processed_; }

  virtual void setProcessingMode(
      const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode& mode) PURE;

  const std::vector<std::string>& untypedForwardingMetadataNamespaces() const {
    return *untyped_forwarding_namespaces_;
  };
  void setUntypedForwardingMetadataNamespaces(const std::vector<std::string>& ns) {
    untyped_forwarding_namespaces_ = &ns;
  };

  const std::vector<std::string>& typedForwardingMetadataNamespaces() const {
    return *typed_forwarding_namespaces_;
  };
  void setTypedForwardingMetadataNamespaces(const std::vector<std::string>& ns) {
    typed_forwarding_namespaces_ = &ns;
  };

  const std::vector<std::string>& untypedReceivingMetadataNamespaces() const {
    return *untyped_receiving_namespaces_;
  };
  void setUntypedReceivingMetadataNamespaces(const std::vector<std::string>& ns) {
    untyped_receiving_namespaces_ = &ns;
  };

  bool sendHeaders() const { return send_headers_; }
  bool sendTrailers() const { return send_trailers_; }
  bool trailersSentToServer() const { return trailers_sent_to_server_; }
  void setTrailersSentToServer(bool b) { trailers_sent_to_server_ = b; }

  envoy::extensions::filters::http::ext_proc::v3::ProcessingMode_BodySendMode bodyMode() const {
    return body_mode_;
  }

  void setRequestHeaders(Http::RequestHeaderMap* headers) { request_headers_ = headers; }
  void setHeaders(Http::RequestOrResponseHeaderMap* headers) { headers_ = headers; }
  void setTrailers(Http::HeaderMap* trailers) { trailers_ = trailers; }
  const Http::RequestHeaderMap* requestHeaders() const { return request_headers_; };
  virtual const Http::RequestOrResponseHeaderMap* responseHeaders() const PURE;
  const Http::HeaderMap* responseTrailers() const { return trailers_; }

  void onStartProcessorCall(Event::TimerCb cb, std::chrono::milliseconds timeout,
                            CallbackState callback_state);
  void onFinishProcessorCall(Grpc::Status::GrpcStatus call_status,
                             CallbackState next_state = CallbackState::Idle);
  void stopMessageTimer();
  bool restartMessageTimer(const uint32_t message_timeout_ms);

  // Idempotent methods for watermarking the body
  virtual void requestWatermark() PURE;
  virtual void clearWatermark() PURE;

  absl::Status handleHeadersResponse(const envoy::service::ext_proc::v3::HeadersResponse& response);

  /**
   * Handles responses containing body modifications from an external processor. Supports three
   * modes of operation: buffered, streamed, and buffered partial.
   *
   * @param response The body response received from the external processor
   * @return Status indicating success or failure of the handling operation
   */
  absl::Status handleBodyResponse(const envoy::service::ext_proc::v3::BodyResponse& response);

  absl::Status
  handleTrailersResponse(const envoy::service::ext_proc::v3::TrailersResponse& response);

  virtual const Buffer::Instance* bufferedData() const PURE;
  bool hasBufferedData() const { return bufferedData() != nullptr && bufferedData()->length() > 0; }
  virtual void addBufferedData(Buffer::Instance& data) const PURE;
  virtual void modifyBufferedData(std::function<void(Buffer::Instance&)> cb) const PURE;
  virtual void injectDataToFilterChain(Buffer::Instance& data, bool end_stream) PURE;
  virtual uint32_t bufferLimit() const PURE;

  ChunkQueue& chunkQueue() { return chunk_queue_; }
  // Move the contents of "data" into a QueuedChunk object on the streaming queue.
  void enqueueStreamingChunk(Buffer::Instance& data, bool end_stream);
  // If the queue has chunks, return the head of the queue.
  QueuedChunkPtr dequeueStreamingChunk(Buffer::OwnedImpl& out_data);
  // Consolidate all the chunks on the queue into a single one and return a reference.
  const QueuedChunk& consolidateStreamedChunks() { return chunk_queue_.consolidate(); }
  bool queueOverHighLimit() const { return chunk_queue_.bytesEnqueued() > bufferLimit(); }
  bool queueBelowLowLimit() const { return chunk_queue_.bytesEnqueued() < bufferLimit() / 2; }
  bool shouldRemoveContentLength() const {
    // Always remove the content length in 3 cases below:
    // 1) STREAMED BodySendMode
    // 2) BUFFERED_PARTIAL BodySendMode
    // 3) BUFFERED BodySendMode + SKIP HeaderSendMode
    // 4) FULL_DUPLEX_STREAMED BodySendMode
    // In these modes, ext_proc filter can not guarantee to set the content length correctly if
    // body is mutated by external processor later.
    // In http1 codec, removing content length will enable chunked encoding whenever feasible.
    return (
        body_mode_ == envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::STREAMED ||
        body_mode_ ==
            envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::FULL_DUPLEX_STREAMED ||
        body_mode_ ==
            envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::BUFFERED_PARTIAL ||
        (body_mode_ == envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::BUFFERED &&
         !send_headers_));
  }

  virtual Http::HeaderMap* addTrailers() PURE;

  virtual void continueProcessing() const PURE;
  void continueIfNecessary();
  void clearAsyncState(Grpc::Status::GrpcStatus call_status = Grpc::Status::Aborted);

  virtual envoy::service::ext_proc::v3::HttpHeaders*
  mutableHeaders(envoy::service::ext_proc::v3::ProcessingRequest& request) const PURE;
  virtual envoy::service::ext_proc::v3::HttpBody*
  mutableBody(envoy::service::ext_proc::v3::ProcessingRequest& request) const PURE;
  virtual envoy::service::ext_proc::v3::HttpTrailers*
  mutableTrailers(envoy::service::ext_proc::v3::ProcessingRequest& request) const PURE;

  virtual Http::StreamFilterCallbacks* callbacks() const PURE;

  virtual bool sendAttributes(const ExpressionManager& mgr) const PURE;

  void setSentAttributes(bool sent) { attributes_sent_ = sent; }

  virtual Protobuf::Struct
  evaluateAttributes(const ExpressionManager& mgr,
                     const Filters::Common::Expr::Activation& activation) const PURE;

protected:
  void setBodyMode(
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode_BodySendMode body_mode);

  Filter& filter_;
  Http::StreamFilterCallbacks* filter_callbacks_;
  CallbackState callback_state_ = CallbackState::Idle;

  // Keep track of whether we requested a watermark.
  bool watermark_requested_ : 1 = false;
  // Keep track of whether we paused processing and may require
  // a "continue."
  bool paused_ : 1 = false;

  // If true, then there is not going to be a body
  bool no_body_ : 1 = false;
  // If true, then the filter received the complete body
  bool complete_body_available_ : 1 = false;
  // If true, the trailers is already sent to the server.
  bool trailers_sent_to_server_ : 1 = false;
  // If true, then a CONTINUE_AND_REPLACE status was used on a response
  bool body_replaced_ : 1 = false;
  // If true, some of the body data is received.
  bool body_received_ : 1 = false;
  // If true, we are in "buffered partial" mode and we already reached the buffer
  // limit, sent the body in a message, and got back a reply.
  bool partial_body_processed_ : 1 = false;

  // If true, the server wants to see the headers
  bool send_headers_ : 1;
  // If true, the server wants to see the trailers
  bool send_trailers_ : 1;

  // The specific mode for body handling
  envoy::extensions::filters::http::ext_proc::v3::ProcessingMode_BodySendMode body_mode_;

  // The request_headers_ field is guaranteed to hold the pointer to the request
  // headers as set in decodeHeaders. This allows both decoding and encoding states
  // to have access to the request headers map.
  Http::RequestHeaderMap* request_headers_ = nullptr;
  Http::RequestOrResponseHeaderMap* headers_ = nullptr;
  Http::HeaderMap* trailers_ = nullptr;
  Event::TimerPtr message_timer_;
  // Flag to track whether Envoy already received the new timeout message.
  // Envoy should receive at most one such message in one particular state.
  bool new_timeout_received_{false};
  ChunkQueue chunk_queue_;
  absl::optional<MonotonicTime> call_start_time_ = absl::nullopt;
  const envoy::config::core::v3::TrafficDirection traffic_direction_;

  const std::vector<std::string>* untyped_forwarding_namespaces_{};
  const std::vector<std::string>* typed_forwarding_namespaces_{};
  const std::vector<std::string>* untyped_receiving_namespaces_{};

  // If true, the attributes for this processing state have already been sent.
  bool attributes_sent_{};

private:
  virtual void clearRouteCache(const envoy::service::ext_proc::v3::CommonResponse&) {}
  bool
  handleStreamedBodyResponse(const envoy::service::ext_proc::v3::CommonResponse& common_response);
  bool handleDuplexStreamedBodyResponse(
      const envoy::service::ext_proc::v3::CommonResponse& common_response);
  void sendBufferedDataInStreamedMode(bool end_stream);
  absl::Status
  processHeaderMutation(const envoy::service::ext_proc::v3::CommonResponse& common_response);
  void clearStreamingChunk() { chunk_queue_.clear(); }
  CallbackState getCallbackStateAfterHeaderResp(
      const envoy::service::ext_proc::v3::CommonResponse& common_response) const;

  /**
   * Handle the header response with CONTINUE_AND_REPLACE action from external processor.
   *
   * @param response HeadersResponse with replace directives
   * @return Status of the operation
   */
  absl::Status
  handleHeaderContinueAndReplace(const envoy::service::ext_proc::v3::HeadersResponse& response);

  /**
   * Handle the header response with CONTINUE action from external processor.
   * Routes to appropriate handler based on body state and processing mode
   * (none, buffered, streamed, partial, or full-duplex).
   *
   * @return Status of the operation
   */
  absl::Status handleHeaderContinue();

  /**
   * Handle the body when the complete body is already available.
   * Sends buffered body to processor based on callback state,
   * manages streamed data, and continues filter chain when appropriate.
   *
   * @return Status of the operation
   */
  absl::Status handleCompleteBodyAvailable();

  /**
   * Handle partial body buffering with watermark control when geting a header response.
   * Enqueues buffered data, sends chunks when high watermark is reached,
   * and holds headers during buffering phase.
   *
   * @return Status of the operation
   */
  absl::Status handleBufferedPartialMode();

  /**
   * Finalizes processing by handling trailers and cleanup.
   * Either sends available trailers to processor or cleans up resources
   * by clearing headers, notifying filter, and continuing the chain.
   *
   * @return Status of the operation
   */
  absl::Status handleTrailersAndCleanup();

  /**
   * Validates if the current callback state is valid for processing body responses.
   *
   * @return true if the callback state is valid for body processing, false otherwise
   */
  bool isValidBodyCallbackState() const;

  /**
   * Handles buffered body callback state by processing header and body mutations if present.
   *
   * @param common_response The common response from the external processor
   * @return StatusOr<bool> Returns Ok(true) if processing should continue, or an error status on
   * failure
   */
  absl::StatusOr<bool>
  handleBufferedBodyCallback(const envoy::service::ext_proc::v3::CommonResponse& common_response);

  /**
   * Handles streamed body callback state by processing header and body mutations.
   *
   * @param common_response The common response from the external processor
   * @return StatusOr<bool> Returns Ok(true) if processing should continue, or an error status on
   * failure
   */
  absl::StatusOr<bool>
  handleStreamedBodyCallback(const envoy::service::ext_proc::v3::CommonResponse& common_response);

  /**
   * Handles buffered partial body callback state by processing both header and body
   * mutations for partial body data.
   *
   * @param common_response The common response from the external processor
   * @return StatusOr<bool> Returns Ok(true) if processing should continue, or an error status on
   * failure
   */
  absl::StatusOr<bool> handleBufferedPartialBodyCallback(
      const envoy::service::ext_proc::v3::CommonResponse& common_response);

  /**
   * Processes header mutations if headers are available in the current state.
   *
   * @param common_response The common response containing potential header mutations
   * @return Status Returns Ok if header mutations were processed successfully or not needed,
   *         or an error status on failure
   */
  absl::Status processHeaderMutationIfAvailable(
      const envoy::service::ext_proc::v3::CommonResponse& common_response);

  /**
   * Validates content length against body mutation size. Content-length header is only
   * allowed in BUFFERED mode when body mutation by external processor is enabled.
   * The mutation will be rejected if the content length doesn't match the mutated body length.
   *
   * @param common_response The common response containing body mutations to validate
   * @return Status Returns Ok if validation passes or is not needed, or an error status if
   *         the content length doesn't match the mutated body length
   */
  absl::Status
  validateContentLength(const envoy::service::ext_proc::v3::CommonResponse& common_response);

  /**
   * Applies body mutations to buffered data.
   *
   * @param common_response The common response containing body mutations to apply
   */
  void
  applyBufferedBodyMutation(const envoy::service::ext_proc::v3::CommonResponse& common_response);

  /**
   * Finalizes body response processing by handling trailers and continuation.
   *
   * @param should_continue Indicates if processing should continue after finalization
   */
  void finalizeBodyResponse(bool should_continue);
};

class DecodingProcessorState : public ProcessorState {
public:
  explicit DecodingProcessorState(
      Filter& filter, const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode& mode,
      const std::vector<std::string>& untyped_forwarding_namespaces,
      const std::vector<std::string>& typed_forwarding_namespaces,
      const std::vector<std::string>& untyped_receiving_namespaces)
      : ProcessorState(filter, envoy::config::core::v3::TrafficDirection::INBOUND,
                       untyped_forwarding_namespaces, typed_forwarding_namespaces,
                       untyped_receiving_namespaces) {
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

  Http::StreamFilterCallbacks* callbacks() const override { return decoder_callbacks_; }

  bool sendAttributes(const ExpressionManager& mgr) const override {
    return !attributes_sent_ && mgr.hasRequestExpr();
  }

  const Http::RequestOrResponseHeaderMap* responseHeaders() const override { return nullptr; }
  Protobuf::Struct
  evaluateAttributes(const ExpressionManager& mgr,
                     const Filters::Common::Expr::Activation& activation) const override {
    return mgr.evaluateRequestAttributes(activation);
  }

private:
  void setProcessingModeInternal(
      const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode& mode);

  void
  clearRouteCache(const envoy::service::ext_proc::v3::CommonResponse& common_response) override;

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
};

class EncodingProcessorState : public ProcessorState {
public:
  explicit EncodingProcessorState(
      Filter& filter, const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode& mode,
      const std::vector<std::string>& untyped_forwarding_namespaces,
      const std::vector<std::string>& typed_forwarding_namespaces,
      const std::vector<std::string>& untyped_receiving_namespaces)
      : ProcessorState(filter, envoy::config::core::v3::TrafficDirection::OUTBOUND,
                       untyped_forwarding_namespaces, typed_forwarding_namespaces,
                       untyped_receiving_namespaces) {
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

  Http::StreamFilterCallbacks* callbacks() const override { return encoder_callbacks_; }

  bool sendAttributes(const ExpressionManager& mgr) const override {
    return !attributes_sent_ && mgr.hasResponseExpr();
  }

  const Http::RequestOrResponseHeaderMap* responseHeaders() const override { return headers_; }

  Protobuf::Struct
  evaluateAttributes(const ExpressionManager& mgr,
                     const Filters::Common::Expr::Activation& activation) const override {
    return mgr.evaluateResponseAttributes(activation);
  }

  // Check whether external processing is configured in the encoding path.
  bool noExternalProcess() const {
    return (!send_headers_ && !send_trailers_ &&
            body_mode_ == envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::NONE);
  }

private:
  void setProcessingModeInternal(
      const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode& mode);

  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
