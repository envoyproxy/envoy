#include "source/common/network/raw_buffer_socket.h"

#include <algorithm>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/http/headers.h"

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
  std::optional<Api::IoError::IoErrorCode> err = std::nullopt;
  if (max_read_buffer_size_.has_value()) {
    accumulated_read_buffer_size_ = std::max(accumulated_read_buffer_size_, buffer.length());
  }
  do {
    if (max_read_buffer_size_.has_value() &&
        accumulated_read_buffer_size_ >= max_read_buffer_size_.value()) {
      break;
    }

    const std::optional<uint64_t> max_length =
        max_read_buffer_size_.has_value()
            ? std::make_optional(max_read_buffer_size_.value() - accumulated_read_buffer_size_)
            : std::nullopt;
    Api::IoCallUint64Result result = callbacks_->ioHandle().read(buffer, max_length);

    if (result.ok()) {
      ENVOY_CONN_LOG(trace, "read returns: {}", callbacks_->connection(), result.return_value_);
      if (result.return_value_ == 0) {
        // Remote close.
        end_stream = true;
        break;
      }
      bytes_read += result.return_value_;
      if (max_read_buffer_size_.has_value()) {
        accumulated_read_buffer_size_ += result.return_value_;
      }
      const bool read_limit_reached =
          max_read_buffer_size_.has_value() &&
          accumulated_read_buffer_size_ >= max_read_buffer_size_.value();
      if (callbacks_->shouldDrainReadBuffer() || read_limit_reached) {
        callbacks_->setTransportSocketIsReadable();
        break;
      }
    } else {
      // Remote error (might be no data).
      ENVOY_CONN_LOG(trace, "read error: {}, code: {}", callbacks_->connection(),
                     result.err_->getErrorDetails(), static_cast<int>(result.err_->getErrorCode()));
      if (result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
        action = PostIoAction::Close;
        err = result.err_->getErrorCode();
      }
      break;
    }
  } while (true);

  return {action, bytes_read, end_stream, err};
}

IoResult RawBufferSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  PostIoAction action;
  uint64_t bytes_written = 0;
  std::optional<Api::IoError::IoErrorCode> err = std::nullopt;
  ASSERT(!shutdown_ || buffer.length() == 0);
  do {
    if (buffer.length() == 0) {
      if (end_stream && !shutdown_) {
        // Ignore the result. This can only fail if the connection failed. In that case, the
        // error will be detected on the next read, and dealt with appropriately.
        callbacks_->ioHandle().shutdown(ENVOY_SHUT_WR);
        shutdown_ = true;
      }
      action = PostIoAction::KeepOpen;
      break;
    }
    Api::IoCallUint64Result result = callbacks_->ioHandle().write(buffer);

    if (result.ok()) {
      ENVOY_CONN_LOG(trace, "write returns: {}", callbacks_->connection(), result.return_value_);
      bytes_written += result.return_value_;
    } else {
      ENVOY_CONN_LOG(trace, "write error: {}, code: {}", callbacks_->connection(),
                     result.err_->getErrorDetails(), static_cast<int>(result.err_->getErrorCode()));
      if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
        action = PostIoAction::KeepOpen;
      } else {
        err = result.err_->getErrorCode();
        action = PostIoAction::Close;
      }
      break;
    }
  } while (true);

  return {action, bytes_written, false, err};
}

std::string RawBufferSocket::protocol() const { return EMPTY_STRING; }
absl::string_view RawBufferSocket::failureReason() const { return EMPTY_STRING; }

void RawBufferSocket::onConnected() { callbacks_->raiseEvent(ConnectionEvent::Connected); }

TransportSocketPtr
RawBufferSocketFactory::createTransportSocket(TransportSocketOptionsConstSharedPtr,
                                              Upstream::HostDescriptionConstSharedPtr) const {
  return std::make_unique<RawBufferSocket>(max_read_buffer_size_);
}

TransportSocketPtr RawBufferSocketFactory::createDownstreamTransportSocket() const {
  return std::make_unique<RawBufferSocket>(max_read_buffer_size_);
}

bool RawBufferSocketFactory::implementsSecureTransport() const { return false; }

} // namespace Network
} // namespace Envoy
