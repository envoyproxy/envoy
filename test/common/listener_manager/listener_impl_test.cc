#include "envoy/network/address.h"

#include "source/server/config_validation/server.h"

#include "test/integration/server.h"
#include "test/mocks/server/options.h"
#include "test/test_common/environment.h"
#include "test/test_common/file_system_for_test.h"
#include "test/test_common/thread_factory_for_test.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace {
// Regression test for https://github.com/envoyproxy/envoy/issues/28413
TEST(ConfigValidateTest, ValidateGood) {
  Server::TestComponentFactory component_factory;
  EXPECT_TRUE(validateConfig(testing::NiceMock<Server::MockOptions>(TestEnvironment::runfilesPath(
                                 "test/common/listener_manager/internal_listener.yaml")),
                             Network::Address::InstanceConstSharedPtr(), component_factory,
                             Thread::threadFactoryForTest(), Filesystem::fileSystemForTest()));
}

TEST(ConfigValidateTest, ValidateBad) {
  Server::TestComponentFactory component_factory;
  EXPECT_FALSE(validateConfig(testing::NiceMock<Server::MockOptions>(TestEnvironment::runfilesPath(
                                  "test/common/listener_manager/"
                                  "internal_listener_missing_bootstrap.yaml")),
                              Network::Address::InstanceConstSharedPtr(), component_factory,
                              Thread::threadFactoryForTest(), Filesystem::fileSystemForTest()));
}
} // namespace
} // namespace Server
} // namespace Envoy
