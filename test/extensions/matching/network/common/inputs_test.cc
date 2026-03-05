#include <cstdint>

#include "envoy/http/filter.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/matching/data_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/matching/network/application_protocol/config.h"
#include "source/extensions/matching/network/common/inputs.h"

#include "test/mocks/network/mocks.h"

namespace Envoy {
namespace Network {
namespace Matching {

TEST(MatchingData, DestinationIPInput) {
  DestinationIPInput<MatchingData> input;
  MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);

  {
    socket.connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "127.0.0.1");
  }

  {
    socket.connection_info_provider_->setLocalAddress(
        *Network::Address::PipeInstance::create("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

TEST(MatchingData, HttpDestinationIPInput) {
  auto connection_info_provider = std::make_shared<Network::ConnectionInfoSetterImpl>(
      std::make_shared<Address::Ipv4Instance>("127.0.0.1", 8080),
      std::make_shared<Address::Ipv4Instance>("10.0.0.1", 9090));
  connection_info_provider->setDirectRemoteAddressForTest(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.2", 8081));
  auto host = "example.com";
  connection_info_provider->setRequestedServerName(host);
  StreamInfo::StreamInfoImpl stream_info(
      Http::Protocol::Http2, Event::GlobalTimeSystem().timeSystem(), connection_info_provider,
      StreamInfo::FilterState::LifeSpan::FilterChain);
  Http::Matching::HttpMatchingDataImpl data(stream_info);
  {
    DestinationIPInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "127.0.0.1");
  }
  {
    DestinationPortInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "8080");
  }
  {
    SourceIPInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "10.0.0.1");
  }
  {
    SourcePortInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "9090");
  }
  {
    DirectSourceIPInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "127.0.0.2");
  }
  {
    ServerNameInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), host);
  }

  connection_info_provider->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8081));
  {
    SourceTypeInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "local");
  }
}

TEST(MatchingData, DestinationPortInput) {
  DestinationPortInput<MatchingData> input;
  MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);

  {
    socket.connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "8080");
  }

  {
    socket.connection_info_provider_->setLocalAddress(
        *Network::Address::PipeInstance::create("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

TEST(MatchingData, SourceIPInput) {
  SourceIPInput<MatchingData> input;
  MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);

  {
    socket.connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "127.0.0.1");
  }

  {
    socket.connection_info_provider_->setRemoteAddress(
        *Network::Address::PipeInstance::create("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

TEST(MatchingData, SourcePortInput) {
  SourcePortInput<MatchingData> input;
  MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);

  {
    socket.connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "8080");
  }

  {
    socket.connection_info_provider_->setRemoteAddress(
        *Network::Address::PipeInstance::create("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

TEST(MatchingData, DirectSourceIPInput) {
  DirectSourceIPInput<MatchingData> input;
  MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);

  {
    socket.connection_info_provider_->setDirectRemoteAddressForTest(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "127.0.0.1");
  }

  {
    socket.connection_info_provider_->setDirectRemoteAddressForTest(
        *Network::Address::PipeInstance::create("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

TEST(MatchingData, SourceTypeInput) {
  SourceTypeInput<MatchingData> input;
  MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);

  {
    socket.connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "local");
  }

  {
    socket.connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

TEST(MatchingData, ServerNameInput) {
  ServerNameInput<MatchingData> input;
  MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);

  {
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }

  {
    const auto host = "example.com";
    socket.connection_info_provider_->setRequestedServerName(host);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), host);
  }
}

TEST(MatchingData, TransportProtocolInput) {
  TransportProtocolInput input;
  MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);

  {
    EXPECT_CALL(socket, detectedTransportProtocol).WillOnce(testing::Return(""));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }

  {
    const auto protocol = "tls";
    EXPECT_CALL(socket, detectedTransportProtocol).WillOnce(testing::Return(protocol));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), protocol);
  }
}

TEST(MatchingData, ApplicationProtocolInput) {
  ApplicationProtocolInput input;
  MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);

  {
    std::vector<std::string> protocols = {};
    EXPECT_CALL(socket, requestedApplicationProtocols).WillOnce(testing::ReturnRef(protocols));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }

  {
    std::vector<std::string> protocols = {"h2c"};
    EXPECT_CALL(socket, requestedApplicationProtocols).WillOnce(testing::ReturnRef(protocols));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "'h2c'");
  }

  {
    std::vector<std::string> protocols = {"h2", "http/1.1"};
    EXPECT_CALL(socket, requestedApplicationProtocols).WillOnce(testing::ReturnRef(protocols));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "'h2','http/1.1'");
  }
}

TEST(MatchingData, FilterStateInput) {
  std::string key = "filter_state_key";
  FilterStateInput<MatchingData> input(key);

  MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);

  {
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }

  filter_state.setData("unknown_key", std::make_shared<Router::StringAccessorImpl>("some_value"),
                       StreamInfo::FilterState::StateType::Mutable,
                       StreamInfo::FilterState::LifeSpan::Connection);

  {
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }

  std::string value = "filter_state_value";
  filter_state.setData(key, std::make_shared<Router::StringAccessorImpl>(value),
                       StreamInfo::FilterState::StateType::Mutable,
                       StreamInfo::FilterState::LifeSpan::Connection);

  {
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), value);
  }
}

