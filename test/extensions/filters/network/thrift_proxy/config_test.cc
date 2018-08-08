#include "envoy/config/filter/network/thrift_proxy/v2alpha1/thrift_proxy.pb.validate.h"

#include "extensions/filters/network/thrift_proxy/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

namespace {

std::vector<envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy_TransportType>
getTransportTypes() {
  std::vector<envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy_TransportType> v;
  int transport = envoy::config::filter::network::thrift_proxy::v2alpha1::
      ThriftProxy_TransportType_TransportType_MIN;
  while (transport <= envoy::config::filter::network::thrift_proxy::v2alpha1::
                          ThriftProxy_TransportType_TransportType_MAX) {
    v.push_back(static_cast<
                envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy_TransportType>(
        transport));
    transport++;
  }
  return v;
}

std::vector<envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy_ProtocolType>
getProtocolTypes() {
  std::vector<envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy_ProtocolType> v;
  int protocol = envoy::config::filter::network::thrift_proxy::v2alpha1::
      ThriftProxy_ProtocolType_ProtocolType_MIN;
  while (protocol <= envoy::config::filter::network::thrift_proxy::v2alpha1::
                         ThriftProxy_ProtocolType_ProtocolType_MAX) {
    v.push_back(static_cast<
                envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy_ProtocolType>(
        protocol));
    protocol++;
  }
  return v;
}

} // namespace

class ThriftFilterConfigTestBase {
public:
  void testConfig(envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy& config) {
    Network::FilterFactoryCb cb;
    EXPECT_NO_THROW({ cb = factory_.createFilterFactoryFromProto(config, context_); });

    Network::MockConnection connection;
    EXPECT_CALL(connection, addReadFilter(_));
    cb(connection);
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  ThriftProxyFilterConfigFactory factory_;
};

class ThriftFilterConfigTest : public ThriftFilterConfigTestBase, public testing::Test {};

class ThriftFilterTransportConfigTest
    : public ThriftFilterConfigTestBase,
      public testing::TestWithParam<
          envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy_TransportType> {};

INSTANTIATE_TEST_CASE_P(TransportTypes, ThriftFilterTransportConfigTest,
                        testing::ValuesIn(getTransportTypes()));

class ThriftFilterProtocolConfigTest
    : public ThriftFilterConfigTestBase,
      public testing::TestWithParam<
          envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy_ProtocolType> {};

INSTANTIATE_TEST_CASE_P(ProtocolTypes, ThriftFilterProtocolConfigTest,
                        testing::ValuesIn(getProtocolTypes()));

TEST_F(ThriftFilterConfigTest, ValidateFail) {
  EXPECT_THROW(factory_.createFilterFactoryFromProto(
                   envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy(), context_),
               ProtoValidationException);
}

TEST_F(ThriftFilterConfigTest, ValidProtoConfiguration) {
  envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy config{};
  config.set_stat_prefix("my_stat_prefix");

  testConfig(config);
}

TEST_P(ThriftFilterTransportConfigTest, ValidProtoConfiguration) {
  envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy config{};
  config.set_stat_prefix("my_stat_prefix");
  config.set_transport(GetParam());
  testConfig(config);
}

TEST_P(ThriftFilterProtocolConfigTest, ValidProtoConfiguration) {
  envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy config{};
  config.set_stat_prefix("my_stat_prefix");
  config.set_protocol(GetParam());
  testConfig(config);
}

TEST_F(ThriftFilterConfigTest, ThriftProxyWithEmptyProto) {
  envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy config =
      *dynamic_cast<envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy*>(
          factory_.createEmptyConfigProto().get());
  config.set_stat_prefix("my_stat_prefix");

  testConfig(config);
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
