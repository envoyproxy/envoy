#include "source/common/network/raw_buffer_socket.h"
#include "source/common/network/transport_socket_options_impl.h"

#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {

TEST(RawBufferSocketFactory, RawBufferSocketFactory) {
  UpstreamTransportSocketFactoryPtr factory = Envoy::Network::Test::createRawBufferSocketFactory();
  EXPECT_FALSE(factory->implementsSecureTransport());
  std::vector<uint8_t> keys;
  factory->hashKey(keys, nullptr);
  EXPECT_EQ(keys.size(), 0);
  auto options = std::make_shared<TransportSocketOptionsImpl>("server");
  factory->hashKey(keys, options);
  EXPECT_GT(keys.size(), 0);
}

} // namespace Network
} // namespace Envoy
