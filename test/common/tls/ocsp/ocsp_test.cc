#include "source/common/filesystem/filesystem_impl.h"
#include "source/common/tls/ocsp/ocsp.h"
#include "source/common/tls/utility.h"

#include "test/common/tls/ssl_test_utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace Ocsp {

namespace {

namespace CertUtility = Envoy::Extensions::TransportSockets::Tls::Utility;

class OcspFullResponseParsingTest : public testing::Test {
public:
  std::string fullPath(std::string filename) {
    return TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/ocsp/test_data/" +
                                       filename);
  }

  std::vector<uint8_t> readFile(std::string filename) {
    auto str = TestEnvironment::readFileToStringForTest(fullPath(filename));
    return {str.begin(), str.end()};
  }

  void setup(std::string response_filename) {
    auto der_response = readFile(response_filename);
    response_ = OcspResponseWrapperImpl::create(der_response, time_system_).value();
    EXPECT_EQ(response_->rawBytes(), der_response);
  }

  void expectSuccessful() {
    EXPECT_EQ(OcspResponseStatus::Successful, response_->getResponseStatus());
  }

  void expectCertificateMatches(std::string cert_filename) {
    auto cert_ = readCertFromFile(fullPath(cert_filename));
    EXPECT_TRUE(response_->matchesCertificate(*cert_));
  }

protected:
  Event::SimulatedTimeSystem time_system_;
  std::unique_ptr<OcspResponseWrapperImpl> response_;
};

TEST_F(OcspFullResponseParsingTest, GoodCertTest) {
  setup("good_ocsp_resp.der");
  expectSuccessful();
  expectCertificateMatches("good_cert.pem");

  auto cert = readCertFromFile(fullPath("revoked_cert.pem"));
  EXPECT_FALSE(response_->matchesCertificate(*cert));

  // Contains nextUpdate that is in the future
  EXPECT_FALSE(response_->isExpired());
  EXPECT_GT(response_->secondsUntilExpiration(), 0);
}

TEST_F(OcspFullResponseParsingTest, RevokedCertTest) {
  setup("revoked_ocsp_resp.der");
  expectSuccessful();
  expectCertificateMatches("revoked_cert.pem");
  EXPECT_TRUE(response_->isExpired());
  EXPECT_EQ(response_->secondsUntilExpiration(), 0);
}

TEST_F(OcspFullResponseParsingTest, UnknownCertTest) {
  setup("unknown_ocsp_resp.der");
  expectSuccessful();
  expectCertificateMatches("good_cert.pem");
  EXPECT_TRUE(response_->isExpired());
}

TEST_F(OcspFullResponseParsingTest, ExpiredResponseTest) {
  auto ten_years_forward = time_system_.systemTime() + std::chrono::hours(24 * 365 * 10);
  time_system_.setSystemTime(ten_years_forward);
  setup("good_ocsp_resp.der");
  // nextUpdate is present but in the past
  EXPECT_TRUE(response_->isExpired());
  EXPECT_EQ(response_->secondsUntilExpiration(), 0);
}

TEST_F(OcspFullResponseParsingTest, ThisUpdateAfterNowTest) {
  auto past_time = TestUtility::parseTime("2000 01 01", "%Y %m %d");
  time_system_.setSystemTime(absl::ToChronoTime(past_time));
  EXPECT_LOG_CONTAINS("warning", "OCSP Response thisUpdate field is set in the future",
                      setup("good_ocsp_resp.der"));
}

TEST_F(OcspFullResponseParsingTest, ResponderIdKeyHashTest) {
  setup("responder_key_hash_ocsp_resp.der");
  expectSuccessful();
  expectCertificateMatches("good_cert.pem");
  EXPECT_TRUE(response_->isExpired());
}

TEST_F(OcspFullResponseParsingTest, MultiCertResponseTest) {
  auto resp_bytes = readFile("multiple_cert_ocsp_resp.der");
  EXPECT_EQ(OcspResponseWrapperImpl::create(resp_bytes, time_system_).status().message(),
            "OCSP Response must be for one certificate only");
}

TEST_F(OcspFullResponseParsingTest, UnsuccessfulResponseTest) {
  std::vector<uint8_t> data = {
      // SEQUENCE
      0x30, 3,
      // OcspResponseStatus - InternalError
      0xau, 1, 2,
      // no response bytes
  };
  EXPECT_EQ(OcspResponseWrapperImpl::create(data, time_system_).status().message(),
            "OCSP response was unsuccessful");
}

