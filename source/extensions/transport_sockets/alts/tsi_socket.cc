#include "extensions/transport_sockets/alts/tsi_socket.h"

#include "common/common/assert.h"
#include "common/common/cleanup.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

TsiSocket::TsiSocket(HandshakerFactory handshaker_factory, HandshakeValidator handshake_validator,
                     Network::TransportSocketPtr&& raw_socket)
    : handshaker_factory_(handshaker_factory), handshake_validator_(handshake_validator),
      raw_buffer_socket_(std::move(raw_socket)) {}

TsiSocket::TsiSocket(HandshakerFactory handshaker_factory, HandshakeValidator handshake_validator)
    : TsiSocket(handshaker_factory, handshake_validator,
                std::make_unique<Network::RawBufferSocket>()) {}

TsiSocket::~TsiSocket() { ASSERT(!handshaker_); }

void TsiSocket::setTransportSocketCallbacks(Envoy::Network::TransportSocketCallbacks& callbacks) {
  ASSERT(!callbacks_);
  callbacks_ = &callbacks;

  noop_callbacks_ = std::make_unique<NoOpTransportSocketCallbacks>(callbacks);
  raw_buffer_socket_->setTransportSocketCallbacks(*noop_callbacks_);
}

std::string TsiSocket::protocol() const {
  // TSI doesn't have a generic way to indicate application layer protocol.
  // TODO(lizan): support application layer protocol from TSI for known TSIs.
  return EMPTY_STRING;
}

absl::string_view TsiSocket::failureReason() const {
  // TODO(htuch): Implement error reason for TSI.
  return EMPTY_STRING;
}

Network::PostIoAction TsiSocket::doHandshake() {
  ASSERT(!handshake_complete_);
  ENVOY_CONN_LOG(debug, "TSI: doHandshake", callbacks_->connection());
  if (!handshaker_next_calling_ && raw_read_buffer_.length() > 0) {
    doHandshakeNext();
  }
  return Network::PostIoAction::KeepOpen;
}

void TsiSocket::doHandshakeNext() {
  ENVOY_CONN_LOG(debug, "TSI: doHandshake next: received: {}", callbacks_->connection(),
                 raw_read_buffer_.length());

  if (!handshaker_) {
    handshaker_ = handshaker_factory_(callbacks_->connection().dispatcher(),
                                      callbacks_->connection().localAddress(),
                                      callbacks_->connection().remoteAddress());
    if (!handshaker_) {
      ENVOY_CONN_LOG(warn, "TSI: failed to create handshaker", callbacks_->connection());
      callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
      return;
    }

    handshaker_->setHandshakerCallbacks(*this);
  }

  handshaker_next_calling_ = true;
  Buffer::OwnedImpl handshaker_buffer;
  handshaker_buffer.move(raw_read_buffer_);
  handshaker_->next(handshaker_buffer);
}

Network::PostIoAction TsiSocket::doHandshakeNextDone(NextResultPtr&& next_result) {
  ASSERT(next_result);

  ENVOY_CONN_LOG(debug, "TSI: doHandshake next done: status: {} to_send: {}",
                 callbacks_->connection(), next_result->status_, next_result->to_send_->length());

  tsi_result status = next_result->status_;
  tsi_handshaker_result* handshaker_result = next_result->result_.get();

  if (status != TSI_INCOMPLETE_DATA && status != TSI_OK) {
    ENVOY_CONN_LOG(debug, "TSI: Handshake failed: status: {}", callbacks_->connection(), status);
    return Network::PostIoAction::Close;
  }

  if (next_result->to_send_->length() > 0) {
    raw_write_buffer_.move(*next_result->to_send_);
  }

  if (status == TSI_OK && handshaker_result != nullptr) {
    tsi_peer peer;
    // returns TSI_OK assuming there is no fatal error. Asserting OK.
    status = tsi_handshaker_result_extract_peer(handshaker_result, &peer);
    ASSERT(status == TSI_OK);
    Cleanup peer_cleanup([&peer]() { tsi_peer_destruct(&peer); });
    ENVOY_CONN_LOG(debug, "TSI: Handshake successful: peer properties: {}",
                   callbacks_->connection(), peer.property_count);
    for (size_t i = 0; i < peer.property_count; ++i) {
      ENVOY_CONN_LOG(debug, "  {}: {}", callbacks_->connection(), peer.properties[i].name,
                     std::string(peer.properties[i].value.data, peer.properties[i].value.length));
    }
    if (handshake_validator_) {
      std::string err;
      const bool peer_validated = handshake_validator_(peer, err);
      if (peer_validated) {
        ENVOY_CONN_LOG(debug, "TSI: Handshake validation succeeded.", callbacks_->connection());
      } else {
        ENVOY_CONN_LOG(debug, "TSI: Handshake validation failed: {}", callbacks_->connection(),
                       err);
        return Network::PostIoAction::Close;
      }
    } else {
      ENVOY_CONN_LOG(debug, "TSI: Handshake validation skipped.", callbacks_->connection());
    }

    const unsigned char* unused_bytes;
    size_t unused_byte_size;

    // returns TSI_OK assuming there is no fatal error. Asserting OK.
    status =
        tsi_handshaker_result_get_unused_bytes(handshaker_result, &unused_bytes, &unused_byte_size);
    ASSERT(status == TSI_OK);
    if (unused_byte_size > 0) {
      raw_read_buffer_.prepend(
          absl::string_view{reinterpret_cast<const char*>(unused_bytes), unused_byte_size});
    }
    ENVOY_CONN_LOG(debug, "TSI: Handshake successful: unused_bytes: {}", callbacks_->connection(),
                   unused_byte_size);

    // returns TSI_OK assuming there is no fatal error. Asserting OK.
    tsi_zero_copy_grpc_protector* frame_protector;
    grpc_core::ExecCtx exec_ctx;
    status = tsi_handshaker_result_create_zero_copy_grpc_protector(handshaker_result, nullptr,
                                                                   &frame_protector);
    ASSERT(status == TSI_OK);
    frame_protector_ = std::make_unique<TsiFrameProtector>(frame_protector);

    handshake_complete_ = true;
    callbacks_->raiseEvent(Network::ConnectionEvent::Connected);
  }

  if (read_error_ || (!handshake_complete_ && end_stream_read_)) {
    ENVOY_CONN_LOG(debug, "TSI: Handshake failed: end of stream without enough data",
                   callbacks_->connection());
    return Network::PostIoAction::Close;
  }

  if (raw_read_buffer_.length() > 0) {
    callbacks_->setReadBufferReady();
  }

  // Try to write raw buffer when next call is done, even this is not in do[Read|Write] stack.
  if (raw_write_buffer_.length() > 0) {
    return raw_buffer_socket_->doWrite(raw_write_buffer_, false).action_;
  }

  return Network::PostIoAction::KeepOpen;
}

