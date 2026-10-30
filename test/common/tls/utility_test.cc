#include <openssl/x509.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "envoy/extensions/transport_sockets/tls/v3/common.pb.h"
#include "envoy/ssl/tls_certificate_config.h"

#include "source/common/common/c_smart_ptr.h"
#include "source/common/tls/utility.h"

#include "test/common/tls/ssl_test_utility.h"
#include "test/common/tls/test_data/intermediate_ca_cert_info.h"
#include "test/common/tls/test_data/long_validity_cert_info.h"
#include "test/common/tls/test_data/san_dns_cert_info.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/thread_factory_for_test.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace {

using ::Envoy::StatusHelpers::HasStatusMessage;
using X509StoreContextPtr = CSmartPtr<X509_STORE_CTX, X509_STORE_CTX_free>;
using X509StorePtr = CSmartPtr<X509_STORE, X509_STORE_free>;

TEST(UtilityTest, TestDnsNameMatching) {
  EXPECT_TRUE(Utility::dnsNameMatch("lyft.com", "lyft.com"));
  EXPECT_TRUE(Utility::dnsNameMatch("a.lyft.com", "*.lyft.com"));
  EXPECT_TRUE(Utility::dnsNameMatch("a.LYFT.com", "*.lyft.COM"));
  EXPECT_TRUE(Utility::dnsNameMatch("lyft.com", "*yft.com"));
  EXPECT_TRUE(Utility::dnsNameMatch("LYFT.com", "*yft.com"));
  EXPECT_TRUE(Utility::dnsNameMatch("lyft.com", "*lyft.com"));
  EXPECT_TRUE(Utility::dnsNameMatch("lyft.com", "lyf*.com"));
  EXPECT_TRUE(Utility::dnsNameMatch("lyft.com", "lyft*.com"));
  EXPECT_TRUE(Utility::dnsNameMatch("lyft.com", "l*ft.com"));
  EXPECT_TRUE(Utility::dnsNameMatch("t.lyft.com", "t*.lyft.com"));
  EXPECT_TRUE(Utility::dnsNameMatch("test.lyft.com", "t*.lyft.com"));
  EXPECT_TRUE(Utility::dnsNameMatch("l-lots-of-stuff-ft.com", "l*ft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("t.lyft.com", "t*t.lyft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("lyft.com", "l*ft.co"));
  EXPECT_FALSE(Utility::dnsNameMatch("lyft.com", "ly?t.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("lyft.com", "lf*t.com"));
  EXPECT_FALSE(Utility::dnsNameMatch(".lyft.com", "*lyft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("lyft.com", "**lyft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("lyft.com", "lyft**.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("lyft.com", "ly**ft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("lyft.com", "lyft.c*m"));
  EXPECT_FALSE(Utility::dnsNameMatch("lyft.com", "*yft.c*m"));
  EXPECT_FALSE(Utility::dnsNameMatch("test.lyft.com.extra", "*.lyft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("a.b.lyft.com", "*.lyft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("foo.test.com", "*.lyft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("lyft.com", "*.lyft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("alyft.com", "*.lyft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("", "*lyft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("lyft.com", ""));
}

TEST(UtilityTest, TestOtherNameUniversalWithEmbeddedNull) {
  // Universal strings are utf-32.
  uint32_t utf32_data[] = {
      htonl('t'), htonl('e'), htonl('s'), htonl('t'), 0 /* embedded null */, htonl('s'), htonl('t'),
      htonl('r'), htonl('i'), htonl('n'), htonl('g'),
  };
  ASN1_STRING* asn1_str = ASN1_UNIVERSALSTRING_new();
  ASN1_STRING_set(asn1_str, utf32_data, sizeof(utf32_data));
  GENERAL_NAME* name = GENERAL_NAME_new();
  ASN1_OBJECT* oid = OBJ_txt2obj("1.2.3.4.5", 1);
  ASN1_TYPE* type = ASN1_TYPE_new();
  ASN1_TYPE_set(type, V_ASN1_UNIVERSALSTRING, asn1_str);
  GENERAL_NAME_set0_othername(name, oid, type);

  std::string expected = "test";
  expected += '\0';
  expected += "string";

  EXPECT_EQ(Utility::generalNameAsString(name), expected);

  GENERAL_NAME_free(name);
}

TEST(UtilityTest, TestOtherNameBmpWithEmbeddedNull) {
  // `BMP` strings are utf-16.
  uint16_t utf16_data[] = {
      htons('t'), htons('e'), htons('s'), htons('t'), 0 /* embedded null */, htons('s'), htons('t'),
      htons('r'), htons('i'), htons('n'), htons('g'),
  };
  ASN1_STRING* asn1_str = ASN1_BMPSTRING_new();
  ASN1_STRING_set(asn1_str, utf16_data, sizeof(utf16_data));
  GENERAL_NAME* name = GENERAL_NAME_new();
  ASN1_OBJECT* oid = OBJ_txt2obj("1.2.3.4.5", 1);
  ASN1_TYPE* type = ASN1_TYPE_new();
  ASN1_TYPE_set(type, V_ASN1_BMPSTRING, asn1_str);
  GENERAL_NAME_set0_othername(name, oid, type);

  std::string expected = "test";
  expected += '\0';
  expected += "string";

  EXPECT_EQ(Utility::generalNameAsString(name), expected);

  GENERAL_NAME_free(name);
}

TEST(UtilityTest, TestGetSubjectAlternateNamesWithDNS) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  const auto& subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_DNS);
  EXPECT_EQ(1, subject_alt_names.size());
}

TEST(UtilityTest, TestMultipleGetSubjectAlternateNamesWithDNS) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/san_multiple_dns_cert.pem"));
  const auto& subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_DNS);
  EXPECT_EQ(2, subject_alt_names.size());
}

TEST(UtilityTest, TestGetSubjectAlternateNamesWithUri) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"));
  const auto& subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_URI);
  EXPECT_EQ(1, subject_alt_names.size());
}

TEST(UtilityTest, TestGetSubjectAlternateNamesWithEmail) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/spiffe_san_cert.pem"));
  const auto& subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_EMAIL);
  EXPECT_EQ(1, subject_alt_names.size());
  EXPECT_EQ("envoy@example.com", subject_alt_names.front());
}