TEST_F(OcspFullResponseParsingTest, NoResponseBodyTest) {
  std::vector<uint8_t> data = {
      // SEQUENCE
      0x30, 3,
      // OcspResponseStatus - Success
      0xau, 1, 0,
      // no response bytes
  };
  EXPECT_EQ(OcspResponseWrapperImpl::create(data, time_system_).status().message(),
            "OCSP response has no body");
}

TEST_F(OcspFullResponseParsingTest, OnlyOneResponseInByteStringTest) {
  auto resp_bytes = readFile("good_ocsp_resp.der");
  auto resp2_bytes = readFile("revoked_ocsp_resp.der");
  resp_bytes.insert(resp_bytes.end(), resp2_bytes.begin(), resp2_bytes.end());

  EXPECT_EQ(OcspResponseWrapperImpl::create(resp_bytes, time_system_).status().message(),
            "Data contained more than a single OCSP response");
}

TEST_F(OcspFullResponseParsingTest, ParseOcspResponseWrongTagTest) {
  auto resp_bytes = readFile("good_ocsp_resp.der");
  // Change the SEQUENCE tag to an `OCTETSTRING` tag
  resp_bytes[0] = 0x4u;
  EXPECT_EQ(OcspResponseWrapperImpl::create(resp_bytes, time_system_).status().message(),
            "OCSP Response is not a well-formed ASN.1 SEQUENCE");
}

class Asn1OcspUtilityTest : public testing::Test {
public:
  void expectResponseStatus(uint8_t code, OcspResponseStatus expected) {
    std::vector<uint8_t> asn1_enum = {0xau, 1, code};
    CBS cbs;
    CBS_init(&cbs, asn1_enum.data(), asn1_enum.size());

    EXPECT_EQ(expected, Asn1OcspUtility::parseResponseStatus(cbs).value());
  }

  void expectThrowOnWrongTag(std::function<void(CBS&)> parse) {
    CBS cbs;
    CBS_init(&cbs, asn1_true.data(), asn1_true.size());
    EXPECT_THROW(parse(cbs), EnvoyException);
  }

  template <class T> void expectFailOnWrongTag(std::function<absl::StatusOr<T>(CBS&)> parse) {
    CBS cbs;
    CBS_init(&cbs, asn1_true.data(), asn1_true.size());
    EXPECT_FALSE(parse(cbs).status().ok());
  }

  const std::vector<uint8_t> asn1_true = {0x1u, 1, 0xff};
};

TEST_F(Asn1OcspUtilityTest, ParseResponseStatusTest) {
  expectResponseStatus(0, OcspResponseStatus::Successful);
  expectResponseStatus(1, OcspResponseStatus::MalformedRequest);
  expectResponseStatus(2, OcspResponseStatus::InternalError);
  expectResponseStatus(3, OcspResponseStatus::TryLater);
  expectResponseStatus(5, OcspResponseStatus::SigRequired);
  expectResponseStatus(6, OcspResponseStatus::Unauthorized);
}

TEST_F(Asn1OcspUtilityTest, ParseMethodWrongTagTest) {
  expectFailOnWrongTag<ResponsePtr>(Asn1OcspUtility::parseResponseBytes);
  expectFailOnWrongTag<std::unique_ptr<BasicOcspResponse>>(Asn1OcspUtility::parseBasicOcspResponse);
  expectFailOnWrongTag<ResponseData>(Asn1OcspUtility::parseResponseData);
  expectFailOnWrongTag<SingleResponse>(Asn1OcspUtility::parseSingleResponse);
  expectFailOnWrongTag<CertId>(Asn1OcspUtility::parseCertId);
  expectFailOnWrongTag<OcspResponseStatus>(Asn1OcspUtility::parseResponseStatus);
}

TEST_F(Asn1OcspUtilityTest, ParseResponseDataUnsupportedVersionTest) {
  // SEQUENCE {
  //   # Invalid version field. The value of v1 is 0, not 1.
  //   [0] { INTEGER { 1 } }
  // }
  // Generated by https://github.com/google/der-ascii
  std::vector<uint8_t> data = {0x30, 0x05, 0xa0, 0x03, 0x02, 0x01, 0x01};
  CBS cbs;
  CBS_init(&cbs, data.data(), data.size());
  EXPECT_EQ(Asn1OcspUtility::parseResponseData(cbs).status().message(),
            "OCSP ResponseData version 0x01 is not supported");
}

