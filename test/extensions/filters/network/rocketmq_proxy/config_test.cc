#include "envoy/extensions/filters/network/rocketmq_proxy/v3/rocketmq_proxy.pb.h"
#include "envoy/extensions/filters/network/rocketmq_proxy/v3/rocketmq_proxy.pb.validate.h"

#include "extensions/filters/network/rocketmq_proxy/config.h"

#include "test/mocks/local_info/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/registry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

using RocketmqProxyProto = envoy::extensions::filters::network::rocketmq_proxy::v3::RocketmqProxy;

RocketmqProxyProto parseRocketmqProxyFromV2Yaml(const std::string& yaml) {
  RocketmqProxyProto rocketmq_proxy;
  TestUtility::loadFromYaml(yaml, rocketmq_proxy);
  return rocketmq_proxy;
}

class RocketmqFilterConfigTestBase {
public:
  void testConfig(RocketmqProxyProto& config) {
    Network::FilterFactoryCb cb;
    EXPECT_NO_THROW({ cb = factory_.createFilterFactoryFromProto(config, context_); });
    Network::MockConnection connection;
    EXPECT_CALL(connection, addReadFilter(_));
    cb(connection);
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  RocketmqProxyFilterConfigFactory factory_;
};

class RocketmqFilterConfigTest : public RocketmqFilterConfigTestBase, public testing::Test {
public:
  ~RocketmqFilterConfigTest() override = default;
};

TEST_F(RocketmqFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(
      RocketmqProxyFilterConfigFactory().createFilterFactoryFromProto(
          envoy::extensions::filters::network::rocketmq_proxy::v3::RocketmqProxy(), context),
      ProtoValidationException);
}

TEST_F(RocketmqFilterConfigTest, ValidProtoConfiguration) {
  envoy::extensions::filters::network::rocketmq_proxy::v3::RocketmqProxy config{};
  config.set_stat_prefix("my_stat_prefix");
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RocketmqProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST_F(RocketmqFilterConfigTest, RocketmqProxyWithEmptyProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RocketmqProxyFilterConfigFactory factory;
  envoy::extensions::filters::network::rocketmq_proxy::v3::RocketmqProxy config =
      *dynamic_cast<envoy::extensions::filters::network::rocketmq_proxy::v3::RocketmqProxy*>(
          factory.createEmptyConfigProto().get());
  config.set_stat_prefix("my_stat_prefix");
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST_F(RocketmqFilterConfigTest, RocketmqProxyWithFullConfig) {
  const std::string yaml = R"EOF(
    stat_prefix: rocketmq_incomming_stats
    develop_mode: true
    transient_object_life_span:
      seconds: 30
    )EOF";
  RocketmqProxyProto config = parseRocketmqProxyFromV2Yaml(yaml);
  testConfig(config);
}

TEST_F(RocketmqFilterConfigTest, ProxyAddress) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  Server::Configuration::MockServerFactoryContext factory_context;
  EXPECT_CALL(context, getServerFactoryContext()).WillRepeatedly(ReturnRef(factory_context));

  LocalInfo::MockLocalInfo local_info;
  EXPECT_CALL(factory_context, localInfo()).WillRepeatedly(ReturnRef(local_info));
  std::shared_ptr<const Network::MockResolvedAddress> instance =
      std::make_shared<Network::MockResolvedAddress>("logical", "physical");
  EXPECT_CALL(local_info, address()).WillRepeatedly(Return(instance));
  EXPECT_CALL(*instance, type()).WillRepeatedly(Return(Network::Address::Type::Ip));

  Network::MockIp* ip = new Network::MockIp();
  EXPECT_CALL(*instance, ip()).WillRepeatedly(testing::Return(ip));

  std::string address("1.2.3.4");
  EXPECT_CALL(*ip, addressAsString()).WillRepeatedly(ReturnRef(address));
  EXPECT_CALL(*ip, port()).WillRepeatedly(Return(1234));
  ConfigImpl::RocketmqProxyConfig proxyConfig;
  ConfigImpl configImpl(proxyConfig, context);

  EXPECT_STREQ("1.2.3.4:1234", configImpl.proxyAddress().c_str());
  delete ip;
}

TEST_F(RocketmqFilterConfigTest, ProxyAddressWithDefaultPort) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  Server::Configuration::MockServerFactoryContext factory_context;
  EXPECT_CALL(context, getServerFactoryContext()).WillRepeatedly(ReturnRef(factory_context));

  LocalInfo::MockLocalInfo local_info;
  EXPECT_CALL(factory_context, localInfo()).WillRepeatedly(ReturnRef(local_info));
  std::shared_ptr<const Network::MockResolvedAddress> instance =
      std::make_shared<Network::MockResolvedAddress>("logical", "physical");
  EXPECT_CALL(local_info, address()).WillRepeatedly(Return(instance));
  EXPECT_CALL(*instance, type()).WillRepeatedly(Return(Network::Address::Type::Ip));

  Network::MockIp* ip = new Network::MockIp();
  EXPECT_CALL(*instance, ip()).WillRepeatedly(testing::Return(ip));

  std::string address("1.2.3.4");
  EXPECT_CALL(*ip, addressAsString()).WillRepeatedly(ReturnRef(address));
  EXPECT_CALL(*ip, port()).WillRepeatedly(Return(0));
  ConfigImpl::RocketmqProxyConfig proxyConfig;
  ConfigImpl configImpl(proxyConfig, context);

  EXPECT_STREQ("1.2.3.4:10000", configImpl.proxyAddress().c_str());
  delete ip;
}

TEST_F(RocketmqFilterConfigTest, ProxyAddressWithNonIpType) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  Server::Configuration::MockServerFactoryContext factory_context;
  EXPECT_CALL(context, getServerFactoryContext()).WillRepeatedly(ReturnRef(factory_context));

  LocalInfo::MockLocalInfo local_info;
  EXPECT_CALL(factory_context, localInfo()).WillRepeatedly(ReturnRef(local_info));
  std::shared_ptr<const Network::MockResolvedAddress> instance =
      std::make_shared<Network::MockResolvedAddress>("logical", "physical");
  EXPECT_CALL(local_info, address()).WillRepeatedly(Return(instance));
  EXPECT_CALL(*instance, type()).WillRepeatedly(Return(Network::Address::Type::Pipe));

  Network::MockIp* ip = new Network::MockIp();
  EXPECT_CALL(*instance, ip()).WillRepeatedly(testing::Return(ip));

  std::string address("1.2.3.4");
  EXPECT_CALL(*ip, addressAsString()).WillRepeatedly(ReturnRef(address));
  EXPECT_CALL(*ip, port()).WillRepeatedly(Return(0));
  ConfigImpl::RocketmqProxyConfig proxyConfig;
  ConfigImpl configImpl(proxyConfig, context);

  EXPECT_STREQ("physical", configImpl.proxyAddress().c_str());
  delete ip;
}

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy