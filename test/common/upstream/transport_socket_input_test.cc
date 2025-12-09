#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/matching/common_inputs/transport_socket/v3/transport_socket_inputs.pb.h"
#include "envoy/matcher/matcher.h"

#include "source/common/config/metadata.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/matching/common_inputs/transport_socket/config.h"

#include "test/mocks/protobuf/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;
using testing::NiceMock;

namespace Envoy {
namespace Upstream {
namespace {

using Envoy::Matcher::DataInputGetResult;

using Extensions::Matching::CommonInputs::TransportSocket::EndpointMetadataInput;
using Extensions::Matching::CommonInputs::TransportSocket::EndpointMetadataInputFactory;
using Extensions::Matching::CommonInputs::TransportSocket::FilterStateInput;
using Extensions::Matching::CommonInputs::TransportSocket::FilterStateInputFactory;
using Extensions::Matching::CommonInputs::TransportSocket::LocalityMetadataInput;
using Extensions::Matching::CommonInputs::TransportSocket::LocalityMetadataInputFactory;

class TransportSocketInputTest : public testing::Test {};

TEST_F(TransportSocketInputTest, TransportSocketMatchingData_Name) {
  // Test the name() function in TransportSocketMatchingData.
  EXPECT_EQ(TransportSocketMatchingData::name(), "transport_socket");
}

TEST_F(TransportSocketInputTest, EndpointMetadataInput_NoEndpointMetadata) {
  EndpointMetadataInput input("envoy.lb", {"type"});
  TransportSocketMatchingData data(nullptr, nullptr);
  auto result = input.get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
}

TEST_F(TransportSocketInputTest, EndpointMetadataInput_StringAndNonString) {
  // Prepare endpoint metadata with string value.
  envoy::config::core::v3::Metadata endpoint_md;
  auto& val_string = Config::Metadata::mutableMetadataValue(endpoint_md, "envoy.lb", "type");
  val_string.set_string_value("tls");

  EndpointMetadataInput input_string("envoy.lb", std::vector<std::string>{"type"});
  TransportSocketMatchingData data_with_md(&endpoint_md, nullptr);
  auto got_string = input_string.get(data_with_md);
  EXPECT_EQ(got_string.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(got_string.data_));
  EXPECT_EQ(absl::get<std::string>(got_string.data_), "tls");

  // Overwrite to a non-string (number) and expect JSON conversion.
  auto& val_number = Config::Metadata::mutableMetadataValue(endpoint_md, "envoy.lb", "type");
  val_number.set_number_value(123);

  auto got_number = input_string.get(data_with_md);
  EXPECT_EQ(got_number.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(got_number.data_));
  EXPECT_EQ(absl::get<std::string>(got_number.data_), "123");
}

TEST_F(TransportSocketInputTest, EndpointMetadataInput_EmptyString) {
  // Test when metadata exists but results in empty string.
  envoy::config::core::v3::Metadata endpoint_md;
  auto& val = Config::Metadata::mutableMetadataValue(endpoint_md, "envoy.lb", "type");
  val.set_string_value("");

  EndpointMetadataInput input("envoy.lb", std::vector<std::string>{"type"});
  TransportSocketMatchingData data(&endpoint_md, nullptr);
  auto result = input.get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
}

TEST_F(TransportSocketInputTest, EndpointMetadataInputFactory_WithFilterAndPath) {
  const std::string yaml_string = R"EOF(
    filter: custom.filter
    path:
      - key: region
  )EOF";

  envoy::extensions::matching::common_inputs::transport_socket::v3::EndpointMetadataInput config;
  TestUtility::loadFromYaml(yaml_string, config);

  EndpointMetadataInputFactory factory;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;
  auto factory_cb = factory.createDataInputFactoryCb(config, validation_visitor);
  auto input = factory_cb();

  // Prepare test data.
  envoy::config::core::v3::Metadata endpoint_md;
  auto& val = Config::Metadata::mutableMetadataValue(endpoint_md, "custom.filter", "region");
  val.set_string_value("us-west");

  TransportSocketMatchingData data(&endpoint_md, nullptr);
  auto result = input->get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "us-west");
}

TEST_F(TransportSocketInputTest, EndpointMetadataInputFactory_DefaultFilter) {
  const std::string yaml_string = R"EOF(
    path:
      - key: socket_type
  )EOF";

  envoy::extensions::matching::common_inputs::transport_socket::v3::EndpointMetadataInput config;
  TestUtility::loadFromYaml(yaml_string, config);

