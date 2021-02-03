#include "common/network/address_impl.h"
#include "common/network/listen_socket_impl.h"

#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace {

// Only do the integration tests in supported platforms.
#if defined(TCP_INFO) || defined(SIO_TCP_INFO)
TEST(IoSocketHandleImplIntegration, LastRoundTripIntegrationTest) {
  struct sockaddr_in server;
  // TCP info can not be calculated on loopback.
  // For that reason we connect to a public dns server.
  server.sin_addr.s_addr = inet_addr("1.1.1.1");
  server.sin_family = AF_INET;
  server.sin_port = htons(80);

  Address::InstanceConstSharedPtr addr(new Address::Ipv4Instance(&server));
  auto socket_ = std::make_shared<Envoy::Network::ClientSocketImpl>(addr, nullptr);
  socket_->setBlockingForTest(true);
  EXPECT_TRUE(socket_->connect(addr).rc_ == 0);

  EXPECT_TRUE(socket_->ioHandle().lastRoundTripTime() != absl::nullopt);
}
#endif

} // namespace
} // namespace Network
} // namespace Envoy
