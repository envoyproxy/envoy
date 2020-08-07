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
 * TestSocketInterface::Install() must be called early in the test initialization before
 * any network connections were established.
 */
namespace Envoy {
namespace Network {

class TestIoSocketHandle : public IoSocketHandleImpl {
public:
  TestIoSocketHandle(os_fd_t fd = INVALID_SOCKET, bool socket_v6only = false)
      : IoSocketHandleImpl(fd, socket_v6only) {}

  ~TestIoSocketHandle() override;

  /**
   * Override the behavior of the IoSocketHandleImpl::writev() method.
   * The supplied callback is invoked with the arguments of the writev method.
   * Returning absl::nullopt from the callback continues normal execution of the
   * IoSocketHandleImpl::writev() method. Returning a Api::IoCallUint64Result from callback aborts
   * the IoSocketHandleImpl::writev() with the returned result value.
   */
  using WritevOverrideProc = std::function<absl::optional<Api::IoCallUint64Result>(
      const Buffer::RawSlice* slices, uint64_t num_slice)>;
  void setWritevOverride(WritevOverrideProc override_proc);

private:
  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;

  absl::Mutex mutex_;
  WritevOverrideProc writev_override_ ABSL_GUARDED_BY(mutex_);
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
  /**
   * Replace default socket interface with the TestSocketInterface singleton.
   * Must be called early in the test initialization before any network connections were
   * established.
   */
  static void Install();
  static const TestSocketInterface& GetSingleton() { return *singleton_; }

  /**
   * Wait for the Nth accepted socket and return a pointer to it.
   */
  TestIoSocketHandle* waitForAcceptedSocket(uint32_t index) const;

  // SocketInterface
  IoHandlePtr socket(os_fd_t fd) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override {
    return "envoy.extensions.network.socket_interface.test_socket_interface";
  };

private:
  friend class TestIoSocketHandle;
  static TestSocketInterface& GetMutableSingleton() { return *singleton_; }

  void addAcceptedSocket(TestIoSocketHandle* handle);
  void clearSocket(TestIoSocketHandle* handle);
  void clearAll();

  static TestSocketInterface* singleton_;
  mutable absl::Mutex mutex_;
  std::vector<TestIoSocketHandle*> accepted_sockets_ ABSL_GUARDED_BY(mutex_);
};

} // namespace Network
} // namespace Envoy
