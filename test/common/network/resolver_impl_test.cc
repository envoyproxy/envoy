#include <cstdint>
#include <list>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/network/resolver.h"
#include "envoy/registry/registry.h"

#include "common/common/thread.h"
#include "common/network/address_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace Address {
class IpResolverTest : public testing::Test {
public:
  ResolverFactory* factory_{Registry::FactoryRegistry<ResolverFactory>::getFactory("envoy.ip")};
};

TEST_F(IpResolverTest, Basic) {
  auto address = factory_->create()->resolve("1.2.3.4", 443);
  EXPECT_EQ(address->ip()->addressAsString(), "1.2.3.4");
  EXPECT_EQ(address->ip()->port(), 443);
}

TEST_F(IpResolverTest, DisallowsNamedPort) {
  auto resolver = factory_->create();
  EXPECT_THROW(resolver->resolve("1.2.3.4", "http"), EnvoyException);
}

} // namespace Address
} // namespace Network
} // namespace Envoy
