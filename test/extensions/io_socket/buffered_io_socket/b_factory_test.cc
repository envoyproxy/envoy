
#include "common/network/address_impl.h"

#include "extensions/io_socket/buffered_io_socket/b_factory.h"
#include "test/test_common/registry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace BufferedIoSocket {
namespace {

class FactoryTest : public testing::Test {};

TEST_F(FactoryTest, TestCreate) {
  BioClientConnectionFactory factory;
  Registry::InjectFactory<ClientConnectionFactory> registration(factory);
  // TODO(lambdai): share_ptr<const Type> covariance
  auto address = std::dynamic_pointer_cast<const Envoy::Network::Address::Instance>(
      std::make_shared<const Network::Address::EnvoyInternalInstance>("abc"));
  auto* f = ClientConnectionFactory::getFactoryByAddress(address);
  // Registered.
  ASSERT_NE(nullptr, f);
  auto connection = f->createClientConnection();
  // Naive implemention.
  ASSERT_EQ(nullptr, connection);
}
} // namespace
} // namespace BufferedIoSocket
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy