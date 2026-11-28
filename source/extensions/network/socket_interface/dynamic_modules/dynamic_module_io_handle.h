#pragma once

#include <limits>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/network/address.h"
#include "envoy/network/socket.h"
#include "envoy/network/socket_interface.h"

#include "source/common/network/io_socket_handle_impl.h"
#include "source/extensions/network/socket_interface/dynamic_modules/socket_interface_config.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace SocketInterface {
namespace DynamicModules {

// The interface types live in ::Envoy::Network, which the enclosing namespace name shadows.
namespace Network = ::Envoy::Network;

class DynamicModuleIoHandle;

/**
 * Readiness state shared between the worker thread that owns a DynamicModuleIoHandle and the
 * module threads that report readiness for its socket. Owned by the handle; module threads reach
 * it only through the weak reference in SocketReadyHandle.
 *
 * Reported readiness stays pending until it is consumed: Read until a read or accept hook returns
 * Again, and Write once it is delivered. It is delivered on the worker the handle is registered
 * with, and survives the handle not being registered, so readiness reported before registration
 * or while the events are disabled is delivered later.
 */
class SocketSharedState : public std::enable_shared_from_this<SocketSharedState> {
public:
  // Any thread. Records the events and posts one delivery to the registered worker.
  void report(uint32_t file_events);

  // Worker thread of the handle.
  void setRegistration(DynamicModuleIoHandle& handle, Event::Dispatcher& dispatcher);
  void clearRegistration();
  void markClosed();
  uint32_t pending() const;
  uint64_t reportSequence() const;
  // Clears the events unless a report arrived after the sequence was read, so a report racing
  // with the hook that consumed the earlier readiness is not lost.
  void clearPending(uint32_t file_events, uint64_t sequence);

private:
  void onPosted(uint64_t generation);

  mutable absl::Mutex mutex_;
  // The worker and handle of the current registration, cleared on the worker before the
  // registration ends, so a post never reaches a dispatcher or handle that is gone.
  Event::Dispatcher* dispatcher_ ABSL_GUARDED_BY(mutex_){nullptr};
  DynamicModuleIoHandle* handle_ ABSL_GUARDED_BY(mutex_){nullptr};
  // Incremented whenever the registration changes, so a delivery posted for an earlier
  // registration is dropped.
  uint64_t generation_ ABSL_GUARDED_BY(mutex_){0};
  uint64_t report_sequence_ ABSL_GUARDED_BY(mutex_){0};
  uint32_t pending_ ABSL_GUARDED_BY(mutex_){0};
  bool post_in_flight_ ABSL_GUARDED_BY(mutex_){false};
  bool closed_ ABSL_GUARDED_BY(mutex_){false};
};

using SocketSharedStateSharedPtr = std::shared_ptr<SocketSharedState>;
using SocketSharedStateWeakPtr = std::weak_ptr<SocketSharedState>;

/**
 * The readiness handle Envoy hands to the module with
 * envoy_dynamic_module_on_socket_interface_set_socket_handle. The module calls
 * envoy_dynamic_module_callback_socket_interface_io_ready on it from any thread and deletes it
 * with envoy_dynamic_module_callback_socket_interface_io_ready_delete. It holds only a weak
 * reference to the shared state, so deleting the socket on the worker side is never raced by a
 * module thread holding this handle.
 */
class SocketReadyHandle {
public:
  explicit SocketReadyHandle(SocketSharedStateWeakPtr state) : state_(std::move(state)) {}

  // Safe to call from any thread. No-op once the socket is closed.
  void commit(uint32_t events);

private:
  const SocketSharedStateWeakPtr state_;
};

/**
 * Creates a socket of the given family and type whose bind, listen, connect and I/O fail, used in
 * place of a socket the module failed to create, so the operation that requested it fails rather
 * than Envoy dereferencing a null handle. Returns nullptr only when socket(2) itself fails, as the
 * standard socket interface does.
 */
Network::IoHandlePtr makeFailedIoHandle(int domain, Network::Socket::Type socket_type,
                                        bool socket_v6only);

/**
 * A Network::IoHandle over a socket the module created. Everything except the overridden methods
 * is inherited from IoSocketHandleImpl, which operates on the module's file descriptor.
 *
 * With the DelegateIo flag, reads, writes and shutdown go through the module's I/O hooks. With the
 * HandsOverConnections flag, the socket is a listen socket that is never bound or listened on,
 * whose accepted connections come from the module's accept hook and whose readiness comes only
 * from the module. Without a flag, the handle only skips connect() for an already connected socket
 * and releases the module's per-socket state on close.
 */
class DynamicModuleIoHandle : public Network::IoSocketHandleImpl {
public:
  // Takes ownership of the module-created file descriptor. The per-socket module state stays
  // owned by the module; it is released through socket_close.
  DynamicModuleIoHandle(
      DynamicModuleSocketInterfaceConfigSharedPtr config, os_fd_t fd, bool socket_v6only,
      std::optional<int> domain, const Network::SocketCreationOptions& options,
      envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr,
      uint32_t flags, bool already_connected, bool connecting);