  EndpointMetadataInputFactory factory;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;
  auto factory_cb = factory.createDataInputFactoryCb(config, validation_visitor);
  auto input = factory_cb();

  // Prepare test data with default "envoy.lb" filter.
  envoy::config::core::v3::Metadata endpoint_md;
  auto& val = Config::Metadata::mutableMetadataValue(endpoint_md, "envoy.lb", "socket_type");
  val.set_string_value("mtls");

  TransportSocketMatchingData data(&endpoint_md, nullptr);
  auto result = input->get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "mtls");
}

TEST_F(TransportSocketInputTest, LocalityMetadataInput_NoLocalityMetadata) {
  LocalityMetadataInput input("envoy.lb", {"zone"});
  TransportSocketMatchingData data(nullptr, nullptr);
  auto result = input.get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
}

TEST_F(TransportSocketInputTest, LocalityMetadataInput_StringValue) {
  // Prepare locality metadata.
  envoy::config::core::v3::Metadata locality_md;
  auto& val = Config::Metadata::mutableMetadataValue(locality_md, "envoy.lb", "zone");
  val.set_string_value("zone-a");

  LocalityMetadataInput input("envoy.lb", std::vector<std::string>{"zone"});
  TransportSocketMatchingData data(nullptr, &locality_md);
  auto result = input.get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "zone-a");
}

TEST_F(TransportSocketInputTest, LocalityMetadataInput_NonStringValue) {
  // Test non-string (number) value that requires JSON conversion.
  envoy::config::core::v3::Metadata locality_md;
  auto& val = Config::Metadata::mutableMetadataValue(locality_md, "envoy.lb", "priority");
  val.set_number_value(100);

  LocalityMetadataInput input("envoy.lb", std::vector<std::string>{"priority"});
  TransportSocketMatchingData data(nullptr, &locality_md);
  auto result = input.get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "100");
}

TEST_F(TransportSocketInputTest, LocalityMetadataInput_EmptyString) {
  // Test when locality metadata exists but results in empty string.
  envoy::config::core::v3::Metadata locality_md;
  auto& val = Config::Metadata::mutableMetadataValue(locality_md, "envoy.lb", "zone");
  val.set_string_value("");

  LocalityMetadataInput input("envoy.lb", std::vector<std::string>{"zone"});
  TransportSocketMatchingData data(nullptr, &locality_md);
  auto result = input.get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
}

TEST_F(TransportSocketInputTest, LocalityMetadataInputFactory_WithFilterAndPath) {
  const std::string yaml_string = R"EOF(
    filter: locality.custom
    path:
      - key: environment
  )EOF";

  envoy::extensions::matching::common_inputs::transport_socket::v3::LocalityMetadataInput config;
  TestUtility::loadFromYaml(yaml_string, config);

  LocalityMetadataInputFactory factory;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;
  auto factory_cb = factory.createDataInputFactoryCb(config, validation_visitor);
  auto input = factory_cb();

  // Prepare test data.
  envoy::config::core::v3::Metadata locality_md;
  auto& val = Config::Metadata::mutableMetadataValue(locality_md, "locality.custom", "environment");
  val.set_string_value("production");

  TransportSocketMatchingData data(nullptr, &locality_md);
  auto result = input->get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "production");
}

TEST_F(TransportSocketInputTest, LocalityMetadataInputFactory_DefaultFilter) {
  const std::string yaml_string = R"EOF(
    path:
      - key: tier
  )EOF";

  envoy::extensions::matching::common_inputs::transport_socket::v3::LocalityMetadataInput config;
  TestUtility::loadFromYaml(yaml_string, config);

  LocalityMetadataInputFactory factory;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;
  auto factory_cb = factory.createDataInputFactoryCb(config, validation_visitor);
  auto input = factory_cb();

  // Prepare test data with default "envoy.lb" filter.
  envoy::config::core::v3::Metadata locality_md;
  auto& val = Config::Metadata::mutableMetadataValue(locality_md, "envoy.lb", "tier");
  val.set_string_value("premium");

  TransportSocketMatchingData data(nullptr, &locality_md);
  auto result = input->get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "premium");
}

