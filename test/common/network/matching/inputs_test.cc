#include "envoy/http/filter.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/matching/data_impl.h"
#include "source/common/network/matching/inputs.h"

#include "test/mocks/network/mocks.h"

namespace Envoy {
namespace Network {
namespace Matching {

using Matcher::InputValue;

TEST(MatchingData, DestinationIPInput) {
  DestinationIPInput<MatchingData> input;
  MockConnectionSocket socket;
  MatchingDataImpl data(socket);

  {
    socket.connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue("127.0.0.1"));
  }

  {
    socket.connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue());
  }
}

TEST(MatchingData, HttpDestinationIPInput) {
  ConnectionInfoSetterImpl connection_info_provider(
      std::make_shared<Address::Ipv4Instance>("127.0.0.1", 8080),
      std::make_shared<Address::Ipv4Instance>("10.0.0.1", 9090));
  connection_info_provider.setDirectRemoteAddressForTest(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.2", 8081));
  auto host = "example.com";
  connection_info_provider.setRequestedServerName(host);
  Http::Matching::HttpMatchingDataImpl data(connection_info_provider);
  {
    DestinationIPInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue("127.0.0.1"));
  }
  {
    DestinationPortInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue(8080));
  }
  {
    SourceIPInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue("10.0.0.1"));
  }
  {
    SourcePortInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue(9090));
  }
  {
    DirectSourceIPInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue("127.0.0.2"));
  }
  {
    ServerNameInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue(host));
  }

  connection_info_provider.setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8081));
  {
    SourceTypeInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue("local"));
  }
}

TEST(MatchingData, DestinationPortInput) {
  DestinationPortInput<MatchingData> input;
  MockConnectionSocket socket;
  MatchingDataImpl data(socket);

  {
    socket.connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue(8080));
  }

  {
    socket.connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue());
  }
}

TEST(MatchingData, SourceIPInput) {
  SourceIPInput<MatchingData> input;
  MockConnectionSocket socket;
  MatchingDataImpl data(socket);

  {
    socket.connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue("127.0.0.1"));
  }

  {
    socket.connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue());
  }
}

TEST(MatchingData, SourcePortInput) {
  SourcePortInput<MatchingData> input;
  MockConnectionSocket socket;
  MatchingDataImpl data(socket);

  {
    socket.connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue(8080));
  }

  {
    socket.connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue());
  }
}

TEST(MatchingData, DirectSourceIPInput) {
  DirectSourceIPInput<MatchingData> input;
  MockConnectionSocket socket;
  MatchingDataImpl data(socket);

  {
    socket.connection_info_provider_->setDirectRemoteAddressForTest(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue("127.0.0.1"));
  }

  {
    socket.connection_info_provider_->setDirectRemoteAddressForTest(
        std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue());
  }
}

TEST(MatchingData, SourceTypeInput) {
  SourceTypeInput<MatchingData> input;
  MockConnectionSocket socket;
  MatchingDataImpl data(socket);

  {
    socket.connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue("local"));
  }

  {
    socket.connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue());
  }
}

TEST(MatchingData, ServerNameInput) {
  ServerNameInput<MatchingData> input;
  MockConnectionSocket socket;
  MatchingDataImpl data(socket);

  {
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue());
  }

  {
    const auto host = "example.com";
    socket.connection_info_provider_->setRequestedServerName(host);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue(host));
  }
}

TEST(MatchingData, TransportProtocolInput) {
  TransportProtocolInput input;
  MockConnectionSocket socket;
  MatchingDataImpl data(socket);

  {
    EXPECT_CALL(socket, detectedTransportProtocol).WillOnce(testing::Return(""));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue());
  }

  {
    const auto protocol = "tls";
    EXPECT_CALL(socket, detectedTransportProtocol).WillOnce(testing::Return(protocol));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue(protocol));
  }
}

TEST(MatchingData, ApplicationProtocolInput) {
  ApplicationProtocolInput input;
  MockConnectionSocket socket;
  MatchingDataImpl data(socket);

  {
    std::vector<std::string> protocols = {};
    EXPECT_CALL(socket, requestedApplicationProtocols).WillOnce(testing::ReturnRef(protocols));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue());
  }

  {
    std::vector<std::string> protocols = {"h2c"};
    EXPECT_CALL(socket, requestedApplicationProtocols).WillOnce(testing::ReturnRef(protocols));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue("'h2c'"));
  }

  {
    std::vector<std::string> protocols = {"h2", "http/1.1"};
    EXPECT_CALL(socket, requestedApplicationProtocols).WillOnce(testing::ReturnRef(protocols));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue("'h2','http/1.1'"));
  }
}

TEST(UdpMatchingData, UdpDestinationIPInput) {
  DestinationIPInput<UdpMatchingData> input;
  const Address::Ipv4Instance ip("127.0.0.1", 8080);
  const Address::PipeInstance pipe("/pipe/path");

  {
    UdpMatchingDataImpl data(ip, ip);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue("127.0.0.1"));
  }

  {
    UdpMatchingDataImpl data(pipe, ip);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue());
  }
}

TEST(UdpMatchingData, UdpDestinationPortInput) {
  DestinationPortInput<UdpMatchingData> input;
  const Address::Ipv4Instance ip("127.0.0.1", 8080);
  const Address::PipeInstance pipe("/pipe/path");

  {
    UdpMatchingDataImpl data(ip, ip);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue(8080));
  }

  {
    UdpMatchingDataImpl data(pipe, ip);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue());
  }
}

TEST(UdpMatchingData, UdpSourceIPInput) {
  SourceIPInput<UdpMatchingData> input;
  const Address::Ipv4Instance ip("127.0.0.1", 8080);
  const Address::PipeInstance pipe("/pipe/path");

  {
    UdpMatchingDataImpl data(ip, ip);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue("127.0.0.1"));
  }

  {
    UdpMatchingDataImpl data(ip, pipe);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue());
  }
}

TEST(UdpMatchingData, UdpSourcePortInput) {
  SourcePortInput<UdpMatchingData> input;
  const Address::Ipv4Instance ip("127.0.0.1", 8080);
  const Address::PipeInstance pipe("/pipe/path");

  {
    UdpMatchingDataImpl data(ip, ip);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue(8080));
  }

  {
    UdpMatchingDataImpl data(ip, pipe);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, InputValue());
  }
}

} // namespace Matching
} // namespace Network
} // namespace Envoy
