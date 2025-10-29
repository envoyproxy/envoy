#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/matching/common_inputs/transport_socket/v3/transport_socket_inputs.pb.h"
#include "envoy/matcher/matcher.h"

#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/matching/common_inputs/transport_socket/config.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Upstream {
namespace {

using Envoy::Matcher::DataInputGetResult;

using Extensions::Matching::CommonInputs::TransportSocket::ApplicationProtocolInput;
using Extensions::Matching::CommonInputs::TransportSocket::ApplicationProtocolInputFactory;
using Extensions::Matching::CommonInputs::TransportSocket::DestinationIPInput;
using Extensions::Matching::CommonInputs::TransportSocket::DestinationIPInputFactory;
using Extensions::Matching::CommonInputs::TransportSocket::DestinationPortInput;
using Extensions::Matching::CommonInputs::TransportSocket::DestinationPortInputFactory;
using Extensions::Matching::CommonInputs::TransportSocket::EndpointMetadataInput;
using Extensions::Matching::CommonInputs::TransportSocket::EndpointMetadataInputFactory;
using Extensions::Matching::CommonInputs::TransportSocket::LocalityMetadataInput;
using Extensions::Matching::CommonInputs::TransportSocket::LocalityMetadataInputFactory;
using Extensions::Matching::CommonInputs::TransportSocket::ServerNameInput;
using Extensions::Matching::CommonInputs::TransportSocket::ServerNameInputFactory;
using Extensions::Matching::CommonInputs::TransportSocket::SourceIPInput;
using Extensions::Matching::CommonInputs::TransportSocket::SourceIPInputFactory;
using Extensions::Matching::CommonInputs::TransportSocket::SourcePortInput;
using Extensions::Matching::CommonInputs::TransportSocket::SourcePortInputFactory;

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

TEST_F(TransportSocketInputTest, LocalityMetadataInputFactory_WithPath) {
  LocalityMetadataInputFactory factory;
  envoy::extensions::matching::common_inputs::transport_socket::v3::LocalityMetadataInput config;
  config.set_filter("my.locality.filter");
  auto* seg1 = config.add_path();
  seg1->set_key("region");
  auto* seg2 = config.add_path();
  seg2->set_key("zone");

  auto& visitor = ProtobufMessage::getNullValidationVisitor();
  auto cb = factory.createDataInputFactoryCb(config, visitor);
  auto input = cb();

  // Populate nested struct at my.locality.filter: { region: { zone: "us-west1-a" } }.
  envoy::config::core::v3::Metadata locality_md;
  auto& filter_struct = (*locality_md.mutable_filter_metadata())["my.locality.filter"];
  auto& region_value = (*filter_struct.mutable_fields())["region"];
  auto* region_struct = region_value.mutable_struct_value();
  (*region_struct->mutable_fields())["zone"].set_string_value("us-west1-a");

  TransportSocketMatchingData data_with_md(nullptr, &locality_md);
  auto result = input->get(data_with_md);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "us-west1-a");
}

TEST_F(TransportSocketInputTest, MetadataValue_FloatConversion) {
  // Test that float values are properly converted to strings.
  envoy::config::core::v3::Metadata endpoint_md;
  auto& val_float = Config::Metadata::mutableMetadataValue(endpoint_md, "envoy.lb", "weight");
  val_float.set_number_value(123.456);

  EndpointMetadataInput input("envoy.lb", std::vector<std::string>{"weight"});
  TransportSocketMatchingData data(&endpoint_md, nullptr);
  auto result = input.get(data);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  // Float values should be converted to string representation.
  std::string value = absl::get<std::string>(result.data_);
  EXPECT_THAT(value, testing::HasSubstr("123.45"));
}

