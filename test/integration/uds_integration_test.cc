#include "uds_integration_test.h"

#include "common/event/dispatcher_impl.h"
#include "common/network/utility.h"

#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

namespace Envoy {

INSTANTIATE_TEST_CASE_P(TestParameters, UdsUpstreamIntegrationTest,
                        testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
#if defined(__linux__)
                                         testing::Values(false, true)
#else
                                         testing::Values(false)
#endif
                                             ));

TEST_P(UdsUpstreamIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(UdsUpstreamIntegrationTest, RouterHeaderOnlyRequestAndResponse) {
  testRouterHeaderOnlyRequestAndResponse(true);
}

TEST_P(UdsUpstreamIntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete();
}

TEST_P(UdsUpstreamIntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete();
}

TEST_P(UdsUpstreamIntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete();
}

INSTANTIATE_TEST_CASE_P(TestParameters, UdsListenerIntegrationTest,
                        testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
#if defined(__linux__)
                                         testing::Values(false, true)
#else
                                         testing::Values(false)
#endif
                                             ));

void UdsListenerIntegrationTest::initialize() {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
    auto* admin_addr = bootstrap.mutable_admin()->mutable_address();
    admin_addr->clear_socket_address();
    admin_addr->mutable_pipe()->set_path(getAdminSocketName());

    auto* listeners = bootstrap.mutable_static_resources()->mutable_listeners();
    RELEASE_ASSERT(listeners->size() > 0, "");
    auto filter_chains = listeners->Get(0).filter_chains();
    listeners->Clear();
    auto* listener = listeners->Add();
    listener->set_name("listener_0");
    listener->mutable_address()->mutable_pipe()->set_path(getListenerSocketName());
    *(listener->mutable_filter_chains()) = filter_chains;
  });
  HttpIntegrationTest::initialize();
}

HttpIntegrationTest::ConnectionCreationFunction UdsListenerIntegrationTest::createConnectionFn() {
  return [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn(dispatcher_->createClientConnection(
        Network::Utility::resolveUrl(fmt::format("unix://{}", getListenerSocketName())),
        Network::Address::InstanceConstSharedPtr(), Network::Test::createRawBufferSocket(),
        nullptr));
    conn->enableHalfClose(enable_half_close_);
    return conn;
  };
}

TEST_P(UdsListenerIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = createConnectionFn();
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
}

TEST_P(UdsListenerIntegrationTest, RouterHeaderOnlyRequestAndResponse) {
  ConnectionCreationFunction creator = createConnectionFn();
  testRouterHeaderOnlyRequestAndResponse(true, &creator);
}

TEST_P(UdsListenerIntegrationTest, RouterListenerDisconnectBeforeResponseComplete) {
  ConnectionCreationFunction creator = createConnectionFn();
  testRouterUpstreamDisconnectBeforeResponseComplete(&creator);
}

TEST_P(UdsListenerIntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  ConnectionCreationFunction creator = createConnectionFn();
  testRouterDownstreamDisconnectBeforeRequestComplete(&creator);
}

// TODO(htuch): This is disabled due to
// https://github.com/envoyproxy/envoy/issues/2829.
TEST_P(UdsListenerIntegrationTest, DISABLED_RouterDownstreamDisconnectBeforeResponseComplete) {
  ConnectionCreationFunction creator = createConnectionFn();
  testRouterDownstreamDisconnectBeforeResponseComplete(&creator);
}

} // namespace Envoy
