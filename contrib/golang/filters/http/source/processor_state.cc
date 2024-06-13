#include "contrib/golang/filters/http/source/processor_state.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Golang {

Buffer::Instance& BufferList::push(Buffer::Instance& data) {
  bytes_ += data.length();

  auto ptr = std::make_unique<Buffer::OwnedImpl>();
  Buffer::Instance& buffer = *ptr;
  buffer.move(data);
  queue_.push_back(std::move(ptr));

  return buffer;
}

void BufferList::moveOut(Buffer::Instance& data) {
  for (auto it = queue_.begin(); it != queue_.end(); it = queue_.erase(it)) {
    data.move(**it);
  }
  bytes_ = 0;
};

void BufferList::clearLatest() {
  auto buffer = std::move(queue_.back());
  bytes_ -= buffer->length();
  queue_.pop_back();
};

void BufferList::clearAll() {
  bytes_ = 0;
  queue_.clear();
};

bool BufferList::checkExisting(Buffer::Instance* data) {
  for (auto& it : queue_) {
    if (it.get() == data) {
      return true;
    }
  }
  return false;
};

// headers_ should set to nullptr when return true.
bool ProcessorState::handleHeaderGolangStatus(GolangStatus status) {
  ENVOY_LOG(debug, "golang filter handle header status, state: {}, status: {}", stateStr(),
            int(status));

  ASSERT(filterState() == FilterState::ProcessingHeader);
  bool done = false;

  switch (status) {
  case GolangStatus::LocalReply:
    // already invoked sendLocalReply, do nothing
    break;

  case GolangStatus::Running:
    // do nothing, go side turn to async mode
    break;

  case GolangStatus::Continue:
    if (do_end_stream_) {
      setFilterState(FilterState::Done);
    } else {
      setFilterState(FilterState::WaitingData);
    }
    done = true;
    break;

  case GolangStatus::StopAndBuffer:
    setFilterState(FilterState::WaitingAllData);
    break;

  case GolangStatus::StopAndBufferWatermark:
    setFilterState(FilterState::WaitingData);
    break;

  default:
    ENVOY_LOG(error, "unexpected status: {}", int(status));
    break;
  }

  ENVOY_LOG(debug, "golang filter after handle header status, state: {}, status: {}", stateStr(),
            int(status));

  return done;
};

bool ProcessorState::handleDataGolangStatus(const GolangStatus status) {
  ENVOY_LOG(debug, "golang filter handle data status, state: {}, status: {}", stateStr(),
            int(status));

  ASSERT(filterState() == FilterState::ProcessingData);

  bool done = false;

  switch (status) {
  case GolangStatus::LocalReply:
    // already invoked sendLocalReply, do nothing
    // return directly to skip phase grow by checking trailers
    return false;

  case GolangStatus::Running:
    // do nothing, go side turn to async mode
    // return directly to skip phase grow by checking trailers
    return false;

  case GolangStatus::Continue:
    if (do_end_stream_) {
      setFilterState(FilterState::Done);
    } else {
      setFilterState(FilterState::WaitingData);
    }
    done = true;
    break;

  case GolangStatus::StopAndBuffer:
    if (do_end_stream_) {
      ENVOY_LOG(error, "want more data while stream is end");
      // TODO: terminate the stream?
    }
    setFilterState(FilterState::WaitingAllData);
    break;

  case GolangStatus::StopAndBufferWatermark:
    if (do_end_stream_) {
      ENVOY_LOG(error, "want more data while stream is end");
      // TODO: terminate the stream?
    }
    setFilterState(FilterState::WaitingData);
    break;

  case GolangStatus::StopNoBuffer:
    if (do_end_stream_) {
      ENVOY_LOG(error, "want more data while stream is end");
      // TODO: terminate the stream?
    }
    doDataList.clearLatest();
    setFilterState(FilterState::WaitingData);
    break;

  default:
    // TODO: terminate the stream?
    ENVOY_LOG(error, "unexpected status: {}", int(status));
    break;
  }

  // see trailers and no buffered data
  if (trailers != nullptr && isBufferDataEmpty()) {
    ENVOY_LOG(debug, "see trailers and buffer is empty");
    setFilterState(FilterState::WaitingTrailer);
  }

  ENVOY_LOG(debug, "golang filter after handle data status, state: {}, status: {}", stateStr(),
            int(status));

  return done;
};

// should set trailers_ to nullptr when return true.
// means we should not read/write trailers then, since trailers will pass to next fitler.
bool ProcessorState::handleTrailerGolangStatus(const GolangStatus status) {
  ENVOY_LOG(debug, "golang filter handle trailer status, state: {}, status: {}", stateStr(),
            int(status));

  ASSERT(filterState() == FilterState::ProcessingTrailer);

  auto done = false;

  switch (status) {
  case GolangStatus::LocalReply:
    // already invoked sendLocalReply, do nothing
    break;

  case GolangStatus::Running:
    // do nothing, go side turn to async mode
    break;

  case GolangStatus::Continue:
    setFilterState(FilterState::Done);
    done = true;
    break;

  default:
    // TODO: terminate the stream?
    ENVOY_LOG(error, "unexpected status: {}", int(status));
    break;
  }

  ENVOY_LOG(debug, "golang filter after handle trailer status, state: {}, status: {}", stateStr(),
            int(status));

  return done;
};