// Helper filter state object that supports field access for testing FilterStateInput with field.
class TestFieldFilterStateObject : public StreamInfo::FilterState::Object {
public:
  TestFieldFilterStateObject(const absl::flat_hash_map<std::string, std::string>& fields)
      : fields_(fields) {}

  bool hasFieldSupport() const override { return true; }

  FieldType getField(absl::string_view field_name) const override {
    auto it = fields_.find(std::string(field_name));
    if (it != fields_.end()) {
      return absl::string_view(it->second);
    }
    return absl::monostate{};
  }

  absl::optional<std::string> serializeAsString() const override {
    return "serialized_whole_object";
  }

private:
  absl::flat_hash_map<std::string, std::string> fields_;
};

TEST(MatchingData, FilterStateInputWithField) {
  std::string key = "composite_state";
  std::string field = "my_field";
  FilterStateInput<MatchingData> input(key, field);

  MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);

  // No filter state object set — should return monostate.
  {
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }

  // Set a filter state object with field support.
  filter_state.setData(
      key,
      std::make_shared<TestFieldFilterStateObject>(absl::flat_hash_map<std::string, std::string>{
          {"my_field", "field_value"}, {"other_field", "other_value"}}),
      StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection);

  // Should return the specific field value, not the serialized whole object.
  {
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "field_value");
  }

  // Access a different field via a different input instance.
  {
    FilterStateInput<MatchingData> other_input(key, "other_field");
    const auto result = other_input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "other_value");
  }

  // Access a non-existent field — should return monostate.
  {
    FilterStateInput<MatchingData> missing_input(key, "nonexistent");
    const auto result = missing_input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

TEST(MatchingData, FilterStateInputWithFieldFallbackToSerialize) {
  // When field is specified but the object does NOT support field access,
  // it should fall back to serializeAsString().
  std::string key = "string_state";
  std::string field = "some_field";
  FilterStateInput<MatchingData> input(key, field);

  MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);

  // StringAccessorImpl does NOT support field access.
  filter_state.setData(key, std::make_shared<Router::StringAccessorImpl>("plain_value"),
                       StreamInfo::FilterState::StateType::Mutable,
                       StreamInfo::FilterState::LifeSpan::Connection);

  {
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    // Falls back to serializeAsString() since object doesn't support field access.
    EXPECT_EQ(absl::get<std::string>(result.data_), "plain_value");
  }
}

TEST(MatchingData, FilterStateInputWithoutFieldUsesSerialize) {
  // When no field is specified, should always use serializeAsString() even if object
  // supports field access.
  std::string key = "composite_state";
  FilterStateInput<MatchingData> input(key); // No field.

  MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);

  filter_state.setData(
      key,
      std::make_shared<TestFieldFilterStateObject>(
          absl::flat_hash_map<std::string, std::string>{{"my_field", "field_value"}}),
      StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection);

  {
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    // Should return serialized whole object, not a field value.
    EXPECT_EQ(absl::get<std::string>(result.data_), "serialized_whole_object");
  }
}