TEST(UtilityTest, TestGetSubjectAlternateNamesWithNoSAN) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"));
  const auto& uri_subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_URI);
  EXPECT_EQ(0, uri_subject_alt_names.size());
}

TEST(UtilityTest, TestGetSubject) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  EXPECT_EQ("CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US",
            Utility::getSubjectFromCertificate(*cert));
}

TEST(UtilityTest, TestParseSubject) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  Envoy::Ssl::ParsedX509NamePtr subject = Utility::parseSubjectFromCertificate(*cert);
  EXPECT_EQ("Test Server", subject->commonName_);
  EXPECT_EQ(1, subject->organizationName_.size());
  EXPECT_EQ("Lyft", subject->organizationName_[0]);
}

TEST(UtilityTest, TestGetIssuer) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  EXPECT_EQ("CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US",
            Utility::getIssuerFromCertificate(*cert));
}

TEST(UtilityTest, TestParseIssuer) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  Envoy::Ssl::ParsedX509NamePtr issuer = Utility::parseIssuerFromCertificate(*cert);
  EXPECT_EQ("Test CA", issuer->commonName_);
  EXPECT_EQ(1, issuer->organizationName_.size());
  EXPECT_EQ("Lyft", issuer->organizationName_[0]);
}

TEST(UtilityTest, TestGetSerialNumber) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  EXPECT_EQ(TEST_SAN_DNS_CERT_SERIAL, Utility::getSerialNumberFromCertificate(*cert));
}

TEST(UtilityTest, TestGetSha256DigestFromCertificate) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  EXPECT_EQ(TEST_SAN_DNS_CERT_256_HASH, Utility::getSha256DigestFromCertificate(*cert));
}

TEST(UtilityTest, TestGetSha1DigestFromCertificate) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  EXPECT_EQ(TEST_SAN_DNS_CERT_1_HASH, Utility::getSha1DigestFromCertificate(*cert));
}

TEST(UtilityTest, TestGetSha256DigestFromIntermediateCertificate) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/intermediate_ca_cert.pem"));
  EXPECT_EQ(TEST_INTERMEDIATE_CA_CERT_256_HASH, Utility::getSha256DigestFromCertificate(*cert));
}

TEST(UtilityTest, TestGetSha1DigestFromIntermediateCertificate) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/intermediate_ca_cert.pem"));
  EXPECT_EQ(TEST_INTERMEDIATE_CA_CERT_1_HASH, Utility::getSha1DigestFromCertificate(*cert));
}

TEST(UtilityTest, TestExpirationWithUnixTimeWithExpiredCert) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  // Set a known date (2033-05-18 03:33:20 UTC) so that we get fixed output from this test.
  const time_t known_date_time = 2000000000;
  Event::SimulatedTimeSystem time_source;
  time_source.setSystemTime(std::chrono::system_clock::from_time_t(known_date_time));

  auto cert_expiry = TestUtility::parseTime(TEST_SAN_DNS_CERT_NOT_AFTER, "%b %d %H:%M:%S %Y GMT");
  EXPECT_EQ(absl::ToUnixSeconds(cert_expiry), Utility::getExpirationUnixTime(cert.get()).count());
}

TEST(UtilityTest, TestExpirationWithUnixTimeWithNotExpiredCert) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  const time_t known_date_time = 0;
  Event::SimulatedTimeSystem time_source;
  time_source.setSystemTime(std::chrono::system_clock::from_time_t(known_date_time));

  auto cert_expiry = TestUtility::parseTime(TEST_SAN_DNS_CERT_NOT_AFTER, "%b %d %H:%M:%S %Y GMT");
  EXPECT_EQ(absl::ToUnixSeconds(cert_expiry), Utility::getExpirationUnixTime(cert.get()).count());
}