Network::IoResult TsiSocket::doRead(Buffer::Instance& buffer) {
  Network::IoResult result = {Network::PostIoAction::KeepOpen, 0, false};
  if (!end_stream_read_ && !read_error_) {
    result = raw_buffer_socket_->doRead(raw_read_buffer_);
    ENVOY_CONN_LOG(debug, "TSI: raw read result action {} bytes {} end_stream {}",
                   callbacks_->connection(), enumToInt(result.action_), result.bytes_processed_,
                   result.end_stream_read_);
    if (result.action_ == Network::PostIoAction::Close && result.bytes_processed_ == 0) {
      return result;
    }

    if (!handshake_complete_ && result.end_stream_read_ && result.bytes_processed_ == 0) {
      return {Network::PostIoAction::Close, result.bytes_processed_, result.end_stream_read_};
    }

    end_stream_read_ = result.end_stream_read_;
    read_error_ = result.action_ == Network::PostIoAction::Close;
  }

  if (!handshake_complete_) {
    Network::PostIoAction action = doHandshake();
    if (action == Network::PostIoAction::Close || !handshake_complete_) {
      return {action, 0, false};
    }
  }

  if (handshake_complete_) {
    ASSERT(frame_protector_);

    uint64_t read_size = raw_read_buffer_.length();
    ENVOY_CONN_LOG(debug, "TSI: unprotecting buffer size: {}", callbacks_->connection(),
                   raw_read_buffer_.length());
    tsi_result status = frame_protector_->unprotect(raw_read_buffer_, buffer);
    ENVOY_CONN_LOG(debug, "TSI: unprotected buffer left: {} result: {}", callbacks_->connection(),
                   raw_read_buffer_.length(), tsi_result_to_string(status));
    result.bytes_processed_ = read_size - raw_read_buffer_.length();
  }

  ENVOY_CONN_LOG(debug, "TSI: do read result action {} bytes {} end_stream {}",
                 callbacks_->connection(), enumToInt(result.action_), result.bytes_processed_,
                 result.end_stream_read_);
  return result;
}

Network::IoResult TsiSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  if (!handshake_complete_) {
    Network::PostIoAction action = doHandshake();
    ASSERT(action == Network::PostIoAction::KeepOpen);
    // TODO(lizan): Handle synchronous handshake when TsiHandshaker supports it.
  }

  if (handshake_complete_) {
    ASSERT(frame_protector_);
    ENVOY_CONN_LOG(debug, "TSI: protecting buffer size: {}", callbacks_->connection(),
                   buffer.length());
    tsi_result status = frame_protector_->protect(buffer, raw_write_buffer_);
    ENVOY_CONN_LOG(debug, "TSI: protected buffer left: {} result: {}", callbacks_->connection(),
                   buffer.length(), tsi_result_to_string(status));
  }

  if (raw_write_buffer_.length() > 0) {
    ENVOY_CONN_LOG(debug, "TSI: raw_write length {} end_stream {}", callbacks_->connection(),
                   raw_write_buffer_.length(), end_stream);
    return raw_buffer_socket_->doWrite(raw_write_buffer_, end_stream && (buffer.length() == 0));
  }
  return {Network::PostIoAction::KeepOpen, 0, false};
}

void TsiSocket::closeSocket(Network::ConnectionEvent) {
  ENVOY_CONN_LOG(debug, "TSI: closing socket", callbacks_->connection());
  if (handshaker_) {
    handshaker_.release()->deferredDelete();
  }
}

void TsiSocket::onConnected() {
  ASSERT(!handshake_complete_);
  doHandshakeNext();
}

void TsiSocket::onNextDone(NextResultPtr&& result) {
  handshaker_next_calling_ = false;

  Network::PostIoAction action = doHandshakeNextDone(std::move(result));
  if (action == Network::PostIoAction::Close) {
    callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
  }
}

TsiSocketFactory::TsiSocketFactory(HandshakerFactory handshaker_factory,
                                   HandshakeValidator handshake_validator)
    : handshaker_factory_(std::move(handshaker_factory)),
      handshake_validator_(std::move(handshake_validator)) {}

bool TsiSocketFactory::implementsSecureTransport() const { return true; }

Network::TransportSocketPtr
TsiSocketFactory::createTransportSocket(Network::TransportSocketOptionsSharedPtr) const {
  return std::make_unique<TsiSocket>(handshaker_factory_, handshake_validator_);
}

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
