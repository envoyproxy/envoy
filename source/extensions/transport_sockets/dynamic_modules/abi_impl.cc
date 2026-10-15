#include <algorithm>
#include <list>

#include "envoy/api/io_error.h"
#include "envoy/common/platform.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/event/schedulable_cb.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/linked_object.h"
#include "source/extensions/transport_sockets/dynamic_modules/splice_pump.h"
#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

namespace {

DynamicModuleTransportSocket*
toSocket(envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  return static_cast<DynamicModuleTransportSocket*>(transport_socket_envoy_ptr);
}

// Adds bytes moved outside the read and write buffers to the stream info and, on a TCP proxy
// downstream connection, to its wire byte meters, crediting the upstream meter the other way round.
void addWireBytes(StreamInfo::StreamInfo& stream_info, uint64_t bytes_received,
                  uint64_t bytes_sent) {
  stream_info.addBytesReceived(bytes_received);
  stream_info.addBytesSent(bytes_sent);
  const StreamInfo::BytesMeterSharedPtr& downstream_bytes_meter =
      stream_info.getDownstreamBytesMeter();
  if (downstream_bytes_meter == nullptr) {
    return;
  }
  downstream_bytes_meter->addWireBytesReceived(bytes_received);
  downstream_bytes_meter->addWireBytesSent(bytes_sent);
  const StreamInfo::BytesMeterSharedPtr& upstream_bytes_meter = stream_info.getUpstreamBytesMeter();
  if (upstream_bytes_meter != nullptr) {
    upstream_bytes_meter->addWireBytesSent(bytes_received);
    upstream_bytes_meter->addWireBytesReceived(bytes_sent);
  }
}

// Whether a read or write running on the socket still holds bytes in its buffer.
bool hasPendingBytes(const DynamicModuleTransportSocket& socket) {
  const Buffer::Instance* read_buffer = socket.currentReadBuffer();
  const Buffer::Instance* write_buffer = socket.currentWriteBuffer();
  return (read_buffer != nullptr && read_buffer->length() > 0) ||
         (write_buffer != nullptr && write_buffer->length() > 0);
}

class SpliceSession;

// Per-worker registry owning the active splice sessions.
std::list<std::unique_ptr<SpliceSession>>& spliceSessionRegistry();

// Owns the engine-run splice pump of a paired downstream and upstream connection. It watches both
// connections, so when either one closes the pump stops before its borrowed fd closes.
class SpliceSession : public Network::ConnectionCallbacks,
                      public LinkedObject<SpliceSession>,
                      public Event::DeferredDeletable {
public:
  SpliceSession(Network::Connection& down, Network::Connection& up, os_fd_t down_fd, os_fd_t up_fd)
      : down_(down), up_(up),
        pump_(std::make_unique<SplicePump>(
            down_fd, up_fd, down.dispatcher(),
            [this](SplicePump::Outcome outcome) { scheduleTeardown(outcome); })),
        teardown_cb_(down.dispatcher().createSchedulableCallback([this]() { teardown(); })) {}

  bool prepare(bool upstream_needs_ktls) { return pump_->prepare(upstream_needs_ktls); }

  // Removes both connection file events so the pump owns each fd, watches both connections, and
  // arms the pump.
  void start(Network::TransportSocketCallbacks& down_cb, Network::TransportSocketCallbacks& up_cb) {
    down_cb.ioHandle().resetFileEvents();
    up_cb.ioHandle().resetFileEvents();
    down_.addConnectionCallbacks(*this);
    up_.addConnectionCallbacks(*this);
    pump_->arm();
  }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override {
    // Another path closed a connection, such as the TCP proxy idle timeout. Stop the pump in this
    // iteration so it never touches the closed fd or a later socket reusing its number.
    // An error the pump already reported keeps its outcome, so the other connection still resets.
    if (event == Network::ConnectionEvent::LocalClose ||
        event == Network::ConnectionEvent::RemoteClose) {
      teardown();
    }
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  // The pump ends inside one of its own file event callbacks, so tear down on the next iteration.
  void scheduleTeardown(SplicePump::Outcome outcome) {
    outcome_ = outcome;
    teardown_cb_->scheduleCallbackNextIteration();
  }

  // Stops the pump, accounts its bytes, closes both connections, and destroys the session at the
  // end of the iteration. A socket error resets both connections, starting with the one that did
  // not fail, because closing the failed one first makes the TCP proxy close the other gracefully
  // and hide the truncation.
  void teardown() {
    if (torn_down_) {
      return;
    }
    torn_down_ = true;
    down_.removeConnectionCallbacks(*this);
    up_.removeConnectionCallbacks(*this);
    addWireBytes(down_.streamInfo(), pump_->bytesUploaded(), pump_->bytesDownloaded());
    addWireBytes(up_.streamInfo(), pump_->bytesDownloaded(), pump_->bytesUploaded());
    // Remove the pump first so its file events and pipes are gone before a socket fd closes.
    pump_.reset();
    switch (outcome_) {
    case SplicePump::Outcome::Done:
      closeUnlessClosed(down_, Network::ConnectionCloseType::NoFlush);
      closeUnlessClosed(up_, Network::ConnectionCloseType::NoFlush);
      break;
    case SplicePump::Outcome::DownstreamError:
      closeUnlessClosed(up_, Network::ConnectionCloseType::AbortReset);
      closeUnlessClosed(down_, Network::ConnectionCloseType::AbortReset);
      break;
    case SplicePump::Outcome::UpstreamError:
      closeUnlessClosed(down_, Network::ConnectionCloseType::AbortReset);
      closeUnlessClosed(up_, Network::ConnectionCloseType::AbortReset);
      break;
    }
    down_.dispatcher().deferredDelete(removeFromList(spliceSessionRegistry()));
  }

  // A module that broke the empty buffer rule can leave a connection in a delayed close with no
  // file event to finish it, so that one closes now too.
  static void closeUnlessClosed(Network::Connection& connection,
                                Network::ConnectionCloseType type) {
    if (connection.state() != Network::Connection::State::Closed) {
      connection.close(type);
    }
  }

  Network::Connection& down_;
  Network::Connection& up_;
  SplicePumpPtr pump_;
  Event::SchedulableCallbackPtr teardown_cb_;
  SplicePump::Outcome outcome_{SplicePump::Outcome::Done};
  bool torn_down_{false};
};

std::list<std::unique_ptr<SpliceSession>>& spliceSessionRegistry() {
  static thread_local std::list<std::unique_ptr<SpliceSession>> sessions;
  return sessions;
}

// Converts the result of a buffer I/O call to the ABI status and records a peer reset.
envoy_dynamic_module_type_transport_socket_io_status
toIoStatus(DynamicModuleTransportSocket& socket, const Api::IoCallUint64Result& result,
           size_t* bytes_transferred) {
  if (result.ok()) {
    *bytes_transferred = result.return_value_;
    return envoy_dynamic_module_type_transport_socket_io_status_Success;
  }
  const Api::IoError::IoErrorCode error_code = result.err_->getErrorCode();
  if (error_code == Api::IoError::IoErrorCode::Again) {
    return envoy_dynamic_module_type_transport_socket_io_status_Again;
  }
  if (error_code == Api::IoError::IoErrorCode::ConnectionReset) {
    socket.onPeerReset();
  }
  return envoy_dynamic_module_type_transport_socket_io_status_Error;
}

// Returns nullptr within on_set_callbacks, where the stream info of a client connection does not
// exist yet.
StreamInfo::StreamInfo* connectionStreamInfo(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  DynamicModuleTransportSocket* socket = toSocket(transport_socket_envoy_ptr);
  if (socket->callbacks() == nullptr || socket->inSetCallbacks()) {
    return nullptr;
  }
  return &socket->callbacks()->connection().streamInfo();
}

// Fills the address string and port from an IP address. Returns false for null or non-IP addresses.
bool fillIpAddress(const Network::Address::InstanceConstSharedPtr& address,
                   envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  if (address == nullptr || address->ip() == nullptr) {
    *port_out = 0;
    return false;
  }
  const std::string& addr_str = address->ip()->addressAsString();
  *address_out = {addr_str.data(), addr_str.size()};
  *port_out = address->ip()->port();
  return true;
}

} // namespace

// Transport socket callbacks. These are the strong definitions that override the weak stubs in
// source/extensions/dynamic_modules/abi_impl.cc when this extension is linked.
extern "C" {

envoy_dynamic_module_type_transport_socket_io_status
envoy_dynamic_module_callback_transport_socket_io_read(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr, char* buffer,
    size_t length, size_t* bytes_read) {
  *bytes_read = 0;
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    return envoy_dynamic_module_type_transport_socket_io_status_Error;
  }
  Api::IoCallUint64Result result = callbacks->ioHandle().recv(buffer, length, 0);
  if (result.ok()) {
    *bytes_read = result.return_value_;
    return envoy_dynamic_module_type_transport_socket_io_status_Success;
  }
  if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
    return envoy_dynamic_module_type_transport_socket_io_status_Again;
  }
  return envoy_dynamic_module_type_transport_socket_io_status_Error;
}