TEST_F(TransportSocketInputTest, BothEndpointAndLocalityMetadata) {
  // Prepare both endpoint and locality metadata.
  envoy::config::core::v3::Metadata endpoint_md;
  auto& ep_val = Config::Metadata::mutableMetadataValue(endpoint_md, "envoy.lb", "type");
  ep_val.set_string_value("secure");

  envoy::config::core::v3::Metadata locality_md;
  auto& loc_val = Config::Metadata::mutableMetadataValue(locality_md, "envoy.lb", "zone");
  loc_val.set_string_value("zone-1");

  TransportSocketMatchingData data(&endpoint_md, &locality_md);

  // Test endpoint metadata input.
  EndpointMetadataInput ep_input("envoy.lb", {"type"});
  auto ep_result = ep_input.get(data);
  EXPECT_EQ(ep_result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(ep_result.data_));
  EXPECT_EQ(absl::get<std::string>(ep_result.data_), "secure");

  // Test locality metadata input.
  LocalityMetadataInput loc_input("envoy.lb", {"zone"});
  auto loc_result = loc_input.get(data);
  EXPECT_EQ(loc_result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(loc_result.data_));
  EXPECT_EQ(absl::get<std::string>(loc_result.data_), "zone-1");
}

// Simple filter state object for testing.
class TestFilterStateObject : public StreamInfo::FilterState::Object {
public:
  explicit TestFilterStateObject(std::string value) : value_(std::move(value)) {}
  absl::optional<std::string> serializeAsString() const override { return value_; }

private:
  std::string value_;
};

// Filter state object that returns nullopt on serialization.
class NonSerializableFilterStateObject : public StreamInfo::FilterState::Object {
public:
  absl::optional<std::string> serializeAsString() const override { return absl::nullopt; }
};

// Filter state object that returns empty string on serialization.
class EmptySerializableFilterStateObject : public StreamInfo::FilterState::Object {
public:
  absl::optional<std::string> serializeAsString() const override { return ""; }
};

TEST_F(TransportSocketInputTest, FilterStateInput_NoFilterState) {
  FilterStateInput input("test.key");
  TransportSocketMatchingData data(nullptr, nullptr, nullptr);
  auto result = input.get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
}

TEST_F(TransportSocketInputTest, FilterStateInput_WithValue) {
  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  filter_state->setData(
      "envoy.network.namespace", std::make_shared<TestFilterStateObject>("namespace-1"),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Connection,
      StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);

  FilterStateInput input("envoy.network.namespace");
  TransportSocketMatchingData data(nullptr, nullptr, filter_state.get());
  auto result = input.get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "namespace-1");
}

TEST_F(TransportSocketInputTest, FilterStateInput_MissingKey) {
  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  filter_state->setData("some.other.key", std::make_shared<TestFilterStateObject>("value"),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::Connection,
                        StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);

  FilterStateInput input("envoy.network.namespace");
  TransportSocketMatchingData data(nullptr, nullptr, filter_state.get());
  auto result = input.get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
}

TEST_F(TransportSocketInputTest, FilterStateInput_NonSerializable) {
  // Test when filter state object returns nullopt from serializeAsString().
  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  filter_state->setData("test.key", std::make_shared<NonSerializableFilterStateObject>(),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::Connection,
                        StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);

  FilterStateInput input("test.key");
  TransportSocketMatchingData data(nullptr, nullptr, filter_state.get());
  auto result = input.get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
}

TEST_F(TransportSocketInputTest, FilterStateInput_EmptyString) {
  // Test when filter state object returns empty string from serializeAsString().
  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  filter_state->setData("test.key", std::make_shared<EmptySerializableFilterStateObject>(),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::Connection,
                        StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);

  FilterStateInput input("test.key");
  TransportSocketMatchingData data(nullptr, nullptr, filter_state.get());
  auto result = input.get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
}

TEST_F(TransportSocketInputTest, FilterStateInputFactory) {
  const std::string yaml_string = R"EOF(
    key: "envoy.network.namespace"
  )EOF";

  envoy::extensions::matching::common_inputs::transport_socket::v3::FilterStateInput config;
  TestUtility::loadFromYaml(yaml_string, config);

  FilterStateInputFactory factory;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;
  auto factory_cb = factory.createDataInputFactoryCb(config, validation_visitor);
  auto input = factory_cb();

  // Test with filter state.
  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  filter_state->setData(
      "envoy.network.namespace", std::make_shared<TestFilterStateObject>("test-namespace"),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Connection,
      StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);

  TransportSocketMatchingData data(nullptr, nullptr, filter_state.get());
  auto result = input->get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "test-namespace");
}

} // namespace
} // namespace Upstream
} // namespace Envoy
