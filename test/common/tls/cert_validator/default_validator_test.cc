#include <string>
#include <vector>

#include "source/common/tls/cert_validator/default_validator.h"
#include "source/common/tls/cert_validator/san_matcher.h"

#include "test/common/tls/cert_validator/test_common.h"
#include "test/common/tls/ssl_test_utility.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

using TestCertificateValidationContextConfigPtr =
    std::unique_ptr<TestCertificateValidationContextConfig>;
using X509StoreContextPtr = CSmartPtr<X509_STORE_CTX, X509_STORE_CTX_free>;
using X509StorePtr = CSmartPtr<X509_STORE, X509_STORE_free>;
using SSLContextPtr = CSmartPtr<SSL_CTX, SSL_CTX_free>;

TEST(DefaultCertValidatorTest, TestVerifySubjectAltNameDNSMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  std::vector<std::string> verify_subject_alt_name_list = {"server1.example.com",
                                                           "server2.example.com"};
  EXPECT_TRUE(DefaultCertValidator::verifySubjectAltName(cert.get(), verify_subject_alt_name_list));
}

TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameDNSMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw([^.]*\.example.com)raw"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_DNS, matcher, context)});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// All OtherName SAN tests below are matched against an expected OID
// and an expected SAN value

// Test to check if the cert has an OtherName SAN
// of UTF8String type with value containing "example.com"
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_othername_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw([^.]*\.example.com)raw"));
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.3", 0));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN
// with a Boolean type value "true"
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameBooleanTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.5.5.7.8.7", 0));
  matcher.MergeFrom(TestUtility::createExactMatcher("true"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN
// with an Enumerated type value "5"
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameEnumeratedTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.1", 0));
  matcher.MergeFrom(TestUtility::createExactMatcher("5"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN
// with an Integer type value "5464".
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameIntegerTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.5.5.7.8.3", 0));
  matcher.MergeFrom(TestUtility::createExactMatcher("5464"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN
// with an Object type value "1.3.6.1.4.1.311.20.2.3".
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameObjectTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.5.2.2", 0));
  matcher.MergeFrom(TestUtility::createExactMatcher("1.3.6.1.4.1.311.20.2.3"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN with a NULL type.
// NULL value is matched against an empty string since matcher is required.
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameNullTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.3", 0));
  matcher.MergeFrom(TestUtility::createExactMatcher(""));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN
// with a BitString type value "01010101".
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameBitStringTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_string_type_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.3", 0));
  matcher.MergeFrom(TestUtility::createExactMatcher("01010101"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN
// with an OctetString type value "48656C6C6F20576F726C64".
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameOctetStringTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_string_type_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.4", 0));
  matcher.MergeFrom(TestUtility::createExactMatcher("48656C6C6F20576F726C64"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN
// with a PrintableString type value "PrintableStringExample".
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNamePrintableStringTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_string_type_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.5", 0));
  matcher.MergeFrom(TestUtility::createExactMatcher("PrintableStringExample"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN
// with a T61String type value "T61StringExample".
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameT61StringTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_string_type_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.6", 0));
  matcher.MergeFrom(TestUtility::createExactMatcher("T61StringExample"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN
// with a IA5String type value "IA5StringExample".
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameIA5StringTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_string_type_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.7", 0));
  matcher.MergeFrom(TestUtility::createExactMatcher("IA5StringExample"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN
// with a GeneralStringExample type value "GeneralStringExample".
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameGeneralStringTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_string_type_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.8", 0));
  matcher.MergeFrom(TestUtility::createExactMatcher("GeneralStringExample"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN
// with an UniversalStringExample type value "UniversalStringExample".
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameUniversalStringTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_string_type_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.9", 0));
  matcher.MergeFrom(TestUtility::createExactMatcher("UniversalStringExample"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN
// with an UTCTime type value "230616120000Z".
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameUtcTimeTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_string_type_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.10", 0));
  matcher.MergeFrom(TestUtility::createExactMatcher("230616120000Z"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN
// with a GeneralizedTime type value "20230616120000Z".
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameGeneralizedTimeTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_string_type_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.11", 0));
  matcher.MergeFrom(TestUtility::createExactMatcher("20230616120000Z"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN
// with a VisibleString type value "VisibleStringExample".
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameVisibleStringTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_string_type_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.12", 0));
  matcher.MergeFrom(TestUtility::createExactMatcher("VisibleStringExample"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN
// with an UTF8 String type value "UTF8StringExample".
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameUTF8StringTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_string_type_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.13", 0));
  matcher.MergeFrom(TestUtility::createExactMatcher("UTF8StringExample"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN
// with a `BMPString` type value "BMPStringExample".
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameBmpStringTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_string_type_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.14", 0));
  matcher.MergeFrom(TestUtility::createExactMatcher("BMPStringExample"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN with a SET type
// containing select values "test1" and "test2".
// SET is a non-primitive type containing multiple values and is DER-encoded.
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameSetTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_string_type_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.3", 0));
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw(.*test1.*test2.*)raw"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

// Test to check if the cert has an OtherName SAN with a SEQUENCE type
// containing select values "test3" and "test4".
// SEQUENCE is a non-primitive type containing multiple values and is DER-encoded.
TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameSequenceTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_multiple_othername_string_type_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.3", 0));
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw(.*test3.*test4.*)raw"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameDnsAndOtherNameMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_dns_and_othername_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw([^.]*\.example.com)raw"));
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.3", 0));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_DNS, matcher, context)});
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameIncorrectOidMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_othername_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw([^.]*\.example.com)raw"));
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.2", 0));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_FALSE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameOtherNameIncorrectValueMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/san_othername_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw([^.]*\.example.net)raw"));
  bssl::UniquePtr<ASN1_OBJECT> oid(OBJ_txt2obj("1.3.6.1.4.1.311.20.2.3", 0));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(SanMatcherPtr{
      std::make_unique<StringSanMatcher>(GEN_OTHERNAME, matcher, context, std::move(oid))});
  EXPECT_FALSE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameIncorrectTypeMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw([^.]*\.example.com)raw"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_URI, matcher, context)});
  EXPECT_FALSE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameWildcardDNSMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/san_multiple_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_exact("api.example.com");
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_DNS, matcher, context)});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, TestMultiLevelMatch) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  // san_multiple_dns_cert matches *.example.com
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/san_multiple_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_exact("foo.api.example.com");
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_DNS, matcher, context)});
  EXPECT_FALSE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, TestVerifySubjectAltNameURIMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"));
  std::vector<std::string> verify_subject_alt_name_list = {"spiffe://lyft.com/fake-team",
                                                           "spiffe://lyft.com/test-team"};
  EXPECT_TRUE(DefaultCertValidator::verifySubjectAltName(cert.get(), verify_subject_alt_name_list));
}

TEST(DefaultCertValidatorTest, TestVerifySubjectAltMultiDomain) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/san_multiple_dns_cert.pem"));
  std::vector<std::string> verify_subject_alt_name_list = {"https://a.www.example.com"};
  EXPECT_FALSE(
      DefaultCertValidator::verifySubjectAltName(cert.get(), verify_subject_alt_name_list));
}

TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameURIMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw(spiffe://lyft.com/[^/]*-team)raw"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_URI, matcher, context)});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, TestVerifySubjectAltNameNotMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  std::vector<std::string> verify_subject_alt_name_list = {"foo", "bar"};
  EXPECT_FALSE(
      DefaultCertValidator::verifySubjectAltName(cert.get(), verify_subject_alt_name_list));
}

TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameNotMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw([^.]*\.example\.net)raw"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_DNS, matcher, context)});
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_IPADD, matcher, context)});
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_URI, matcher, context)});
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_EMAIL, matcher, context)});
  EXPECT_FALSE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, TestCertificateVerificationWithSANMatcher) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::TestUtil::TestStore test_store;
  SslStats stats = generateSslStats(*test_store.rootScope());
  // Create the default validator object.
  auto default_validator =
      std::make_unique<Extensions::TransportSockets::Tls::DefaultCertValidator>(
          /*CertificateValidationContextConfig=*/nullptr, stats, context);

  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw([^.]*\.example.com)raw"));
  std::vector<SanMatcherPtr> san_matchers;
  san_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_DNS, matcher, context)});
  // Verify the certificate with correct SAN regex matcher.
  EXPECT_EQ(default_validator->verifyCertificate(cert.get(), /*verify_san_list=*/{}, san_matchers,
                                                 nullptr, nullptr),
            Envoy::Ssl::ClientValidationStatus::Validated);
  EXPECT_EQ(stats.fail_verify_san_.value(), 0);

  matcher.MergeFrom(TestUtility::createExactMatcher("hello.example.com"));
  std::vector<SanMatcherPtr> invalid_san_matchers;
  invalid_san_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_DNS, matcher, context)});
  std::string error;
  // Verify the certificate with incorrect SAN exact matcher.
  EXPECT_EQ(default_validator->verifyCertificate(cert.get(), /*verify_san_list=*/{},
                                                 invalid_san_matchers, &error, nullptr),
            Envoy::Ssl::ClientValidationStatus::Failed);
  EXPECT_EQ(stats.fail_verify_san_.value(), 1);
}

TEST(DefaultCertValidatorTest, TestCertificateVerificationWithNoValidationContext) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::TestUtil::TestStore test_store;
  SslStats stats = generateSslStats(*test_store.rootScope());
  // Create the default validator object.
  auto default_validator =
      std::make_unique<Extensions::TransportSockets::Tls::DefaultCertValidator>(
          /*CertificateValidationContextConfig=*/nullptr, stats, context);

  EXPECT_EQ(default_validator->verifyCertificate(/*cert=*/nullptr, /*verify_san_list=*/{},
                                                 /*subject_alt_name_matchers=*/{}, nullptr,
                                                 nullptr),
            Envoy::Ssl::ClientValidationStatus::NotValidated);
  bssl::UniquePtr<X509> cert(X509_new());
  SSLContextPtr ssl_ctx = SSL_CTX_new(TLS_method());
  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  ASSERT_TRUE(bssl::PushToStack(cert_chain.get(), std::move(cert)));
  EXPECT_EQ(ValidationResults::ValidationStatus::Failed,
            default_validator
                ->doVerifyCertChain(*cert_chain, /*callback=*/nullptr,
                                    /*transport_socket_options=*/nullptr, *ssl_ctx, {}, false, "")
                .status);
}

TEST(DefaultCertValidatorTest, TestCertificateVerificationWithEmptyCertChain) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::TestUtil::TestStore test_store;
  SslStats stats = generateSslStats(*test_store.rootScope());
  // Create the default validator object.
  auto default_validator =
      std::make_unique<Extensions::TransportSockets::Tls::DefaultCertValidator>(
          /*CertificateValidationContextConfig=*/nullptr, stats, context);

  SSLContextPtr ssl_ctx = SSL_CTX_new(TLS_method());
  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  TestSslExtendedSocketInfo extended_socket_info;
  ValidationResults results = default_validator->doVerifyCertChain(
      *cert_chain, /*callback=*/nullptr,
      /*transport_socket_options=*/nullptr, *ssl_ctx, {}, false, "");
  EXPECT_EQ(ValidationResults::ValidationStatus::Failed, results.status);
  EXPECT_EQ(Ssl::ClientValidationStatus::NoClientCertificate, results.detailed_status);
}