envoy_dynamic_module_type_transport_socket_io_status
envoy_dynamic_module_callback_transport_socket_io_write(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    const char* buffer, size_t length, size_t* bytes_written) {
  *bytes_written = 0;
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    return envoy_dynamic_module_type_transport_socket_io_status_Error;
  }
  Buffer::RawSlice slice = {const_cast<char*>(buffer), length};
  Api::IoCallUint64Result result = callbacks->ioHandle().writev(&slice, 1);
  if (result.ok()) {
    *bytes_written = result.return_value_;
    return envoy_dynamic_module_type_transport_socket_io_status_Success;
  }
  if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
    return envoy_dynamic_module_type_transport_socket_io_status_Again;
  }
  return envoy_dynamic_module_type_transport_socket_io_status_Error;
}

envoy_dynamic_module_type_transport_socket_io_status
envoy_dynamic_module_callback_transport_socket_io_read_to_read_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    size_t max_length, size_t* bytes_read) {
  *bytes_read = 0;
  DynamicModuleTransportSocket* socket = toSocket(transport_socket_envoy_ptr);
  Buffer::Instance* buffer = socket->currentReadBuffer();
  if (socket->callbacks() == nullptr || buffer == nullptr) {
    return envoy_dynamic_module_type_transport_socket_io_status_Error;
  }
  return toIoStatus(*socket, socket->callbacks()->ioHandle().read(*buffer, max_length), bytes_read);
}

