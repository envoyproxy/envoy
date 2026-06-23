#include "envoy/network/address.h"

#include "source/common/listener_manager/listener_impl.h"
#include "source/common/network/utility.h"
#include "source/server/config_validation/server.h"

#include "test/integration/server.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/listener_component_factory.h"
#include "test/mocks/server/options.h"
#include "test/test_common/environment.h"
#include "test/test_common/file_system_for_test.h"
#include "test/test_common/thread_factory_for_test.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

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

TEST(ListenSocketFactoryImplTest, DeferredSocketOptionAppliedAtFinalPreWorkerInit) {
  NiceMock<MockListenerComponentFactory> listener_factory;
  auto deferred_options = std::make_shared<Network::Socket::Options>();
  auto mock_option = std::make_shared<NiceMock<Network::MockSocketOption>>();
  EXPECT_CALL(*mock_option,
              setOption(testing::_, envoy::config::core::v3::SocketOption::STATE_BOUND))
      .WillOnce(Return(true));
  deferred_options->push_back(mock_option);

  auto factory_or = ListenSocketFactoryImpl::create(
      listener_factory, Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 0),
      Network::Socket::Type::Datagram, /*options=*/nullptr, "deferred_options_listener",
      /*tcp_backlog_size=*/0, ListenerComponentFactory::BindType::ReusePort,
      Network::SocketCreationOptions{}, /*num_sockets=*/1, deferred_options);
  ASSERT_TRUE(factory_or.status().ok());

  EXPECT_TRUE((*factory_or)->doFinalPreWorkerInit().ok());
}
} // namespace
} // namespace Server
} // namespace Envoy
