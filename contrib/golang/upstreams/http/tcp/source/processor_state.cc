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

} // namespace Golang
} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