TEST(DefaultCertValidatorTest, NoSanInCert) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/fake_ca_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw([^.]*\.example\.net)raw"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_DNS, matcher, context)});
  EXPECT_FALSE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, WithVerifyDepth) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::TestUtil::TestStore test_store;
  SslStats stats = generateSslStats(*test_store.rootScope());
  envoy::config::core::v3::TypedExtensionConfig typed_conf;
  std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher> san_matchers{};

  bssl::UniquePtr<STACK_OF(X509)> cert_chain = readCertChainFromFile(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/test_long_cert_chain.pem"));
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/test_random_cert.pem"));
  bssl::UniquePtr<X509> ca_cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));

  // Create the default validator object.
  // Config includes ca_cert and the verify-depth.
  // Set verify depth < 3, so verification fails. ( There are 3 intermediate certs )

  std::string ca_cert_str(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  TestCertificateValidationContextConfigPtr test_config =
      std::make_unique<TestCertificateValidationContextConfig>(typed_conf, false, san_matchers,
                                                               ca_cert_str, 2);
  auto default_validator =
      std::make_unique<Extensions::TransportSockets::Tls::DefaultCertValidator>(test_config.get(),
                                                                                stats, context);

  STACK_OF(X509)* intermediates = cert_chain.get();
  SSLContextPtr ssl_ctx = SSL_CTX_new(TLS_method());
  X509StoreContextPtr store_ctx = X509_STORE_CTX_new();

  X509_STORE* storep = SSL_CTX_get_cert_store(ssl_ctx.get());
  X509_STORE_add_cert(storep, ca_cert.get());
  EXPECT_TRUE(X509_STORE_CTX_init(store_ctx.get(), storep, cert.get(), intermediates));

  ASSERT_TRUE(default_validator->addClientValidationContext(ssl_ctx.get(), false).ok());
  X509_VERIFY_PARAM_set1(X509_STORE_CTX_get0_param(store_ctx.get()),
                         SSL_CTX_get0_param(ssl_ctx.get()));

  EXPECT_EQ(X509_verify_cert(store_ctx.get()), 0);

  // Now, create config with no depth configuration, verification should pass.
  test_config = std::make_unique<TestCertificateValidationContextConfig>(typed_conf, false,
                                                                         san_matchers, ca_cert_str);
  default_validator = std::make_unique<Extensions::TransportSockets::Tls::DefaultCertValidator>(
      test_config.get(), stats, context);

  // Re-initialize context
  ssl_ctx = SSL_CTX_new(TLS_method());
  store_ctx = X509_STORE_CTX_new();
  storep = SSL_CTX_get_cert_store(ssl_ctx.get());
  X509_STORE_add_cert(storep, ca_cert.get());
  EXPECT_TRUE(X509_STORE_CTX_init(store_ctx.get(), storep, cert.get(), intermediates));

  ASSERT_TRUE(default_validator->addClientValidationContext(ssl_ctx.get(), false).ok());
  X509_VERIFY_PARAM_set1(X509_STORE_CTX_get0_param(store_ctx.get()),
                         SSL_CTX_get0_param(ssl_ctx.get()));

  EXPECT_EQ(X509_verify_cert(store_ctx.get()), 1);
  EXPECT_EQ(X509_STORE_CTX_get_error(store_ctx.get()), X509_V_OK);
}