TEST(UtilityTest, TestDaysUntilExpiration) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  // Set a known date (2033-05-18 03:33:20 UTC) so that we get fixed output from this test.
  const time_t known_date_time = 2000000000;
  Event::SimulatedTimeSystem time_source;
  time_source.setSystemTime(std::chrono::system_clock::from_time_t(known_date_time));

  EXPECT_EQ(std::nullopt, Utility::getDaysUntilExpiration(cert.get(), time_source));
}

TEST(UtilityTest, TestDaysUntilExpirationWithNull) {
  Event::SimulatedTimeSystem time_source;
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(),
            Utility::getDaysUntilExpiration(nullptr, time_source).value());
}

TEST(UtilityTest, TestValidFrom) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  const std::string formatted =
      TestUtility::formatTime(Utility::getValidFrom(*cert), "%b %e %H:%M:%S %Y GMT");
  EXPECT_EQ(TEST_SAN_DNS_CERT_NOT_BEFORE, formatted);
}

TEST(UtilityTest, TestExpirationTime) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  const std::string formatted =
      TestUtility::formatTime(Utility::getExpirationTime(*cert), "%b %e %H:%M:%S %Y GMT");
  EXPECT_EQ(TEST_SAN_DNS_CERT_NOT_AFTER, formatted);
}

TEST(UtilityTest, TestLongExpirationTime) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/long_validity_cert.pem"));
  const std::string formatted =
      TestUtility::formatTime(Utility::getExpirationTime(*cert), "%b %e %H:%M:%S %Y GMT");
  EXPECT_EQ(TEST_LONG_VALIDITY_CERT_NOT_AFTER, formatted);
}

TEST(UtilityTest, GetLastCryptoError) {
  // Clearing the error stack leaves us with no error to get.
  ERR_clear_error();
  EXPECT_FALSE(Utility::getLastCryptoError().has_value());

  ERR_put_error(ERR_LIB_SSL, 0, ERR_R_MALLOC_FAILURE, __FILE__, __LINE__);
  EXPECT_EQ(Utility::getLastCryptoError().value(),
            "error:10000041:SSL routines:OPENSSL_internal:malloc failure");

  // We consumed the last error, so back to not having an error to get.
  EXPECT_FALSE(Utility::getLastCryptoError().has_value());
}

TEST(UtilityTest, TestGetCertificateExtensionOids) {
  const std::string test_data_path = "{{ test_rundir }}/test/common/tls/test_data/";
  const std::vector<std::pair<std::string, int>> test_set = {
      {"unittest_cert.pem", 1},
      {"no_extension_cert.pem", 0},
      {"extensions_cert.pem", 7},
  };

  for (const auto& test_case : test_set) {
    bssl::UniquePtr<X509> cert =
        readCertFromFile(TestEnvironment::substitute(test_data_path + test_case.first));
    const auto& extension_oids = Utility::getCertificateExtensionOids(*cert);
    EXPECT_EQ(test_case.second, extension_oids.size());
  }

  bssl::UniquePtr<X509> cert =
      readCertFromFile(TestEnvironment::substitute(test_data_path + "extensions_cert.pem"));
  // clang-format off
  std::vector<std::string> expected_oids{
      "2.5.29.14", "2.5.29.15", "2.5.29.19",
      "2.5.29.35", "2.5.29.37",
      "1.2.3.4.5.6.7.8", "1.2.3.4.5.6.7.9"};
  // clang-format on
  const auto& extension_oids = Utility::getCertificateExtensionOids(*cert);
  EXPECT_THAT(extension_oids, testing::UnorderedElementsAreArray(expected_oids));
}

TEST(UtilityTest, TestGetCertificationExtensionValue) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/extensions_cert.pem"));
  EXPECT_EQ("\xc\x9Something", Utility::getCertificateExtensionValue(*cert, "1.2.3.4.5.6.7.8"));
  EXPECT_EQ("\x30\x3\x1\x1\xFF", Utility::getCertificateExtensionValue(*cert, "1.2.3.4.5.6.7.9"));
  EXPECT_EQ("", Utility::getCertificateExtensionValue(*cert, "1.2.3.4.5.6.7.10"));
  EXPECT_EQ("", Utility::getCertificateExtensionValue(*cert, "1.2.3.4"));
  EXPECT_EQ("", Utility::getCertificateExtensionValue(*cert, ""));
  EXPECT_EQ("", Utility::getCertificateExtensionValue(*cert, "foo"));
}

TEST(UtilityTest, SslErrorDescriptionTest) {
  const std::vector<std::pair<int, std::string>> test_set = {
      {SSL_ERROR_NONE, "NONE"},
      {SSL_ERROR_SSL, "SSL"},
      {SSL_ERROR_WANT_READ, "WANT_READ"},
      {SSL_ERROR_WANT_WRITE, "WANT_WRITE"},
#ifdef SSL_ERROR_WANT_PRIVATE_KEY_OPERATION
      {SSL_ERROR_WANT_PRIVATE_KEY_OPERATION, "WANT_PRIVATE_KEY_OPERATION"},
#endif
  };

  for (const auto& test_data : test_set) {
    EXPECT_EQ(test_data.second, Utility::getErrorDescription(test_data.first));
  }

  EXPECT_ENVOY_BUG(EXPECT_EQ(Utility::getErrorDescription(-1), "UNKNOWN_ERROR"),
                   "BoringSSL error had occurred: SSL_error_description() returned nullptr");
}

