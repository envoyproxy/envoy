#pragma once

#include <functional>

#include "envoy/network/address.h"
#include "envoy/network/socket.h"

#include "common/network/io_socket_handle_impl.h"
#include "common/network/socket_interface_impl.h"

#include "absl/types/optional.h"

/**
 * TestSocketInterface allows overriding the behavior of the IoHandle interface.
 */
namespace Envoy {
namespace Network {

class TestIoSocketHandle : public IoSocketHandleImpl {
public:
  using WritevOverrideType = absl::optional<Api::IoCallUint64Result>(TestIoSocketHandle* io_handle,
                                                                     const Buffer::RawSlice* slices,
                                                                     uint64_t num_slice);
  using WritevOverrideProc = std::function<WritevOverrideType>;

  TestIoSocketHandle(WritevOverrideProc writev_override_proc, os_fd_t fd = INVALID_SOCKET,
                     bool socket_v6only = false, absl::optional<int> domain = absl::nullopt)
      : IoSocketHandleImpl(fd, socket_v6only, domain), writev_override_(writev_override_proc) {}

  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override {
    absl::MutexLock lock(&mutex_);
    dispatcher_ = &dispatcher;
    IoSocketHandleImpl::initializeFileEvent(dispatcher, cb, trigger, events);
  }

  // Schedule resumption on the IoHandle by posting a callback to the IoHandle's dispatcher. Note
  // that this operation is inherently racy, nothing guarantees that the TestIoSocketHandle is not
  // deleted before the posted callback executes.
  void activateInDispatcherThread(uint32_t events) {
    absl::MutexLock lock(&mutex_);
    RELEASE_ASSERT(dispatcher_ != nullptr, "null dispatcher");
    dispatcher_->post([this, events]() { activateFileEvents(events); });
  }

private:
  IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override;
  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;
  IoHandlePtr duplicate() override;

  const WritevOverrideProc writev_override_;
  absl::Mutex mutex_;
  Event::Dispatcher* dispatcher_ ABSL_GUARDED_BY(mutex_) = nullptr;
};

/**
 * TestSocketInterface allows overriding of the behavior of the IoHandle interface of
 * accepted sockets.
 * Most integration tests have deterministic order in which Envoy accepts connections.
 * For example a test with one client connection will result in two accepted sockets. First
 * is for the client<->Envoy connection and the second is for the Envoy<->upstream connection.
 */

class TestSocketInterface : public SocketInterfaceImpl {
public:
  /**
   * Override the behavior of the IoSocketHandleImpl::writev() method.
   * The supplied callback is invoked with the arguments of the writev method and the index
   * of the accepted socket.
   * Returning absl::nullopt from the callback continues normal execution of the
   * IoSocketHandleImpl::writev() method. Returning a Api::IoCallUint64Result from callback skips
   * the IoSocketHandleImpl::writev() with the returned result value.
   */
  TestSocketInterface(TestIoSocketHandle::WritevOverrideProc writev)
      : writev_override_proc_(writev) {}

private:
  // SocketInterfaceImpl
  IoHandlePtr makeSocket(int socket_fd, bool socket_v6only,
                         absl::optional<int> domain) const override;

  const TestIoSocketHandle::WritevOverrideProc writev_override_proc_;
};

} // namespace Network
} // namespace Envoy
