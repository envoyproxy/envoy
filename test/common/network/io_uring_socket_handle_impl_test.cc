#include "source/common/network/io_uring_socket_handle_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/io/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

namespace Envoy {
namespace Network {
namespace {

class IoUringSocketHandleTestImpl : public IoUringSocketHandleImpl {
public:
  IoUringSocketHandleTestImpl(Io::IoUringWorkerFactory& factory, bool is_server_socket)
      : IoUringSocketHandleImpl(factory, INVALID_SOCKET, false, absl::nullopt, is_server_socket) {}
  IoUringSocketType ioUringSocketType() const { return io_uring_socket_type_; }
};

TEST(IoUringSocketHandleImpl, CreateServerSocket) {
  Io::MockIoUringWorkerFactory factory;
  IoUringSocketHandleTestImpl impl(factory, true);
  EXPECT_EQ(IoUringSocketType::Server, impl.ioUringSocketType());
}

TEST(IoUringSocketHandleImpl, CreateClientSocket) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Io::MockIoUringSocket socket;
  Io::MockIoUringWorker worker;
  Io::MockIoUringWorkerFactory factory;
  Event::MockDispatcher dispatcher;
  IoUringSocketHandleTestImpl impl(factory, false);
  EXPECT_EQ(IoUringSocketType::Client, impl.ioUringSocketType());
  EXPECT_CALL(worker, addClientSocket(_, _, _)).WillOnce(testing::ReturnRef(socket));
  EXPECT_CALL(factory, getIoUringWorker())
      .WillOnce(testing::Return(OptRef<Io::IoUringWorker>(worker)));
  impl.initializeFileEvent(
      dispatcher, [](uint32_t) {}, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  EXPECT_EQ(IoUringSocketType::Client, impl.ioUringSocketType());
}

} // namespace
} // namespace Network
} // namespace Envoy
