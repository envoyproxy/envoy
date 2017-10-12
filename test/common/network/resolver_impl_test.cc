#include <cstdint>
#include <list>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/network/resolver.h"
#include "envoy/registry/registry.h"

#include "common/common/thread.h"
#include "common/network/address_impl.h"
#include "common/network/resolver_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "api/address.pb.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace Address {
class IpResolverTest : public testing::Test {
public:
  ResolverFactory* factory_{Registry::FactoryRegistry<ResolverFactory>::getFactory("envoy.ip")};
};

TEST_F(IpResolverTest, Basic) {
  envoy::api::v2::SocketAddress socket_address;
  socket_address.set_address("1.2.3.4");
  socket_address.set_port_value(443);
  auto address = factory_->createResolver()->resolve(socket_address);
  EXPECT_EQ(address->ip()->addressAsString(), "1.2.3.4");
  EXPECT_EQ(address->ip()->port(), 443);
}

TEST_F(IpResolverTest, DisallowsNamedPort) {
  auto resolver = factory_->createResolver();
  envoy::api::v2::SocketAddress socket_address;
  socket_address.set_address("1.2.3.4");
  socket_address.set_named_port("http");
  EXPECT_THROW_WITH_MESSAGE(resolver->resolve(socket_address), EnvoyException,
                            fmt::format("IP resolver can't handle port specifier type {}",
                                        envoy::api::v2::SocketAddress::kNamedPort));
}

TEST(ResolverTest, FromProtoAddress) {
  envoy::api::v2::Address ipv4_address;
  ipv4_address.mutable_socket_address()->set_address("1.2.3.4");
  ipv4_address.mutable_socket_address()->set_port_value(5);
  EXPECT_EQ("1.2.3.4:5", resolveProtoAddress(ipv4_address)->asString());

  envoy::api::v2::Address ipv6_address;
  ipv4_address.mutable_socket_address()->set_address("1::1");
  ipv4_address.mutable_socket_address()->set_port_value(2);
  EXPECT_EQ("[1::1]:2", resolveProtoAddress(ipv4_address)->asString());

  envoy::api::v2::Address pipe_address;
  pipe_address.mutable_pipe()->set_path("/foo/bar");
  EXPECT_EQ("/foo/bar", resolveProtoAddress(pipe_address)->asString());
}

class TestResolver : public Resolver {
public:
  TestResolver(const std::map<std::string, std::string> name_mappings)
      : name_mappings_(name_mappings) {}
  InstanceConstSharedPtr resolve(const envoy::api::v2::SocketAddress& socket_address) {
    const std::string logical = socket_address.address();
    const std::string physical = getPhysicalName(logical);
    const std::string port = getPort(socket_address);
    return InstanceConstSharedPtr{new MockResolvedAddress(fmt::format("{}:{}", logical, port),
                                                          fmt::format("{}:{}", physical, port))};
  }

private:
  std::string getPhysicalName(const std::string& logical) {
    auto it = name_mappings_.find(logical);
    if (it == name_mappings_.end()) {
      throw EnvoyException("no such mapping exists");
    }
    return it->second;
  }

  std::string getPort(const envoy::api::v2::SocketAddress& socket_address) {
    switch (socket_address.port_specifier_case()) {
    case envoy::api::v2::SocketAddress::kNamedPort:
      return socket_address.named_port();
    case envoy::api::v2::SocketAddress::kPortValue:
    // default to port 0 if no port value is specified
    case envoy::api::v2::SocketAddress::PORT_SPECIFIER_NOT_SET:
      return fmt::format("{}", socket_address.port_value());

    default:
      throw EnvoyException(
          fmt::format("Unknown port specifier type {}", socket_address.port_specifier_case()));
    }
  }

  std::map<std::string, std::string> name_mappings_;
};

class TestResolverFactory : public ResolverFactory {
public:
  std::string name() const override { return "envoy.test.resolver"; }

  ResolverPtr createResolver() const override {
    return ResolverPtr{new TestResolver(name_mappings_)};
  }

  void addMapping(const std::string& logical, const std::string& physical) {
    name_mappings_[logical] = physical;
  }

private:
  std::map<std::string, std::string> name_mappings_;
};

TEST(ResolverTest, NonStandardResolver) {
  Registry::RegisterFactory<TestResolverFactory, ResolverFactory> register_resolver;
  auto& test_factory = register_resolver.testGetFactory();
  test_factory.addMapping("foo", "1.2.3.4");
  test_factory.addMapping("bar", "4.3.2.1");

  {
    envoy::api::v2::Address address;
    auto socket = address.mutable_socket_address();
    socket->set_address("foo");
    socket->set_port_value(5);
    socket->set_resolver_name("envoy.test.resolver");
    auto instance = resolveProtoAddress(address);
    EXPECT_EQ("1.2.3.4:5", instance->asString());
    EXPECT_EQ("foo:5", instance->logicalName());
  }
  {
    envoy::api::v2::Address address;
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
  envoy::api::v2::Address address;
  EXPECT_THROW(resolveProtoAddress(address), EnvoyException);
}

TEST(ResolverTest, NoSuchResolver) {
  envoy::api::v2::Address address;
  auto socket = address.mutable_socket_address();
  socket->set_address("foo");
  socket->set_port_value(5);
  socket->set_resolver_name("envoy.test.resolver");
  EXPECT_THROW(resolveProtoAddress(address), EnvoyException);
}

} // namespace Address
} // namespace Network
} // namespace Envoy
