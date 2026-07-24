#include "envoy/buffer/buffer.h"
#include "envoy/common/platform.h"

#include "source/extensions/network/socket_interface/sockmap/io_handle.h"

#include "test/mocks/api/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Network {
namespace {

// Records the registration and unregistration requests issued by the IoHandle so the tests can
// assert on them.
class FakeBpfDatapath : public BpfDatapath {
public:
  void registerSocket(os_fd_t fd, const Address::Instance& local,
                      const Address::Instance& peer) override {
    ++calls_;
    last_fd_ = fd;
    last_local_ = local.asString();
    last_peer_ = peer.asString();
  }

  void unregisterSocket(const Address::Instance& local, const Address::Instance& peer) override {
    ++unregister_calls_;
    last_unregister_local_ = local.asString();
    last_unregister_peer_ = peer.asString();
  }

  int calls_{0};
  os_fd_t last_fd_{-1};
  std::string last_local_;
  std::string last_peer_;
  int unregister_calls_{0};
  std::string last_unregister_local_;
  std::string last_unregister_peer_;
};

// Fills name with an IPv4 sockaddr for the given dotted address and port.
void fillV4(sockaddr* name, socklen_t* namelen, const char* ip, uint16_t port) {
  sockaddr_in sin;
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons(port);
  inet_pton(AF_INET, ip, &sin.sin_addr);
  memcpy(name, &sin, sizeof(sin));
  *namelen = sizeof(sin);
}

void expectLocalAndPeer(NiceMock<Api::MockOsSysCalls>& os) {
  EXPECT_CALL(os, getsockname(_, _, _))
      .WillRepeatedly(Invoke([](os_fd_t, sockaddr* name, socklen_t* namelen) {
        fillV4(name, namelen, "127.0.0.1", 1000);
        return Api::SysCallIntResult{0, 0};
      }));
  EXPECT_CALL(os, getpeername(_, _, _))
      .WillRepeatedly(Invoke([](os_fd_t, sockaddr* name, socklen_t* namelen) {
        fillV4(name, namelen, "127.0.0.1", 2000);
        return Api::SysCallIntResult{0, 0};
      }));
}

TEST(SockmapIoSocketHandle, RegistersOnFirstSuccessfulWrite) {
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  auto datapath = std::make_shared<FakeBpfDatapath>();
  SockmapIoSocketHandle handle(datapath, 5);

  EXPECT_CALL(os_sys_calls, send(_, _, _, _)).WillRepeatedly(Return(Api::SysCallSizeResult{4, 0}));
  expectLocalAndPeer(os_sys_calls);

  char buf[4] = {'d', 'a', 't', 'a'};
  Buffer::RawSlice slice{buf, sizeof(buf)};
  Api::IoCallUint64Result result = handle.writev(&slice, 1);
  ASSERT_TRUE(result.ok());

  EXPECT_EQ(datapath->calls_, 1);
  EXPECT_EQ(datapath->last_fd_, 5);
  EXPECT_EQ(datapath->last_local_, "127.0.0.1:1000");
  EXPECT_EQ(datapath->last_peer_, "127.0.0.1:2000");
}

TEST(SockmapIoSocketHandle, RegistersOnFirstSuccessfulRead) {
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  auto datapath = std::make_shared<FakeBpfDatapath>();
  SockmapIoSocketHandle handle(datapath, 7);

  EXPECT_CALL(os_sys_calls, recv(_, _, _, _)).WillRepeatedly(Return(Api::SysCallSizeResult{4, 0}));
  expectLocalAndPeer(os_sys_calls);

  char buf[8];
  Buffer::RawSlice slice{buf, sizeof(buf)};
  Api::IoCallUint64Result result = handle.readv(sizeof(buf), &slice, 1);
  ASSERT_TRUE(result.ok());

  EXPECT_EQ(datapath->calls_, 1);
  EXPECT_EQ(datapath->last_fd_, 7);
  EXPECT_EQ(datapath->last_local_, "127.0.0.1:1000");
  EXPECT_EQ(datapath->last_peer_, "127.0.0.1:2000");
}

TEST(SockmapIoSocketHandle, RegistersOnceAcrossReadsAndWrites) {
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  auto datapath = std::make_shared<FakeBpfDatapath>();
  SockmapIoSocketHandle handle(datapath, 5);

  EXPECT_CALL(os_sys_calls, recv(_, _, _, _)).WillRepeatedly(Return(Api::SysCallSizeResult{4, 0}));
  EXPECT_CALL(os_sys_calls, send(_, _, _, _)).WillRepeatedly(Return(Api::SysCallSizeResult{4, 0}));
  expectLocalAndPeer(os_sys_calls);

  char buf[8];
  Buffer::RawSlice slice{buf, sizeof(buf)};
  handle.readv(sizeof(buf), &slice, 1);
  handle.readv(sizeof(buf), &slice, 1);
  handle.writev(&slice, 1);

  EXPECT_EQ(datapath->calls_, 1);
}

TEST(SockmapIoSocketHandle, DoesNotRegisterOnError) {
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  auto datapath = std::make_shared<FakeBpfDatapath>();
  SockmapIoSocketHandle handle(datapath, 5);

  EXPECT_CALL(os_sys_calls, send(_, _, _, _))
      .WillRepeatedly(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_AGAIN}));

  char buf[4] = {'d', 'a', 't', 'a'};
  Buffer::RawSlice slice{buf, sizeof(buf)};
  handle.writev(&slice, 1);

  EXPECT_EQ(datapath->calls_, 0);
}

