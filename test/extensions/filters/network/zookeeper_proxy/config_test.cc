#include "envoy/extensions/filters/network/zookeeper_proxy/v3/zookeeper_proxy.pb.h"
#include "envoy/extensions/filters/network/zookeeper_proxy/v3/zookeeper_proxy.pb.validate.h"

#include "source/extensions/filters/network/zookeeper_proxy/config.h"
#include "source/extensions/filters/network/zookeeper_proxy/filter.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

using ZooKeeperProxyProtoConfig =
    envoy::extensions::filters::network::zookeeper_proxy::v3::ZooKeeperProxy;

class ZookeeperFilterConfigTest : public testing::Test {
public:
  std::string populateFullConfig(const ProtobufWkt::EnumDescriptor* opcode_descriptor) {
    std::string yaml = R"EOF(
stat_prefix: test_prefix
max_packet_bytes: 1048576
enable_per_opcode_request_bytes_metrics: true
enable_per_opcode_response_bytes_metrics: true
enable_per_opcode_decoder_error_metrics: true
enable_latency_threshold_metrics: true
default_latency_threshold: "0.1s"
latency_threshold_overrides:)EOF";

    for (int i = 0; i < opcode_descriptor->value_count(); i++) {
      const auto* opcode_tuple = opcode_descriptor->value(i);
      std::string opcode = opcode_tuple->name();
      int threshold_delta = opcode_tuple->number();
      std::string threshold = fmt::format("0.{}s", 150 + threshold_delta);
      yaml += fmt::format(R"EOF(
  - opcode: {}
    threshold: {})EOF",
                          opcode, threshold);
    };

    return yaml;
  }

  ZooKeeperProxyProtoConfig proto_config_;
  testing::NiceMock<Server::Configuration::MockFactoryContext> context_;
  ZooKeeperConfigFactory factory_;
  Network::MockConnection connection_;
};

TEST_F(ZookeeperFilterConfigTest, ValidateFail) {
  EXPECT_THROW_WITH_REGEX(
      ZooKeeperConfigFactory()
          .createFilterFactoryFromProto(ZooKeeperProxyProtoConfig(), context_)
          .IgnoreError(),
      ProtoValidationException,
      "Proto constraint validation failed \\(ZooKeeperProxyValidationError.StatPrefix: value "
      "length must be at least 1 characters\\)");
}

TEST_F(ZookeeperFilterConfigTest, InvalidStatPrefix) {
  const std::string yaml = R"EOF(
stat_prefix: ""
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      TestUtility::loadFromYamlAndValidate(yaml, proto_config_), ProtoValidationException,
      "Proto constraint validation failed \\(ZooKeeperProxyValidationError.StatPrefix: value "
      "length must be at least 1 characters\\)");
}

TEST_F(ZookeeperFilterConfigTest, InvalidMaxPacketBytes) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
max_packet_bytes: -1
  )EOF";

  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYamlAndValidate(yaml, proto_config_), EnvoyException,
                          "max_packet_bytes");
}

TEST_F(ZookeeperFilterConfigTest, UndefinedOpcode) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
latency_threshold_overrides:
  - opcode: Undefined
    threshold:
      nanos: 150000000
  )EOF";

  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYamlAndValidate(yaml, proto_config_), EnvoyException,
                          "Undefined");
}

TEST_F(ZookeeperFilterConfigTest, NegativeLatencyThreshold) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
latency_threshold_overrides:
  - opcode: Connect
    threshold:
      nanos: -150000000
  )EOF";

  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYamlAndValidate(yaml, proto_config_), EnvoyException,
                          "Invalid duration: Expected positive duration");
}

TEST_F(ZookeeperFilterConfigTest, TooSmallLatencyThreshold) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
latency_threshold_overrides:
  - opcode: Multi
    threshold:
      nanos: 999999
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      TestUtility::loadFromYamlAndValidate(yaml, proto_config_), EnvoyException,
      "Proto constraint validation failed "
      "\\(ZooKeeperProxyValidationError.LatencyThresholdOverrides\\[0\\]: embedded message failed "
      "validation \\| caused by LatencyThresholdOverrideValidationError.Threshold: value must be "
      "greater than or equal to 1ms\\)");
}

