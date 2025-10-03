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

TEST_F(TransportSocketInputTest, DestinationIPInput) {
  DestinationIPInput input;

  // No local address.
  TransportSocketMatchingData data(nullptr, nullptr);
  EXPECT_EQ(input.get(data).data_availability_, DataInputGetResult::DataAvailability::NotAvailable);

  // With local IPv4 address.
  auto local_address = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 80);
  TransportSocketMatchingData data_with_address(nullptr, nullptr, local_address.get(), nullptr);
  auto result = input.get(data_with_address);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "1.2.3.4");

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
  auto local_address = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 8080);
  TransportSocketMatchingData data_with_address(nullptr, nullptr, local_address.get(), nullptr);
  auto result = input.get(data_with_address);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "8080");

  // With local IPv6 address.
  auto local_address_v6 = std::make_shared<Network::Address::Ipv6Instance>("::1", 443);
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
  auto remote_address = std::make_shared<Network::Address::Ipv4Instance>("192.168.1.1", 1234);
  TransportSocketMatchingData data_with_address(nullptr, nullptr, nullptr, remote_address.get());
  auto result = input.get(data_with_address);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "192.168.1.1");

  // With remote IPv6 address.
  auto remote_address_v6 = std::make_shared<Network::Address::Ipv6Instance>("2001:db8::1", 5678);
  TransportSocketMatchingData data_with_v6(nullptr, nullptr, nullptr, remote_address_v6.get());
  auto result_v6 = input.get(data_with_v6);
  EXPECT_EQ(result_v6.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result_v6.data_));
  EXPECT_EQ(absl::get<std::string>(result_v6.data_), "2001:db8::1");

  // With non-IP address.
  auto pipe_address_or_error = Network::Address::PipeInstance::create("/tmp/test.sock");
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
  auto remote_address = std::make_shared<Network::Address::Ipv4Instance>("192.168.1.1", 1234);
  TransportSocketMatchingData data_with_address(nullptr, nullptr, nullptr, remote_address.get());
  auto result = input.get(data_with_address);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "1234");

  // With remote IPv6 address.
  auto remote_address_v6 = std::make_shared<Network::Address::Ipv6Instance>("::1", 5678);
  TransportSocketMatchingData data_with_v6(nullptr, nullptr, nullptr, remote_address_v6.get());
  auto result_v6 = input.get(data_with_v6);
  EXPECT_EQ(result_v6.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result_v6.data_));
  EXPECT_EQ(absl::get<std::string>(result_v6.data_), "5678");

  // With non-IP address.
  auto pipe_address_or_error = Network::Address::PipeInstance::create("/tmp/test.sock");
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

  // With SNI.
  TransportSocketMatchingData data_with_sni(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                            "foo.com");
  auto result = input.get(data_with_sni);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "foo.com");

  // With SNI through connection info provider.
  NiceMock<Network::MockConnectionSocket> connection_socket;
  // Set the requested server name on the connection info provider
  connection_socket.connectionInfoProvider().setRequestedServerName("bar.com");
  TransportSocketMatchingData data_with_conn_info(nullptr, nullptr, nullptr, nullptr,
                                                  &connection_socket.connectionInfoProvider());
  auto result_conn = input.get(data_with_conn_info);
  EXPECT_EQ(result_conn.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result_conn.data_));
  EXPECT_EQ(absl::get<std::string>(result_conn.data_), "bar.com");
}

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
  envoy::type::metadata::v3::MetadataKey key;
  key.set_key("my.filter");
  auto* seg1 = key.add_path();
  seg1->set_key("foo");
  auto* seg2 = key.add_path();
  seg2->set_key("bar");

  auto& visitor = ProtobufMessage::getNullValidationVisitor();
  auto cb = factory.createDataInputFactoryCb(key, visitor);
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
  // Empty key/path should default to filter "envoy.lb" and path ["type"].
  EndpointMetadataInputFactory factory;
  envoy::type::metadata::v3::MetadataKey key; // empty
  auto& visitor = ProtobufMessage::getNullValidationVisitor();
  auto cb = factory.createDataInputFactoryCb(key, visitor);
  auto input = cb();

  envoy::config::core::v3::Metadata endpoint_md;
  auto& v = Config::Metadata::mutableMetadataValue(endpoint_md, "envoy.lb", "type");
  v.set_string_value("foo");

  TransportSocketMatchingData data_with_md(&endpoint_md, nullptr);
  auto result = input->get(data_with_md);
  EXPECT_EQ(result.data_availability_, DataInputGetResult::DataAvailability::AllDataAvailable);
  ASSERT_TRUE(absl::holds_alternative<std::string>(result.data_));
  EXPECT_EQ(absl::get<std::string>(result.data_), "foo");
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
  // The factory ignores config and uses default filter with empty path.
  envoy::config::core::v3::SocketAddress dummy_config; // any message type accepted.
  auto& visitor = ProtobufMessage::getNullValidationVisitor();
  auto cb = factory.createDataInputFactoryCb(dummy_config, visitor);
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
  EXPECT_NE(dynamic_cast<envoy::type::metadata::v3::MetadataKey*>(endpoint_empty.get()), nullptr);

  LocalityMetadataInputFactory locality_factory;
  auto locality_empty = locality_factory.createEmptyConfigProto();
  EXPECT_NE(locality_empty, nullptr);
  EXPECT_NE(dynamic_cast<envoy::config::core::v3::SocketAddress*>(locality_empty.get()), nullptr);

  // Network input factories.
  DestinationIPInputFactory dip;
  auto dip_empty = dip.createEmptyConfigProto();
  EXPECT_NE(dip_empty, nullptr);
  EXPECT_NE(
      dynamic_cast<envoy::extensions::matching::common_inputs::network::v3::DestinationIPInput*>(
          dip_empty.get()),
      nullptr);

  SourceIPInputFactory sip;
  auto sip_empty = sip.createEmptyConfigProto();
  EXPECT_NE(sip_empty, nullptr);
  EXPECT_NE(dynamic_cast<envoy::extensions::matching::common_inputs::network::v3::SourceIPInput*>(
                sip_empty.get()),
            nullptr);

  DestinationPortInputFactory dport;
  auto dport_empty = dport.createEmptyConfigProto();
  EXPECT_NE(dport_empty, nullptr);
  EXPECT_NE(
      dynamic_cast<envoy::extensions::matching::common_inputs::network::v3::DestinationPortInput*>(
          dport_empty.get()),
      nullptr);

  SourcePortInputFactory sport;
  auto sport_empty = sport.createEmptyConfigProto();
  EXPECT_NE(sport_empty, nullptr);
  EXPECT_NE(dynamic_cast<envoy::extensions::matching::common_inputs::network::v3::SourcePortInput*>(
                sport_empty.get()),
            nullptr);

  ServerNameInputFactory sni;
  auto sni_empty = sni.createEmptyConfigProto();
  EXPECT_NE(sni_empty, nullptr);
  EXPECT_NE(dynamic_cast<envoy::extensions::matching::common_inputs::network::v3::ServerNameInput*>(
                sni_empty.get()),
            nullptr);

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
