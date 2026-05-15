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

} // namespace
} // namespace Io
} // namespace Envoy
