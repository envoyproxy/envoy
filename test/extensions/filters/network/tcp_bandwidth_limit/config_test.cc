#include "source/extensions/filters/network/tcp_bandwidth_limit/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpBandwidthLimit {
namespace {

class TcpBandwidthLimitConfigTest : public ::testing::Test {
public:
  void setup(const std::string& yaml) {
    envoy::extensions::filters::network::tcp_bandwidth_limit::v3::TcpBandwidthLimit proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    factory_ = std::make_unique<TcpBandwidthLimitConfigFactory>();

    ON_CALL(context_, scope()).WillByDefault(ReturnRef(*stats_store_.rootScope()));
    ON_CALL(context_, serverFactoryContext()).WillByDefault(ReturnRef(server_context_));
    ON_CALL(server_context_, runtime()).WillByDefault(ReturnRef(runtime_));
    ON_CALL(server_context_, mainThreadDispatcher()).WillByDefault(ReturnRef(dispatcher_));
    ON_CALL(server_context_, timeSource()).WillByDefault(ReturnRef(time_source_));

    auto result = factory_->createFilterFactoryFromProto(proto_config, context_);
    EXPECT_TRUE(result.ok());
    cb_ = result.value();
  }

  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::SimulatedTimeSystem time_source_;
  std::unique_ptr<TcpBandwidthLimitConfigFactory> factory_;
  Network::FilterFactoryCb cb_;
};

TEST_F(TcpBandwidthLimitConfigTest, BasicConfig) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    download_limit_kbps: 1024
    upload_limit_kbps: 512
  )EOF";

  setup(yaml);
  EXPECT_TRUE(cb_ != nullptr);

  // Test filter creation
  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addFilter(_));
  cb_(filter_manager);
}

TEST_F(TcpBandwidthLimitConfigTest, MinimalConfig) {
  const std::string yaml = R"EOF(
    stat_prefix: test
  )EOF";

  setup(yaml);
  EXPECT_TRUE(cb_ != nullptr);

  // Test filter creation
  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addFilter(_));
  cb_(filter_manager);
}

TEST_F(TcpBandwidthLimitConfigTest, ConfigWithFillInterval) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    download_limit_kbps: 100
    fill_interval:
      seconds: 0
      nanos: 100000000
  )EOF";

  setup(yaml);
  EXPECT_TRUE(cb_ != nullptr);

  // Test filter creation
  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addFilter(_));
  cb_(filter_manager);
}

TEST_F(TcpBandwidthLimitConfigTest, ConfigWithRuntimeFlag) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    upload_limit_kbps: 256
    runtime_enabled:
      default_value: false
      runtime_key: bandwidth_limit_enabled
  )EOF";

  setup(yaml);
  EXPECT_TRUE(cb_ != nullptr);

  // Test filter creation
  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addFilter(_));
  cb_(filter_manager);
}

TEST_F(TcpBandwidthLimitConfigTest, ConfigWithZeroLimits) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    download_limit_kbps: 0
    upload_limit_kbps: 0
  )EOF";

  setup(yaml);
  EXPECT_TRUE(cb_ != nullptr);

  // Test filter creation
  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addFilter(_));
  cb_(filter_manager);
}

TEST_F(TcpBandwidthLimitConfigTest, FactoryName) {
  auto factory = TcpBandwidthLimitConfigFactory();
  EXPECT_EQ("envoy.filters.network.tcp_bandwidth_limit", factory.name());
}

TEST_F(TcpBandwidthLimitConfigTest, EmptyProto) {
  envoy::extensions::filters::network::tcp_bandwidth_limit::v3::TcpBandwidthLimit proto_config;
  TcpBandwidthLimitConfigFactory factory;

  // Should throw with empty stat_prefix
  EXPECT_THROW(
      {
        auto result = factory.createFilterFactoryFromProto(proto_config, context_);
        (void)result; // Suppress unused result warning
      },
      EnvoyException);
}

TEST_F(TcpBandwidthLimitConfigTest, InvalidFillInterval) {
  // Test fill interval too small (< 20ms)
  const std::string yaml1 = R"EOF(
    stat_prefix: test
    download_limit_kbps: 100
    fill_interval:
      seconds: 0
      nanos: 10000000
  )EOF";

  envoy::extensions::filters::network::tcp_bandwidth_limit::v3::TcpBandwidthLimit proto_config1;
  TestUtility::loadFromYaml(yaml1, proto_config1);

  TcpBandwidthLimitConfigFactory factory;
  // Should throw when creating config with invalid fill interval
  EXPECT_THROW(
      {
        auto result = factory.createFilterFactoryFromProto(proto_config1, context_);
        (void)result; // Suppress unused result warning
      },
      EnvoyException);

  // Test fill interval too large (> 1s)
  const std::string yaml2 = R"EOF(
    stat_prefix: test
    download_limit_kbps: 100
    fill_interval:
      seconds: 2
      nanos: 0
  )EOF";

  envoy::extensions::filters::network::tcp_bandwidth_limit::v3::TcpBandwidthLimit proto_config2;
  TestUtility::loadFromYaml(yaml2, proto_config2);

  // Should throw when creating config with invalid fill interval
  EXPECT_THROW(
      {
        auto result = factory.createFilterFactoryFromProto(proto_config2, context_);
        (void)result; // Suppress unused result warning
      },
      EnvoyException);
}

} // namespace
} // namespace TcpBandwidthLimit
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