TEST(UtilityTest, TestGetX509ErrorInfo) {
  auto cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  X509StoreContextPtr store_ctx = X509_STORE_CTX_new();
  X509StorePtr ssl_ctx = X509_STORE_new();
  EXPECT_TRUE(X509_STORE_CTX_init(store_ctx.get(), ssl_ctx.get(), cert.get(), nullptr));
  X509_STORE_CTX_set_error(store_ctx.get(), X509_V_ERR_UNSPECIFIED);
  EXPECT_EQ(Utility::getX509VerificationErrorInfo(store_ctx.get()),
            "X509_verify_cert: certificate verification error at depth 0: unknown certificate "
            "verification error");
}

TEST(UtilityTest, TestGetX509ErrorInfoWithCrlError) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert_with_multiple_crl_dps_cert.pem"));
  bssl::UniquePtr<X509> ca_cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  // Not using X509_STORE_CTX_set_error as later calls to X509_STORE_CTX_get_current_cert will not
  // return the violating cert
  X509StoreContextPtr store_ctx = X509_STORE_CTX_new();
  X509StorePtr ssl_ctx = X509_STORE_new();
  X509_STORE_add_cert(ssl_ctx.get(), ca_cert.get());
  X509_STORE_set_flags(ssl_ctx.get(), X509_V_FLAG_CRL_CHECK);
  EXPECT_TRUE(X509_STORE_CTX_init(store_ctx.get(), ssl_ctx.get(), cert.get(), nullptr));
  int result = X509_verify_cert(store_ctx.get());
  EXPECT_EQ(result, 0); // Verification should fail
  EXPECT_EQ(X509_STORE_CTX_get_error(store_ctx.get()), X509_V_ERR_UNABLE_TO_GET_CRL);

  EXPECT_EQ(Utility::getX509VerificationErrorInfo(store_ctx.get()),
            "X509_verify_cert: certificate verification error at depth 0: certificate revocation "
            "check against provided CRLs failed: unable to get certificate CRL, "
            "certificate CRL distribution points: [http://crl.example.com/ca.crl, "
            "http://backup-crl.example.com/ca.crl]");
}

TEST(UtilityTest, TestGetCertificateCrlDpsForLogging) {
  bssl::UniquePtr<X509> cert_no_crldp = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  EXPECT_TRUE(Utility::getCertificateCrlDpsForLogging(cert_no_crldp.get()).empty());

  bssl::UniquePtr<X509> cert_single_crldp = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert_with_single_crl_dp_cert.pem"));
  std::vector<std::string> expected_crldp = {"http://crl.example.com/ca.crl"};
  EXPECT_EQ(Utility::getCertificateCrlDpsForLogging(cert_single_crldp.get()), expected_crldp);

  bssl::UniquePtr<X509> cert_multiple_crldps = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert_with_multiple_crl_dps_cert.pem"));
  std::vector<std::string> expected_crldps = {"http://crl.example.com/ca.crl",
                                              "http://backup-crl.example.com/ca.crl"};
  EXPECT_EQ(Utility::getCertificateCrlDpsForLogging(cert_multiple_crldps.get()), expected_crldps);
}

TEST(UtilityTest, TestMapX509Stack) {
  bssl::UniquePtr<STACK_OF(X509)> cert_chain = readCertChainFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/no_san_chain.pem"));

  std::vector<std::string> expected_subject{
      "CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San "
      "Francisco,ST=California,C=US",
      "CN=Test Intermediate CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,"
      "ST=California,C=US"};
  auto func = [](X509& cert) -> std::string { return Utility::getSubjectFromCertificate(cert); };
  EXPECT_EQ(expected_subject, Utility::mapX509Stack(*cert_chain, func));

  bssl::UniquePtr<STACK_OF(X509)> empty_chain(sk_X509_new_null());
  EXPECT_ENVOY_BUG(Utility::mapX509Stack(*empty_chain, func), "x509 stack is empty or NULL");
  EXPECT_ENVOY_BUG(Utility::mapX509Stack(*cert_chain, nullptr), "field_extractor is nullptr");
  bssl::UniquePtr<STACK_OF(X509)> fake_cert_chain(sk_X509_new_null());
  sk_X509_push(fake_cert_chain.get(), nullptr);
  EXPECT_EQ(std::vector<std::string>{""}, Utility::mapX509Stack(*fake_cert_chain, func));
}

using TlsProto = envoy::extensions::transport_sockets::tls::v3::TlsParameters;

TEST(UtilityTest, CompliancePolicyFromProto) {
  TlsProto params;
  EXPECT_EQ(std::nullopt, Utility::compliancePolicyFromProto(params));

  params.add_compliance_policies(TlsProto::FIPS_202205);
  EXPECT_EQ(TlsProto::FIPS_202205, Utility::compliancePolicyFromProto(params));

  // More than one policy only reaches here for a proto built without validation.
  params.add_compliance_policies(TlsProto::CNSA1_202603);
  EXPECT_ENVOY_BUG(EXPECT_EQ(std::nullopt, Utility::compliancePolicyFromProto(params)),
                   "more than one policies are not supported");
}

