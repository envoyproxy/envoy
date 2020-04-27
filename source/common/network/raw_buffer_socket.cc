#include "common/network/raw_buffer_socket.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Network {

void RawBufferSocket::setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) {
  ASSERT(!callbacks_);
  callbacks_ = &callbacks;
}

IoResult RawBufferSocket::doRead(Buffer::Instance& buffer) {
  PostIoAction action = PostIoAction::KeepOpen;
  uint64_t bytes_read = 0;
  bool end_stream = false;
  do {
    // 16K read is arbitrary. TODO(mattklein123) PERF: Tune the read size.
    Api::IoCallUint64Result result = buffer.read(callbacks_->ioHandle(), 16384);

    if (result.ok()) {
      ENVOY_CONN_LOG(trace, "read returns: {}", callbacks_->connection(), result.rc_);
      if (result.rc_ == 0) {
        // Remote close.
        end_stream = true;
        break;
      }
      bytes_read += result.rc_;
      if (callbacks_->shouldDrainReadBuffer()) {
        callbacks_->setReadBufferReady();
        break;
      }
    } else {
      // Remote error (might be no data).
      ENVOY_CONN_LOG(trace, "read error: {}", callbacks_->connection(),
                     result.err_->getErrorDetails());
      if (result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
        action = PostIoAction::Close;
      }
      break;
    }
  } while (true);

  return {action, bytes_read, end_stream};
}

IoResult RawBufferSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  PostIoAction action;
  uint64_t bytes_written = 0;
  ASSERT(!shutdown_ || buffer.length() == 0);
  do {
    if (buffer.length() == 0) {
      if (end_stream && !shutdown_) {
        // Ignore the result. This can only fail if the connection failed. In that case, the
        // error will be detected on the next read, and dealt with appropriately.
        Api::OsSysCallsSingleton::get().shutdown(callbacks_->ioHandle().fd(), ENVOY_SHUT_WR);
        shutdown_ = true;
      }
      action = PostIoAction::KeepOpen;
      break;
    }
    Api::IoCallUint64Result result = buffer.write(callbacks_->ioHandle());

    if (result.ok()) {
      ENVOY_CONN_LOG(trace, "write returns: {}", callbacks_->connection(), result.rc_);
      bytes_written += result.rc_;
    } else {
      ENVOY_CONN_LOG(trace, "write error: {}", callbacks_->connection(),
                     result.err_->getErrorDetails());
      if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
        action = PostIoAction::KeepOpen;
      } else {
        action = PostIoAction::Close;
      }
      break;
    }
  } while (true);

  return {action, bytes_written, false};
}

std::string RawBufferSocket::protocol() const { return EMPTY_STRING; }
absl::string_view RawBufferSocket::failureReason() const { return EMPTY_STRING; }

void RawBufferSocket::onConnected() { callbacks_->raiseEvent(ConnectionEvent::Connected); }

TransportSocketPtr
RawBufferSocketFactory::createTransportSocket(TransportSocketOptionsSharedPtr) const {
  return std::make_unique<RawBufferSocket>();
}

bool RawBufferSocketFactory::implementsSecureTransport() const { return false; }
} // namespace Network
} // namespace Envoy