envoy_dynamic_module_type_transport_socket_io_status
envoy_dynamic_module_callback_transport_socket_io_write_from_write_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    size_t* bytes_written) {
  *bytes_written = 0;
  DynamicModuleTransportSocket* socket = toSocket(transport_socket_envoy_ptr);
  Buffer::Instance* buffer = socket->currentWriteBuffer();
  if (socket->callbacks() == nullptr || buffer == nullptr) {
    return envoy_dynamic_module_type_transport_socket_io_status_Error;
  }
  if (buffer->length() == 0) {
    return envoy_dynamic_module_type_transport_socket_io_status_Success;
  }
  return toIoStatus(*socket, socket->callbacks()->ioHandle().write(*buffer), bytes_written);
}

void envoy_dynamic_module_callback_transport_socket_io_shutdown_write(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    return;
  }
  callbacks->ioHandle().shutdown(ENVOY_SHUT_WR);
}

int envoy_dynamic_module_callback_transport_socket_get_fd(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    return -1;
  }
  return static_cast<int>(callbacks->ioHandle().fdDoNotUse());
}

void envoy_dynamic_module_callback_transport_socket_read_buffer_add(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    const char* data, size_t length) {
  Buffer::Instance* buffer = toSocket(transport_socket_envoy_ptr)->currentReadBuffer();
  if (buffer == nullptr || data == nullptr || length == 0) {
    return;
  }
  buffer->add(data, length);
}

