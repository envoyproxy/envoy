#pragma once

#include <atomic>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/network/socket_interface.h"

#include "source/common/common/logger.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/extensions/network/socket_interface/dynamic_modules/socket_interface_config.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace SocketInterface {
namespace DynamicModules {

// The interface types live in ::Envoy::Network, which the enclosing namespace name shadows.
namespace Network = ::Envoy::Network;

class DynamicModuleIoHandle;

/**
 * State shared between the worker thread that owns a DynamicModuleIoHandle and the module
 * threads that report readiness for its socket. Owned by the handle; module threads reach it
 * only through the weak reference in SocketReadyHandle.
 */
class SocketSharedState : public std::enable_shared_from_this<SocketSharedState> {
public:
  void setHandle(DynamicModuleIoHandle* handle) {
    handle_.store(handle, std::memory_order_release);
  }
  void clearHandle() { handle_.store(nullptr, std::memory_order_release); }

  void setDispatcher(Event::Dispatcher& dispatcher) {
    dispatcher_.store(&dispatcher, std::memory_order_release);
  }
  void clearDispatcher() { dispatcher_.store(nullptr, std::memory_order_release); }

  Event::Dispatcher* dispatcher() const { return dispatcher_.load(std::memory_order_acquire); }

  void markClosed() { closed_.store(true, std::memory_order_release); }

  // Runs on the worker thread through a posted callback. Activates the file events the module
  // reported so Envoy invokes the I/O hooks on the next event loop iteration. No-op once the
  // socket is closed.
  void onReady(uint32_t events);

private:
  // Both are only dereferenced on the worker thread; the atomics let a module thread observe
  // them being cleared before it posts.
  std::atomic<DynamicModuleIoHandle*> handle_{nullptr};
  std::atomic<Event::Dispatcher*> dispatcher_{nullptr};
  std::atomic<bool> closed_{false};
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

  // Safe to call from any thread. No-op once the socket is closed or the worker is gone.
  void commit(uint32_t events);

private:
  const SocketSharedStateWeakPtr state_;
};

/**
 * A Network::IoHandle whose data path is delegated to a dynamic module. The module created the
 * underlying file descriptor and performs its own I/O on it inside the I/O hooks; Envoy keeps
 * the event loop. Everything except the overridden methods is inherited from
 * IoSocketHandleImpl, which operates on the module's file descriptor.
 */
class DynamicModuleIoHandle : public Network::IoSocketHandleImpl,
                              public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  // Takes ownership of the module-created file descriptor. The per-socket module state stays
  // owned by the module; it is released through socket_close.
  DynamicModuleIoHandle(
      DynamicModuleSocketInterfaceConfigSharedPtr config, os_fd_t fd, bool socket_v6only,
      std::optional<int> domain, const Network::SocketCreationOptions& options,
      envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr,
      bool already_connected = false);

  ~DynamicModuleIoHandle() override;

  void setSocketModulePtr(
      envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr) {
    socket_module_ptr_ = socket_module_ptr;
  }
  envoy_dynamic_module_type_socket_interface_socket_module_ptr socketModulePtr() const {
    return socket_module_ptr_;
  }
  SocketSharedState& sharedState() { return *shared_state_; }

  // Network::IoHandle
  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override;
  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;
  Api::IoCallUint64Result close() override;
  Api::SysCallIntResult shutdown(int how) override;
  // No-op when the module handed Envoy an already-connected socket; connecting it again fails
  // with "already connected".
  Api::SysCallIntResult connect(Network::Address::InstanceConstSharedPtr address) override;
  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override;
  void resetFileEvents() override;

private:
  // Calls socket_close exactly once. Used by close() and by the destructor as a safety net for
  // paths that destroy the handle without closing it.
  void releaseSocketModulePtr();

  DynamicModuleSocketInterfaceConfigSharedPtr config_;
  const SocketSharedStateSharedPtr shared_state_;
  envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr_{nullptr};
  const bool already_connected_;
};

} // namespace DynamicModules
} // namespace SocketInterface
} // namespace Network
} // namespace Extensions
} // namespace Envoy
