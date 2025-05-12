#include "source/common/network/address_impl.h"
#include "source/common/network/listen_socket_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace {

// Only do the integration tests in supported platforms.
// This test requires external internet connectivity and as a result it might
// not work under in environments that limit the external connectivity.
// As such it is tagged with `requires-network` and is not executed in CI.
#if defined(TCP_INFO) || defined(SIO_TCP_INFO)
TEST(IoSocketHandleImplIntegration, LastRoundTripIntegrationTest) {
  // See https://github.com/envoyproxy/envoy/issues/28504.
  DISABLE_UNDER_WINDOWS;

  struct sockaddr_in server;
  // TCP info can not be calculated on loopback.
  // For that reason we connect to a public dns server.
  server.sin_addr.s_addr = inet_addr("1.1.1.1");
  server.sin_family = AF_INET;
  server.sin_port = htons(80);

  Address::InstanceConstSharedPtr addr(new Address::Ipv4Instance(&server));
  auto socket_ = std::make_shared<Envoy::Network::ClientSocketImpl>(addr, nullptr);
  socket_->setBlockingForTest(true);
  EXPECT_TRUE(socket_->connect(addr).return_value_ == 0);

  EXPECT_TRUE(socket_->ioHandle().lastRoundTripTime() != absl::nullopt);
}
#endif

} // namespace
} // namespace Network
} // namespace Envoy
