#include "source/common/io/io_uring_impl.h"
#include "source/common/io/io_uring_worker_factory_impl.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Io {
namespace {

class IoUringWorkerFactoryImplTest : public ::testing::Test {
public:
  IoUringWorkerFactoryImplTest()
      : api_(Api::createApiForTest()), should_skip_(!isIoUringSupported()) {}

  void SetUp() override {
    if (should_skip_) {
      GTEST_SKIP();
    }
  }

  Api::ApiPtr api_;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  std::unique_ptr<IoUringWorkerFactoryImplTest> factory_{};
  bool should_skip_{};
};

TEST_F(IoUringWorkerFactoryImplTest, Basic) {
  IoUringWorkerFactoryImpl factory(2, false, 8192, 1000, context_.threadLocal());
  EXPECT_TRUE(factory.currentThreadRegistered());
  auto dispatcher = api_->allocateDispatcher("test_thread");
  factory.onWorkerThreadInitialized();
  EXPECT_TRUE(factory.getIoUringWorker().has_value());
}

TEST_F(IoUringWorkerFactoryImplTest, ModeConfigurationDefault) {
  // Test default mode (ReadWritev)
  IoUringWorkerFactoryImpl factory(2, false, 8192, 1000, context_.threadLocal());
  auto dispatcher = api_->allocateDispatcher("test_thread");
  factory.onWorkerThreadInitialized();

  auto worker = factory.getIoUringWorker();
  EXPECT_TRUE(worker.has_value());
  EXPECT_EQ(worker->get().getMode(), IoUringMode::ReadWritev);
}

TEST_F(IoUringWorkerFactoryImplTest, ModeConfigurationSendRecv) {
  // Test SendRecv mode
  IoUringWorkerFactoryImpl factory(2, false, 8192, 1000, context_.threadLocal(),
                                   IoUringMode::SendRecv);
  auto dispatcher = api_->allocateDispatcher("test_thread");
  factory.onWorkerThreadInitialized();

  auto worker = factory.getIoUringWorker();
  EXPECT_TRUE(worker.has_value());
  EXPECT_EQ(worker->get().getMode(), IoUringMode::SendRecv);
}

TEST_F(IoUringWorkerFactoryImplTest, ModeConfigurationSendmsgRecvmsg) {
  // Test SendmsgRecvmsg mode
  IoUringWorkerFactoryImpl factory(2, false, 8192, 1000, context_.threadLocal(),
                                   IoUringMode::SendmsgRecvmsg);
  auto dispatcher = api_->allocateDispatcher("test_thread");
  factory.onWorkerThreadInitialized();

  auto worker = factory.getIoUringWorker();
  EXPECT_TRUE(worker.has_value());
  EXPECT_EQ(worker->get().getMode(), IoUringMode::SendmsgRecvmsg);
}

TEST_F(IoUringWorkerFactoryImplTest, ModeConfigurationBackwardCompatibility) {
  // Test that not specifying mode defaults to ReadWritev for backward compatibility
  IoUringWorkerFactoryImpl factory_default(2, false, 8192, 1000, context_.threadLocal());
  IoUringWorkerFactoryImpl factory_explicit(2, false, 8192, 1000, context_.threadLocal(),
                                            IoUringMode::ReadWritev);

  auto dispatcher = api_->allocateDispatcher("test_thread");
  factory_default.onWorkerThreadInitialized();
  factory_explicit.onWorkerThreadInitialized();

  auto worker_default = factory_default.getIoUringWorker();
  auto worker_explicit = factory_explicit.getIoUringWorker();

  EXPECT_TRUE(worker_default.has_value());
  EXPECT_TRUE(worker_explicit.has_value());
  EXPECT_EQ(worker_default->get().getMode(), worker_explicit->get().getMode());
  EXPECT_EQ(worker_default->get().getMode(), IoUringMode::ReadWritev);
}

} // namespace
} // namespace Io
} // namespace Envoy
