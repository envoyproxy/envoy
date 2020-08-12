#pragma once

#include <functional>
#include <vector>

#include "envoy/network/address.h"
#include "envoy/network/socket.h"

#include "common/network/io_socket_handle_impl.h"
#include "common/network/socket_interface_impl.h"

#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"

/**
 * TestSocketInterface allows overriding the behavior of the IoHandle interface.
 * TestSocketInterface::install() must be called early in the test initialization before
 * any network connections were established.
 */
namespace Envoy {
namespace Network {

class TestIoSocketHandle : public IoSocketHandleImpl {
public:
  /**
   * Override the behavior of the IoSocketHandleImpl::writev() method.
   * The supplied callback is invoked with the arguments of the writev method.
   * Returning absl::nullopt from the callback continues normal execution of the
   * IoSocketHandleImpl::writev() method. Returning a Api::IoCallUint64Result from callback skips
   * the IoSocketHandleImpl::writev() with the returned result value.
   */
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
 * Example of overriding IoHandle::writev() method:
 *
 *   auto* io_handle = Envoy::Network::TestSocketInterface::GetSingleton().waitForAcceptedSocket(0);
 *   // Simulate TCP push back on the Envoy's downstream network socket, so that outbound frames
 * start to accumulate
 *   // in the transport socket buffer.
 *   io_handle->setWritevOverride([](const Buffer::RawSlice*, uint64_t) {
 *     return Api::IoCallUint64Result(
 *         0, Api::IoErrorPtr(Network::IoSocketError::getIoSocketEagainInstance(),
 * Network::IoSocketError::deleteIoError));
 *   });
 *
 */

class TestSocketInterface : public SocketInterfaceImpl {
public:
  TestSocketInterface();
  ~TestSocketInterface() override;

  void overrideWritev(TestIoSocketHandle::WritevOverrideProc writev);

private:
  void addAcceptedSocket(TestIoSocketHandle* handle);

  // SocketInterface
  using SocketInterfaceImpl::socket;
  IoHandlePtr socket(os_fd_t fd) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override {
    return "envoy.extensions.network.socket_interface.test_socket_interface";
  };

  uint32_t getAcceptedSocketIndex() {
    absl::MutexLock lock(&mutex_);
    return accepted_socket_index_++;
  }

  SocketInterface* const previous_socket_interface_;
  TestIoSocketHandle::WritevOverrideProc writev_overide_proc_;
  mutable absl::Mutex mutex_;
  uint32_t accepted_socket_index_ ABSL_GUARDED_BY(mutex_) = 0;
};

} // namespace Network
} // namespace Envoy
