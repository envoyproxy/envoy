#include "common/network/raw_buffer_socket.h"

#include "common/common/empty_string.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Network {

void RawBufferSocket::setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

IoResult RawBufferSocket::doRead(Buffer::Instance& buffer) {
  PostIoAction action = PostIoAction::KeepOpen;
  uint64_t bytes_read = 0;
  bool last_byte = false;
  do {
    // 16K read is arbitrary. IIRC, libevent will currently clamp this to 4K. libevent will also
    // use an ioctl() before every read to figure out how much data there is to read.
    //
    // TODO(mattklein123) PERF: Tune the read size and figure out a way of getting rid of the
    // ioctl(). The extra syscall is not worth it.
    int rc = buffer.read(callbacks_->fd(), 16384);
    ENVOY_CONN_LOG(trace, "read returns: {}", callbacks_->connection(), rc);

    if (rc == 0) {
      // Remote close. Might need to raise data before raising close.
      last_byte = true;
      break;
    } else if (rc == -1) {
      // Remote error (might be no data).
      ENVOY_CONN_LOG(trace, "read error: {}", callbacks_->connection(), errno);
      if (errno != EAGAIN) {
        action = PostIoAction::Close;
      }

      break;
    } else {
      bytes_read += rc;
      if (callbacks_->shouldDrainReadBuffer()) {
        callbacks_->setReadBufferReady();
        break;
      }
    }
  } while (true);

  return {action, bytes_read, last_byte};
}

IoResult RawBufferSocket::doWrite(Buffer::Instance& buffer, bool last_byte) {
  PostIoAction action;
  uint64_t bytes_written = 0;
  do {
    if (buffer.length() == 0) {
      if (last_byte && !shutdown_) {
        // Ignore the result. This can only fail if the connection failed. In that case, the
        // error will be detected on the next read, and dealt with appropriately.
        ::shutdown(callbacks_->fd(), SHUT_WR);
        shutdown_ = true;
      }
      action = PostIoAction::KeepOpen;
      break;
    }
    int rc = buffer.write(callbacks_->fd());
    ENVOY_CONN_LOG(trace, "write returns: {}", callbacks_->connection(), rc);
    if (rc == -1) {
      ENVOY_CONN_LOG(trace, "write error: {}", callbacks_->connection(), errno);
      if (errno == EAGAIN) {
        action = PostIoAction::KeepOpen;
      } else {
        action = PostIoAction::Close;
      }

      break;
    } else {
      bytes_written += rc;
    }
  } while (true);

  return {action, bytes_written, false};
}

std::string RawBufferSocket::protocol() const { return EMPTY_STRING; }

void RawBufferSocket::onConnected() { callbacks_->raiseEvent(ConnectionEvent::Connected); }

TransportSocketPtr RawBufferSocketFactory::createTransportSocket() const {
  return std::make_unique<RawBufferSocket>();
}

bool RawBufferSocketFactory::implementsSecureTransport() const { return false; }
} // namespace Network
} // namespace Envoy