TEST(SockmapIoSocketHandle, DoesNotRegisterOnZeroBytes) {
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  auto datapath = std::make_shared<FakeBpfDatapath>();
  SockmapIoSocketHandle handle(datapath, 5);

  EXPECT_CALL(os_sys_calls, recv(_, _, _, _)).WillRepeatedly(Return(Api::SysCallSizeResult{0, 0}));

  char buf[8];
  Buffer::RawSlice slice{buf, sizeof(buf)};
  handle.readv(sizeof(buf), &slice, 1);

  EXPECT_EQ(datapath->calls_, 0);
}

TEST(SockmapIoSocketHandle, DoesNotRegisterWhenLocalAddressUnresolved) {
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  auto datapath = std::make_shared<FakeBpfDatapath>();
  SockmapIoSocketHandle handle(datapath, 5);

  EXPECT_CALL(os_sys_calls, send(_, _, _, _)).WillRepeatedly(Return(Api::SysCallSizeResult{4, 0}));
  EXPECT_CALL(os_sys_calls, getsockname(_, _, _))
      .WillRepeatedly(Return(Api::SysCallIntResult{-1, SOCKET_ERROR_AGAIN}));
  // The peer resolves so the missing local address is the only reason registration is skipped.
  EXPECT_CALL(os_sys_calls, getpeername(_, _, _))
      .WillRepeatedly(Invoke([](os_fd_t, sockaddr* name, socklen_t* namelen) {
        fillV4(name, namelen, "127.0.0.1", 2000);
        return Api::SysCallIntResult{0, 0};
      }));

  char buf[4] = {'d', 'a', 't', 'a'};
  Buffer::RawSlice slice{buf, sizeof(buf)};
  handle.writev(&slice, 1);

  EXPECT_EQ(datapath->calls_, 0);
}

TEST(SockmapIoSocketHandle, DoesNotRegisterWhenPeerAddressUnresolved) {
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  auto datapath = std::make_shared<FakeBpfDatapath>();
  SockmapIoSocketHandle handle(datapath, 5);

  EXPECT_CALL(os_sys_calls, send(_, _, _, _)).WillRepeatedly(Return(Api::SysCallSizeResult{4, 0}));
  EXPECT_CALL(os_sys_calls, getsockname(_, _, _))
      .WillRepeatedly(Invoke([](os_fd_t, sockaddr* name, socklen_t* namelen) {
        fillV4(name, namelen, "127.0.0.1", 1000);
        return Api::SysCallIntResult{0, 0};
      }));
  EXPECT_CALL(os_sys_calls, getpeername(_, _, _))
      .WillRepeatedly(Return(Api::SysCallIntResult{-1, SOCKET_ERROR_AGAIN}));

  char buf[4] = {'d', 'a', 't', 'a'};
  Buffer::RawSlice slice{buf, sizeof(buf)};
  handle.writev(&slice, 1);

  EXPECT_EQ(datapath->calls_, 0);
}