class MockCertificateValidationContextConfig : public Ssl::CertificateValidationContextConfig {
public:
  MockCertificateValidationContextConfig() : MockCertificateValidationContextConfig("") {}

  explicit MockCertificateValidationContextConfig(const std::string& s) : s_(s) {
    auto matcher = envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher();
    matcher.set_san_type(
        static_cast<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher_SanType>(
            123));
    matchers_.emplace_back(matcher);
  };
  const std::string& caCert() const override { return s_; }
  const std::string& caCertPath() const override { return s_; }
  const std::string& certificateRevocationList() const override { return s_; }
  const std::string& certificateRevocationListPath() const override { return s_; }
  const std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher>&
  subjectAltNameMatchers() const override {
    return matchers_;
  }
  const std::vector<std::string>& verifyCertificateHashList() const override { return strs_; }
  const std::vector<std::string>& verifyCertificateSpkiList() const override { return strs_; }
  bool allowExpiredCertificate() const override { return false; }
  MOCK_METHOD(envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext::
                  TrustChainVerification,
              trustChainVerification, (), (const override));
  MOCK_METHOD(const absl::optional<envoy::config::core::v3::TypedExtensionConfig>&,
              customValidatorConfig, (), (const override));
  MOCK_METHOD(Api::Api&, api, (), (const override));
  bool onlyVerifyLeafCertificateCrl() const override { return false; }
  absl::optional<uint32_t> maxVerifyDepth() const override { return absl::nullopt; }

private:
  std::string s_;
  std::vector<std::string> strs_;
  std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher> matchers_;
};