TEST(UtilityTest, CompliancePolicyToSslPolicy) {
  EXPECT_TRUE(Utility::compliancePolicyToSslPolicy(TlsProto::FIPS_202205).ok());
  EXPECT_TRUE(Utility::compliancePolicyToSslPolicy(TlsProto::CNSA2_202603).ok());
  EXPECT_TRUE(Utility::compliancePolicyToSslPolicy(TlsProto::CNSA1_202603).ok());

  const absl::StatusOr<ssl_compliance_policy_t> unknown =
      Utility::compliancePolicyToSslPolicy(static_cast<TlsProto::CompliancePolicy>(1234));
  EXPECT_FALSE(unknown.ok());
  EXPECT_EQ("Unknown compliance policy: 1234", unknown.status().message());
}

TEST(UtilityTest, TlsVersionFromProto) {
  EXPECT_EQ(TLS1_2_VERSION, Utility::tlsVersionFromProto(TlsProto::TLS_AUTO, TLS1_2_VERSION));
  EXPECT_EQ(TLS1_VERSION, Utility::tlsVersionFromProto(TlsProto::TLSv1_0, 0));
  EXPECT_EQ(TLS1_1_VERSION, Utility::tlsVersionFromProto(TlsProto::TLSv1_1, 0));
  EXPECT_EQ(TLS1_2_VERSION, Utility::tlsVersionFromProto(TlsProto::TLSv1_2, 0));
  EXPECT_EQ(TLS1_3_VERSION, Utility::tlsVersionFromProto(TlsProto::TLSv1_3, 0));

  EXPECT_ENVOY_BUG(
      EXPECT_EQ(TLS1_2_VERSION, Utility::tlsVersionFromProto(static_cast<TlsProto::TlsProtocol>(99),
                                                             TLS1_2_VERSION)),
      "unexpected tls version");
}

TEST(UtilityTest, ApplyCompliancePolicy) {
  bssl::UniquePtr<SSL_CTX> ssl_ctx(SSL_CTX_new(TLS_method()));
  bssl::UniquePtr<SSL> ssl(SSL_new(ssl_ctx.get()));
  const TlsProto::CompliancePolicy unknown = static_cast<TlsProto::CompliancePolicy>(1234);

  EXPECT_TRUE(Utility::applyCompliancePolicyToSslCtx(TlsProto::FIPS_202205, ssl_ctx.get()).ok());
  EXPECT_EQ("Unknown compliance policy: 1234",
            Utility::applyCompliancePolicyToSslCtx(unknown, ssl_ctx.get()).message());

  EXPECT_TRUE(Utility::applyCompliancePolicyToSsl(TlsProto::FIPS_202205, ssl.get()).ok());
  EXPECT_EQ("Unknown compliance policy: 1234",
            Utility::applyCompliancePolicyToSsl(unknown, ssl.get()).message());
}

TEST(UtilityTest, EffectiveTlsParamsSkipsHandshakerOwnedFields) {
  const Ssl::TlsParams params{
      .cipher_suites = "ECDHE-RSA-AES128-GCM-SHA256",
      .ecdh_curves = "P-256",
      .signature_algorithms = "rsa_pss_rsae_sha256",
  };
  const Utility::EffectiveTlsParams all = Utility::effectiveTlsParams(params, false, false);
  EXPECT_EQ(params.cipher_suites, all.cipher_suites);
  EXPECT_EQ(params.ecdh_curves, all.ecdh_curves);
  EXPECT_EQ(params.signature_algorithms, all.signature_algorithms);

  const Utility::EffectiveTlsParams none = Utility::effectiveTlsParams(params, true, true);
  EXPECT_TRUE(none.cipher_suites.empty());
  EXPECT_TRUE(none.ecdh_curves.empty());
  EXPECT_TRUE(none.signature_algorithms.empty());
}

// Looks up `pem` in `cache`, parsing it as a CRL blob on a miss.
absl::StatusOr<CrlListSharedPtr> getCrlList(CrlCache& cache, const std::string& pem,
                                            absl::string_view path) {
  return cache.getOrCreate(pem, [&] { return parseCrlList(pem, path); });
}

// Looks up `pem` in `cache`, parsing it as a trusted CA blob on a miss.
absl::StatusOr<CaCertListSharedPtr> getCaCertList(CaCertCache& cache, const std::string& pem,
                                                  absl::string_view path) {
  return cache.getOrCreate(pem, [&] { return parseCaCertList(pem, path); });
}

// The CRL cache returns one shared parsed representation for identical content,
// so a CRL referenced from many contexts is materialized in memory only once.
TEST(CrlCacheTest, SharesIdenticalCrlContent) {
  auto cache = std::make_shared<CrlCache>();
  const std::string crl = TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"));

  absl::StatusOr<CrlListSharedPtr> first = getCrlList(*cache, crl, "ca_cert.crl");
  ASSERT_OK(first);
  absl::StatusOr<CrlListSharedPtr> second = getCrlList(*cache, crl, "ca_cert.crl");
  ASSERT_OK(second);

  // Both lookups return the same CrlList and the same parsed X509_CRL.
  EXPECT_EQ(first->get(), second->get());
  ASSERT_FALSE((*first)->empty());
  EXPECT_EQ((**first)[0].get(), (**second)[0].get());
  EXPECT_EQ(cache->size(), 1);
}

