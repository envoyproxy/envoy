#include "source/common/ssl/matching/inputs.h"

#include "test/common/ssl/matching/test_data.h"

namespace Envoy {
namespace Ssl {
namespace Matching {

using testing::Return;
using testing::ReturnRef;

TEST(Authentication, UriSanInput) {
  UriSanInput<TestMatchingData> input;
  TestMatchingData data;

  {
    std::vector<std::string> uri_sans;
    EXPECT_CALL(*data.ssl_, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    std::vector<std::string> uri_sans{"foo"};
    EXPECT_CALL(*data.ssl_, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "foo");
  }

  {
    std::vector<std::string> uri_sans{"foo", "bar"};
    EXPECT_CALL(*data.ssl_, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "foo,bar");
  }

  {
    data.ssl_.reset();

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

TEST(Authentication, DnsSanInput) {
  DnsSanInput<TestMatchingData> input;
  TestMatchingData data;

  {
    std::vector<std::string> dns_sans;
    EXPECT_CALL(*data.ssl_, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    std::vector<std::string> dns_sans{"foo"};
    EXPECT_CALL(*data.ssl_, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "foo");
  }

  {
    std::vector<std::string> dns_sans{"foo", "bar"};
    EXPECT_CALL(*data.ssl_, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "foo,bar");
  }

  {
    data.ssl_.reset();

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

TEST(Authentication, SubjectInput) {
  SubjectInput<TestMatchingData> input;
  TestMatchingData data;
  std::string subject;
  EXPECT_CALL(*data.ssl_, subjectPeerCertificate()).WillRepeatedly(ReturnRef(subject));

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

  {
    data.ssl_.reset();

    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

} // namespace Matching
} // namespace Ssl
} // namespace Envoy
