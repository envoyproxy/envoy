#include "source/common/network/io_uring_socket_handle_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/io/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

using testing::_;

namespace Envoy {
namespace Network {
namespace {

class IoUringSocketHandleTestImpl : public IoUringSocketHandleImpl {
public:
  IoUringSocketHandleTestImpl(Io::IoUringFactory& factory, bool is_server_socket)
      : IoUringSocketHandleImpl(factory, INVALID_SOCKET, false, absl::nullopt, is_server_socket) {}
  IoUringSocketType ioUringSocketType() const { return io_uring_socket_type_; }
};

TEST(IoUringSocketHandleImpl, CreateAcceptSocket) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Io::MockIoUringFactory factory;
  IoUringSocketHandleTestImpl impl(factory, false);
  EXPECT_EQ(IoUringSocketType::Unknown, impl.ioUringSocketType());
  // Removed when io uring accept socket implemented.
  EXPECT_CALL(os_sys_calls, setsocketblocking(_, false));
  EXPECT_CALL(os_sys_calls, listen(_, _));
  impl.listen(5);
  EXPECT_EQ(IoUringSocketType::Accept, impl.ioUringSocketType());
}

TEST(IoUringSocketHandleImpl, CreateServerSocket) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Io::MockIoUringFactory factory;
  // Removed when io uring server socket implemented.
  EXPECT_CALL(os_sys_calls, setsocketblocking(_, false));
  IoUringSocketHandleTestImpl impl(factory, true);
  EXPECT_EQ(IoUringSocketType::Server, impl.ioUringSocketType());
}

TEST(IoUringSocketHandleImpl, CreateClientSocket) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Io::MockIoUringFactory factory;
  Event::MockDispatcher dispatcher;
  IoUringSocketHandleTestImpl impl(factory, false);
  EXPECT_EQ(IoUringSocketType::Unknown, impl.ioUringSocketType());
  // Removed when io uring client socket implemented.
  EXPECT_CALL(os_sys_calls, setsocketblocking(_, false));
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, _, _));
  impl.initializeFileEvent(
      dispatcher, [](uint32_t) {}, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  EXPECT_EQ(IoUringSocketType::Client, impl.ioUringSocketType());
}

} // namespace
} // namespace Network
} // namespace Envoy
