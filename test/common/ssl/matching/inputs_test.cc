#include "source/common/http/matching/data_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_impl.h"
#include "source/common/ssl/matching/inputs.h"

#include "test/mocks/ssl/mocks.h"

namespace Envoy {
namespace Ssl {
namespace Matching {

using testing::Return;
using testing::ReturnRef;

TEST(Authentication, UriSanInput) {
  UriSanInput<Http::HttpMatchingData> input;
  Network::ConnectionInfoSetterImpl connection_info_provider(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));
  Http::Matching::HttpMatchingDataImpl data(connection_info_provider);

  {
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::NotAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  std::shared_ptr<Ssl::MockConnectionInfo> ssl = std::make_shared<Ssl::MockConnectionInfo>();
  connection_info_provider.setSslConnection(ssl);

  {
    std::vector<std::string> uri_sans;
    EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    std::vector<std::string> uri_sans{"foo"};
    EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "foo");
  }

  {
    std::vector<std::string> uri_sans{"foo", "bar"};
    EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "foo,bar");
  }
}

TEST(Authentication, DnsSanInput) {
  DnsSanInput<Http::HttpMatchingData> input;
  Network::ConnectionInfoSetterImpl connection_info_provider(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));
  Http::Matching::HttpMatchingDataImpl data(connection_info_provider);

  {
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::NotAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  std::shared_ptr<Ssl::MockConnectionInfo> ssl = std::make_shared<Ssl::MockConnectionInfo>();
  connection_info_provider.setSslConnection(ssl);

  {
    std::vector<std::string> dns_sans;
    EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    std::vector<std::string> dns_sans{"foo"};
    EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "foo");
  }

  {
    std::vector<std::string> dns_sans{"foo", "bar"};
    EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "foo,bar");
  }
}

TEST(Authentication, SubjectInput) {
  SubjectInput<Http::HttpMatchingData> input;
  Network::ConnectionInfoSetterImpl connection_info_provider(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));
  Http::Matching::HttpMatchingDataImpl data(connection_info_provider);

  {
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::NotAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  std::shared_ptr<Ssl::MockConnectionInfo> ssl = std::make_shared<Ssl::MockConnectionInfo>();
  connection_info_provider.setSslConnection(ssl);
  std::string subject;
  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillRepeatedly(ReturnRef(subject));

  {
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    subject = "foo";
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "foo");
  }
}

} // namespace Matching
} // namespace Ssl
} // namespace Envoy
