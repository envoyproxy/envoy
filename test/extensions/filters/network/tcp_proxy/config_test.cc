#include <string>

#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.validate.h"

#include "source/extensions/filters/network/tcp_proxy/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpProxy {

TEST(ConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(ConfigFactory().createFilterFactoryFromProto(
                   envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy(), context),
               ProtoValidationException);
}

TEST(ConfigTest, InvalidHeadersToAdd) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  ConfigFactory factory;
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config =
      *dynamic_cast<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy*>(
          factory.createEmptyConfigProto().get());
  config.set_stat_prefix("prefix");
  config.set_cluster("cluster");
  config.mutable_tunneling_config()->set_hostname("example.com:80");

  auto* header = config.mutable_tunneling_config()->add_headers_to_add();
  auto* hdr = header->mutable_header();
  hdr->set_key(":method");
  hdr->set_value("GET");
  EXPECT_THROW(factory.createFilterFactoryFromProto(config, context), EnvoyException);

  config.mutable_tunneling_config()->clear_headers_to_add();
  header = config.mutable_tunneling_config()->add_headers_to_add();
  hdr = header->mutable_header();
  hdr->set_key("host");
  hdr->set_value("example.net:80");
  EXPECT_THROW(factory.createFilterFactoryFromProto(config, context), EnvoyException);
}

// Test that a minimal TcpProxy v2 config works.
TEST(ConfigTest, ConfigTest) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  ConfigFactory factory;
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config =
      *dynamic_cast<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy*>(
          factory.createEmptyConfigProto().get());
  config.set_stat_prefix("prefix");
  config.set_cluster("cluster");

  EXPECT_TRUE(factory.isTerminalFilterByProto(config, context.getServerFactoryContext()));

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  NiceMock<Network::MockReadFilterCallbacks> readFilterCallback;
  EXPECT_CALL(connection, addReadFilter(_))
      .WillRepeatedly(Invoke([&readFilterCallback](Network::ReadFilterSharedPtr filter) {
        filter->initializeReadFilterCallbacks(readFilterCallback);
      }));
  cb(connection);
}

} // namespace TcpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
