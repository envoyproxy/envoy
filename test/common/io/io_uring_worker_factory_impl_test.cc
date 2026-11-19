#include "envoy/extensions/network/socket_interface/v3/default_socket_interface.pb.h"
#include "envoy/extensions/network/socket_interface/v3/default_socket_interface.pb.validate.h"

#include "source/common/io/io_uring_impl.h"
#include "source/common/io/io_uring_worker_factory_impl.h"
#include "source/common/protobuf/utility.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Io {
namespace {

TEST(IoUringOptionsValidationTest, WriteTimeoutMustBePositive) {
  envoy::extensions::network::socket_interface::v3::DefaultSocketInterface config;
  EXPECT_THROW_WITH_REGEX(
      TestUtility::loadFromYamlAndValidate("io_uring_options: {write_timeout_ms: 0}", config),
      ProtoValidationException, "value must be greater than 0");
}

TEST(IoUringOptionsValidationTest, UnsetAndPositiveWriteTimeoutsAreValid) {
  for (const std::string yaml : {"io_uring_options: {}", "io_uring_options: {write_timeout_ms: 1}",
                                 "io_uring_options: {write_timeout_ms: 1000}"}) {
    envoy::extensions::network::socket_interface::v3::DefaultSocketInterface config;
    EXPECT_NO_THROW(TestUtility::loadFromYamlAndValidate(yaml, config));
  }
}

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
  std::unique_ptr<IoUringWorkerFactoryImplTest> factory_;
  bool should_skip_{};
};

TEST_F(IoUringWorkerFactoryImplTest, Basic) {
  IoUringWorkerFactoryImpl factory(2, false, false, 8192, 1000, 131072, 16384,
                                   context_.threadLocal());
  EXPECT_TRUE(factory.currentThreadRegistered());
  auto dispatcher = api_->allocateDispatcher("test_thread");
  factory.onWorkerThreadInitialized();
  EXPECT_TRUE(factory.getIoUringWorker().has_value());
}

} // namespace
} // namespace Io
} // namespace Envoy