TEST(DefaultCertValidatorTest, TestUnexpectedSanMatcherType) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto mock_context_config = std::make_unique<MockCertificateValidationContextConfig>();
  EXPECT_CALL(*mock_context_config.get(), trustChainVerification())
      .WillRepeatedly(testing::Return(envoy::extensions::transport_sockets::tls::v3::
                                          CertificateValidationContext::ACCEPT_UNTRUSTED));
  auto matchers =
      std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher>();
  Stats::TestUtil::TestStore store;
  auto ssl_stats = generateSslStats(*store.rootScope());
  auto validator =
      std::make_unique<DefaultCertValidator>(mock_context_config.get(), ssl_stats, context);
  auto ctx = std::vector<SSL_CTX*>();
  EXPECT_THAT(validator->initializeSslContexts(ctx, false).status().message(),
              testing::ContainsRegex("Failed to create string SAN matcher of type.*"));
}

TEST(DefaultCertValidatorTest, TestInitializeSslContextFailure) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto mock_context_config = std::make_unique<MockCertificateValidationContextConfig>(
      "-----BEGIN CERTIFICATE-----\nincomplete payload");
  EXPECT_CALL(*mock_context_config.get(), trustChainVerification())
      .WillRepeatedly(testing::Return(envoy::extensions::transport_sockets::tls::v3::
                                          CertificateValidationContext::ACCEPT_UNTRUSTED));

  Stats::TestUtil::TestStore store;
  auto ssl_stats = generateSslStats(*store.rootScope());
  auto validator =
      std::make_unique<DefaultCertValidator>(mock_context_config.get(), ssl_stats, context);
  auto ctx = std::vector<SSL_CTX*>();
  EXPECT_THAT(validator->initializeSslContexts(ctx, false).status().message(),
              testing::ContainsRegex("Failed to load trusted CA certificates from.*"));
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
