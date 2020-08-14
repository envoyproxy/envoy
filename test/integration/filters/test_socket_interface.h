#pragma once

#include <functional>

#include "envoy/network/address.h"
#include "envoy/network/socket.h"

#include "common/network/io_socket_handle_impl.h"
#include "common/network/socket_interface_impl.h"

#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"

/**
 * TestSocketInterface allows overriding the behavior of the IoHandle interface.
 */
namespace Envoy {
namespace Network {

class TestIoSocketHandle : public IoSocketHandleImpl {
public:
  using WritevOverrideType = absl::optional<Api::IoCallUint64Result>(uint32_t index,
                                                                     const Buffer::RawSlice* slices,
                                                                     uint64_t num_slice);
  using WritevOverrideProc = std::function<WritevOverrideType>;

  TestIoSocketHandle(uint32_t index, WritevOverrideProc writev_override_proc,
                     os_fd_t fd = INVALID_SOCKET, bool socket_v6only = false)
      : IoSocketHandleImpl(fd, socket_v6only), index_(index),
        writev_override_(writev_override_proc) {}

private:
  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;

  const uint32_t index_;
  const WritevOverrideProc writev_override_;
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
      : writev_overide_proc_(writev) {}

private:
  // SocketInterface
  using SocketInterfaceImpl::socket;
  IoHandlePtr socket(os_fd_t fd) override;

  uint32_t getAcceptedSocketIndex() {
    absl::MutexLock lock(&mutex_);
    return accepted_socket_index_++;
  }

  const TestIoSocketHandle::WritevOverrideProc writev_overide_proc_;
  mutable absl::Mutex mutex_;
  uint32_t accepted_socket_index_ ABSL_GUARDED_BY(mutex_) = 0;
};

} // namespace Network
} // namespace Envoy
