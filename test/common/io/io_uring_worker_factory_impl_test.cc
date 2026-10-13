#include "envoy/common/exception.h"
#include "envoy/extensions/network/socket_interface/v3/default_socket_interface.pb.h"

#include "source/common/io/io_uring_impl.h"
#include "source/common/io/io_uring_worker_factory_impl.h"
#include "source/common/network/socket_interface_impl.h"

#include "test/mocks/server/server_factory_context.h"
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

TEST_F(IoUringWorkerFactoryImplTest, RejectsNonPowerOfTwoProvidedBufferCount) {
  envoy::extensions::network::socket_interface::v3::DefaultSocketInterface config;
  config.mutable_io_uring_options()->mutable_provided_buffer_count()->set_value(129);
  Network::SocketInterfaceImpl socket_interface;
  EXPECT_THROW(socket_interface.createBootstrapExtension(config, context_), EnvoyException);
}

TEST_F(IoUringWorkerFactoryImplTest, RejectsDeferredTaskrunWithSubmissionPolling) {
  envoy::extensions::network::socket_interface::v3::DefaultSocketInterface config;
  auto* options = config.mutable_io_uring_options();
  options->set_enable_deferred_taskrun(true);
  options->set_enable_submission_queue_polling(true);
  Network::SocketInterfaceImpl socket_interface;
  EXPECT_THROW(socket_interface.createBootstrapExtension(config, context_), EnvoyException);
}

TEST_F(IoUringWorkerFactoryImplTest, InitializesWorkerWithReceiveTuningOptions) {
  envoy::extensions::network::socket_interface::v3::DefaultSocketInterface config;
  auto* options = config.mutable_io_uring_options();
  options->mutable_io_uring_size()->set_value(8);
  options->mutable_read_buffer_size()->set_value(64);
  options->mutable_provided_buffer_count()->set_value(128);
  options->set_enable_multishot_receive(true);
  options->set_enable_deferred_taskrun(true);
  Network::SocketInterfaceImpl socket_interface;
  auto extension = socket_interface.createBootstrapExtension(config, context_);
  ASSERT_NE(nullptr, extension);
  extension->onWorkerThreadInitialized();
}

} // namespace
} // namespace Io
} // namespace Envoy