TEST_F(TransportSocketInputTest, DestinationIPInput) {
  DestinationIPInput input;

  // No local address.
  TransportSocketMatchingData data(nullptr, nullptr);
  EXPECT_EQ(input.get(data).data_availability_, DataInputGetResult::DataAvailability::NotAvailable);

  // With local IPv4 address.
  auto local_address = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1", 80);
  TransportSocketMatchingData data_with_address(nullptr, nullptr, local_address.get(), nullptr);
  auto result = input.get(data_with_address);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "10.0.0.1");

  // With local IPv6 address.
  auto local_address_v6 = std::make_shared<Network::Address::Ipv6Instance>("::1", 443);
  TransportSocketMatchingData data_with_v6(nullptr, nullptr, local_address_v6.get(), nullptr);
  auto result_v6 = input.get(data_with_v6);
  EXPECT_EQ(result_v6.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result_v6.data_));
  EXPECT_EQ(absl::get<std::string>(result_v6.data_), "::1");

  // With non-IP address (pipe).
  auto pipe_address_or_error = Network::Address::PipeInstance::create("/tmp/test.sock");
  ASSERT_TRUE(pipe_address_or_error.ok());
  auto pipe_address = std::move(pipe_address_or_error.value());
  TransportSocketMatchingData data_with_pipe(nullptr, nullptr, pipe_address.get(), nullptr);
  auto result_pipe = input.get(data_with_pipe);
  EXPECT_EQ(result_pipe.data_availability_, DataInputGetResult::DataAvailability::NotAvailable);
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result_pipe.data_));
}

TEST_F(TransportSocketInputTest, DestinationPortInput) {
  DestinationPortInput input;

  // No local address.
  TransportSocketMatchingData data(nullptr, nullptr);
  EXPECT_EQ(input.get(data).data_availability_, DataInputGetResult::DataAvailability::NotAvailable);

  // With local IPv4 address.
  auto local_address = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1", 8080);
  TransportSocketMatchingData data_with_address(nullptr, nullptr, local_address.get(), nullptr);
  auto result = input.get(data_with_address);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "8080");

  // With local IPv6 address and different port.
  auto local_address_v6 = std::make_shared<Network::Address::Ipv6Instance>("2001:db8::1", 443);
  TransportSocketMatchingData data_with_v6(nullptr, nullptr, local_address_v6.get(), nullptr);
  auto result_v6 = input.get(data_with_v6);
  EXPECT_EQ(result_v6.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result_v6.data_));
  EXPECT_EQ(absl::get<std::string>(result_v6.data_), "443");

  // With non-IP address.
  auto pipe_address_or_error = Network::Address::PipeInstance::create("/tmp/test.sock");
  ASSERT_TRUE(pipe_address_or_error.ok());
  auto pipe_address = std::move(pipe_address_or_error.value());
  TransportSocketMatchingData data_with_pipe(nullptr, nullptr, pipe_address.get(), nullptr);
  auto result_pipe = input.get(data_with_pipe);
  EXPECT_EQ(result_pipe.data_availability_, DataInputGetResult::DataAvailability::NotAvailable);
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result_pipe.data_));
}

TEST_F(TransportSocketInputTest, SourceIPInput) {
  SourceIPInput input;

  // No remote address.
  TransportSocketMatchingData data(nullptr, nullptr);
  EXPECT_EQ(input.get(data).data_availability_, DataInputGetResult::DataAvailability::NotAvailable);

  // With remote IPv4 address.
  auto remote_address = std::make_shared<Network::Address::Ipv4Instance>("192.168.1.100", 12345);
  TransportSocketMatchingData data_with_address(nullptr, nullptr, nullptr, remote_address.get());
  auto result = input.get(data_with_address);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "192.168.1.100");

  // With remote IPv6 address.
  auto remote_address_v6 = std::make_shared<Network::Address::Ipv6Instance>("2001:db8::2", 54321);
  TransportSocketMatchingData data_with_v6(nullptr, nullptr, nullptr, remote_address_v6.get());
  auto result_v6 = input.get(data_with_v6);
  EXPECT_EQ(result_v6.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result_v6.data_));
  EXPECT_EQ(absl::get<std::string>(result_v6.data_), "2001:db8::2");

  // With non-IP address.
  auto pipe_address_or_error = Network::Address::PipeInstance::create("/var/run/test.sock");
  ASSERT_TRUE(pipe_address_or_error.ok());
  auto pipe_address = std::move(pipe_address_or_error.value());
  TransportSocketMatchingData data_with_pipe(nullptr, nullptr, nullptr, pipe_address.get());
  auto result_pipe = input.get(data_with_pipe);
  EXPECT_EQ(result_pipe.data_availability_, DataInputGetResult::DataAvailability::NotAvailable);
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result_pipe.data_));
}

