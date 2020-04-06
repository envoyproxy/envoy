#include "envoy/config/filter/udp/dns_filter/v2alpha/dns_filter.pb.h"
#include "envoy/config/filter/udp/dns_filter/v2alpha/dns_filter.pb.validate.h"

#include "common/common/logger.h"

#include "extensions/filters/udp/dns_filter/dns_filter.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AtLeast;
using testing::InSequence;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace {

class DnsFilterTest : public testing::Test {
public:
  DnsFilterTest()
      : listener_address_(Network::Utility::parseInternetAddressAndPort("127.0.2.1:5353")) {

    Logger::Registry::setLogLevel(spdlog::level::info);

    EXPECT_CALL(callbacks_, udpListener()).Times(AtLeast(0));
  }

  ~DnsFilterTest() override { EXPECT_CALL(callbacks_.udp_listener_, onDestroy()); }

  void setup(const std::string& yaml) {
    envoy::config::filter::udp::dns_filter::v2alpha::DnsFilterConfig config;
    TestUtility::loadFromYamlAndValidate(yaml, config);
    auto store = stats_store_.createScope("dns_scope");
    EXPECT_CALL(listener_factory_, scope()).WillOnce(ReturnRef(*store));

    config_ = std::make_shared<DnsFilterEnvoyConfig>(listener_factory_, config);
    filter_ = std::make_unique<DnsFilter>(callbacks_, config_);
  }

  const Network::Address::InstanceConstSharedPtr listener_address_;
  Server::Configuration::MockListenerFactoryContext listener_factory_;
  DnsFilterEnvoyConfigSharedPtr config_;

  std::unique_ptr<DnsFilter> filter_;
  Network::MockUdpReadFilterCallbacks callbacks_;
  Stats::IsolatedStoreImpl stats_store_;
  Runtime::RandomGeneratorImpl rng_;

  const std::string config_yaml = R"EOF(
stat_prefix: "my_prefix"
server_config:
  inline_dns_table:
    external_retry_count: 3
    virtual_domains:
      - name: "www.foo1.com"
        endpoint:
          address_list:
            address:
              - 10.0.0.1
              - 10.0.0.2
      - name: "www.foo2.com"
        endpoint:
          address_list:
            address:
              - 2001:8a:c1::2800:7
      - name: "www.foo3.com"
        endpoint:
          address_list:
            address:
              - 10.0.3.1
  )EOF";
};

TEST_F(DnsFilterTest, TestConfig) {
  InSequence s;

  setup(config_yaml);
}

} // namespace
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