TEST(UdpMatchingData, UdpDestinationIPInput) {
  DestinationIPInput<UdpMatchingData> input;
  const Address::Ipv4Instance ip("127.0.0.1", 8080);
  const auto pipe = *Address::PipeInstance::create("/pipe/path");

  {
    UdpMatchingDataImpl data(ip, ip);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "127.0.0.1");
  }

  {
    UdpMatchingDataImpl data(*pipe, ip);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

TEST(UdpMatchingData, UdpDestinationPortInput) {
  DestinationPortInput<UdpMatchingData> input;
  const Address::Ipv4Instance ip("127.0.0.1", 8080);
  const auto pipe = *Address::PipeInstance::create("/pipe/path");

  {
    UdpMatchingDataImpl data(ip, ip);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "8080");
  }

  {
    UdpMatchingDataImpl data(*pipe, ip);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

TEST(UdpMatchingData, UdpSourceIPInput) {
  SourceIPInput<UdpMatchingData> input;
  const Address::Ipv4Instance ip("127.0.0.1", 8080);
  const auto pipe = *Address::PipeInstance::create("/pipe/path");

  {
    UdpMatchingDataImpl data(ip, ip);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "127.0.0.1");
  }

  {
    UdpMatchingDataImpl data(ip, *pipe);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

TEST(UdpMatchingData, UdpSourcePortInput) {
  SourcePortInput<UdpMatchingData> input;
  const Address::Ipv4Instance ip("127.0.0.1", 8080);
  const auto pipe = *Address::PipeInstance::create("/pipe/path");

  {
    UdpMatchingDataImpl data(ip, ip);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "8080");
  }

  {
    UdpMatchingDataImpl data(ip, *pipe);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

TEST(MatchingData, NetworkNamespaceInput) {
  NetworkNamespaceInput<MatchingData> input;
  MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);

  // Test with no network namespace (default case).
  {
    socket.connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }

  // Test with network namespace.
  {
    socket.connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>(
            "127.0.0.1", 8080, nullptr, absl::make_optional(std::string("/var/run/netns/ns1"))));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "/var/run/netns/ns1");
  }

  // Test with empty network namespace.
  {
    socket.connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080, nullptr,
                                                         absl::make_optional(std::string(""))));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }

  // Test with IPv6 address and network namespace.
  {
    socket.connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv6Instance>(
            "::1", 8080, nullptr, true, absl::make_optional(std::string("/var/run/netns/ns2"))));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "/var/run/netns/ns2");
  }

  // Test with pipe address. This should return monostate since pipes don't have network namespaces.
  {
    socket.connection_info_provider_->setLocalAddress(
        *Network::Address::PipeInstance::create("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

TEST(MatchingData, HttpNetworkNamespaceInput) {
  auto connection_info_provider = std::make_shared<Network::ConnectionInfoSetterImpl>(
      std::make_shared<Address::Ipv4Instance>(
          "127.0.0.1", 8080, nullptr, absl::make_optional(std::string("/var/run/netns/http_ns"))),
      std::make_shared<Address::Ipv4Instance>("10.0.0.1", 9090));

  StreamInfo::StreamInfoImpl stream_info(
      Http::Protocol::Http2, Event::GlobalTimeSystem().timeSystem(), connection_info_provider,
      StreamInfo::FilterState::LifeSpan::FilterChain);
  Http::Matching::HttpMatchingDataImpl data(stream_info);

  NetworkNamespaceInput<Http::HttpMatchingData> input;
  const auto result = input.get(data);
  EXPECT_EQ(result.data_availability_,
            Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
  EXPECT_EQ(absl::get<std::string>(result.data_), "/var/run/netns/http_ns");
}

TEST(UdpMatchingData, UdpNetworkNamespaceInput) {
  NetworkNamespaceInput<UdpMatchingData> input;

  // Test with network namespace.
  {
    const Address::Ipv4Instance local_ip("127.0.0.1", 8080, nullptr,
                                         absl::make_optional(std::string("/var/run/netns/udp_ns")));
    const Address::Ipv4Instance remote_ip("10.0.0.1", 9090);
    UdpMatchingDataImpl data(local_ip, remote_ip);

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "/var/run/netns/udp_ns");
  }

  // Test without network namespace.
  {
    const Address::Ipv4Instance local_ip("127.0.0.1", 8080);
    const Address::Ipv4Instance remote_ip("10.0.0.1", 9090);
    UdpMatchingDataImpl data(local_ip, remote_ip);

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

} // namespace Matching
} // namespace Network
} // namespace Envoy
