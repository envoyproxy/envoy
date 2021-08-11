#include "uds_integration_test.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/network/utility.h"

#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

namespace Envoy {

#if defined(__linux__)
INSTANTIATE_TEST_SUITE_P(
    TestParameters, UdsUpstreamIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(false, true)));
#else
INSTANTIATE_TEST_SUITE_P(
    TestParameters, UdsUpstreamIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(false)));
#endif

TEST_P(UdsUpstreamIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(UdsUpstreamIntegrationTest, RouterHeaderOnlyRequestAndResponse) {
  testRouterHeaderOnlyRequestAndResponse();
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

#if defined(__linux__)
INSTANTIATE_TEST_SUITE_P(
    TestParameters, UdsListenerIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(false, true), testing::Values(0)));
#else
INSTANTIATE_TEST_SUITE_P(
    TestParameters, UdsListenerIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(false), testing::Values(0)));
#endif

// Test the mode parameter, excluding abstract namespace enabled
INSTANTIATE_TEST_SUITE_P(
    TestModeParameter, UdsListenerIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(false), testing::Values(0662)));

void UdsListenerIntegrationTest::initialize() {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* admin_addr = bootstrap.mutable_admin()->mutable_address();
    admin_addr->clear_socket_address();
    admin_addr->mutable_pipe()->set_path(getAdminSocketName());

    auto* listeners = bootstrap.mutable_static_resources()->mutable_listeners();
    RELEASE_ASSERT(!listeners->empty(), "");
    auto filter_chains = listeners->Get(0).filter_chains();
    listeners->Clear();
    auto* listener = listeners->Add();
    listener->set_name("listener_0");
    listener->mutable_address()->mutable_pipe()->set_path(getListenerSocketName());
    listener->mutable_address()->mutable_pipe()->set_mode(getMode());
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
    conn->enableHalfClose(enableHalfClose());
    return conn;
  };
}

// Excluding Windows; chmod(2) against Windows AF_UNIX socket files succeeds,
// but stat(2) against those returns ENOENT.
#ifndef WIN32
TEST_P(UdsListenerIntegrationTest, TestSocketMode) {
  if (abstract_namespace_) {
    // stat(2) against sockets in abstract namespace is not possible
    GTEST_SKIP();
  }

  initialize();

  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  struct stat listener_stat;
  EXPECT_EQ(os_sys_calls.stat(getListenerSocketName().c_str(), &listener_stat).return_value_, 0);
  if (mode_ == 0) {
    EXPECT_NE(listener_stat.st_mode & 0777, 0);
  } else {
    EXPECT_EQ(listener_stat.st_mode & mode_, mode_);
  }
}
#endif

TEST_P(UdsListenerIntegrationTest, TestPeerCredentials) {
  fake_upstreams_count_ = 1;
  initialize();
  auto client_connection = createConnectionFn()();
  codec_client_ = makeHttpConnection(std::move(client_connection));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},    {":path", "/test/long/url"}, {":scheme", "http"},
      {":authority", "host"}, {"x-lyft-user-id", "123"},   {"x-forwarded-for", "10.0.0.1"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest(0);

  auto credentials = codec_client_->connection()->unixSocketPeerCredentials();
#ifndef SO_PEERCRED
  EXPECT_EQ(credentials, absl::nullopt);
#else
  EXPECT_EQ(credentials->pid, getpid());
  EXPECT_EQ(credentials->uid, getuid());
  EXPECT_EQ(credentials->gid, getgid());
#endif

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
}

TEST_P(UdsListenerIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = createConnectionFn();
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

TEST_P(UdsListenerIntegrationTest, RouterHeaderOnlyRequestAndResponse) {
  ConnectionCreationFunction creator = createConnectionFn();
  testRouterHeaderOnlyRequestAndResponse(&creator);
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
