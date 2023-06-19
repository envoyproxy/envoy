#include "source/common/http/matching/data_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_impl.h"
#include "source/common/ssl/matching/inputs.h"

#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"

namespace Envoy {
namespace Ssl {
namespace Matching {

using testing::Return;
using testing::ReturnRef;

TEST(Authentication, UriSanInput) {
  UriSanInput<Http::HttpMatchingData> input;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::Matching::HttpMatchingDataImpl data(stream_info);

  {
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::NotAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }

  std::shared_ptr<Ssl::MockConnectionInfo> ssl = std::make_shared<Ssl::MockConnectionInfo>();
  stream_info.downstream_connection_info_provider_->setSslConnection(ssl);

  {
    std::vector<std::string> uri_sans;
    EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }

  {
    std::vector<std::string> uri_sans{"foo"};
    EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "foo");
  }

  {
    std::vector<std::string> uri_sans{"foo", "bar"};
    EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "foo,bar");
  }
}

TEST(Authentication, DnsSanInput) {
  DnsSanInput<Http::HttpMatchingData> input;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::Matching::HttpMatchingDataImpl data(stream_info);
  {
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::NotAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }

  std::shared_ptr<Ssl::MockConnectionInfo> ssl = std::make_shared<Ssl::MockConnectionInfo>();
  stream_info.downstream_connection_info_provider_->setSslConnection(ssl);
  {
    std::vector<std::string> dns_sans;
    EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }

  {
    std::vector<std::string> dns_sans{"foo"};
    EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "foo");
  }

  {
    std::vector<std::string> dns_sans{"foo", "bar"};
    EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "foo,bar");
  }
}

TEST(Authentication, SubjectInput) {
  SubjectInput<Http::HttpMatchingData> input;

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::Matching::HttpMatchingDataImpl data(stream_info);

  {
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::NotAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }

  std::shared_ptr<Ssl::MockConnectionInfo> ssl = std::make_shared<Ssl::MockConnectionInfo>();
  stream_info.downstream_connection_info_provider_->setSslConnection(ssl);
  // connection_info_provider->setSslConnection(ssl);
  std::string subject;
  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillRepeatedly(ReturnRef(subject));

  {
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }

  {
    subject = "foo";
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(absl::get<std::string>(result.data_), "foo");
  }
}

} // namespace Matching
} // namespace Ssl
} // namespace Envoy