// Distinct CRL content is cached separately.
TEST(CrlCacheTest, SeparatesDistinctCrlContent) {
  auto cache = std::make_shared<CrlCache>();
  const std::string crl1 = TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"));
  const std::string crl2 = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/intermediate_ca_cert.crl"));

  absl::StatusOr<CrlListSharedPtr> first = getCrlList(*cache, crl1, "ca_cert.crl");
  ASSERT_OK(first);
  absl::StatusOr<CrlListSharedPtr> second = getCrlList(*cache, crl2, "intermediate_ca_cert.crl");
  ASSERT_OK(second);

  EXPECT_NE(first->get(), second->get());
  EXPECT_EQ(cache->size(), 2);
}

// An entry is released once the last reference to it is dropped, so the cache
// does not grow without bound as CRLs are rotated via xDS.
TEST(CrlCacheTest, ReleasesUnreferencedEntries) {
  auto cache = std::make_shared<CrlCache>();
  const std::string crl = TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"));

  {
    absl::StatusOr<CrlListSharedPtr> entry = getCrlList(*cache, crl, "ca_cert.crl");
    ASSERT_OK(entry);
    EXPECT_EQ(cache->size(), 1);
  }
  // The only reference is gone, so the entry is released.
  EXPECT_EQ(cache->size(), 0);

  // Re-adding the same content succeeds and repopulates the cache.
  absl::StatusOr<CrlListSharedPtr> reloaded = getCrlList(*cache, crl, "ca_cert.crl");
  ASSERT_OK(reloaded);
  EXPECT_EQ(cache->size(), 1);
}

// Invalid CRL content returns an error and is not cached.
TEST(CrlCacheTest, ReturnsErrorForInvalidCrl) {
  auto cache = std::make_shared<CrlCache>();
  const std::string invalid = TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/not_a_crl.crl"));

  absl::StatusOr<CrlListSharedPtr> result = getCrlList(*cache, invalid, "not_a_crl.crl");
  EXPECT_THAT(result,
              HasStatusMessage(testing::HasSubstr("Failed to load CRL from not_a_crl.crl")));
  EXPECT_EQ(cache->size(), 0);
}

