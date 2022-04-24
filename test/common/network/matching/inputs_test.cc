#include "envoy/http/filter.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/matching/data_impl.h"
#include "source/common/network/matching/inputs.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"

namespace Envoy {
namespace Network {
namespace Matching {

using testing::Const;
using testing::Return;
using testing::ReturnRef;

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
    EXPECT_EQ(result.data_, "127.0.0.1");
  }

  {
    socket.connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
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
    EXPECT_EQ(result.data_, "127.0.0.1");
  }
  {
    DestinationPortInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "8080");
  }
  {
    SourceIPInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "10.0.0.1");
  }
  {
    SourcePortInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "9090");
  }
  {
    DirectSourceIPInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "127.0.0.2");
  }
  {
    ServerNameInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, host);
  }

  connection_info_provider.setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8081));
  {
    SourceTypeInput<Http::HttpMatchingData> input;
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "local");
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
    EXPECT_EQ(result.data_, "8080");
  }

  {
    socket.connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
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
    EXPECT_EQ(result.data_, "127.0.0.1");
  }

  {
    socket.connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
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
    EXPECT_EQ(result.data_, "8080");
  }

  {
    socket.connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
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
    EXPECT_EQ(result.data_, "127.0.0.1");
  }

  {
    socket.connection_info_provider_->setDirectRemoteAddressForTest(
        std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
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
    EXPECT_EQ(result.data_, "local");
  }

  {
    socket.connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
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
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    const auto host = "example.com";
    socket.connection_info_provider_->setRequestedServerName(host);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, host);
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
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    const auto protocol = "tls";
    EXPECT_CALL(socket, detectedTransportProtocol).WillOnce(testing::Return(protocol));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, protocol);
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
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    std::vector<std::string> protocols = {"h2c"};
    EXPECT_CALL(socket, requestedApplicationProtocols).WillOnce(testing::ReturnRef(protocols));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "'h2c'");
  }

  {
    std::vector<std::string> protocols = {"h2", "http/1.1"};
    EXPECT_CALL(socket, requestedApplicationProtocols).WillOnce(testing::ReturnRef(protocols));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "'h2','http/1.1'");
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
    EXPECT_EQ(result.data_, "127.0.0.1");
  }

  {
    UdpMatchingDataImpl data(pipe, ip);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
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
    EXPECT_EQ(result.data_, "8080");
  }

  {
    UdpMatchingDataImpl data(pipe, ip);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
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
    EXPECT_EQ(result.data_, "127.0.0.1");
  }

  {
    UdpMatchingDataImpl data(ip, pipe);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
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
    EXPECT_EQ(result.data_, "8080");
  }

  {
    UdpMatchingDataImpl data(ip, pipe);
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

TEST(AuthenticatedInput, UriSan) {
  AuthenticatedInput<Envoy::Network::MockConnection> input(
      envoy::extensions::matching::common_inputs::network::v3::AuthenticatedInput::URI_SAN);

  {
    Envoy::Network::MockConnection conn;
    EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(nullptr));

    const auto result = input.get(conn);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    Envoy::Network::MockConnection conn;
    auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
    std::vector<std::string> uri_sans;
    EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));
    EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

    const auto result = input.get(conn);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    Envoy::Network::MockConnection conn;
    auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
    std::vector<std::string> uri_sans{"bar"};
    EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));
    EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

    const auto result = input.get(conn);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "bar");
  }

  {
    Envoy::Network::MockConnection conn;
    auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
    std::vector<std::string> uri_sans{"foo", "baz"};
    EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));
    EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

    const auto result = input.get(conn);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "foo,baz");
  }
}

TEST(AuthenticatedInput, DnsSan) {
  AuthenticatedInput<Envoy::Network::MockConnection> input(
      envoy::extensions::matching::common_inputs::network::v3::AuthenticatedInput::DNS_SAN);

  {
    Envoy::Network::MockConnection conn;
    EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(nullptr));

    const auto result = input.get(conn);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    Envoy::Network::MockConnection conn;
    auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
    std::vector<std::string> dns_sans;
    EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));
    EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

    const auto result = input.get(conn);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    Envoy::Network::MockConnection conn;
    auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
    std::vector<std::string> dns_sans{"bar"};
    EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));
    EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

    const auto result = input.get(conn);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "bar");
  }

  {
    Envoy::Network::MockConnection conn;
    auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
    std::vector<std::string> dns_sans{"foo", "baz"};
    EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));
    EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

    const auto result = input.get(conn);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "foo,baz");
  }
}

TEST(AuthenticatedInput, Subject) {
  AuthenticatedInput<Envoy::Network::MockConnection> input(
      envoy::extensions::matching::common_inputs::network::v3::AuthenticatedInput::SUBJECT);

  {
    Envoy::Network::MockConnection conn;
    EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(nullptr));

    const auto result = input.get(conn);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    Envoy::Network::MockConnection conn;
    auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
    std::string subject;
    EXPECT_CALL(*ssl, subjectPeerCertificate()).WillRepeatedly(ReturnRef(subject));
    EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

    const auto result = input.get(conn);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    Envoy::Network::MockConnection conn;
    auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
    std::string subject;
    EXPECT_CALL(*ssl, subjectPeerCertificate()).WillRepeatedly(ReturnRef(subject));
    EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

    subject = "subject";
    const auto result = input.get(conn);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "subject");
  }
}

} // namespace Matching
} // namespace Network
} // namespace Envoy