void envoy_dynamic_module_callback_transport_socket_write_buffer_drain(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    size_t length) {
  Buffer::Instance* buffer = toSocket(transport_socket_envoy_ptr)->currentWriteBuffer();
  if (buffer == nullptr) {
    return;
  }
  buffer->drain(std::min<uint64_t>(length, buffer->length()));
}

void envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* slices, size_t* slices_count) {
  Buffer::Instance* buffer = toSocket(transport_socket_envoy_ptr)->currentWriteBuffer();
  if (buffer == nullptr) {
    *slices_count = 0;
    return;
  }
  Buffer::RawSliceVector raw_slices = buffer->getRawSlices();
  if (slices == nullptr) {
    *slices_count = raw_slices.size();
    return;
  }
  const size_t count = std::min(*slices_count, raw_slices.size());
  for (size_t i = 0; i < count; i++) {
    slices[i].ptr = static_cast<const char*>(raw_slices[i].mem_);
    slices[i].length = raw_slices[i].len_;
  }
  *slices_count = count;
}

size_t envoy_dynamic_module_callback_transport_socket_write_buffer_length(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  Buffer::Instance* buffer = toSocket(transport_socket_envoy_ptr)->currentWriteBuffer();
  if (buffer == nullptr) {
    return 0;
  }
  return buffer->length();
}

void envoy_dynamic_module_callback_transport_socket_raise_event(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_network_connection_event event) {
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    return;
  }
  switch (event) {
  case envoy_dynamic_module_type_network_connection_event_RemoteClose:
    callbacks->raiseEvent(Network::ConnectionEvent::RemoteClose);
    return;
  case envoy_dynamic_module_type_network_connection_event_LocalClose:
    callbacks->raiseEvent(Network::ConnectionEvent::LocalClose);
    return;
  case envoy_dynamic_module_type_network_connection_event_Connected:
    callbacks->raiseEvent(Network::ConnectionEvent::Connected);
    return;
  case envoy_dynamic_module_type_network_connection_event_ConnectedZeroRtt:
    callbacks->raiseEvent(Network::ConnectionEvent::ConnectedZeroRtt);
    return;
  }
}

bool envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    return false;
  }
  return callbacks->shouldDrainReadBuffer();
}

void envoy_dynamic_module_callback_transport_socket_set_is_readable(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  DynamicModuleTransportSocket* socket = toSocket(transport_socket_envoy_ptr);
  Network::TransportSocketCallbacks* callbacks = socket->callbacks();
  if (callbacks == nullptr || socket->spliced() || !callbacks->ioHandle().isOpen() ||
      callbacks->connection().connecting()) {
    return;
  }
  callbacks->setTransportSocketIsReadable();
}

void envoy_dynamic_module_callback_transport_socket_set_is_writable(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  DynamicModuleTransportSocket* socket = toSocket(transport_socket_envoy_ptr);
  Network::TransportSocketCallbacks* callbacks = socket->callbacks();
  if (callbacks == nullptr || socket->spliced() || !callbacks->ioHandle().isOpen() ||
      callbacks->connection().connecting()) {
    return;
  }
  // TransportSocketCallbacks has no `writability` setter, so re-arm the write event directly.
  callbacks->ioHandle().activateFileEvents(Event::FileReadyType::Write);
}

