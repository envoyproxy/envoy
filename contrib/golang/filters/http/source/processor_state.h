#pragma once

#include <deque>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/http/codes.h"
#include "source/common/http/utility.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Golang {

class Filter;

class BufferList : public NonCopyable {
public:
  BufferList() = default;

  bool empty() const { return bytes_ == 0; }
  // return a new buffer instance, it will existing until moveOut or drain.
  Buffer::Instance& push(Buffer::Instance& data);
  // move all buffer into data, the list is empty then.
  void moveOut(Buffer::Instance& data);
  // clear the latest push in buffer.
  void clearLatest();
  // clear all.
  void clearAll();
  // check the buffer instance if existing
  bool checkExisting(Buffer::Instance* data);

private:
  std::deque<Buffer::InstancePtr> queue_;
  // The total size of buffers in the list.
  uint32_t bytes_{0};
};

// This describes the processor state.
enum class FilterState {
  // Waiting header
  WaitingHeader,
  // Processing header in Go
  ProcessingHeader,
  // Waiting data
  WaitingData,
  // Waiting all data
  WaitingAllData,
  // Processing data in Go
  ProcessingData,
  // Waiting trailer
  WaitingTrailer,
  // Processing trailer in Go
  ProcessingTrailer,
  // Log in Go
  Log,
  // All done
  Done,
};

/*
 * request phase
 */
enum class Phase {
  DecodeHeader = 1,
  DecodeData,
  DecodeTrailer,
  EncodeHeader,
  EncodeData,
  EncodeTrailer,
  Log,
  Done,
};

/**
 * An enum specific for Golang status.
 */
enum class GolangStatus {
  Running,
  // after called sendLocalReply
  LocalReply,
  // Continue filter chain iteration.
  Continue,
  StopAndBuffer,
  StopAndBufferWatermark,
  StopNoBuffer,
};

class ProcessorState : public Logger::Loggable<Logger::Id::http>, NonCopyable {
public:
  explicit ProcessorState(Filter& filter) : filter_(filter) {}
  virtual ~ProcessorState() = default;

  FilterState state() const { return state_; }
  std::string stateStr();

  virtual Phase phase() PURE;
  std::string phaseStr();

  bool isProcessingInGo() {
    return state_ == FilterState::ProcessingHeader || state_ == FilterState::ProcessingData ||
           state_ == FilterState::ProcessingTrailer || state_ == FilterState::Log;
  }
  bool isProcessingHeader() { return state_ == FilterState::ProcessingHeader; }
  Http::StreamFilterCallbacks* getFilterCallbacks() { return filter_callbacks_; };

  bool isThreadSafe() { return filter_callbacks_->dispatcher().isThreadSafe(); };
  Event::Dispatcher& getDispatcher() { return filter_callbacks_->dispatcher(); }

  /* data buffer */
  // add data to state buffer
  virtual void addBufferData(Buffer::Instance& data) PURE;
  // get state buffer
  Buffer::Instance& getBufferData() { return *data_buffer_.get(); };
  bool isBufferDataEmpty() { return data_buffer_ == nullptr || data_buffer_->length() == 0; };
  void drainBufferData();

  void setSeenTrailers() { seen_trailers_ = true; }
  bool isProcessingEndStream() { return do_end_stream_; }

  virtual void continueProcessing() PURE;
  virtual void injectDataToFilterChain(Buffer::Instance& data, bool end_stream) PURE;
  void continueDoData() {
    if (!end_stream_ && doDataList.empty()) {
      return;
    }
    Buffer::OwnedImpl data_to_write;
    doDataList.moveOut(data_to_write);

    injectDataToFilterChain(data_to_write, do_end_stream_);
  }

  void processHeader(bool end_stream) {
    ASSERT(state_ == FilterState::WaitingHeader);
    state_ = FilterState::ProcessingHeader;
    do_end_stream_ = end_stream;
  }

  void processData(bool end_stream) {
    ASSERT(state_ == FilterState::WaitingData ||
           (state_ == FilterState::WaitingAllData && (end_stream || seen_trailers_)));
    state_ = FilterState::ProcessingData;
    do_end_stream_ = end_stream;
  }

