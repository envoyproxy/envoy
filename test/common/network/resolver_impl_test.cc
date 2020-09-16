#include <cstdint>
#include <list>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/network/resolver.h"
#include "envoy/registry/registry.h"

#include "common/common/thread.h"
#include "common/network/address_impl.h"
#include "common/network/resolver_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace Address {
namespace {

class IpResolverTest : public testing::Test {
public:
  Resolver* resolver_{Registry::FactoryRegistry<Resolver>::getFactory("envoy.ip")};
};

TEST_F(IpResolverTest, Basic) {
  envoy::config::core::v3::SocketAddress socket_address;
  socket_address.set_address("1.2.3.4");
  socket_address.set_port_value(443);
  auto address = resolver_->resolve(socket_address);
  EXPECT_EQ(address->ip()->addressAsString(), "1.2.3.4");
  EXPECT_EQ(address->ip()->port(), 443);
}

TEST_F(IpResolverTest, DisallowsNamedPort) {
  envoy::config::core::v3::SocketAddress socket_address;
  socket_address.set_address("1.2.3.4");
  socket_address.set_named_port("http");
  EXPECT_THROW_WITH_MESSAGE(
      resolver_->resolve(socket_address), EnvoyException,
      fmt::format("IP resolver can't handle port specifier type {}",
                  envoy::config::core::v3::SocketAddress::PortSpecifierCase::kNamedPort));
}

TEST(ResolverTest, FromProtoAddress) {
  envoy::config::core::v3::Address ipv4_address;
  ipv4_address.mutable_socket_address()->set_address("1.2.3.4");
  ipv4_address.mutable_socket_address()->set_port_value(5);
  EXPECT_EQ("1.2.3.4:5", resolveProtoAddress(ipv4_address)->asString());

  envoy::config::core::v3::Address ipv6_address;
  ipv6_address.mutable_socket_address()->set_address("1::1");
  ipv6_address.mutable_socket_address()->set_port_value(2);
  EXPECT_EQ("[1::1]:2", resolveProtoAddress(ipv6_address)->asString());

  envoy::config::core::v3::Address pipe_address;
  pipe_address.mutable_pipe()->set_path("/foo/bar");
  EXPECT_EQ("/foo/bar", resolveProtoAddress(pipe_address)->asString());
}

TEST(ResolverTest, InternalListenerNameFromProtoAddress) {
  envoy::config::core::v3::Address internal_listener_address;
  internal_listener_address.mutable_envoy_internal_address()->set_server_listener_name(
      "internal_listener_foo");
  EXPECT_EQ("envoy://internal_listener_foo",
            resolveProtoAddress(internal_listener_address)->asString());
}

TEST(ResolverTest, UninitializedInternalAddressFromProtoAddress) {
  envoy::config::core::v3::Address internal_address;
  internal_address.mutable_envoy_internal_address();
  EXPECT_DEATH(resolveProtoAddress(internal_address), "panic");
}

// Validate correct handling of ipv4_compat field.
TEST(ResolverTest, FromProtoAddressV4Compat) {
  {
    envoy::config::core::v3::Address ipv6_address;
    ipv6_address.mutable_socket_address()->set_address("1::1");
    ipv6_address.mutable_socket_address()->set_port_value(2);
    auto resolved_addr = resolveProtoAddress(ipv6_address);
    EXPECT_EQ("[1::1]:2", resolved_addr->asString());
  }
  {
    envoy::config::core::v3::Address ipv6_address;
    ipv6_address.mutable_socket_address()->set_address("1::1");
    ipv6_address.mutable_socket_address()->set_port_value(2);
    ipv6_address.mutable_socket_address()->set_ipv4_compat(true);
    auto resolved_addr = resolveProtoAddress(ipv6_address);
    EXPECT_EQ("[1::1]:2", resolved_addr->asString());
  }
}

class TestResolver : public Resolver {
public:
  InstanceConstSharedPtr
  resolve(const envoy::config::core::v3::SocketAddress& socket_address) override {
    const std::string& logical = socket_address.address();
    const std::string physical = getPhysicalName(logical);
    const std::string port = getPort(socket_address);
    return InstanceConstSharedPtr{new MockResolvedAddress(fmt::format("{}:{}", logical, port),
                                                          fmt::format("{}:{}", physical, port))};
  }

  void addMapping(const std::string& logical, const std::string& physical) {
    name_mappings_[logical] = physical;
  }

  std::string name() const override { return "envoy.test.resolver"; }

private:
  std::string getPhysicalName(const std::string& logical) {
    auto it = name_mappings_.find(logical);
    if (it == name_mappings_.end()) {
      throw EnvoyException("no such mapping exists");
    }
    return it->second;
  }

  std::string getPort(const envoy::config::core::v3::SocketAddress& socket_address) {
    switch (socket_address.port_specifier_case()) {
    case envoy::config::core::v3::SocketAddress::PortSpecifierCase::kNamedPort:
      return socket_address.named_port();
    case envoy::config::core::v3::SocketAddress::PortSpecifierCase::kPortValue:
    // default to port 0 if no port value is specified
    case envoy::config::core::v3::SocketAddress::PortSpecifierCase::PORT_SPECIFIER_NOT_SET:
      return absl::StrCat("", socket_address.port_value());

    default:
      throw EnvoyException(
          absl::StrCat("Unknown port specifier type ", socket_address.port_specifier_case()));
    }
  }

  std::map<std::string, std::string> name_mappings_;
};

TEST(ResolverTest, NonStandardResolver) {
  TestResolver test_resolver;
  test_resolver.addMapping("foo", "1.2.3.4");
  test_resolver.addMapping("bar", "4.3.2.1");
  Registry::InjectFactory<Resolver> register_resolver(test_resolver);

  {
    envoy::config::core::v3::Address address;
    auto socket = address.mutable_socket_address();
    socket->set_address("foo");
    socket->set_port_value(5);
    socket->set_resolver_name("envoy.test.resolver");
    auto instance = resolveProtoAddress(address);
    EXPECT_EQ("1.2.3.4:5", instance->asString());
    EXPECT_EQ("foo:5", instance->logicalName());
  }
  {
    envoy::config::core::v3::Address address;
    auto socket = address.mutable_socket_address();
    socket->set_address("bar");
    socket->set_named_port("http");
    socket->set_resolver_name("envoy.test.resolver");
    auto instance = resolveProtoAddress(address);
    EXPECT_EQ("4.3.2.1:http", instance->asString());
    EXPECT_EQ("bar:http", instance->logicalName());
  }
}

TEST(ResolverTest, UninitializedAddress) {
  envoy::config::core::v3::Address address;
  EXPECT_THROW_WITH_MESSAGE(resolveProtoAddress(address), EnvoyException, "Address must be set: ");
}

TEST(ResolverTest, NoSuchResolver) {
  envoy::config::core::v3::Address address;
  auto socket = address.mutable_socket_address();
  socket->set_address("foo");
  socket->set_port_value(5);
  socket->set_resolver_name("envoy.test.resolver");
  EXPECT_THROW_WITH_MESSAGE(resolveProtoAddress(address), EnvoyException,
                            "Unknown address resolver: envoy.test.resolver");
}

} // namespace
} // namespace Address
} // namespace Network
} // namespace Envoy