bool envoy_dynamic_module_callback_transport_socket_start_splice(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_envoy_ptr peer_transport_socket_envoy_ptr) {
  if (peer_transport_socket_envoy_ptr == nullptr) {
    return false;
  }
  DynamicModuleTransportSocket* self = toSocket(transport_socket_envoy_ptr);
  DynamicModuleTransportSocket* peer = toSocket(peer_transport_socket_envoy_ptr);
  // The role check also rejects a socket paired with itself. Bytes still in the buffer of a running
  // read or write would stay in Envoy once the pump owns the fds.
  if (self->isUpstream() == peer->isUpstream() || self->callbacks() == nullptr ||
      peer->callbacks() == nullptr || self->spliced() || peer->spliced() ||
      hasPendingBytes(*self) || hasPendingBytes(*peer)) {
    return false;
  }
  DynamicModuleTransportSocket& down = self->isUpstream() ? *peer : *self;
  DynamicModuleTransportSocket& up = self->isUpstream() ? *self : *peer;
  Network::TransportSocketCallbacks& down_cb = *down.callbacks();
  Network::TransportSocketCallbacks& up_cb = *up.callbacks();
  // A secure downstream decrypts in the module, which the splice would bypass.
  if (down.implementsSecureTransport() || !down_cb.ioHandle().isOpen() ||
      !up_cb.ioHandle().isOpen() || down_cb.connection().connecting() ||
      up_cb.connection().connecting() ||
      &down_cb.connection().dispatcher() != &up_cb.connection().dispatcher()) {
    return false;
  }

  auto session = std::make_unique<SpliceSession>(down_cb.connection(), up_cb.connection(),
                                                 down_cb.ioHandle().fdDoNotUse(),
                                                 up_cb.ioHandle().fdDoNotUse());
  // Create the pipes before detaching anything, because this step can fail and the caller must fall
  // back to its buffered path with both connections intact.
  if (!session->prepare(up.implementsSecureTransport())) {
    return false;
  }
  self->markSpliced();
  peer->markSpliced();
  SpliceSession* session_ptr = session.get();
  LinkedList::moveIntoListBack(std::move(session), spliceSessionRegistry());
  session_ptr->start(down_cb, up_cb);
  return true;
}

void envoy_dynamic_module_callback_transport_socket_flush_write_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  DynamicModuleTransportSocket* socket = toSocket(transport_socket_envoy_ptr);
  Network::TransportSocketCallbacks* callbacks = socket->callbacks();
  if (callbacks == nullptr || socket->spliced()) {
    return;
  }
  callbacks->flushWriteBuffer();
}

bool envoy_dynamic_module_callback_transport_socket_get_remote_address(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    *port_out = 0;
    return false;
  }
  return fillIpAddress(callbacks->connection().connectionInfoProvider().remoteAddress(),
                       address_out, port_out);
}

bool envoy_dynamic_module_callback_transport_socket_get_local_address(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    *port_out = 0;
    return false;
  }
  return fillIpAddress(callbacks->connection().connectionInfoProvider().localAddress(), address_out,
                       port_out);
}

bool envoy_dynamic_module_callback_transport_socket_get_connection_id(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    uint64_t* connection_id_out) {
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    return false;
  }
  *connection_id_out = callbacks->connection().id();
  return true;
}

bool envoy_dynamic_module_callback_transport_socket_get_upstream_connection_id(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    uint64_t* connection_id_out) {
  StreamInfo::StreamInfo* stream_info = connectionStreamInfo(transport_socket_envoy_ptr);
  if (stream_info == nullptr) {
    return false;
  }
  const auto upstream_info = stream_info->upstreamInfo();
  if (upstream_info == nullptr || !upstream_info->upstreamConnectionId().has_value()) {
    return false;
  }
  *connection_id_out = upstream_info->upstreamConnectionId().value();
  return true;
}

void envoy_dynamic_module_callback_transport_socket_add_wire_bytes(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    uint64_t bytes_received, uint64_t bytes_sent) {
  StreamInfo::StreamInfo* stream_info = connectionStreamInfo(transport_socket_envoy_ptr);
  if (stream_info == nullptr) {
    return;
  }
  addWireBytes(*stream_info, bytes_received, bytes_sent);
}

} // extern "C"

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