  void processTrailer() {
    ASSERT(state_ == FilterState::WaitingTrailer || state_ == FilterState::WaitingData ||
           state_ == FilterState::WaitingAllData);
    state_ = FilterState::ProcessingTrailer;
    do_end_stream_ = true;
  }

  void enterLog() {
    prev_state_ = state_;
    state_ = FilterState::Log;
  }
  void leaveLog() {
    state_ = prev_state_;
    prev_state_ = FilterState::Log;
  }

  bool handleHeaderGolangStatus(const GolangStatus status);
  bool handleDataGolangStatus(const GolangStatus status);
  bool handleTrailerGolangStatus(const GolangStatus status);
  bool handleGolangStatus(GolangStatus status);

  virtual void sendLocalReply(Http::Code response_code, absl::string_view body_text,
                              std::function<void(Http::ResponseHeaderMap& headers)> modify_headers,
                              Grpc::Status::GrpcStatus grpc_status, absl::string_view details) PURE;

  const StreamInfo::StreamInfo& streamInfo() const { return filter_callbacks_->streamInfo(); }
  StreamInfo::StreamInfo& streamInfo() { return filter_callbacks_->streamInfo(); }

  void setEndStream(bool end_stream) { end_stream_ = end_stream; }
  bool getEndStream() { return end_stream_; }
  // seen trailers also means stream is end
  bool isStreamEnd() { return end_stream_ || seen_trailers_; }

  BufferList doDataList;

protected:
  Phase state2Phase();
  Filter& filter_;
  Http::StreamFilterCallbacks* filter_callbacks_{nullptr};
  bool watermark_requested_{false};
  Buffer::InstancePtr data_buffer_{nullptr};
  FilterState state_{FilterState::WaitingHeader};
  FilterState prev_state_{FilterState::Done};
  bool end_stream_{false};
  bool do_end_stream_{false};
  bool seen_trailers_{false};
};

class DecodingProcessorState : public ProcessorState {
public:
  explicit DecodingProcessorState(Filter& filter) : ProcessorState(filter) {}

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
    decoder_callbacks_ = &callbacks;
    filter_callbacks_ = &callbacks;
  }

  void injectDataToFilterChain(Buffer::Instance& data, bool end_stream) override {
    decoder_callbacks_->injectDecodedDataToFilterChain(data, end_stream);
  }

  Phase phase() override { return state2Phase(); };

  void addBufferData(Buffer::Instance& data) override;

  void continueProcessing() override {
    ENVOY_LOG(debug, "golang filter callback continue, continueDecoding");
    decoder_callbacks_->continueDecoding();
  }
  void sendLocalReply(Http::Code response_code, absl::string_view body_text,
                      std::function<void(Http::ResponseHeaderMap& headers)> modify_headers,
                      Grpc::Status::GrpcStatus grpc_status, absl::string_view details) override {
    // it's safe to reset state_, since it is read/write in safe thread.
    ENVOY_LOG(debug, "golang filter phase grow to EncodeHeader and state grow to WaitHeader before "
                     "sendLocalReply");
    state_ = FilterState::WaitingHeader;
    decoder_callbacks_->sendLocalReply(response_code, body_text, modify_headers, grpc_status,
                                       details);
  };

private:
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{nullptr};
};

class EncodingProcessorState : public ProcessorState {
public:
  explicit EncodingProcessorState(Filter& filter) : ProcessorState(filter) {}

  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) {
    encoder_callbacks_ = &callbacks;
    filter_callbacks_ = &callbacks;
  }

  void injectDataToFilterChain(Buffer::Instance& data, bool end_stream) override {
    encoder_callbacks_->injectEncodedDataToFilterChain(data, end_stream);
  }

  Phase phase() override { return static_cast<Phase>(static_cast<int>(state2Phase()) + 3); };

  void addBufferData(Buffer::Instance& data) override;

  void continueProcessing() override {
    ENVOY_LOG(debug, "golang filter callback continue, continueEncoding");
    encoder_callbacks_->continueEncoding();
  }
  void sendLocalReply(Http::Code response_code, absl::string_view body_text,
                      std::function<void(Http::ResponseHeaderMap& headers)> modify_headers,
                      Grpc::Status::GrpcStatus grpc_status, absl::string_view details) override {
    encoder_callbacks_->sendLocalReply(response_code, body_text, modify_headers, grpc_status,
                                       details);
  };

private:
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{nullptr};
};

} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