TEST_F(Asn1OcspUtilityTest, ParseResponseDataBadResponderIdVariantTest) {
  std::vector<uint8_t> data = {
      // SEQUENCE
      0x30,
      3,
      // Invalid Responder ID tag 3
      3,
      1,
      0,
  };
  CBS cbs;
  CBS_init(&cbs, data.data(), data.size());
  EXPECT_EQ(Asn1OcspUtility::parseResponseData(cbs).status().message(),
            "Unknown choice for Responder ID: 3");
}

TEST_F(Asn1OcspUtilityTest, ParseOcspResponseBytesMissingTest) {
  std::vector<uint8_t> data = {
      // SEQUENCE
      0x30, 3,
      // OcspResponseStatus - InternalError
      0xau, 1, 2,
      // no response bytes
  };
  CBS cbs;
  CBS_init(&cbs, data.data(), data.size());
  auto response = Asn1OcspUtility::parseOcspResponse(cbs).value();
  EXPECT_EQ(response->status_, OcspResponseStatus::InternalError);
  EXPECT_TRUE(response->response_ == nullptr);
}

TEST_F(Asn1OcspUtilityTest, ParseResponseStatusUnknownVariantTest) {
  std::vector<uint8_t> bad_enum_variant = {0xau, 1, 4};
  CBS cbs;
  CBS_init(&cbs, bad_enum_variant.data(), bad_enum_variant.size());
  EXPECT_EQ(Asn1OcspUtility::parseResponseStatus(cbs).status().message(),
            "Unknown OCSP Response Status variant: 4");
}

TEST_F(Asn1OcspUtilityTest, ParseResponseBytesNoOctetStringTest) {
  std::string oid_str = "1.1.1.1.1.1.1";
  bssl::ScopedCBB cbb;
  CBB seq, oid, obj;
  uint8_t* buf;
  size_t buf_len;

  ASSERT_TRUE(CBB_init(cbb.get(), 0));
  ASSERT_TRUE(CBB_add_asn1(cbb.get(), &seq, CBS_ASN1_SEQUENCE));
  ASSERT_TRUE(CBB_add_asn1(&seq, &oid, CBS_ASN1_OBJECT));
  ASSERT_TRUE(CBB_add_asn1_oid_from_text(&oid, oid_str.c_str(), oid_str.size()));
  // Empty sequence instead of `OCTETSTRING` with the response
  ASSERT_TRUE(CBB_add_asn1(&seq, &obj, CBS_ASN1_SEQUENCE));
  ASSERT_TRUE(CBB_finish(cbb.get(), &buf, &buf_len));

  CBS cbs;
  CBS_init(&cbs, buf, buf_len);
  bssl::UniquePtr<uint8_t> scoped(buf);

  EXPECT_EQ(Asn1OcspUtility::parseResponseBytes(cbs).status().message(),
            "Expected ASN.1 OCTETSTRING for response");
}

TEST_F(Asn1OcspUtilityTest, ParseResponseBytesUnknownResponseTypeTest) {
  std::string oid_str = "1.1.1.1.1.1.1";
  bssl::ScopedCBB cbb;
  CBB seq, oid, obj;
  uint8_t* buf;
  size_t buf_len;

  ASSERT_TRUE(CBB_init(cbb.get(), 0));
  ASSERT_TRUE(CBB_add_asn1(cbb.get(), &seq, CBS_ASN1_SEQUENCE));
  ASSERT_TRUE(CBB_add_asn1(&seq, &oid, CBS_ASN1_OBJECT));
  ASSERT_TRUE(CBB_add_asn1_oid_from_text(&oid, oid_str.c_str(), oid_str.size()));
  ASSERT_TRUE(CBB_add_asn1(&seq, &obj, CBS_ASN1_OCTETSTRING));
  ASSERT_TRUE(CBB_add_bytes(&obj, reinterpret_cast<const uint8_t*>("\x1\x2\x3"), 3));
  ASSERT_TRUE(CBB_finish(cbb.get(), &buf, &buf_len));

  CBS cbs;
  CBS_init(&cbs, buf, buf_len);
  bssl::UniquePtr<uint8_t> scoped(buf);

  EXPECT_EQ(Asn1OcspUtility::parseResponseBytes(cbs).status().message(),
            "Unknown OCSP Response type with OID: 1.1.1.1.1.1.1");
}

} // namespace

} // namespace Ocsp
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