TEST_F(TransportSocketInputTest, SourcePortInput) {
  SourcePortInput input;

  // No remote address.
  TransportSocketMatchingData data(nullptr, nullptr);
  EXPECT_EQ(input.get(data).data_availability_, DataInputGetResult::DataAvailability::NotAvailable);

  // With remote IPv4 address.
  auto remote_address = std::make_shared<Network::Address::Ipv4Instance>("192.168.1.100", 33333);
  TransportSocketMatchingData data_with_address(nullptr, nullptr, nullptr, remote_address.get());
  auto result = input.get(data_with_address);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "33333");

  // With remote IPv6 address and different port.
  auto remote_address_v6 = std::make_shared<Network::Address::Ipv6Instance>("fe80::1", 9999);
  TransportSocketMatchingData data_with_v6(nullptr, nullptr, nullptr, remote_address_v6.get());
  auto result_v6 = input.get(data_with_v6);
  EXPECT_EQ(result_v6.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result_v6.data_));
  EXPECT_EQ(absl::get<std::string>(result_v6.data_), "9999");

  // With non-IP address.
  auto pipe_address_or_error = Network::Address::PipeInstance::create("/var/run/test.sock");
  ASSERT_TRUE(pipe_address_or_error.ok());
  auto pipe_address = std::move(pipe_address_or_error.value());
  TransportSocketMatchingData data_with_pipe(nullptr, nullptr, nullptr, pipe_address.get());
  auto result_pipe = input.get(data_with_pipe);
  EXPECT_EQ(result_pipe.data_availability_, DataInputGetResult::DataAvailability::NotAvailable);
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result_pipe.data_));
}

TEST_F(TransportSocketInputTest, ServerNameInput) {
  ServerNameInput input;
  TransportSocketMatchingData data(nullptr, nullptr);

  // No SNI.
  EXPECT_EQ(input.get(data).data_availability_, DataInputGetResult::DataAvailability::NotAvailable);

  // With explicit server name.
  TransportSocketMatchingData data_with_sni(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                            "example.com");
  auto result = input.get(data_with_sni);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "example.com");

  // With SNI through connection info provider.
  NiceMock<Network::MockConnectionSocket> connection_socket;
  connection_socket.connectionInfoProvider().setRequestedServerName("tls.example.com");
  TransportSocketMatchingData data_with_conn_info(nullptr, nullptr, nullptr, nullptr,
                                                  &connection_socket.connectionInfoProvider());
  auto result_conn = input.get(data_with_conn_info);
  EXPECT_EQ(result_conn.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result_conn.data_));
  EXPECT_EQ(absl::get<std::string>(result_conn.data_), "tls.example.com");

  // Explicit server name takes precedence over connection info.
  TransportSocketMatchingData data_with_both(nullptr, nullptr, nullptr, nullptr,
                                             &connection_socket.connectionInfoProvider(), nullptr,
                                             "explicit.example.com");
  auto result_both = input.get(data_with_both);
  EXPECT_EQ(result_both.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result_both.data_));
  EXPECT_EQ(absl::get<std::string>(result_both.data_), "explicit.example.com");
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

  // Network input factories.
  DestinationIPInputFactory dip_factory;
  auto dip_empty = dip_factory.createEmptyConfigProto();
  EXPECT_NE(dip_empty, nullptr);
  EXPECT_NE(
      dynamic_cast<envoy::extensions::matching::common_inputs::network::v3::DestinationIPInput*>(
          dip_empty.get()),
      nullptr);

  SourceIPInputFactory sip_factory;
  auto sip_empty = sip_factory.createEmptyConfigProto();
  EXPECT_NE(sip_empty, nullptr);
  EXPECT_NE(dynamic_cast<envoy::extensions::matching::common_inputs::network::v3::SourceIPInput*>(
                sip_empty.get()),
            nullptr);

  DestinationPortInputFactory dport_factory;
  auto dport_empty = dport_factory.createEmptyConfigProto();
  EXPECT_NE(dport_empty, nullptr);
  EXPECT_NE(
      dynamic_cast<envoy::extensions::matching::common_inputs::network::v3::DestinationPortInput*>(
          dport_empty.get()),
      nullptr);

  SourcePortInputFactory sport_factory;
  auto sport_empty = sport_factory.createEmptyConfigProto();
  EXPECT_NE(sport_empty, nullptr);
  EXPECT_NE(dynamic_cast<envoy::extensions::matching::common_inputs::network::v3::SourcePortInput*>(
                sport_empty.get()),
            nullptr);

  ServerNameInputFactory sni_factory;
  auto sni_empty = sni_factory.createEmptyConfigProto();
  EXPECT_NE(sni_empty, nullptr);
  EXPECT_NE(dynamic_cast<envoy::extensions::matching::common_inputs::network::v3::ServerNameInput*>(
                sni_empty.get()),
            nullptr);
}

