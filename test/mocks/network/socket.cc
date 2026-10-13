#include "socket.h"

namespace Envoy {
namespace Network {

MockSocket::MockSocket() : io_handle_(std::make_unique<MockIoHandle>()) {}

MockSocket::~MockSocket() = default;

IoHandle& MockSocket::ioHandle() { return *io_handle_; };

const IoHandle& MockSocket::ioHandle() const { return *io_handle_; };

Api::SysCallIntResult MockSocket::setSocketOption(int level, int optname, const void* optval,
                                                  socklen_t len) {
  return io_handle_->setOption(level, optname, optval, len);
}

} // namespace Network
} // namespace Envoy
