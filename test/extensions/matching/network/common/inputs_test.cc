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

} // namespace Matching
} // namespace Network
} // namespace Envoy