TEST_F(TransportSocketInputTest, NetworkInputFactoriesCreateDataInput) {
  auto& visitor = ProtobufMessage::getNullValidationVisitor();

  // DestinationIPInputFactory creates DestinationIPInput.
  DestinationIPInputFactory dip_factory;
  envoy::extensions::matching::common_inputs::network::v3::DestinationIPInput dip_config;
  auto dip_cb = dip_factory.createDataInputFactoryCb(dip_config, visitor);
  auto dip_input = dip_cb();
  EXPECT_NE(dip_input, nullptr);

  // SourceIPInputFactory creates SourceIPInput.
  SourceIPInputFactory sip_factory;
  envoy::extensions::matching::common_inputs::network::v3::SourceIPInput sip_config;
  auto sip_cb = sip_factory.createDataInputFactoryCb(sip_config, visitor);
  auto sip_input = sip_cb();
  EXPECT_NE(sip_input, nullptr);

  // DestinationPortInputFactory creates DestinationPortInput.
  DestinationPortInputFactory dport_factory;
  envoy::extensions::matching::common_inputs::network::v3::DestinationPortInput dport_config;
  auto dport_cb = dport_factory.createDataInputFactoryCb(dport_config, visitor);
  auto dport_input = dport_cb();
  EXPECT_NE(dport_input, nullptr);

  // SourcePortInputFactory creates SourcePortInput.
  SourcePortInputFactory sport_factory;
  envoy::extensions::matching::common_inputs::network::v3::SourcePortInput sport_config;
  auto sport_cb = sport_factory.createDataInputFactoryCb(sport_config, visitor);
  auto sport_input = sport_cb();
  EXPECT_NE(sport_input, nullptr);

  // ServerNameInputFactory creates ServerNameInput.
  ServerNameInputFactory sni_factory;
  envoy::extensions::matching::common_inputs::network::v3::ServerNameInput sni_config;
  auto sni_cb = sni_factory.createDataInputFactoryCb(sni_config, visitor);
  auto sni_input = sni_cb();
  EXPECT_NE(sni_input, nullptr);

  // ApplicationProtocolInputFactory creates ApplicationProtocolInput.
  ApplicationProtocolInputFactory alpn_factory;
  envoy::extensions::matching::common_inputs::network::v3::ApplicationProtocolInput alpn_config;
  auto alpn_cb = alpn_factory.createDataInputFactoryCb(alpn_config, visitor);
  auto alpn_input = alpn_cb();
  EXPECT_NE(alpn_input, nullptr);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
