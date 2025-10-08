#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/type/metadata/v3/metadata.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/common/upstream/transport_socket_input.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Upstream {
namespace {

using Envoy::Matcher::DataInputGetResult;

class TransportSocketInputTest : public testing::Test {};

// Note: Network-level inputs (destination/source IP/port, server name) have
// transport-socket-specific implementations that work with TransportSocketMatchingData. They share
// the same implementation logic as the generic network inputs but are registered with different
// factory names to avoid conflicts.

TEST_F(TransportSocketInputTest, ApplicationProtocolInput) {
  ApplicationProtocolInput input;
  TransportSocketMatchingData data(nullptr, nullptr);

  // No ALPN.
  EXPECT_EQ(input.get(data).data_availability_, DataInputGetResult::DataAvailability::NotAvailable);

  // With ALPN.
  std::vector<std::string> protocols = {"h2c", "http/1.1"};
  TransportSocketMatchingData data_with_alpn(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                             "", &protocols);
  auto result = input.get(data_with_alpn);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "h2c,http/1.1");
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

TEST_F(TransportSocketInputTest, EndpointMetadataInputFactory_WithKeyAndPath) {
  EndpointMetadataInputFactory factory;
  envoy::extensions::matching::common_inputs::transport_socket::v3::EndpointMetadataInput config;
  config.set_filter("my.filter");
  auto* seg1 = config.add_path();
  seg1->set_key("foo");
  auto* seg2 = config.add_path();
  seg2->set_key("bar");

  auto& visitor = ProtobufMessage::getNullValidationVisitor();
  auto cb = factory.createDataInputFactoryCb(config, visitor);
  auto input = cb();

  // Populate nested struct at my.filter: { foo: { bar: "baz" } }.
  envoy::config::core::v3::Metadata endpoint_md;
  auto& filter_struct = (*endpoint_md.mutable_filter_metadata())["my.filter"];
  auto& foo_value = (*filter_struct.mutable_fields())["foo"];
  auto* foo_struct = foo_value.mutable_struct_value();
  (*foo_struct->mutable_fields())["bar"].set_string_value("baz");

  TransportSocketMatchingData data_with_md(&endpoint_md, nullptr);
  auto result = input->get(data_with_md);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "baz");
}

TEST_F(TransportSocketInputTest, EndpointMetadataInputFactory_Defaults) {
  // Empty filter should default to filter "envoy.transport_socket_match".
  // Empty path means we get the entire filter metadata struct as JSON.
  EndpointMetadataInputFactory factory;
  envoy::extensions::matching::common_inputs::transport_socket::v3::EndpointMetadataInput
      config; // empty
  auto& visitor = ProtobufMessage::getNullValidationVisitor();
  auto cb = factory.createDataInputFactoryCb(config, visitor);
  auto input = cb();

  envoy::config::core::v3::Metadata endpoint_md;
  // With empty path, metadataValue returns the entire filter struct.
  // If the filter doesn't exist, it returns a null Value.
  TransportSocketMatchingData data_with_md(&endpoint_md, nullptr);
  auto result = input->get(data_with_md);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  // Empty metadata returns "null" when serialized.
  EXPECT_EQ(absl::get<std::string>(result.data_), "null");
}

TEST_F(TransportSocketInputTest, LocalityMetadataInput_NoLocalityMetadata) {
  LocalityMetadataInput input(Envoy::Config::MetadataFilters::get().ENVOY_TRANSPORT_SOCKET_MATCH,
                              {});
  TransportSocketMatchingData data(nullptr, nullptr);
  auto result = input.get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
}

TEST_F(TransportSocketInputTest, LocalityMetadataInput_StringAndNonString) {
  const std::string filter = Envoy::Config::MetadataFilters::get().ENVOY_TRANSPORT_SOCKET_MATCH;
  envoy::config::core::v3::Metadata locality_md;

  // String value at key "zone".
  auto& vs = Config::Metadata::mutableMetadataValue(locality_md, filter, "zone");
  vs.set_string_value("us-west1");
  LocalityMetadataInput input(filter, std::vector<std::string>{"zone"});
  TransportSocketMatchingData data(nullptr, &locality_md);
  auto got_string = input.get(data);
  EXPECT_EQ(got_string.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(got_string.data_));
  EXPECT_EQ(absl::get<std::string>(got_string.data_), "us-west1");

  // Non-string (bool) value at key "zone" results in JSON conversion.
  auto& vb = Config::Metadata::mutableMetadataValue(locality_md, filter, "zone");
  vb.set_bool_value(true);
  auto got_bool = input.get(data);
  EXPECT_EQ(got_bool.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(got_bool.data_));
  EXPECT_EQ(absl::get<std::string>(got_bool.data_), "true");
}

TEST_F(TransportSocketInputTest, LocalityMetadataInputFactory_Defaults) {
  LocalityMetadataInputFactory factory;
  // Create a LocalityMetadataInput config with default filter.
  envoy::extensions::matching::common_inputs::transport_socket::v3::LocalityMetadataInput config;
  config.set_filter("envoy.transport_socket_match");
  auto& visitor = ProtobufMessage::getNullValidationVisitor();
  auto cb = factory.createDataInputFactoryCb(config, visitor);
  auto input = cb();

  envoy::config::core::v3::Metadata locality_md; // empty metadata.
  TransportSocketMatchingData data(nullptr, &locality_md);
  auto result = input->get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  // When filter metadata is missing, metadataValue returns a null Value which renders as "null".
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "null");
}

TEST_F(TransportSocketInputTest, FactoriesCreateEmptyConfigProto) {
  // Endpoint/locality metadata factories.
  EndpointMetadataInputFactory endpoint_factory;
  auto endpoint_empty = endpoint_factory.createEmptyConfigProto();
  EXPECT_NE(endpoint_empty, nullptr);
  EXPECT_NE(
      dynamic_cast<
          envoy::extensions::matching::common_inputs::transport_socket::v3::EndpointMetadataInput*>(
          endpoint_empty.get()),
      nullptr);

  LocalityMetadataInputFactory locality_factory;
  auto locality_empty = locality_factory.createEmptyConfigProto();
  EXPECT_NE(locality_empty, nullptr);
  EXPECT_NE(
      dynamic_cast<
          envoy::extensions::matching::common_inputs::transport_socket::v3::LocalityMetadataInput*>(
          locality_empty.get()),
      nullptr);

  // Application protocol factory (transport socket specific).
  ApplicationProtocolInputFactory alpn;
  auto alpn_empty = alpn.createEmptyConfigProto();
  EXPECT_NE(alpn_empty, nullptr);
  EXPECT_NE(dynamic_cast<
                envoy::extensions::matching::common_inputs::network::v3::ApplicationProtocolInput*>(
                alpn_empty.get()),
            nullptr);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
