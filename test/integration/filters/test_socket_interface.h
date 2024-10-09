#pragma once

#include <functional>

#include "envoy/network/address.h"
#include "envoy/network/socket.h"

#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface_impl.h"
#include "source/common/network/utility.h"
#include "source/common/network/win32_socket_handle_impl.h"

#include "test/test_common/network_utility.h"

#include "absl/types/optional.h"

/**
 * TestSocketInterface allows overriding the behavior of the IoHandle interface.
 */
namespace Envoy {
namespace Network {

class TestIoSocketHandle : public Test::IoSocketHandlePlatformImpl {
public:
  using WriteOverrideType = absl::optional<Api::IoCallUint64Result>(
      TestIoSocketHandle* io_handle, const Buffer::RawSlice* slices, uint64_t num_slice,
      Address::InstanceConstSharedPtr& peer_address_override_out);
  using WriteOverrideProc = std::function<WriteOverrideType>;
  using ReadOverrideProc = std::function<void(RecvMsgOutput& output)>;
  using ConnectOverrideProc = std::function<absl::optional<Api::IoCallUint64Result>(
      TestIoSocketHandle* io_handle, Address::InstanceConstSharedPtr& peer_address_override_out)>;

  TestIoSocketHandle(ConnectOverrideProc connect_override_proc,
                     WriteOverrideProc write_override_proc, ReadOverrideProc read_override_proc,
                     size_t address_cache_max_capacity, os_fd_t fd = INVALID_SOCKET,
                     bool socket_v6only = false, absl::optional<int> domain = absl::nullopt)
      : Test::IoSocketHandlePlatformImpl(fd, socket_v6only, domain, address_cache_max_capacity),
        connect_override_(connect_override_proc), write_override_(write_override_proc),
        read_override_(read_override_proc) {
    int type;
    socklen_t length = sizeof(int);
    EXPECT_EQ(0, getOption(SOL_SOCKET, SO_TYPE, &type, &length).return_value_);
    socket_type_ = type == SOCK_STREAM ? Socket::Type::Stream : Socket::Type::Datagram;
  }

  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override {
    absl::MutexLock lock(&mutex_);
    dispatcher_ = &dispatcher;
    Test::IoSocketHandlePlatformImpl::initializeFileEvent(dispatcher, cb, trigger, events);
  }

  // Schedule resumption on the IoHandle by posting a callback to the IoHandle's dispatcher. Note
  // that this operation is inherently racy, nothing guarantees that the TestIoSocketHandle is not
  // deleted before the posted callback executes.
  void activateInDispatcherThread(uint32_t events) {
    absl::MutexLock lock(&mutex_);
    RELEASE_ASSERT(dispatcher_ != nullptr, "null dispatcher");
    dispatcher_->post([this, events]() { activateFileEvents(events); });
  }

  // HTTP/3 sockets won't have a bound peer address, but instead get peer
  // address from the argument in sendmsg. TestIoSocketHandle::sendmsg will
  // stash that in peer_address_override_.
  Address::InstanceConstSharedPtr peerAddress() override {
    if (peer_address_override_.has_value()) {
      return Network::Utility::getAddressWithPort(
          peer_address_override_.value().get(), peer_address_override_.value().get().ip()->port());
    }
    return Test::IoSocketHandlePlatformImpl::peerAddress();
  }

  Socket::Type getSocketType() const { return socket_type_; }

private:
  IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override;
  Api::SysCallIntResult connect(Address::InstanceConstSharedPtr address) override;
  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;
  Api::IoCallUint64Result sendmsg(const Buffer::RawSlice* slices, uint64_t num_slice, int flags,
                                  const Address::Ip* self_ip,
                                  const Address::Instance& peer_address) override;
  Api::IoCallUint64Result recvmsg(Buffer::RawSlice* slices, const uint64_t num_slice,
                                  uint32_t self_port, const UdpSaveCmsgConfig& save_cmsg_config,
                                  RecvMsgOutput& output) override;

  IoHandlePtr duplicate() override;

  OptRef<const Address::Instance> peer_address_override_;
  const ConnectOverrideProc connect_override_;
  const WriteOverrideProc write_override_;
  const ReadOverrideProc read_override_;
  absl::Mutex mutex_;
  Event::Dispatcher* dispatcher_ ABSL_GUARDED_BY(mutex_) = nullptr;
  Socket::Type socket_type_;
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
   * Override the behavior of the IoSocketHandleImpl::writev() and
   * IoSocketHandleImpl::sendmsg() methods.
   * The supplied callback is invoked with the slices arguments of the write method and the index
   * of the accepted socket.
   * Returning absl::nullopt from the callback continues normal execution of the
   * write methods. Returning a Api::IoCallUint64Result from callback skips
   * the write methods with the returned result value.
   */
  TestSocketInterface(TestIoSocketHandle::ConnectOverrideProc connect,
                      TestIoSocketHandle::WriteOverrideProc write,
                      TestIoSocketHandle::ReadOverrideProc read)
      : connect_override_proc_(connect), write_override_proc_(write), read_override_proc_(read) {}

private:
  // SocketInterfaceImpl
  IoHandlePtr makeSocket(int socket_fd, bool socket_v6only, absl::optional<int> domain,
                         const SocketCreationOptions& options) const override;

  const TestIoSocketHandle::ConnectOverrideProc connect_override_proc_;
  const TestIoSocketHandle::WriteOverrideProc write_override_proc_;
  const TestIoSocketHandle::ReadOverrideProc read_override_proc_;
};

} // namespace Network
} // namespace Envoy