TEST(SockmapIoSocketHandle, DoesNotRetryRegistrationAfterUnresolvedAttempt) {
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  auto datapath = std::make_shared<FakeBpfDatapath>();
  SockmapIoSocketHandle handle(datapath, 5);

  EXPECT_CALL(os_sys_calls, send(_, _, _, _)).WillRepeatedly(Return(Api::SysCallSizeResult{4, 0}));
  // The first transfer consumes the single attempt while the local address is still unresolved, and
  // later transfers must not retry even once the address resolves.
  EXPECT_CALL(os_sys_calls, getsockname(_, _, _))
      .WillOnce(Return(Api::SysCallIntResult{-1, SOCKET_ERROR_AGAIN}))
      .WillRepeatedly(Invoke([](os_fd_t, sockaddr* name, socklen_t* namelen) {
        fillV4(name, namelen, "127.0.0.1", 1000);
        return Api::SysCallIntResult{0, 0};
      }));
  EXPECT_CALL(os_sys_calls, getpeername(_, _, _))
      .WillRepeatedly(Invoke([](os_fd_t, sockaddr* name, socklen_t* namelen) {
        fillV4(name, namelen, "127.0.0.1", 2000);
        return Api::SysCallIntResult{0, 0};
      }));

  char buf[4] = {'d', 'a', 't', 'a'};
  Buffer::RawSlice slice{buf, sizeof(buf)};
  handle.writev(&slice, 1);
  EXPECT_EQ(datapath->calls_, 0);
  handle.writev(&slice, 1);
  EXPECT_EQ(datapath->calls_, 0);
}

TEST(SockmapIoSocketHandle, AcceptInvalidReturnsNull) {
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  auto datapath = std::make_shared<FakeBpfDatapath>();
  SockmapIoSocketHandle handle(datapath, 5);

  EXPECT_CALL(os_sys_calls, accept(_, _, _))
      .WillOnce(Return(Api::SysCallSocketResult{-1, SOCKET_ERROR_AGAIN}));

  sockaddr_storage ss;
  socklen_t ss_len = sizeof(ss);
  IoHandlePtr accepted = handle.accept(reinterpret_cast<sockaddr*>(&ss), &ss_len);
  EXPECT_EQ(accepted, nullptr);
}

TEST(SockmapIoSocketHandle, AcceptValidWrapsSockmapHandleSharingDatapath) {
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  auto datapath = std::make_shared<FakeBpfDatapath>();
  SockmapIoSocketHandle handle(datapath, 5, false, AF_INET);

  EXPECT_CALL(os_sys_calls, accept(_, _, _)).WillOnce(Return(Api::SysCallSocketResult{9, 0}));

  sockaddr_storage ss;
  socklen_t ss_len = sizeof(ss);
  IoHandlePtr accepted = handle.accept(reinterpret_cast<sockaddr*>(&ss), &ss_len);
  ASSERT_NE(accepted, nullptr);
  auto* sockmap_handle = dynamic_cast<SockmapIoSocketHandle*>(accepted.get());
  ASSERT_NE(sockmap_handle, nullptr);
  // The accepted handle inherits the listener domain and has not registered before any transfer.
  EXPECT_EQ(sockmap_handle->domain(), AF_INET);
  EXPECT_EQ(datapath->calls_, 0);

  // The accepted handle shares the datapath, so its first successful transfer registers too.
  EXPECT_CALL(os_sys_calls, send(_, _, _, _)).WillRepeatedly(Return(Api::SysCallSizeResult{4, 0}));
  expectLocalAndPeer(os_sys_calls);

  char buf[4] = {'d', 'a', 't', 'a'};
  Buffer::RawSlice slice{buf, sizeof(buf)};
  sockmap_handle->writev(&slice, 1);
  EXPECT_EQ(datapath->calls_, 1);
  EXPECT_EQ(datapath->last_fd_, 9);
}