// A returned CrlList keeps the cache alive, so a caller only needs to hold the
// CrlList (this is what lets the validator store a single shared_ptr).
TEST(CrlCacheTest, CrlListKeepsCacheAlive) {
  std::weak_ptr<CrlCache> weak_cache;
  CrlListSharedPtr crl_list;
  {
    auto cache = std::make_shared<CrlCache>();
    weak_cache = cache;
    const std::string crl = TestEnvironment::readFileToStringForTest(
        TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"));
    absl::StatusOr<CrlListSharedPtr> result = getCrlList(*cache, crl, "ca_cert.crl");
    ASSERT_OK(result);
    crl_list = std::move(*result);
  }
  // The local cache reference is gone, but the CrlList still holds it alive.
  EXPECT_FALSE(weak_cache.expired());
  EXPECT_EQ(weak_cache.lock()->size(), 1);

  // Dropping the CrlList releases the cache.
  crl_list.reset();
  EXPECT_TRUE(weak_cache.expired());
}

// A minimal value type used to test the shared release and lifetime behavior independently of any
// PEM format.
struct TestPemValue {
  std::string pem;
};

// Wraps a SharedPemCache and counts parses.
class TestPemCache {
public:
  absl::StatusOr<std::shared_ptr<TestPemValue>> get(const std::string& pem) {
    return cache_->getOrCreate(pem, [&]() -> absl::StatusOr<std::unique_ptr<TestPemValue>> {
      ++parses_;
      if (pem == "invalid") {
        return absl::InvalidArgumentError("invalid pem");
      }
      return std::make_unique<TestPemValue>(TestPemValue{pem});
    });
  }
  size_t size() const { return cache_->size(); }

  std::shared_ptr<SharedPemCache<TestPemValue>> cache_ =
      std::make_shared<SharedPemCache<TestPemValue>>();
  int parses_{0};
};

// An entry is erased as soon as its last reference is released, without waiting for a later
// insert, and is parsed only once while any reference is held.
TEST(SharedPemCacheTest, ErasesEntryWhenLastReferenceIsReleased) {
  auto cache = std::make_shared<TestPemCache>();
  auto first = cache->get("a");
  ASSERT_OK(first);
  auto second = cache->get("a");
  ASSERT_OK(second);
  EXPECT_EQ(first->get(), second->get());
  EXPECT_EQ(cache->parses_, 1);
  EXPECT_EQ(cache->size(), 1);

  first->reset();
  EXPECT_EQ(cache->size(), 1);
  second->reset();
  EXPECT_EQ(cache->size(), 0);

  // The released key is parsed again on the next lookup.
  auto again = cache->get("a");
  ASSERT_OK(again);
  EXPECT_EQ(cache->parses_, 2);
  EXPECT_EQ(cache->size(), 1);
}

// Releasing one of many distinct entries erases only that entry.
TEST(SharedPemCacheTest, ErasesOnlyTheReleasedEntry) {
  auto cache = std::make_shared<TestPemCache>();
  std::vector<std::shared_ptr<TestPemValue>> entries;
  for (int i = 0; i < 100; ++i) {
    auto entry = cache->get(absl::StrCat("pem-", i));
    ASSERT_OK(entry);
    entries.push_back(std::move(*entry));
  }
  EXPECT_EQ(cache->size(), 100);

  for (int i = 0; i < 100; i += 2) {
    entries[i].reset();
  }
  EXPECT_EQ(cache->size(), 50);

  // The surviving entries are still shared rather than re-parsed.
  const int parses = cache->parses_;
  auto survivor = cache->get("pem-1");
  ASSERT_OK(survivor);
  EXPECT_EQ(survivor->get(), entries[1].get());
  EXPECT_EQ(cache->parses_, parses);
}

// A parse error is returned to the caller and nothing is cached.
TEST(SharedPemCacheTest, ParseErrorIsNotCached) {
  auto cache = std::make_shared<TestPemCache>();
  EXPECT_THAT(cache->get("invalid"), HasStatusMessage(testing::HasSubstr("invalid pem")));
  EXPECT_EQ(cache->size(), 0);
  EXPECT_THAT(cache->get("invalid"), HasStatusMessage(testing::HasSubstr("invalid pem")));
  EXPECT_EQ(cache->parses_, 2);
}

// An entry keeps its cache alive, and releasing the last entry after every other cache reference
// is gone erases the entry and then destroys the cache.
TEST(SharedPemCacheTest, EntryKeepsCacheAlive) {
  std::weak_ptr<SharedPemCache<TestPemValue>> weak_cache;
  std::shared_ptr<TestPemValue> entry;
  {
    auto cache = std::make_shared<TestPemCache>();
    weak_cache = cache->cache_;
    auto result = cache->get("a");
    ASSERT_OK(result);
    entry = std::move(*result);
  }
  ASSERT_FALSE(weak_cache.expired());
  EXPECT_EQ(weak_cache.lock()->size(), 1);

  entry.reset();
  EXPECT_TRUE(weak_cache.expired());
}

// The last reference can be released on a thread other than the one that created the entry.
TEST(SharedPemCacheTest, ReleasesOnAnotherThread) {
  auto cache = std::make_shared<TestPemCache>();
  std::vector<std::shared_ptr<TestPemValue>> entries;
  for (int i = 0; i < 10; ++i) {
    auto entry = cache->get(absl::StrCat("pem-", i));
    ASSERT_OK(entry);
    entries.push_back(std::move(*entry));
  }
  EXPECT_EQ(cache->size(), 10);

  Thread::ThreadPtr worker =
      Thread::threadFactoryForTest().createThread([&entries]() { entries.clear(); });
  worker->join();
  EXPECT_EQ(cache->size(), 0);
}

// The last reference to a key is released on another thread while the main thread looks the same
// key up again. Whichever order the two take, the entry the main thread ends up holding must stay
// cached: a release that loses the race to a re-add must not erase the replacement.
TEST(SharedPemCacheTest, ConcurrentReleaseDoesNotEraseReplacement) {
  auto cache = std::make_shared<TestPemCache>();
  // Counts the iterations in which the re-add lost the race and was served the entry being
  // released, as opposed to re-parsing after the release.
  int shared_with_release = 0;
  for (int i = 0; i < 1000; ++i) {
    auto held = cache->get("a");
    ASSERT_OK(held);
    std::shared_ptr<TestPemValue> to_release = std::move(*held);
    const int parses_before = cache->parses_;

    Thread::ThreadPtr worker =
        Thread::threadFactoryForTest().createThread([&to_release]() { to_release.reset(); });
    auto latest = cache->get("a");
    ASSERT_OK(latest);
    worker->join();
    if (cache->parses_ == parses_before) {
      ++shared_with_release;
    }

    // The held entry is still the cached one.
    EXPECT_EQ(cache->size(), 1);
    auto lookup = cache->get("a");
    ASSERT_OK(lookup);
    EXPECT_EQ(lookup->get(), latest->get());
    latest->reset();
    lookup->reset();
    ASSERT_EQ(cache->size(), 0);
  }
  testing::Test::RecordProperty("re_adds_sharing_the_released_entry", shared_with_release);
}

// The trusted CA cache returns one shared parsed representation for identical
// content, so a trust bundle referenced from many contexts is materialized in
// memory only once.
TEST(CaCertCacheTest, SharesIdenticalCaContent) {
  auto cache = std::make_shared<CaCertCache>();
  const std::string ca_cert = TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));

  absl::StatusOr<CaCertListSharedPtr> first = getCaCertList(*cache, ca_cert, "ca_cert.pem");
  ASSERT_OK(first);
  absl::StatusOr<CaCertListSharedPtr> second = getCaCertList(*cache, ca_cert, "ca_cert.pem");
  ASSERT_OK(second);

  // Both lookups return the same CaCertList and the same parsed X509.
  EXPECT_EQ(first->get(), second->get());
  ASSERT_FALSE((*first)->certs.empty());
  EXPECT_EQ((*first)->certs[0].get(), (*second)->certs[0].get());
  EXPECT_EQ(cache->size(), 1);
}