TEST_F(ZookeeperFilterConfigTest, UnsetLatencyThreshold) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
latency_threshold_overrides:
  - opcode: Create
    threshold:
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      TestUtility::loadFromYamlAndValidate(yaml, proto_config_), EnvoyException,
      "Proto constraint validation failed "
      "\\(ZooKeeperProxyValidationError.LatencyThresholdOverrides\\[0\\]: embedded message failed "
      "validation \\| caused by LatencyThresholdOverrideValidationError.Threshold: value is "
      "required\\)");
}

TEST_F(ZookeeperFilterConfigTest, DuplicatedOpcodes) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
max_packet_bytes: 1048576
latency_threshold_overrides:
  - opcode: Sync
    threshold:
      nanos: 150000000
  - opcode: Sync
    threshold:
      nanos: 151000000
  - opcode: Create
    threshold:
      nanos: 152000000
  - opcode: Create
    threshold:
      nanos: 153000000
  - opcode: Create
    threshold:
      nanos: 154000000
  - opcode: Create
    threshold:
      nanos: 155000000
  )EOF";

  TestUtility::loadFromYamlAndValidate(yaml, proto_config_);
  EXPECT_THROW_WITH_REGEX(
      ZooKeeperConfigFactory().createFilterFactoryFromProto(proto_config_, context_).IgnoreError(),
      EnvoyException, "Duplicated opcode find in config");
}

TEST_F(ZookeeperFilterConfigTest, SimpleConfig) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
  )EOF";

  TestUtility::loadFromYamlAndValidate(yaml, proto_config_);
  EXPECT_EQ(proto_config_.stat_prefix(), "test_prefix");
  EXPECT_EQ(proto_config_.max_packet_bytes().value(), 0);
  EXPECT_EQ(proto_config_.enable_per_opcode_request_bytes_metrics(), false);
  EXPECT_EQ(proto_config_.enable_per_opcode_response_bytes_metrics(), false);
  EXPECT_EQ(proto_config_.enable_per_opcode_decoder_error_metrics(), false);
  EXPECT_EQ(proto_config_.enable_latency_threshold_metrics(), false);
  EXPECT_EQ(proto_config_.default_latency_threshold(),
            ProtobufWkt::util::TimeUtil::SecondsToDuration(0));
  EXPECT_EQ(proto_config_.latency_threshold_overrides_size(), 0);

  Network::FilterFactoryCb cb =
      factory_.createFilterFactoryFromProto(proto_config_, context_).value();
  EXPECT_CALL(connection_, addFilter(_));
  cb(connection_);
}

TEST_F(ZookeeperFilterConfigTest, ConfigWithDefaultLatencyThreshold) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
default_latency_threshold: "0.15s"
  )EOF";

  TestUtility::loadFromYamlAndValidate(yaml, proto_config_);
  EXPECT_EQ(proto_config_.stat_prefix(), "test_prefix");
  EXPECT_EQ(proto_config_.max_packet_bytes().value(), 0);
  EXPECT_EQ(proto_config_.enable_per_opcode_request_bytes_metrics(), false);
  EXPECT_EQ(proto_config_.enable_per_opcode_response_bytes_metrics(), false);
  EXPECT_EQ(proto_config_.enable_per_opcode_decoder_error_metrics(), false);
  EXPECT_EQ(proto_config_.enable_latency_threshold_metrics(), false);
  EXPECT_EQ(proto_config_.default_latency_threshold(),
            ProtobufWkt::util::TimeUtil::MillisecondsToDuration(150));
  EXPECT_EQ(proto_config_.latency_threshold_overrides_size(), 0);

  Network::FilterFactoryCb cb =
      factory_.createFilterFactoryFromProto(proto_config_, context_).value();
  EXPECT_CALL(connection_, addFilter(_));
  cb(connection_);
}

