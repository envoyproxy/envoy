#include "processor_state.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {
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

void ProcessorState::processData() {
  ASSERT(filterState() == FilterState::WaitingData ||
          (filterState() == FilterState::WaitingAllData));
  setFilterState(FilterState::ProcessingData);
}
void ProcessorState::drainBufferData() {
  if (data_buffer_ != nullptr) {
    auto len = data_buffer_->length();
    if (len > 0) {
      ENVOY_LOG(debug, "tcp upstream drain buffer data");
      data_buffer_->drain(len);
    }
  }
}

DecodingProcessorState::DecodingProcessorState(TcpUpstream& tcp_upstream) : ProcessorState(dynamic_cast<httpRequest*>(&tcp_upstream)) {
    is_encoding = 0;
}

EncodingProcessorState::EncodingProcessorState(TcpUpstream& tcp_upstream) : ProcessorState(dynamic_cast<httpRequest*>(&tcp_upstream)) {
    is_encoding = 1;
  }

} // namespace Golang
} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
