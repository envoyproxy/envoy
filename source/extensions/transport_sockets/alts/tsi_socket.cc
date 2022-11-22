#include "source/extensions/transport_sockets/alts/tsi_socket.h"

#include "source/common/common/assert.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

TsiSocket::TsiSocket(HandshakerFactory handshaker_factory, HandshakeValidator handshake_validator,
                     Network::TransportSocketPtr&& raw_socket, bool downstream)
    : handshaker_factory_(handshaker_factory), handshake_validator_(handshake_validator),
      raw_buffer_socket_(std::move(raw_socket)), downstream_(downstream) {}

TsiSocket::TsiSocket(HandshakerFactory handshaker_factory, HandshakeValidator handshake_validator,
                     bool downstream)
    : TsiSocket(handshaker_factory, handshake_validator,
                std::make_unique<Network::RawBufferSocket>(), downstream) {
  raw_read_buffer_.setWatermarks(default_max_frame_size_);
}

TsiSocket::~TsiSocket() { ASSERT(!handshaker_); }

void TsiSocket::setTransportSocketCallbacks(Envoy::Network::TransportSocketCallbacks& callbacks) {
  ASSERT(!callbacks_);
  callbacks_ = &callbacks;

  tsi_callbacks_ = std::make_unique<TsiTransportSocketCallbacks>(callbacks, raw_read_buffer_);
  raw_buffer_socket_->setTransportSocketCallbacks(*tsi_callbacks_);
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
    handshaker_ =
        handshaker_factory_(callbacks_->connection().dispatcher(),
                            callbacks_->connection().connectionInfoProvider().localAddress(),
                            callbacks_->connection().connectionInfoProvider().remoteAddress());
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
      TsiInfo tsi_info;
      const bool peer_validated = handshake_validator_(peer, tsi_info, err);
      if (peer_validated) {
        ENVOY_CONN_LOG(debug, "TSI: Handshake validation succeeded.", callbacks_->connection());
      } else {
        ENVOY_CONN_LOG(debug, "TSI: Handshake validation failed: {}", callbacks_->connection(),
                       err);
        return Network::PostIoAction::Close;
      }
      ProtobufWkt::Struct dynamic_metadata;
      ProtobufWkt::Value val;
      val.set_string_value(tsi_info.name_);
      dynamic_metadata.mutable_fields()->insert({std::string("peer_identity"), val});
      callbacks_->connection().streamInfo().setDynamicMetadata(
          "envoy.transport_sockets.peer_information", dynamic_metadata);
      ENVOY_CONN_LOG(debug, "TSI hanshake with peer: {}", callbacks_->connection(), tsi_info.name_);
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
      // All handshake data is consumed.
      ASSERT(raw_read_buffer_.length() == 0);
      raw_read_buffer_.prepend(
          absl::string_view{reinterpret_cast<const char*>(unused_bytes), unused_byte_size});
    }
    ENVOY_CONN_LOG(debug, "TSI: Handshake successful: unused_bytes: {}", callbacks_->connection(),
                   unused_byte_size);

    // returns TSI_OK assuming there is no fatal error. Asserting OK.
    tsi_zero_copy_grpc_protector* frame_protector;
    grpc_core::ExecCtx exec_ctx;
    status = tsi_handshaker_result_create_zero_copy_grpc_protector(
        handshaker_result, &default_max_frame_size_, &frame_protector);
    ASSERT(status == TSI_OK);

    // TODO(yihuazhang): Check the return value once fake TSI frame protector
    // used in tsi_socket_test.cc implements the interface returning the max frame size.
    tsi_zero_copy_grpc_protector_max_frame_size(frame_protector, &actual_frame_size_to_use_);

    // Reset the watermarks with actual negotiated max frame size.
    raw_read_buffer_.setWatermarks(
        std::max<size_t>(actual_frame_size_to_use_, callbacks_->connection().bufferLimit()));
    frame_protector_ = std::make_unique<TsiFrameProtector>(frame_protector);

    handshake_complete_ = true;
    if (raw_write_buffer_.length() == 0) {
      callbacks_->raiseEvent(Network::ConnectionEvent::Connected);
    }
  }

  if (read_error_ || (!handshake_complete_ && end_stream_read_)) {
    ENVOY_CONN_LOG(debug, "TSI: Handshake failed: end of stream without enough data",
                   callbacks_->connection());
    return Network::PostIoAction::Close;
  }

  if (raw_read_buffer_.length() > 0) {
    callbacks_->setTransportSocketIsReadable();
  }

  // Try to write raw buffer when next call is done, even this is not in do[Read|Write] stack.
  if (raw_write_buffer_.length() > 0) {
    Network::IoResult result = raw_buffer_socket_->doWrite(raw_write_buffer_, false);
    if (handshake_complete_ && result.action_ != Network::PostIoAction::Close) {
      callbacks_->raiseEvent(Network::ConnectionEvent::Connected);
    }
    return result.action_;
  }

  return Network::PostIoAction::KeepOpen;
}