  ~DynamicModuleIoHandle() override;

  // Hands the module the readiness handle of a socket that sets a flag. Called by whoever creates
  // the handle, right after construction.
  void attachReadyHandle();

  void setAddresses(Network::Address::InstanceConstSharedPtr local_address,
                    Network::Address::InstanceConstSharedPtr peer_address) {
    local_address_ = std::move(local_address);
    peer_address_ = std::move(peer_address);
  }

  SocketSharedState& sharedState() { return *shared_state_; }

  // Worker thread. Activates the pending readiness Envoy currently wants.
  void deliverPending();

  // Network::IoHandle
  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override;
  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;
  Api::IoCallUint64Result recv(void* buffer, size_t length, int flags) override;
  Api::IoCallUint64Result send(const void* buffer, size_t length) override;
  Api::IoCallUint64Result close() override;
  Api::SysCallIntResult shutdown(int how) override;
  Api::SysCallIntResult bind(Network::Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult listen(int backlog) override;
  Api::SysCallIntResult connect(Network::Address::InstanceConstSharedPtr address) override;
  Network::IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override;
  Network::IoHandlePtr duplicate() override;
  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override;
  void enableFileEvents(uint32_t events) override;
  void resetFileEvents() override;
  absl::StatusOr<Network::Address::InstanceConstSharedPtr> localAddress() override;
  absl::StatusOr<Network::Address::InstanceConstSharedPtr> peerAddress() override;

private:
  bool delegatesIo() const;
  bool handsOverConnections() const;
  // Shared by readv() and recv(); peek leaves the bytes with the module.
  Api::IoCallUint64Result
  readFromModule(envoy_dynamic_module_type_socket_interface_io_slice* slices, size_t num_slices,
                 uint64_t total_length, bool peek);
  // Rejects a connection the accept hook handed over, releasing what the module gave Envoy.
  void rejectAccepted(os_fd_t fd,
                      envoy_dynamic_module_type_socket_interface_socket_module_ptr module_ptr,
                      absl::string_view reason);
  // Calls socket_close at most once, from close() or from a destructor that runs without close().
  void releaseSocketModulePtr();

  const DynamicModuleSocketInterfaceConfigSharedPtr config_;
  const SocketSharedStateSharedPtr shared_state_;
  const size_t max_addresses_cache_size_;
  envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr_{nullptr};
  const uint32_t flags_;
  const bool already_connected_;
  Network::Address::InstanceConstSharedPtr local_address_;
  Network::Address::InstanceConstSharedPtr peer_address_;

  // Worker thread state of the current registration.
  uint32_t enabled_events_{0};
  bool registered_{false};
  // Changes on every registration and reset, so a callback can tell it was replaced.
  uint64_t registration_generation_{0};
  // A delegated client socket holds the module's readiness until the kernel reports its connect
  // done, since Envoy treats the first Write event as the connect completing.
  bool connecting_;
  // Set when a hook consumed readiness during the current event callback without draining it, for
  // example a read stopped by a limit or a capped accept loop, so the rest is delivered again.
  bool progress_{false};
};

// Converts an fd a module returned, mapping a value outside the fd range to INVALID_SOCKET so a
// truncated value never names another socket.
inline os_fd_t toOsFd(int64_t fd) {
  return fd >= 0 && fd <= std::numeric_limits<os_fd_t>::max() ? static_cast<os_fd_t>(fd)
                                                              : INVALID_SOCKET;
}

} // namespace DynamicModules
} // namespace SocketInterface
} // namespace Network
} // namespace Extensions
} // namespace Envoy