// Distinct CA content is cached separately.
TEST(CaCertCacheTest, SeparatesDistinctCaContent) {
  auto cache = std::make_shared<CaCertCache>();
  const std::string ca_cert = TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  const std::string fake_ca_cert = TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/fake_ca_cert.pem"));

  absl::StatusOr<CaCertListSharedPtr> first = getCaCertList(*cache, ca_cert, "ca_cert.pem");
  ASSERT_OK(first);
  absl::StatusOr<CaCertListSharedPtr> second =
      getCaCertList(*cache, fake_ca_cert, "fake_ca_cert.pem");
  ASSERT_OK(second);

  EXPECT_NE(first->get(), second->get());
  EXPECT_EQ(cache->size(), 2);
}

// A trust bundle carrying several certificates is parsed into one entry holding
// all of them.
TEST(CaCertCacheTest, KeepsEveryCertificateInABundle) {
  auto cache = std::make_shared<CaCertCache>();
  const std::string bundle = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/ca_certificates.pem"));

  absl::StatusOr<CaCertListSharedPtr> entry = getCaCertList(*cache, bundle, "ca_certificates.pem");
  ASSERT_OK(entry);
  EXPECT_GT((*entry)->certs.size(), 1);
}

// A CA blob is allowed to carry CRLs alongside the certificates; both are kept
// on the shared entry.
TEST(CaCertCacheTest, KeepsCrlsCarriedInTheCaBlob) {
  auto cache = std::make_shared<CaCertCache>();
  const std::string ca_cert_with_crl =
      TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
          "{{ test_rundir }}/test/common/tls/test_data/ca_cert_with_crl.pem"));

  absl::StatusOr<CaCertListSharedPtr> entry =
      getCaCertList(*cache, ca_cert_with_crl, "ca_cert_with_crl.pem");
  ASSERT_OK(entry);
  EXPECT_FALSE((*entry)->certs.empty());
  EXPECT_FALSE((*entry)->crls.empty());
}

// An entry is released once the last reference to it is dropped, so the cache
// does not grow without bound as certificates are rotated via xDS.
TEST(CaCertCacheTest, ReleasesUnreferencedEntries) {
  auto cache = std::make_shared<CaCertCache>();
  const std::string ca_cert = TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));

  {
    absl::StatusOr<CaCertListSharedPtr> entry = getCaCertList(*cache, ca_cert, "ca_cert.pem");
    ASSERT_OK(entry);
    EXPECT_EQ(cache->size(), 1);
  }
  // The only reference is gone, so the entry is released.
  EXPECT_EQ(cache->size(), 0);

  // Re-adding the same content succeeds and repopulates the cache.
  absl::StatusOr<CaCertListSharedPtr> reloaded = getCaCertList(*cache, ca_cert, "ca_cert.pem");
  ASSERT_OK(reloaded);
  EXPECT_EQ(cache->size(), 1);
}

// Content that carries no usable certificate returns an error and is not cached.
TEST(CaCertCacheTest, ReturnsErrorForInvalidCaCert) {
  auto cache = std::make_shared<CaCertCache>();
  // Valid PEM envelope but garbage base64 inside, so nothing parses out of it.
  const std::string invalid = "-----BEGIN CERTIFICATE-----\n"
                              "not valid base64 content!!!\n"
                              "-----END CERTIFICATE-----\n";

  absl::StatusOr<CaCertListSharedPtr> result =
      getCaCertList(*cache, invalid, "invalid_ca_cert.pem");
  EXPECT_THAT(result, HasStatusMessage(testing::HasSubstr(
                          "Failed to load trusted CA certificates from invalid_ca_cert.pem")));
  EXPECT_EQ(cache->size(), 0);
}

// A returned CaCertList keeps the cache alive, so a caller only needs to hold
// the CaCertList (this is what lets the validator store a single shared_ptr).
TEST(CaCertCacheTest, CaCertListKeepsCacheAlive) {
  std::weak_ptr<CaCertCache> weak_cache;
  CaCertListSharedPtr ca_cert_list;
  {
    auto cache = std::make_shared<CaCertCache>();
    weak_cache = cache;
    const std::string ca_cert = TestEnvironment::readFileToStringForTest(
        TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
    absl::StatusOr<CaCertListSharedPtr> result = getCaCertList(*cache, ca_cert, "ca_cert.pem");
    ASSERT_OK(result);
    ca_cert_list = std::move(*result);
  }
  // The local cache reference is gone, but the CaCertList still holds it alive.
  EXPECT_FALSE(weak_cache.expired());
  EXPECT_EQ(weak_cache.lock()->size(), 1);

  // Dropping the CaCertList releases the cache.
  ca_cert_list.reset();
  EXPECT_TRUE(weak_cache.expired());
}

} // namespace
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
