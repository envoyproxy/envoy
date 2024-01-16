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
  Buffer::Instance& buffer = *ptr.get();
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
  ENVOY_LOG(debug, "golang filter handle header status, state: {}, phase: {}, status: {}",
            stateStr(), phaseStr(), int(status));

  ASSERT(state_ == FilterState::ProcessingHeader);
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
      state_ = FilterState::Done;
    } else {
      state_ = FilterState::WaitingData;
    }
    done = true;
    break;

  case GolangStatus::StopAndBuffer:
    state_ = FilterState::WaitingAllData;
    break;

  case GolangStatus::StopAndBufferWatermark:
    state_ = FilterState::WaitingData;
    break;

  default:
    ENVOY_LOG(error, "unexpected status: {}", int(status));
    break;
  }

  ENVOY_LOG(debug, "golang filter after handle header status, state: {}, phase: {}, status: {}",
            stateStr(), phaseStr(), int(status));

  return done;
};

bool ProcessorState::handleDataGolangStatus(const GolangStatus status) {
  ENVOY_LOG(debug, "golang filter handle data status, state: {}, phase: {}, status: {}", stateStr(),
            phaseStr(), int(status));

  ASSERT(state_ == FilterState::ProcessingData);

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
      state_ = FilterState::Done;
    } else {
      state_ = FilterState::WaitingData;
    }
    done = true;
    break;

  case GolangStatus::StopAndBuffer:
    if (do_end_stream_) {
      ENVOY_LOG(error, "want more data while stream is end");
      // TODO: terminate the stream?
    }
    state_ = FilterState::WaitingAllData;
    break;

  case GolangStatus::StopAndBufferWatermark:
    if (do_end_stream_) {
      ENVOY_LOG(error, "want more data while stream is end");
      // TODO: terminate the stream?
    }
    state_ = FilterState::WaitingData;
    break;

  case GolangStatus::StopNoBuffer:
    if (do_end_stream_) {
      ENVOY_LOG(error, "want more data while stream is end");
      // TODO: terminate the stream?
    }
    doDataList.clearLatest();
    state_ = FilterState::WaitingData;
    break;

  default:
    // TODO: terminate the stream?
    ENVOY_LOG(error, "unexpected status: {}", int(status));
    break;
  }

  // see trailers and no buffered data
  if (seen_trailers_ && isBufferDataEmpty()) {
    ENVOY_LOG(error, "see trailers and buffer is empty");
    state_ = FilterState::WaitingTrailer;
  }

  ENVOY_LOG(debug, "golang filter after handle data status, state: {}, phase: {}, status: {}",
            int(state_), phaseStr(), int(status));

  return done;
};

// should set trailers_ to nullptr when return true.
// means we should not read/write trailers then, since trailers will pass to next fitler.
bool ProcessorState::handleTrailerGolangStatus(const GolangStatus status) {
  ENVOY_LOG(debug, "golang filter handle trailer status, state: {}, phase: {}, status: {}",
            stateStr(), phaseStr(), int(status));

  ASSERT(state_ == FilterState::ProcessingTrailer);

  auto done = false;

  switch (status) {
  case GolangStatus::LocalReply:
    // already invoked sendLocalReply, do nothing
    break;

  case GolangStatus::Running:
    // do nothing, go side turn to async mode
    break;

  case GolangStatus::Continue:
    state_ = FilterState::Done;
    done = true;
    break;

  default:
    // TODO: terminate the stream?
    ENVOY_LOG(error, "unexpected status: {}", int(status));
    break;
  }

  ENVOY_LOG(debug, "golang filter after handle trailer status, state: {}, phase: {}, status: {}",
            stateStr(), phaseStr(), int(status));

  return done;
};

// must in envoy thread.
bool ProcessorState::handleGolangStatus(GolangStatus status) {
  ASSERT(isThreadSafe());
  ASSERT(isProcessingInGo(), "unexpected state");

  ENVOY_LOG(debug,
            "before handle golang status, status: {}, state: {}, phase: {}, "
            "do_end_stream_: {}",
            int(status), stateStr(), phaseStr(), do_end_stream_);

  bool done = false;
  switch (state_) {
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
            "after handle golang status, status: {}, state: {}, phase: {}, "
            "do_end_stream_: {}",
            int(status), stateStr(), phaseStr(), do_end_stream_);

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

std::string ProcessorState::stateStr() {
  switch (state_) {
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
  case FilterState::Log:
    return "Log";
  case FilterState::Done:
    return "Done";
  default:
    return "unknown";
  }
}

Phase ProcessorState::state2Phase() {
  Phase phase;
  switch (state_) {
  case FilterState::WaitingHeader:
  case FilterState::ProcessingHeader:
    phase = Phase::DecodeHeader;
    break;
  case FilterState::WaitingData:
  case FilterState::WaitingAllData:
  case FilterState::ProcessingData:
    phase = Phase::DecodeData;
    break;
  case FilterState::WaitingTrailer:
  case FilterState::ProcessingTrailer:
    phase = Phase::DecodeTrailer;
    break;
  case FilterState::Log:
    phase = Phase::Log;
    break;
  // decode Done state means encode header phase, encode done state means done phase
  case FilterState::Done:
    phase = Phase::EncodeHeader;
    break;
  }
  return phase;
};

std::string ProcessorState::phaseStr() {
  switch (phase()) {
  case Phase::DecodeHeader:
    return "DecodeHeader";
  case Phase::DecodeData:
    return "DecodeData";
  case Phase::DecodeTrailer:
    return "DecodeTrailer";
  case Phase::EncodeHeader:
    return "EncodeHeader";
  case Phase::EncodeData:
    return "EncodeData";
  case Phase::EncodeTrailer:
    return "EncodeTrailer";
  case Phase::Log:
    return "Log";
  default:
    return "unknown";
  }
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
          if (state_ == FilterState::WaitingAllData) {
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
          if (state_ == FilterState::WaitingAllData) {
            // On the request path exceeding buffer limits will result in a 413.
            ENVOY_LOG(debug, "golang filter encode data buffer is full, reply with 413");
            encoder_callbacks_->sendLocalReply(
                Http::Code::PayloadTooLarge,
                Http::CodeUtility::toString(Http::Code::PayloadTooLarge), nullptr, absl::nullopt,
                StreamInfo::ResponseCodeDetails::get().RequestPayloadTooLarge);
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
