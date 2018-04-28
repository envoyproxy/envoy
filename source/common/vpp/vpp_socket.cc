#include "common/vpp/vpp_socket.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/buffer/buffer_impl.h"


using Envoy::Network::PostIoAction;

namespace Envoy {
namespace Vpp {

void VppSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  ASSERT(!callbacks_);
  callbacks_ = &callbacks;
}

Network::IoResult VppSocket::doRead(Buffer::Instance& read_buffer) {
  PostIoAction action = PostIoAction::KeepOpen;
  uint64_t bytes_read = 0;
  bool end_stream = false;
  do {
    // 16K read is arbitrary. IIRC, libevent will currently clamp this to 4K. libevent will also
    // use an ioctl() before every read to figure out how much data there is to read.
    //
    // TODO(mattklein123) PERF: Tune the read size and figure out a way of getting rid of the
    // ioctl(). The extra syscall is not worth it.
    int rc = read_buffer.read(callbacks_->fd(), 16384);
    ENVOY_CONN_LOG(trace, "read returns: {}", callbacks_->connection(), rc);

    if (rc == 0) {
      // Remote close.
      end_stream = true;
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

  return {action, bytes_read, end_stream};
}

Network::IoResult VppSocket::doWrite(Buffer::Instance& buffer, bool) {
  PostIoAction action;
  uint64_t bytes_written = 0;
  uint64_t bytes_to_write = buffer.length();

  while (true) {
    if (bytes_written == bytes_to_write) {
      action = PostIoAction::KeepOpen;
      break;
    }
    int rc = buffer.write(callbacks_->fd());
    ENVOY_CONN_LOG(trace, "write returns: {}", callbacks_->connection(), rc);
    if (rc == -1) {
      ENVOY_CONN_LOG(trace, "write error: {} ({})", callbacks_->connection(), errno,
                     strerror(errno));
      if (errno == EAGAIN) {
        action = PostIoAction::KeepOpen;
      } else {
        action = PostIoAction::Close;
      }
      break;
    } else {
      bytes_written += rc;
    }
  }

  return {action, bytes_written, false};
}

std::string VppSocket::protocol() const { return EMPTY_STRING; }

void VppSocket::onConnected() {
  callbacks_->raiseEvent(Network::ConnectionEvent::Connected);
}

void VppSocket::closeSocket(Network::ConnectionEvent) {}

Network::TransportSocketPtr VppSocketFactory::createTransportSocket() const {
  return std::make_unique<VppSocket>();
}

bool VppSocketFactory::implementsSecureTransport() const { return false; }
} // namespace Vpp
} // namespace Envoy