Network::IoResult TsiSocket::repeatReadAndUnprotect(Buffer::Instance& buffer,
                                                    Network::IoResult prev_result) {
  Network::IoResult result = prev_result;
  uint64_t total_bytes_processed = 0;

  while (true) {
    // Do unprotect.
    if (raw_read_buffer_.length() > 0) {
      uint64_t prev_size = buffer.length();
      ENVOY_CONN_LOG(debug, "TSI: unprotecting buffer size: {}", callbacks_->connection(),
                     raw_read_buffer_.length());
      tsi_result status = frame_protector_->unprotect(raw_read_buffer_, buffer);
      if (status != TSI_OK) {
        ENVOY_CONN_LOG(debug, "TSI: unprotect failed: status: {}", callbacks_->connection(),
                       status);
        result.action_ = Network::PostIoAction::Close;
        break;
      }
      ASSERT(raw_read_buffer_.length() == 0);
      ENVOY_CONN_LOG(debug, "TSI: unprotected buffer left: {} result: {}", callbacks_->connection(),
                     raw_read_buffer_.length(), tsi_result_to_string(status));
      total_bytes_processed += buffer.length() - prev_size;

      // Check if buffer needs to be drained.
      if (callbacks_->shouldDrainReadBuffer()) {
        callbacks_->setTransportSocketIsReadable();
        break;
      }
    }

    if (result.action_ == Network::PostIoAction::Close) {
      break;
    }

    // End of stream is reached in the previous read.
    if (end_stream_read_) {
      result.end_stream_read_ = true;
      break;
    }
    // Do another read.
    result = readFromRawSocket();
    // No data is read.
    if (result.bytes_processed_ == 0) {
      break;
    }
  };
  result.bytes_processed_ = total_bytes_processed;
  ENVOY_CONN_LOG(debug, "TSI: do read result action {} bytes {} end_stream {}",
                 callbacks_->connection(), enumToInt(result.action_), result.bytes_processed_,
                 result.end_stream_read_);
  return result;
}

Network::IoResult TsiSocket::readFromRawSocket() {
  Network::IoResult result = raw_buffer_socket_->doRead(raw_read_buffer_);
  end_stream_read_ = result.end_stream_read_;
  read_error_ = result.action_ == Network::PostIoAction::Close;
  return result;
}

Network::IoResult TsiSocket::doRead(Buffer::Instance& buffer) {
  Network::IoResult result = {Network::PostIoAction::KeepOpen, 0, false};
  if (!handshake_complete_) {
    if (!end_stream_read_ && !read_error_) {
      result = readFromRawSocket();
      ENVOY_CONN_LOG(debug, "TSI: raw read result action {} bytes {} end_stream {}",
                     callbacks_->connection(), enumToInt(result.action_), result.bytes_processed_,
                     result.end_stream_read_);
      if (result.action_ == Network::PostIoAction::Close && result.bytes_processed_ == 0) {
        return result;
      }

      if (result.end_stream_read_ && result.bytes_processed_ == 0) {
        return {Network::PostIoAction::Close, result.bytes_processed_, result.end_stream_read_};
      }
    }
    Network::PostIoAction action = doHandshake();
    if (action == Network::PostIoAction::Close || !handshake_complete_) {
      return {action, 0, false};
    }
  }
  // Handshake finishes.
  ASSERT(handshake_complete_);
  ASSERT(frame_protector_);
  return repeatReadAndUnprotect(buffer, result);
}