TEST(SockmapIoSocketHandle, DuplicateWrapsSockmapHandleSharingDatapath) {
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  auto datapath = std::make_shared<FakeBpfDatapath>();
  SockmapIoSocketHandle handle(datapath, 5, false, AF_INET);

  EXPECT_CALL(os_sys_calls, duplicate(5)).WillOnce(Return(Api::SysCallSocketResult{11, 0}));

  IoHandlePtr duplicated = handle.duplicate();
  ASSERT_NE(duplicated, nullptr);
  auto* sockmap_handle = dynamic_cast<SockmapIoSocketHandle*>(duplicated.get());
  ASSERT_NE(sockmap_handle, nullptr);
  // The duplicate inherits the domain and accelerates accepted connections on cloned listeners.
  EXPECT_EQ(sockmap_handle->domain(), AF_INET);

  // The duplicate shares the datapath, so its first successful transfer registers too.
  EXPECT_CALL(os_sys_calls, send(_, _, _, _)).WillRepeatedly(Return(Api::SysCallSizeResult{4, 0}));
  expectLocalAndPeer(os_sys_calls);

  char buf[4] = {'d', 'a', 't', 'a'};
  Buffer::RawSlice slice{buf, sizeof(buf)};
  sockmap_handle->writev(&slice, 1);
  EXPECT_EQ(datapath->calls_, 1);
  EXPECT_EQ(datapath->last_fd_, 11);
}

TEST(SockmapIoSocketHandle, UnregistersOnClose) {
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  auto datapath = std::make_shared<FakeBpfDatapath>();
  SockmapIoSocketHandle handle(datapath, 5);

  EXPECT_CALL(os_sys_calls, send(_, _, _, _)).WillRepeatedly(Return(Api::SysCallSizeResult{4, 0}));
  expectLocalAndPeer(os_sys_calls);

  char buf[4] = {'d', 'a', 't', 'a'};
  Buffer::RawSlice slice{buf, sizeof(buf)};
  handle.writev(&slice, 1);
  ASSERT_EQ(datapath->calls_, 1);

  // Closing a registered socket removes its tuple key using the captured addresses.
  EXPECT_CALL(os_sys_calls, close(5)).WillRepeatedly(Return(Api::SysCallIntResult{0, 0}));
  Api::IoCallUint64Result result = handle.close();
  ASSERT_TRUE(result.ok());

  EXPECT_EQ(datapath->unregister_calls_, 1);
  EXPECT_EQ(datapath->last_unregister_local_, "127.0.0.1:1000");
  EXPECT_EQ(datapath->last_unregister_peer_, "127.0.0.1:2000");
}

TEST(SockmapIoSocketHandle, DoesNotUnregisterWhenNeverRegistered) {
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  auto datapath = std::make_shared<FakeBpfDatapath>();
  SockmapIoSocketHandle handle(datapath, 5);

  // No transfer happened, so there is no key to remove on close.
  EXPECT_CALL(os_sys_calls, close(5)).WillRepeatedly(Return(Api::SysCallIntResult{0, 0}));
  handle.close();

  EXPECT_EQ(datapath->unregister_calls_, 0);
}

TEST(SockmapIoSocketHandle, UnregistersOnDestructionWithoutClose) {
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  auto datapath = std::make_shared<FakeBpfDatapath>();
  EXPECT_CALL(os_sys_calls, send(_, _, _, _)).WillRepeatedly(Return(Api::SysCallSizeResult{4, 0}));
  expectLocalAndPeer(os_sys_calls);
  // The base destructor closes the still-open fd when the handle is dropped without an explicit
  // close.
  EXPECT_CALL(os_sys_calls, close(5)).WillRepeatedly(Return(Api::SysCallIntResult{0, 0}));

  {
    SockmapIoSocketHandle handle(datapath, 5);
    char buf[4] = {'d', 'a', 't', 'a'};
    Buffer::RawSlice slice{buf, sizeof(buf)};
    handle.writev(&slice, 1);
    ASSERT_EQ(datapath->calls_, 1);
    // No explicit close; the destructor at the end of this scope removes the registration.
  }

  EXPECT_EQ(datapath->unregister_calls_, 1);
  EXPECT_EQ(datapath->last_unregister_local_, "127.0.0.1:1000");
  EXPECT_EQ(datapath->last_unregister_peer_, "127.0.0.1:2000");
}

} // namespace
} // namespace Network
} // namespace Envoy
