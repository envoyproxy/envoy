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
  enum class CallbackState {
    Idle,
    // Waiting for a "headers" response
    Headers,
    // Waiting for a "body" response
    BufferedBody,
  };

  explicit ProcessorState(Filter& filter) : filter_(filter) {}
  ProcessorState(const ProcessorState&) = delete;
  virtual ~ProcessorState() = default;
  ProcessorState& operator=(const ProcessorState&) = delete;

  CallbackState callbackState() const { return callback_state_; }
  void setCallbackState(CallbackState state) { callback_state_ = state; }
  bool callbacksIdle() const { return callback_state_ == CallbackState::Idle; }

  void setBodySendDeferred(bool deferred) { body_send_deferred_ = deferred; }

  void setHeaders(Http::HeaderMap* headers) { headers_ = headers; }

  void startMessageTimer(Event::TimerCb cb, std::chrono::milliseconds timeout);
  void cleanUpTimer() const;

  // Idempotent methods for watermarking the body
  virtual void requestWatermark() PURE;
  virtual void clearWatermark() PURE;

  bool handleHeadersResponse(
      const envoy::service::ext_proc::v3alpha::HeadersResponse& response,
      envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode_BodySendMode body_mode);
  bool handleBodyResponse(const envoy::service::ext_proc::v3alpha::BodyResponse& response);

  virtual const Buffer::Instance* bufferedData() const PURE;
  virtual void addBufferedData(Buffer::Instance& data) const PURE;
  virtual void modifyBufferedData(std::function<void(Buffer::Instance&)> cb) const PURE;

  virtual void continueProcessing() const PURE;
  void clearAsyncState();

  virtual envoy::service::ext_proc::v3alpha::HttpHeaders*
  mutableHeaders(envoy::service::ext_proc::v3alpha::ProcessingRequest& request) const PURE;
  virtual envoy::service::ext_proc::v3alpha::HttpBody*
  mutableBody(envoy::service::ext_proc::v3alpha::ProcessingRequest& request) const PURE;

protected:
  Filter& filter_;
  Http::StreamFilterCallbacks* filter_callbacks_;
  CallbackState callback_state_ = CallbackState::Idle;
  // Keep track of whether we must send the body when the header processing callback is done.
  bool body_send_deferred_ = false;
  // Keep track of whether we requested a watermark.
  bool watermark_requested_ = false;
  Http::HeaderMap* headers_ = nullptr;
  Event::TimerPtr message_timer_;
};

class DecodingProcessorState : public ProcessorState {
public:
  explicit DecodingProcessorState(Filter& filter) : ProcessorState(filter) {}
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

  void continueProcessing() const override { decoder_callbacks_->continueDecoding(); }

  envoy::service::ext_proc::v3alpha::HttpHeaders*
  mutableHeaders(envoy::service::ext_proc::v3alpha::ProcessingRequest& request) const override {
    return request.mutable_request_headers();
  }

  envoy::service::ext_proc::v3alpha::HttpBody*
  mutableBody(envoy::service::ext_proc::v3alpha::ProcessingRequest& request) const override {
    return request.mutable_request_body();
  }

  void requestWatermark() override;
  void clearWatermark() override;

private:
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
};

class EncodingProcessorState : public ProcessorState {
public:
  explicit EncodingProcessorState(Filter& filter) : ProcessorState(filter) {}
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

  void continueProcessing() const override { encoder_callbacks_->continueEncoding(); }

  envoy::service::ext_proc::v3alpha::HttpHeaders*
  mutableHeaders(envoy::service::ext_proc::v3alpha::ProcessingRequest& request) const override {
    return request.mutable_response_headers();
  }

  envoy::service::ext_proc::v3alpha::HttpBody*
  mutableBody(envoy::service::ext_proc::v3alpha::ProcessingRequest& request) const override {
    return request.mutable_response_body();
  }

  void requestWatermark() override;
  void clearWatermark() override;

private:
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
