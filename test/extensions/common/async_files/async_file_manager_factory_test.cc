#include <cerrno>
#include <memory>
#include <string>
#include <thread>

#include "envoy/extensions/common/async_files/v3/async_file_manager.pb.h"

#include "source/extensions/common/async_files/async_file_action.h"
#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/common/async_files/async_file_manager.h"
#include "source/extensions/common/async_files/async_file_manager_factory.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

using ::testing::Return;
using ::testing::StrictMock;

class AsyncFileManagerFactoryTest : public ::testing::Test {
public:
  void SetUp() override {
    singleton_manager_ = std::make_unique<Singleton::ManagerImpl>();
    factory_ = AsyncFileManagerFactory::singleton(singleton_manager_.get());
    EXPECT_CALL(mock_posix_file_operations_, supportsAllPosixFileOperations())
        .WillRepeatedly(Return(true));
  }

protected:
  std::unique_ptr<Singleton::ManagerImpl> singleton_manager_;
  StrictMock<Api::MockOsSysCalls> mock_posix_file_operations_;
  std::shared_ptr<AsyncFileManagerFactory> factory_;
};

TEST_F(AsyncFileManagerFactoryTest, ExceptionIfConfigIsInvalid) {
  envoy::extensions::common::async_files::v3::AsyncFileManagerConfig config;
  EXPECT_THROW_WITH_MESSAGE(factory_->getAsyncFileManager(config, &mock_posix_file_operations_),
                            EnvoyException, "unrecognized AsyncFileManagerConfig::ManagerType");
}

TEST_F(AsyncFileManagerFactoryTest, ExceptionIfThreadPoolSelectedAndUnsupported) {
  envoy::extensions::common::async_files::v3::AsyncFileManagerConfig config;
  config.mutable_thread_pool()->set_thread_count(1);
  EXPECT_CALL(mock_posix_file_operations_, supportsAllPosixFileOperations())
      .WillRepeatedly(Return(false));
  EXPECT_THROW_WITH_MESSAGE(factory_->getAsyncFileManager(config, &mock_posix_file_operations_),
                            EnvoyException, "AsyncFileManagerThreadPool not supported");
}

TEST_F(AsyncFileManagerFactoryTest, ExceptionIfGivenInconsistentConfigForSameManagerId) {
  envoy::extensions::common::async_files::v3::AsyncFileManagerConfig config;
  config.mutable_thread_pool()->set_thread_count(1);
  auto manager1 = factory_->getAsyncFileManager(config, &mock_posix_file_operations_);
  config.mutable_thread_pool()->set_thread_count(2);
  EXPECT_THROW_WITH_MESSAGE(factory_->getAsyncFileManager(config, &mock_posix_file_operations_),
                            EnvoyException, "AsyncFileManager mismatched config");
}

TEST_F(AsyncFileManagerFactoryTest, ManagersAreCombinedById) {
  envoy::extensions::common::async_files::v3::AsyncFileManagerConfig config;
  config.mutable_thread_pool()->set_thread_count(1);
  auto manager1 = factory_->getAsyncFileManager(config, &mock_posix_file_operations_);
  auto manager2 = factory_->getAsyncFileManager(config, &mock_posix_file_operations_);
  config.set_id("other");
  config.mutable_thread_pool()->set_thread_count(2);
  auto manager3 = factory_->getAsyncFileManager(config, &mock_posix_file_operations_);
  auto manager4 = factory_->getAsyncFileManager(config, &mock_posix_file_operations_);
  EXPECT_EQ(manager1, manager2);
  EXPECT_EQ(manager3, manager4);
  EXPECT_NE(manager1, manager3);
  EXPECT_THAT(manager1->describe(), testing::ContainsRegex("thread_pool_size = 1"));
  EXPECT_THAT(manager3->describe(), testing::ContainsRegex("thread_pool_size = 2"));
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
