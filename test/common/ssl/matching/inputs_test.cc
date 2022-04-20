#include "envoy/extensions/matching/common_inputs/ssl/v3/ssl_inputs.pb.h"

#include "source/common/ssl/matching/inputs.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"

namespace Envoy {
namespace Ssl {
namespace Matching {

using testing::Const;
using testing::Return;
using testing::ReturnRef;

TEST(AuthenticatedInput, UriSan) {
  AuthenticatedInput<Envoy::Network::MockConnection> uri_san_input(
      envoy::extensions::matching::common_inputs::ssl::v3::AuthenticatedInput::UriSan);

  {
    Envoy::Network::MockConnection conn;
    auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
    std::vector<std::string> uri_sans;
    EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));
    EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

    const auto result = uri_san_input.get(conn);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    Envoy::Network::MockConnection conn;
    auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
    std::vector<std::string> uri_sans{"foo", "baz"};
    EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));
    EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

    const auto result = uri_san_input.get(conn);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "foo,baz");
  }
}

TEST(AuthenticatedInput, DnsSan) {
  AuthenticatedInput<Envoy::Network::MockConnection> dns_san_input(
      envoy::extensions::matching::common_inputs::ssl::v3::AuthenticatedInput::DnsSan);

  {
    Envoy::Network::MockConnection conn;
    auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
    std::vector<std::string> dns_sans;
    EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));
    EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

    const auto result = dns_san_input.get(conn);
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

    const auto result = dns_san_input.get(conn);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "bar");
  }
}

TEST(AuthenticatedInput, Subject) {
  AuthenticatedInput<Envoy::Network::MockConnection> subject_input(
      envoy::extensions::matching::common_inputs::ssl::v3::AuthenticatedInput::Subject);

  Envoy::Network::MockConnection conn;
  auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
  std::string subject;
  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillRepeatedly(ReturnRef(subject));
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

  {
    const auto result = subject_input.get(conn);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    subject = "subject";
    const auto result = subject_input.get(conn);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "subject");
  }
}

} // namespace Matching
} // namespace Ssl
} // namespace Envoy
