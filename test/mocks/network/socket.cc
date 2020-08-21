#include "socket.h"

namespace Envoy {
namespace Network {

MockSocket::MockSocket()
    : io_handle_(std::make_unique<MockIoHandle>()),
      options_(std::make_shared<std::vector<Socket::OptionConstSharedPtr>>()) {}

MockSocket::~MockSocket() = default;

MockIoHandle& MockSocket::mockIoHandle() const { return *io_handle_; }

IoHandle& MockSocket::ioHandle() { return *io_handle_; };

const IoHandle& MockSocket::ioHandle() const { return *io_handle_; };

Api::SysCallIntResult MockSocket::setSocketOption(int level, int optname, const void* optval,
                                                  socklen_t len) {
  return io_handle_->setOption(level, optname, optval, len);
}

void MockSocket::addOptions(const Socket::OptionsSharedPtr& options) {
  Socket::appendOptions(options_, options);
}

} // namespace Network
} // namespace Envoy