// must in envoy thread.
bool ProcessorState::handleGolangStatus(GolangStatus status) {
  ASSERT(isThreadSafe());
  ASSERT(isProcessingInGo(), "unexpected state");

  ENVOY_LOG(debug,
            "before handle golang status, status: {}, state: {}, "
            "do_end_stream_: {}, seen trailers: {}",
            int(status), stateStr(), do_end_stream_, trailers != nullptr);

  bool done = false;
  switch (filterState()) {
  case FilterState::ProcessingHeader:
    done = handleHeaderGolangStatus(status);
    break;

  case FilterState::ProcessingData:
    done = handleDataGolangStatus(status);
    break;

  case FilterState::ProcessingTrailer:
    done = handleTrailerGolangStatus(status);
    break;

  default:
    ASSERT(0, "unexpected state");
  }

  ENVOY_LOG(debug,
            "after handle golang status, status: {}, state: {}, "
            "do_end_stream_: {}, done: {}, seen trailers: {}",
            int(status), stateStr(), do_end_stream_, done, trailers != nullptr);

  return done;
}

void ProcessorState::drainBufferData() {
  if (data_buffer_ != nullptr) {
    auto len = data_buffer_->length();
    if (len > 0) {
      ENVOY_LOG(debug, "golang filter drain buffer data");
      data_buffer_->drain(len);
    }
  }
}

std::string state2Str(FilterState state) {
  switch (state) {
  case FilterState::WaitingHeader:
    return "WaitingHeader";
  case FilterState::ProcessingHeader:
    return "ProcessingHeader";
  case FilterState::WaitingData:
    return "WaitingData";
  case FilterState::WaitingAllData:
    return "WaitingAllData";
  case FilterState::ProcessingData:
    return "ProcessingData";
  case FilterState::WaitingTrailer:
    return "WaitingTrailer";
  case FilterState::ProcessingTrailer:
    return "ProcessingTrailer";
  case FilterState::Done:
    return "Done";
  default:
    return "unknown(" + std::to_string(static_cast<int>(state)) + ")";
  }
}

std::string ProcessorState::stateStr() {
  std::string prefix = is_encoding == 1 ? "encoder" : "decoder";
  auto state_str = state2Str(filterState());
  return prefix + ":" + state_str;
}

void DecodingProcessorState::addBufferData(Buffer::Instance& data) {
  if (data_buffer_ == nullptr) {
    data_buffer_ = decoder_callbacks_->dispatcher().getWatermarkFactory().createBuffer(
        [this]() -> void {
          if (watermark_requested_) {
            watermark_requested_ = false;
            ENVOY_LOG(debug, "golang filter decode data buffer want more data");
            decoder_callbacks_->onDecoderFilterBelowWriteBufferLowWatermark();
          }
        },
        [this]() -> void {
          if (filterState() == FilterState::WaitingAllData) {
            // On the request path exceeding buffer limits will result in a 413.
            ENVOY_LOG(debug, "golang filter decode data buffer is full, reply with 413");
            decoder_callbacks_->sendLocalReply(
                Http::Code::PayloadTooLarge,
                Http::CodeUtility::toString(Http::Code::PayloadTooLarge), nullptr, absl::nullopt,
                StreamInfo::ResponseCodeDetails::get().RequestPayloadTooLarge);
            return;
          }
          if (!watermark_requested_) {
            watermark_requested_ = true;
            ENVOY_LOG(debug, "golang filter decode data buffer is full, disable reading");
            decoder_callbacks_->onDecoderFilterAboveWriteBufferHighWatermark();
          }
        },
        []() -> void { /* TODO: Handle overflow watermark */ });
    data_buffer_->setWatermarks(decoder_callbacks_->decoderBufferLimit());
  }
  data_buffer_->move(data);
}

void EncodingProcessorState::addBufferData(Buffer::Instance& data) {
  if (data_buffer_ == nullptr) {
    data_buffer_ = encoder_callbacks_->dispatcher().getWatermarkFactory().createBuffer(
        [this]() -> void {
          if (watermark_requested_) {
            watermark_requested_ = false;
            ENVOY_LOG(debug, "golang filter encode data buffer want more data");
            encoder_callbacks_->onEncoderFilterBelowWriteBufferLowWatermark();
          }
        },
        [this]() -> void {
          if (filterState() == FilterState::WaitingAllData) {
            ENVOY_LOG(debug, "golang filter encode data buffer is full, reply with 500");

            // In this case, sendLocalReply will either send a response directly to the encoder, or
            // reset the stream.
            encoder_callbacks_->sendLocalReply(
                Http::Code::InternalServerError,
                Http::CodeUtility::toString(Http::Code::InternalServerError), nullptr,
                absl::nullopt, StreamInfo::ResponseCodeDetails::get().ResponsePayloadTooLarge);
            return;
          }
          if (!watermark_requested_) {
            watermark_requested_ = true;
            ENVOY_LOG(debug, "golang filter encode data buffer is full, disable reading");
            encoder_callbacks_->onEncoderFilterAboveWriteBufferHighWatermark();
          }
        },
        []() -> void { /* TODO: Handle overflow watermark */ });
    data_buffer_->setWatermarks(encoder_callbacks_->encoderBufferLimit());
  }
  data_buffer_->move(data);
}

} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
