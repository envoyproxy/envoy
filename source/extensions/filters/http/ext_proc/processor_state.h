#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/event/timer.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/service/ext_proc/v3alpha/external_processor.pb.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class ProcessorState : public Logger::Loggable<Logger::Id::filter> {
public:
  enum class CallbackState {
    Idle,
    // Waiting for a "headers" response
    Headers,
    // Waiting for a "body" response
    BufferedBody,
  };

  virtual ~ProcessorState() = default;

  void setCallbackState(CallbackState state) { callback_state_ = state; }
  bool callbacksIdle() const { return callback_state_ == CallbackState::Idle; }

  void setHeaders(Http::HeaderMap* headers) { headers_ = headers; }

  void startMessageTimer(Event::TimerCb cb, std::chrono::milliseconds timeout);
  void cleanUpTimer() const;

  bool handleHeadersResponse(const envoy::service::ext_proc::v3alpha::HeadersResponse& response);
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
  Http::StreamFilterCallbacks* filter_callbacks_;
  CallbackState callback_state_ = CallbackState::Idle;
  Http::HeaderMap* headers_ = nullptr;
  Event::TimerPtr message_timer_;
};

class DecodingProcessorState : public ProcessorState {
public:
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

private:
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
};

class EncodingProcessorState : public ProcessorState {
public:
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

private:
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