Network::IoResult TsiSocket::repeatProtectAndWrite(Buffer::Instance& buffer, bool end_stream) {
  uint64_t total_bytes_written = 0;
  Network::IoResult result = {Network::PostIoAction::KeepOpen, 0, false};
  // There should be no handshake bytes in raw_write_buffer_.
  ASSERT(!(raw_write_buffer_.length() > 0 && prev_bytes_to_drain_ == 0));
  while (true) {
    uint64_t bytes_to_drain_this_iteration =
        prev_bytes_to_drain_ > 0
            ? prev_bytes_to_drain_
            : std::min<uint64_t>(buffer.length(), actual_frame_size_to_use_ - frame_overhead_size_);
    // Consumed all data. Exit.
    if (bytes_to_drain_this_iteration == 0) {
      break;
    }
    // Short write did not occur previously.
    if (raw_write_buffer_.length() == 0) {
      ASSERT(frame_protector_);
      ASSERT(prev_bytes_to_drain_ == 0);

      // Do protect.
      ENVOY_CONN_LOG(debug, "TSI: protecting buffer size: {}", callbacks_->connection(),
                     bytes_to_drain_this_iteration);
      tsi_result status = frame_protector_->protect(
          grpc_slice_from_static_buffer(buffer.linearize(bytes_to_drain_this_iteration),
                                        bytes_to_drain_this_iteration),
          raw_write_buffer_);
      ENVOY_CONN_LOG(debug, "TSI: protected buffer left: {} result: {}", callbacks_->connection(),
                     bytes_to_drain_this_iteration, tsi_result_to_string(status));
    }

    // Write raw_write_buffer_ to network.
    ENVOY_CONN_LOG(debug, "TSI: raw_write length {} end_stream {}", callbacks_->connection(),
                   raw_write_buffer_.length(), end_stream);
    result = raw_buffer_socket_->doWrite(raw_write_buffer_, end_stream && (buffer.length() == 0));

    // Short write. Exit.
    if (raw_write_buffer_.length() > 0) {
      prev_bytes_to_drain_ = bytes_to_drain_this_iteration;
      break;
    } else {
      buffer.drain(bytes_to_drain_this_iteration);
      prev_bytes_to_drain_ = 0;
      total_bytes_written += bytes_to_drain_this_iteration;
    }
  }

  return {result.action_, total_bytes_written, false};
}

Network::IoResult TsiSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  if (!handshake_complete_) {
    Network::PostIoAction action = doHandshake();
    // Envoy ALTS implements asynchronous tsi_handshaker_next() interface
    // which returns immediately after scheduling a handshake request to
    // the handshake service. The handshake response will be handled by a
    // dedicated thread in a separate API within which handshake_complete_
    // will be set to true if the handshake completes.
    ASSERT(!handshake_complete_);
    ASSERT(action == Network::PostIoAction::KeepOpen);
    // TODO(lizan): Handle synchronous handshake when TsiHandshaker supports it.
    return {Network::PostIoAction::KeepOpen, 0, false};
  } else {
    ASSERT(frame_protector_);
    // Check if we need to flush outstanding handshake bytes.
    if (raw_write_buffer_.length() > 0 && prev_bytes_to_drain_ == 0) {
      ENVOY_CONN_LOG(debug, "TSI: raw_write length {} end_stream {}", callbacks_->connection(),
                     raw_write_buffer_.length(), end_stream);
      Network::IoResult result =
          raw_buffer_socket_->doWrite(raw_write_buffer_, end_stream && (buffer.length() == 0));
      // Check if short write occurred.
      if (raw_write_buffer_.length() > 0) {
        return {result.action_, 0, false};
      }
    }
    return repeatProtectAndWrite(buffer, end_stream);
  }
}

void TsiSocket::closeSocket(Network::ConnectionEvent) {
  ENVOY_CONN_LOG(debug, "TSI: closing socket", callbacks_->connection());
  if (handshaker_) {
    handshaker_.release()->deferredDelete();
  }
}

void TsiSocket::onConnected() {
  ASSERT(!handshake_complete_);
  // Client initiates the handshake, so ignore onConnect call on the downstream.
  if (!downstream_) {
    doHandshakeNext();
  }
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
TsiSocketFactory::createTransportSocket(Network::TransportSocketOptionsConstSharedPtr,
                                        Upstream::HostDescriptionConstSharedPtr) const {
  return std::make_unique<TsiSocket>(handshaker_factory_, handshake_validator_, false);
}

Network::TransportSocketPtr TsiSocketFactory::createDownstreamTransportSocket() const {
  return std::make_unique<TsiSocket>(handshaker_factory_, handshake_validator_, true);
}

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