TEST_F(ZookeeperFilterConfigTest, ConfigWithConnectLatencyThreshold) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
latency_threshold_overrides:
  - opcode: Connect
    threshold: "0.151s"
  )EOF";

  TestUtility::loadFromYamlAndValidate(yaml, proto_config_);
  EXPECT_EQ(proto_config_.stat_prefix(), "test_prefix");
  EXPECT_EQ(proto_config_.max_packet_bytes().value(), 0);
  EXPECT_EQ(proto_config_.enable_per_opcode_request_bytes_metrics(), false);
  EXPECT_EQ(proto_config_.enable_per_opcode_response_bytes_metrics(), false);
  EXPECT_EQ(proto_config_.enable_per_opcode_decoder_error_metrics(), false);
  EXPECT_EQ(proto_config_.enable_latency_threshold_metrics(), false);
  EXPECT_EQ(proto_config_.default_latency_threshold(),
            ProtobufWkt::util::TimeUtil::SecondsToDuration(0));
  EXPECT_EQ(proto_config_.latency_threshold_overrides_size(), 1);
  LatencyThresholdOverride threshold_override = proto_config_.latency_threshold_overrides().at(0);
  EXPECT_EQ(threshold_override.opcode(), LatencyThresholdOverride::Connect);
  EXPECT_EQ(threshold_override.threshold(),
            ProtobufWkt::util::TimeUtil::MillisecondsToDuration(151));

  Network::FilterFactoryCb cb =
      factory_.createFilterFactoryFromProto(proto_config_, context_).value();
  EXPECT_CALL(connection_, addFilter(_));
  cb(connection_);
}

TEST_F(ZookeeperFilterConfigTest, FullConfig) {
  const ProtobufWkt::EnumDescriptor* opcode_descriptor = envoy::extensions::filters::network::
      zookeeper_proxy::v3::LatencyThresholdOverride_Opcode_descriptor();
  std::string yaml = populateFullConfig(opcode_descriptor);
  TestUtility::loadFromYamlAndValidate(yaml, proto_config_);

  EXPECT_EQ(proto_config_.stat_prefix(), "test_prefix");
  EXPECT_EQ(proto_config_.max_packet_bytes().value(), 1048576);
  EXPECT_EQ(proto_config_.enable_per_opcode_request_bytes_metrics(), true);
  EXPECT_EQ(proto_config_.enable_per_opcode_response_bytes_metrics(), true);
  EXPECT_EQ(proto_config_.enable_per_opcode_decoder_error_metrics(), true);
  EXPECT_EQ(proto_config_.enable_latency_threshold_metrics(), true);
  EXPECT_EQ(proto_config_.default_latency_threshold(),
            ProtobufWkt::util::TimeUtil::MillisecondsToDuration(100));
  EXPECT_EQ(proto_config_.latency_threshold_overrides_size(), 27);

  for (int i = 0; i < opcode_descriptor->value_count(); i++) {
    LatencyThresholdOverride threshold_override = proto_config_.latency_threshold_overrides().at(i);
    const auto* opcode_tuple = opcode_descriptor->value(i);
    std::string opcode_name = envoy::extensions::filters::network::zookeeper_proxy::v3::
        LatencyThresholdOverride_Opcode_Name(threshold_override.opcode());
    EXPECT_EQ(opcode_name, opcode_tuple->name());
    uint64_t threshold_delta = static_cast<uint64_t>(opcode_tuple->number());
    EXPECT_EQ(threshold_override.threshold(),
              ProtobufWkt::util::TimeUtil::MillisecondsToDuration(150 + threshold_delta));
  }

  Network::FilterFactoryCb cb =
      factory_.createFilterFactoryFromProto(proto_config_, context_).value();
  EXPECT_CALL(connection_, addFilter(_));
  cb(connection_);
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
