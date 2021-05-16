#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/http/ext_proc/v3alpha/processing_mode.pb.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/service/ext_proc/v3alpha/external_processor.pb.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class Filter;

class ProcessorState : public Logger::Loggable<Logger::Id::filter> {
public:
  // This describes whether the filter is waiting for a response to a gRPC message
  enum class CallbackState {
    // Not waiting for anything
    Idle,
    // Waiting for a "headers" response
    HeadersCallback,
    // Waiting for a "body" response in buffered mode
    BufferedBodyCallback,
    // and waiting for a "trailers" response
    TrailersCallback,
  };

  explicit ProcessorState(Filter& filter)
      : filter_(filter), watermark_requested_(false), complete_body_available_(false),
        trailers_available_(false) {}
  ProcessorState(const ProcessorState&) = delete;
  virtual ~ProcessorState() = default;
  ProcessorState& operator=(const ProcessorState&) = delete;

  CallbackState callbackState() const { return callback_state_; }
  void setCallbackState(CallbackState state) { callback_state_ = state; }

  bool completeBodyAvailable() const { return complete_body_available_; }
  void setCompleteBodyAvailable(bool d) { complete_body_available_ = d; }
  void setTrailersAvailable(bool d) { trailers_available_ = d; }

  virtual void setProcessingMode(
      const envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode& mode) PURE;
  bool sendHeaders() const { return send_headers_; }
  bool sendTrailers() const { return send_trailers_; }
  envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode_BodySendMode
  bodyMode() const {
    return body_mode_;
  }

  void setHeaders(Http::HeaderMap* headers) { headers_ = headers; }
  void setTrailers(Http::HeaderMap* trailers) { trailers_ = trailers; }

  void startMessageTimer(Event::TimerCb cb, std::chrono::milliseconds timeout);
  void cleanUpTimer() const;

  // Idempotent methods for watermarking the body
  virtual void requestWatermark() PURE;
  virtual void clearWatermark() PURE;

  bool handleHeadersResponse(const envoy::service::ext_proc::v3alpha::HeadersResponse& response);
  bool handleBodyResponse(const envoy::service::ext_proc::v3alpha::BodyResponse& response);
  bool handleTrailersResponse(const envoy::service::ext_proc::v3alpha::TrailersResponse& response);

  virtual const Buffer::Instance* bufferedData() const PURE;
  virtual void addBufferedData(Buffer::Instance& data) const PURE;
  virtual void modifyBufferedData(std::function<void(Buffer::Instance&)> cb) const PURE;

  virtual Http::HeaderMap* addTrailers() PURE;

  virtual void continueProcessing() const PURE;
  void clearAsyncState();

  virtual envoy::service::ext_proc::v3alpha::HttpHeaders*
  mutableHeaders(envoy::service::ext_proc::v3alpha::ProcessingRequest& request) const PURE;
  virtual envoy::service::ext_proc::v3alpha::HttpBody*
  mutableBody(envoy::service::ext_proc::v3alpha::ProcessingRequest& request) const PURE;
  virtual envoy::service::ext_proc::v3alpha::HttpTrailers*
  mutableTrailers(envoy::service::ext_proc::v3alpha::ProcessingRequest& request) const PURE;

protected:
  Filter& filter_;
  Http::StreamFilterCallbacks* filter_callbacks_;
  CallbackState callback_state_ = CallbackState::Idle;

  // Keep track of whether we requested a watermark.
  bool watermark_requested_ : 1;

  // If true, then the filter received the complete body
  bool complete_body_available_ : 1;
  // If true, then the filter received the trailers
  bool trailers_available_ : 1;

  // If true, the server wants to see the headers
  bool send_headers_ : 1;
  // If true, the server wants to see the trailers
  bool send_trailers_ : 1;

  // The specific mode for body handling
  envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode_BodySendMode body_mode_;

  Http::HeaderMap* headers_ = nullptr;
  Http::HeaderMap* trailers_ = nullptr;
  Event::TimerPtr message_timer_;
};

class DecodingProcessorState : public ProcessorState {
public:
  explicit DecodingProcessorState(
      Filter& filter,
      const envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode& mode)
      : ProcessorState(filter) {
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

  Http::HeaderMap* addTrailers() override {
    trailers_ = &decoder_callbacks_->addDecodedTrailers();
    return trailers_;
  }

  void continueProcessing() const override { decoder_callbacks_->continueDecoding(); }

  envoy::service::ext_proc::v3alpha::HttpHeaders*
  mutableHeaders(envoy::service::ext_proc::v3alpha::ProcessingRequest& request) const override {
    return request.mutable_request_headers();
  }

  envoy::service::ext_proc::v3alpha::HttpBody*
  mutableBody(envoy::service::ext_proc::v3alpha::ProcessingRequest& request) const override {
    return request.mutable_request_body();
  }

  envoy::service::ext_proc::v3alpha::HttpTrailers*
  mutableTrailers(envoy::service::ext_proc::v3alpha::ProcessingRequest& request) const override {
    return request.mutable_request_trailers();
  }

  void setProcessingMode(
      const envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode& mode) override {
    setProcessingModeInternal(mode);
  }

  void requestWatermark() override;
  void clearWatermark() override;

private:
  void setProcessingModeInternal(
      const envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode& mode);

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
};

class EncodingProcessorState : public ProcessorState {
public:
  explicit EncodingProcessorState(
      Filter& filter,
      const envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode& mode)
      : ProcessorState(filter) {
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

  Http::HeaderMap* addTrailers() override {
    trailers_ = &encoder_callbacks_->addEncodedTrailers();
    return trailers_;
  }

  void continueProcessing() const override { encoder_callbacks_->continueEncoding(); }

  envoy::service::ext_proc::v3alpha::HttpHeaders*
  mutableHeaders(envoy::service::ext_proc::v3alpha::ProcessingRequest& request) const override {
    return request.mutable_response_headers();
  }

  envoy::service::ext_proc::v3alpha::HttpBody*
  mutableBody(envoy::service::ext_proc::v3alpha::ProcessingRequest& request) const override {
    return request.mutable_response_body();
  }

  envoy::service::ext_proc::v3alpha::HttpTrailers*
  mutableTrailers(envoy::service::ext_proc::v3alpha::ProcessingRequest& request) const override {
    return request.mutable_response_trailers();
  }

  void setProcessingMode(
      const envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode& mode) override {
    setProcessingModeInternal(mode);
  }

  void requestWatermark() override;
  void clearWatermark() override;

private:
  void setProcessingModeInternal(
      const envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode& mode);

  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
