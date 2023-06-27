#include "source/common/network/io_uring_socket_handle_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/io/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Network {
namespace {

class IoUringSocketHandleTestImpl : public IoUringSocketHandleImpl {
public:
  IoUringSocketHandleTestImpl(Io::IoUringFactory& factory, bool is_server_socket)
      : IoUringSocketHandleImpl(factory, INVALID_SOCKET, false, absl::nullopt, is_server_socket) {}
  IoUringSocketType ioUringSocketType() const { return io_uring_socket_type_; }
};

// TEST(IoUringSocketHandleImpl, CreateAcceptSocket) {
//   Api::MockOsSysCalls os_sys_calls;
//   TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

//   Io::MockIoUringFactory factory;
//   IoUringSocketHandleTestImpl impl(factory, false);
//   EXPECT_EQ(IoUringSocketType::Unknown, impl.ioUringSocketType());
//   EXPECT_CALL(os_sys_calls, listen(_, _));
//   impl.listen(5);
//   EXPECT_EQ(IoUringSocketType::Accept, impl.ioUringSocketType());
// }

TEST(IoUringSocketHandleImpl, CreateServerSocket) {
  Io::MockIoUringFactory factory;
  IoUringSocketHandleTestImpl impl(factory, true);
  EXPECT_EQ(IoUringSocketType::Server, impl.ioUringSocketType());
}

TEST(IoUringSocketHandleImpl, CreateClientSocket) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Io::MockIoUringSocket socket;
  Io::MockIoUringWorker worker;
  Io::MockIoUringFactory factory;
  Event::MockDispatcher dispatcher;
  IoUringSocketHandleTestImpl impl(factory, false);
  EXPECT_EQ(IoUringSocketType::Unknown, impl.ioUringSocketType());
  EXPECT_CALL(worker, addClientSocket(_, _, _)).WillOnce(testing::ReturnRef(socket));
  EXPECT_CALL(factory, getIoUringWorker())
      .WillOnce(testing::Return(OptRef<Io::IoUringWorker>(worker)));
  impl.initializeFileEvent(
      dispatcher, [](uint32_t) {}, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  EXPECT_EQ(IoUringSocketType::Client, impl.ioUringSocketType());
}

// TEST(IoUringSocketHandleImpl, AcceptError) {
//   Api::MockOsSysCalls os_sys_calls;
//   TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
//   Event::MockDispatcher dispatcher;

//   Io::MockIoUringFactory factory;
//   IoUringSocketHandleTestImpl impl(factory, false);
//   struct sockaddr addr;
//   socklen_t addrlen = sizeof(addr);

//   Io::MockIoUringSocket socket;
//   Io::MockIoUringWorker worker;
//   EXPECT_CALL(os_sys_calls, listen(_, _));
//   impl.listen(5);
//   EXPECT_CALL(worker, addAcceptSocket(_, _, _)).WillOnce(ReturnRef(socket));
//   EXPECT_CALL(factory, getIoUringWorker()).WillOnce(Return(OptRef<Io::IoUringWorker>(worker)));
//   impl.initializeFileEvent(
//       dispatcher,
//       [&](uint32_t) {
//         auto handle = impl.accept(&addr, &addrlen);
//         EXPECT_EQ(handle, nullptr);
//       },
//       Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
//   EXPECT_EQ(IoUringSocketType::Accept, impl.ioUringSocketType());
//   Io::AcceptedSocketParam param{-1, nullptr, 0};
//   const OptRef<Io::AcceptedSocketParam> param_ref = param;
//   EXPECT_CALL(socket, getAcceptedSocketParam()).WillOnce(ReturnRef(param_ref));
//   EXPECT_CALL(socket, clearAcceptedSocketParam());
//   // Accept without AcceptedSocketParam returns nullptr.
//   EXPECT_EQ(impl.accept(&addr, &addrlen), nullptr);
// }

} // namespace
} // namespace Network
} // namespace Envoy
