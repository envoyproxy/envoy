#include "common/network/raw_buffer_socket.h"

#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {

TEST(RawBufferSocketFactory, RawBufferSocketFactory) {
  TransportSocketFactoryPtr factory = Envoy::Network::Test::createRawBufferSocketFactory();
  EXPECT_FALSE(factory->usesProxyProtocolOptions());
}

} // namespace Network
} // namespace Envoy
