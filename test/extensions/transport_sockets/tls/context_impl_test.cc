#include <string>
#include <vector>

#include "envoy/admin/v3/certs.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.validate.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "common/json/json_loader.h"
#include "common/secret/sds_api.h"
#include "common/stats/isolated_store_impl.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"
#include "extensions/transport_sockets/tls/context_impl.h"
#include "extensions/transport_sockets/tls/utility.h"

#include "test/extensions/transport_sockets/tls/ssl_certs_test.h"
#include "test/extensions/transport_sockets/tls/ssl_test_utility.h"
#include "test/extensions/transport_sockets/tls/test_data/no_san_cert_info.h"
#include "test/extensions/transport_sockets/tls/test_data/san_dns3_cert_info.h"
#include "test/extensions/transport_sockets/tls/test_data/san_ip_cert_info.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "openssl/x509v3.h"

using Envoy::Protobuf::util::MessageDifferencer;
using testing::EndsWith;
using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class SslContextImplTest : public SslCertsTest {
protected:
  Event::SimulatedTimeSystem time_system_;
  ContextManagerImpl manager_{time_system_};
};

TEST_F(SslContextImplTest, TestDnsNameMatching) {
  EXPECT_TRUE(ContextImpl::dnsNameMatch("lyft.com", "lyft.com"));
  EXPECT_TRUE(ContextImpl::dnsNameMatch("a.lyft.com", "*.lyft.com"));
  EXPECT_FALSE(ContextImpl::dnsNameMatch("a.b.lyft.com", "*.lyft.com"));
  EXPECT_FALSE(ContextImpl::dnsNameMatch("foo.test.com", "*.lyft.com"));
  EXPECT_FALSE(ContextImpl::dnsNameMatch("lyft.com", "*.lyft.com"));
  EXPECT_FALSE(ContextImpl::dnsNameMatch("alyft.com", "*.lyft.com"));
  EXPECT_FALSE(ContextImpl::dnsNameMatch("alyft.com", "*lyft.com"));
  EXPECT_FALSE(ContextImpl::dnsNameMatch("lyft.com", "*lyft.com"));
  EXPECT_FALSE(ContextImpl::dnsNameMatch("", "*lyft.com"));
  EXPECT_FALSE(ContextImpl::dnsNameMatch("lyft.com", ""));
}

TEST_F(SslContextImplTest, TestDnsNameMatchingLegacy) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.fix_wildcard_matching", "false"}});
  EXPECT_TRUE(ContextImpl::dnsNameMatch("lyft.com", "lyft.com"));
  EXPECT_TRUE(ContextImpl::dnsNameMatch("a.lyft.com", "*.lyft.com"));
  // Legacy behavior
  EXPECT_TRUE(ContextImpl::dnsNameMatch("a.b.lyft.com", "*.lyft.com"));
  EXPECT_FALSE(ContextImpl::dnsNameMatch("foo.test.com", "*.lyft.com"));
  EXPECT_FALSE(ContextImpl::dnsNameMatch("lyft.com", "*.lyft.com"));
  EXPECT_FALSE(ContextImpl::dnsNameMatch("alyft.com", "*.lyft.com"));
  EXPECT_FALSE(ContextImpl::dnsNameMatch("alyft.com", "*lyft.com"));
  EXPECT_FALSE(ContextImpl::dnsNameMatch("lyft.com", "*lyft.com"));
  EXPECT_FALSE(ContextImpl::dnsNameMatch("", "*lyft.com"));
  EXPECT_FALSE(ContextImpl::dnsNameMatch("lyft.com", ""));
}

TEST_F(SslContextImplTest, TestVerifySubjectAltNameDNSMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  std::vector<std::string> verify_subject_alt_name_list = {"server1.example.com",
                                                           "server2.example.com"};
  EXPECT_TRUE(ContextImpl::verifySubjectAltName(cert.get(), verify_subject_alt_name_list));
}

TEST_F(SslContextImplTest, TestMatchSubjectAltNameDNSMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_hidden_envoy_deprecated_regex(".*.example.com");
  std::vector<Matchers::StringMatcherImpl> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(Matchers::StringMatcherImpl(matcher));
  EXPECT_TRUE(ContextImpl::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST_F(SslContextImplTest, TestMatchSubjectAltNameWildcardDNSMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_exact("api.example.com");
  std::vector<Matchers::StringMatcherImpl> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(Matchers::StringMatcherImpl(matcher));
  EXPECT_TRUE(ContextImpl::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST_F(SslContextImplTest, TestMultiLevelMatch) {
  // san_multiple_dns_cert matches *.example.com
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_exact("foo.api.example.com");
  std::vector<Matchers::StringMatcherImpl> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(Matchers::StringMatcherImpl(matcher));
  EXPECT_FALSE(ContextImpl::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST_F(SslContextImplTest, TestMultiLevelMatchLegacy) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.fix_wildcard_matching", "false"}});
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_exact("foo.api.example.com");
  std::vector<Matchers::StringMatcherImpl> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(Matchers::StringMatcherImpl(matcher));
  EXPECT_TRUE(ContextImpl::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST_F(SslContextImplTest, TestVerifySubjectAltNameURIMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"));
  std::vector<std::string> verify_subject_alt_name_list = {"spiffe://lyft.com/fake-team",
                                                           "spiffe://lyft.com/test-team"};
  EXPECT_TRUE(ContextImpl::verifySubjectAltName(cert.get(), verify_subject_alt_name_list));
}

TEST_F(SslContextImplTest, TestVerifySubjectAltMultiDomain) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem"));
  std::vector<std::string> verify_subject_alt_name_list = {"https://a.www.example.com"};
  EXPECT_FALSE(ContextImpl::verifySubjectAltName(cert.get(), verify_subject_alt_name_list));
}

TEST_F(SslContextImplTest, TestVerifySubjectAltMultiDomainLegacy) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.fix_wildcard_matching", "false"}});
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem"));
  std::vector<std::string> verify_subject_alt_name_list = {"https://a.www.example.com"};
  EXPECT_TRUE(ContextImpl::verifySubjectAltName(cert.get(), verify_subject_alt_name_list));
}

TEST_F(SslContextImplTest, TestMatchSubjectAltNameURIMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_hidden_envoy_deprecated_regex("spiffe://lyft.com/.*-team");
  std::vector<Matchers::StringMatcherImpl> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(Matchers::StringMatcherImpl(matcher));
  EXPECT_TRUE(ContextImpl::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST_F(SslContextImplTest, TestVerifySubjectAltNameNotMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  std::vector<std::string> verify_subject_alt_name_list = {"foo", "bar"};
  EXPECT_FALSE(ContextImpl::verifySubjectAltName(cert.get(), verify_subject_alt_name_list));
}

TEST_F(SslContextImplTest, TestMatchSubjectAltNameNotMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_hidden_envoy_deprecated_regex(".*.foo.com");
  std::vector<Matchers::StringMatcherImpl> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(Matchers::StringMatcherImpl(matcher));
  EXPECT_FALSE(ContextImpl::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST_F(SslContextImplTest, TestCipherSuites) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites: "-ALL:+[AES128-SHA|BOGUS1-SHA256]:BOGUS2-SHA:AES256-SHA"
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  ClientContextConfigImpl cfg(tls_context, factory_context_);
  EXPECT_THROW_WITH_MESSAGE(
      manager_.createSslClientContext(store_, cfg), EnvoyException,
      "Failed to initialize cipher suites "
      "-ALL:+[AES128-SHA|BOGUS1-SHA256]:BOGUS2-SHA:AES256-SHA. The following "
      "ciphers were rejected when tried individually: BOGUS1-SHA256, BOGUS2-SHA");
}

TEST_F(SslContextImplTest, TestExpiringCert) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
 )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);

  ClientContextConfigImpl cfg(tls_context, factory_context_);
  Envoy::Ssl::ClientContextSharedPtr context(manager_.createSslClientContext(store_, cfg));

  // This is a total hack, but right now we generate the cert and it expires in 15 days only in the
  // first second that it's valid. This can become invalid and then cause slower tests to fail.
  // Optimally we would make the cert valid for 15 days and 23 hours, but that is not easy to do
  // with the command line so we have this for now. Good enough.
  EXPECT_TRUE(15 == context->daysUntilFirstCertExpires() ||
              14 == context->daysUntilFirstCertExpires());
}

TEST_F(SslContextImplTest, TestExpiredCert) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/expired_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/expired_key.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  ClientContextConfigImpl cfg(tls_context, factory_context_);
  Envoy::Ssl::ClientContextSharedPtr context(manager_.createSslClientContext(store_, cfg));
  EXPECT_EQ(0U, context->daysUntilFirstCertExpires());
}

TEST_F(SslContextImplTest, TestGetCertInformation) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/no_san_cert.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  ClientContextConfigImpl cfg(tls_context, factory_context_);

  Envoy::Ssl::ClientContextSharedPtr context(manager_.createSslClientContext(store_, cfg));
  // This is similar to the hack above, but right now we generate the ca_cert and it expires in 15
  // days only in the first second that it's valid. We will partially match for up until Days until
  // Expiration: 1.
  // For the cert_chain, it is dynamically created when we run_envoy_test.sh which changes the
  // serial number with
  // every build. For cert_chain output, we check only for the certificate path.
  std::string ca_cert_json = absl::StrCat(R"EOF({
 "path": "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/no_san_cert.pem",
 "serial_number": ")EOF",
                                          TEST_NO_SAN_CERT_SERIAL, R"EOF(",
 "subject_alt_names": [],
 }
)EOF");

  std::string cert_chain_json = R"EOF({
 "path": "{{ test_tmpdir }}/unittestcert.pem",
 }
)EOF";

  std::string ca_cert_partial_output(TestEnvironment::substitute(ca_cert_json));
  std::string cert_chain_partial_output(TestEnvironment::substitute(cert_chain_json));
  envoy::admin::v3::CertificateDetails certificate_details, cert_chain_details;
  TestUtility::loadFromJson(ca_cert_partial_output, certificate_details);
  TestUtility::loadFromJson(cert_chain_partial_output, cert_chain_details);

  MessageDifferencer message_differencer;
  message_differencer.set_scope(MessageDifferencer::Scope::PARTIAL);
  EXPECT_TRUE(message_differencer.Compare(certificate_details, *context->getCaCertInformation()));
  EXPECT_TRUE(
      message_differencer.Compare(cert_chain_details, *context->getCertChainInformation()[0]));
}

TEST_F(SslContextImplTest, TestGetCertInformationWithSAN) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_chain.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_cert.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  ClientContextConfigImpl cfg(tls_context, factory_context_);

  Envoy::Ssl::ClientContextSharedPtr context(manager_.createSslClientContext(store_, cfg));
  std::string ca_cert_json = absl::StrCat(R"EOF({
 "path": "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_cert.pem",
 "serial_number": ")EOF",
                                          TEST_SAN_DNS3_CERT_SERIAL, R"EOF(",
 "subject_alt_names": [
  {
   "dns": "server1.example.com"
  }
 ]
 }
)EOF");

  std::string cert_chain_json = R"EOF({
 "path": "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_chain.pem",
 }
)EOF";

  // This is similar to the hack above, but right now we generate the ca_cert and it expires in 15
  // days only in the first second that it's valid. We will partially match for up until Days until
  // Expiration: 1.
  // For the cert_chain, it is dynamically created when we run_envoy_test.sh which changes the
  // serial number with
  // every build. For cert_chain output, we check only for the certificate path.
  std::string ca_cert_partial_output(TestEnvironment::substitute(ca_cert_json));
  std::string cert_chain_partial_output(TestEnvironment::substitute(cert_chain_json));
  envoy::admin::v3::CertificateDetails certificate_details, cert_chain_details;
  TestUtility::loadFromJson(ca_cert_partial_output, certificate_details);
  TestUtility::loadFromJson(cert_chain_partial_output, cert_chain_details);

  MessageDifferencer message_differencer;
  message_differencer.set_scope(MessageDifferencer::Scope::PARTIAL);
  EXPECT_TRUE(message_differencer.Compare(certificate_details, *context->getCaCertInformation()));
  EXPECT_TRUE(
      message_differencer.Compare(cert_chain_details, *context->getCertChainInformation()[0]));
}

TEST_F(SslContextImplTest, TestGetCertInformationWithIPSAN) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_ip_chain.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_ip_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_ip_cert.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  ClientContextConfigImpl cfg(tls_context, factory_context_);

  Envoy::Ssl::ClientContextSharedPtr context(manager_.createSslClientContext(store_, cfg));
  std::string ca_cert_json = absl::StrCat(R"EOF({
 "path": "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_ip_cert.pem",
 "serial_number": ")EOF",
                                          TEST_SAN_IP_CERT_SERIAL, R"EOF(",
 "subject_alt_names": [
  {
   "ip_address": "1.1.1.1"
  }
 ]
 }
)EOF");

  std::string cert_chain_json = R"EOF({
 "path": "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_ip_chain.pem",
 }
)EOF";

  // This is similar to the hack above, but right now we generate the ca_cert and it expires in 15
  // days only in the first second that it's valid. We will partially match for up until Days until
  // Expiration: 1.
  // For the cert_chain, it is dynamically created when we run_envoy_test.sh which changes the
  // serial number with
  // every build. For cert_chain output, we check only for the certificate path.
  std::string ca_cert_partial_output(TestEnvironment::substitute(ca_cert_json));
  std::string cert_chain_partial_output(TestEnvironment::substitute(cert_chain_json));
  envoy::admin::v3::CertificateDetails certificate_details, cert_chain_details;
  TestUtility::loadFromJson(ca_cert_partial_output, certificate_details);
  TestUtility::loadFromJson(cert_chain_partial_output, cert_chain_details);

  MessageDifferencer message_differencer;
  message_differencer.set_scope(MessageDifferencer::Scope::PARTIAL);
  EXPECT_TRUE(message_differencer.Compare(certificate_details, *context->getCaCertInformation()));
  EXPECT_TRUE(
      message_differencer.Compare(cert_chain_details, *context->getCertChainInformation()[0]));
}

std::string convertTimeCertInfoToCertDetails(std::string cert_info_time) {
  return TestUtility::convertTime(cert_info_time, "%b %e %H:%M:%S %Y GMT", "%Y-%m-%dT%H:%M:%SZ");
}

TEST_F(SslContextImplTest, TestGetCertInformationWithExpiration) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_chain.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_cert.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  ClientContextConfigImpl cfg(tls_context, factory_context_);

  Envoy::Ssl::ClientContextSharedPtr context(manager_.createSslClientContext(store_, cfg));
  std::string ca_cert_json =
      absl::StrCat(R"EOF({
 "path": "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns3_cert.pem",
 "serial_number": ")EOF",
                   TEST_SAN_DNS3_CERT_SERIAL, R"EOF(",
 "subject_alt_names": [
  {
   "dns": "server1.example.com"
  }
 ],
 "valid_from": ")EOF",
                   convertTimeCertInfoToCertDetails(TEST_SAN_DNS3_CERT_NOT_BEFORE), R"EOF(",
 "expiration_time": ")EOF",
                   convertTimeCertInfoToCertDetails(TEST_SAN_DNS3_CERT_NOT_AFTER), R"EOF("
 }
)EOF");

  const std::string ca_cert_partial_output(TestEnvironment::substitute(ca_cert_json));
  envoy::admin::v3::CertificateDetails certificate_details;
  TestUtility::loadFromJson(ca_cert_partial_output, certificate_details);

  MessageDifferencer message_differencer;
  message_differencer.set_scope(MessageDifferencer::Scope::PARTIAL);
  EXPECT_TRUE(message_differencer.Compare(certificate_details, *context->getCaCertInformation()));
}

TEST_F(SslContextImplTest, TestNoCert) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext config;
  ClientContextConfigImpl cfg(config, factory_context_);
  Envoy::Ssl::ClientContextSharedPtr context(manager_.createSslClientContext(store_, cfg));
  EXPECT_EQ(nullptr, context->getCaCertInformation());
  EXPECT_TRUE(context->getCertChainInformation().empty());
}

// Multiple RSA certificates are rejected.
TEST_F(SslContextImplTest, AtMostOneRsaCert) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned2_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  ServerContextConfigImpl server_context_config(tls_context, factory_context_);
  EXPECT_THROW_WITH_REGEX(manager_.createSslServerContext(store_, server_context_config, {}),
                          EnvoyException,
                          "at most one certificate of a given type may be specified");
}

// Multiple ECDSA certificates are rejected.
TEST_F(SslContextImplTest, AtMostOneEcdsaCert) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_ecdsa_p256_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_ecdsa_p256_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned2_ecdsa_p256_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_ecdsa_p256_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  ServerContextConfigImpl server_context_config(tls_context, factory_context_);
  EXPECT_THROW_WITH_REGEX(manager_.createSslServerContext(store_, server_context_config, {}),
                          EnvoyException,
                          "at most one certificate of a given type may be specified");
}

// Certificates with no subject CN and no SANs are rejected.
TEST_F(SslContextImplTest, MustHaveSubjectOrSAN) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/no_subject_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/no_subject_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  ServerContextConfigImpl server_context_config(tls_context, factory_context_);
  EXPECT_THROW_WITH_REGEX(manager_.createSslServerContext(store_, server_context_config, {}),
                          EnvoyException, "has neither subject CN nor SAN names");
}

class SslServerContextImplTicketTest : public SslContextImplTest {
public:
  void loadConfig(ServerContextConfigImpl& cfg) {
    Envoy::Ssl::ServerContextSharedPtr server_ctx(
        manager_.createSslServerContext(store_, cfg, std::vector<std::string>{}));
  }

  void loadConfigV2(envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext& cfg) {
    // Must add a certificate for the config to be considered valid.
    envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
        cfg.mutable_common_tls_context()->add_tls_certificates();
    server_cert->mutable_certificate_chain()->set_filename(
        TestEnvironment::substitute("{{ test_tmpdir }}/unittestcert.pem"));
    server_cert->mutable_private_key()->set_filename(
        TestEnvironment::substitute("{{ test_tmpdir }}/unittestkey.pem"));

    ServerContextConfigImpl server_context_config(cfg, factory_context_);
    loadConfig(server_context_config);
  }

  void loadConfigYaml(const std::string& yaml) {
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
    ServerContextConfigImpl cfg(tls_context, factory_context_);
    loadConfig(cfg);
  }
};

TEST_F(SslServerContextImplTicketTest, TicketKeySuccess) {
  // Both keys are valid; no error should be thrown
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
      filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_b"
)EOF";
  EXPECT_NO_THROW(loadConfigYaml(yaml));
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInvalidLen) {
  // First key is valid, second key isn't. Should throw if any keys are invalid.
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
      filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_wrong_len"
)EOF";
  EXPECT_THROW(loadConfigYaml(yaml), EnvoyException);
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInvalidCannotRead) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/this_file_does_not_exist"
)EOF";
  EXPECT_THROW(loadConfigYaml(yaml), std::exception);
}

TEST_F(SslServerContextImplTicketTest, TicketKeyNone) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext cfg;
  EXPECT_NO_THROW(loadConfigV2(cfg));
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineBytesSuccess) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_bytes(std::string(80, '\0'));
  EXPECT_NO_THROW(loadConfigV2(cfg));
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineStringSuccess) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_string(std::string(80, '\0'));
  EXPECT_NO_THROW(loadConfigV2(cfg));
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineBytesFailTooBig) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_bytes(std::string(81, '\0'));
  EXPECT_THROW(loadConfigV2(cfg), EnvoyException);
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineStringFailTooBig) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_string(std::string(81, '\0'));
  EXPECT_THROW(loadConfigV2(cfg), EnvoyException);
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineBytesFailTooSmall) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_bytes(std::string(79, '\0'));
  EXPECT_THROW(loadConfigV2(cfg), EnvoyException);
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineStringFailTooSmall) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_string(std::string(79, '\0'));
  EXPECT_THROW(loadConfigV2(cfg), EnvoyException);
}

TEST_F(SslServerContextImplTicketTest, TicketKeySdsNotReady) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"));
  server_cert->mutable_private_key()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"));

  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockRandomGenerator> random;
  Stats::IsolatedStoreImpl stats;
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  NiceMock<Init::MockManager> init_manager;
  EXPECT_CALL(factory_context_, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(factory_context_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher));
  // EXPECT_CALL(factory_context_, random()).WillOnce(ReturnRef(random));
  EXPECT_CALL(factory_context_, stats()).WillOnce(ReturnRef(stats));
  EXPECT_CALL(factory_context_, clusterManager()).WillOnce(ReturnRef(cluster_manager));
  EXPECT_CALL(factory_context_, initManager()).WillRepeatedly(ReturnRef(init_manager));
  auto* sds_secret_configs = tls_context.mutable_session_ticket_keys_sds_secret_config();
  sds_secret_configs->set_name("abc.com");
  sds_secret_configs->mutable_sds_config();
  ServerContextConfigImpl server_context_config(tls_context, factory_context_);
  // When sds secret is not downloaded, config is not ready.
  EXPECT_FALSE(server_context_config.isReady());
  // Set various callbacks to config.
  NiceMock<Secret::MockSecretCallbacks> secret_callback;
  server_context_config.setSecretUpdateCallback(
      [&secret_callback]() { secret_callback.onAddOrUpdateSecret(); });
  server_context_config.setSecretUpdateCallback([]() {});
}

// Validate that client context config with static TLS ticket encryption keys is created
// successfully.
TEST_F(SslServerContextImplTicketTest, StaticTickeyKey) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;

  const std::string yaml = R"EOF(
name: "abc.com"
session_ticket_keys:
  keys:
    - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
    - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_b"
)EOF";

  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);
  factory_context_.secretManager().addStaticSecret(secret_config);

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"));
  server_cert->mutable_private_key()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"));

  tls_context.mutable_session_ticket_keys_sds_secret_config()->set_name("abc.com");

  ServerContextConfigImpl server_context_config(tls_context, factory_context_);

  EXPECT_TRUE(server_context_config.isReady());
  ASSERT_EQ(server_context_config.sessionTicketKeys().size(), 2);
}

TEST_F(SslServerContextImplTicketTest, CRLSuccess) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.crl"
)EOF";
  EXPECT_NO_THROW(loadConfigYaml(yaml));
}

TEST_F(SslServerContextImplTicketTest, CRLInvalid) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/not_a_crl.crl"
)EOF";
  EXPECT_THROW_WITH_REGEX(loadConfigYaml(yaml), EnvoyException,
                          "^Failed to load CRL from .*/not_a_crl.crl$");
}

TEST_F(SslServerContextImplTicketTest, CRLWithNoCA) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem"
    validation_context:
      crl:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/not_a_crl.crl"
)EOF";
  EXPECT_THROW_WITH_REGEX(loadConfigYaml(yaml), EnvoyException,
                          "^Failed to load CRL from .* without trusted CA$");
}

TEST_F(SslServerContextImplTicketTest, VerifySanWithNoCA) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_key.pem"
    validation_context:
      verify_subject_alt_name: "spiffe://lyft.com/testclient"
)EOF";
  EXPECT_THROW_WITH_MESSAGE(loadConfigYaml(yaml), EnvoyException,
                            "SAN-based verification of peer certificates without trusted CA "
                            "is insecure and not allowed");
}

TEST_F(SslServerContextImplTicketTest, StatelessSessionResumptionEnabledByDefault) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);

  ServerContextConfigImpl server_context_config(tls_context, factory_context_);
  EXPECT_FALSE(server_context_config.disableStatelessSessionResumption());
}

TEST_F(SslServerContextImplTicketTest, StatelessSessionResumptionExplicitlyEnabled) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
  disable_stateless_session_resumption: false
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);

  ServerContextConfigImpl server_context_config(tls_context, factory_context_);
  EXPECT_FALSE(server_context_config.disableStatelessSessionResumption());
}

TEST_F(SslServerContextImplTicketTest, StatelessSessionResumptionDisabled) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
  disable_stateless_session_resumption: true
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);

  ServerContextConfigImpl server_context_config(tls_context, factory_context_);
  EXPECT_TRUE(server_context_config.disableStatelessSessionResumption());
}

TEST_F(SslServerContextImplTicketTest, StatelessSessionResumptionEnabledWhenKeyIsConfigured) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
)EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);

  ServerContextConfigImpl server_context_config(tls_context, factory_context_);
  EXPECT_FALSE(server_context_config.disableStatelessSessionResumption());
}

class ClientContextConfigImplTest : public SslCertsTest {};

// Validate that empty SNI (according to C string rules) fails config validation.
TEST_F(ClientContextConfigImplTest, EmptyServerNameIndication) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;

  tls_context.set_sni(std::string("\000", 1));
  EXPECT_THROW_WITH_MESSAGE(
      ClientContextConfigImpl client_context_config(tls_context, factory_context), EnvoyException,
      "SNI names containing NULL-byte are not allowed");
  tls_context.set_sni(std::string("a\000b", 3));
  EXPECT_THROW_WITH_MESSAGE(
      ClientContextConfigImpl client_context_config(tls_context, factory_context), EnvoyException,
      "SNI names containing NULL-byte are not allowed");
}

// Validate that values other than a hex-encoded SHA-256 fail config validation.
TEST_F(ClientContextConfigImplTest, InvalidCertificateHash) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
  tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      // This is valid hex-encoded string, but it doesn't represent SHA-256 (80 vs 64 chars).
      ->add_verify_certificate_hash("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  ClientContextConfigImpl client_context_config(tls_context, factory_context);
  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(time_system);
  Stats::IsolatedStoreImpl store;
  EXPECT_THROW_WITH_REGEX(manager.createSslClientContext(store, client_context_config),
                          EnvoyException, "Invalid hex-encoded SHA-256 .*");
}

// Validate that values other than a base64-encoded SHA-256 fail config validation.
TEST_F(ClientContextConfigImplTest, InvalidCertificateSpki) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
  tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      // Not a base64-encoded string.
      ->add_verify_certificate_spki("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  ClientContextConfigImpl client_context_config(tls_context, factory_context);
  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(time_system);
  Stats::IsolatedStoreImpl store;
  EXPECT_THROW_WITH_REGEX(manager.createSslClientContext(store, client_context_config),
                          EnvoyException, "Invalid base64-encoded SHA-256 .*");
}

// Validate that 2048-bit RSA certificates load successfully.
TEST_F(ClientContextConfigImplTest, RSA2048Cert) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  ClientContextConfigImpl client_context_config(tls_context, factory_context_);
  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(time_system);
  Stats::IsolatedStoreImpl store;
  manager.createSslClientContext(store, client_context_config);
}

// Validate that 1024-bit RSA certificates are rejected.
TEST_F(ClientContextConfigImplTest, RSA1024Cert) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_rsa_1024_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_rsa_1024_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  ClientContextConfigImpl client_context_config(tls_context, factory_context_);
  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(time_system);
  Stats::IsolatedStoreImpl store;

  std::string error_msg(
      "Failed to load certificate chain from .*selfsigned_rsa_1024_cert.pem, only RSA certificates "
#ifdef BORINGSSL_FIPS
      "with 2048-bit or 3072-bit keys are supported in FIPS mode"
#else
      "with 2048-bit or larger keys are supported"
#endif
  );
  EXPECT_THROW_WITH_REGEX(manager.createSslClientContext(store, client_context_config),
                          EnvoyException, error_msg);
}

// Validate that 3072-bit RSA certificates load successfully.
TEST_F(ClientContextConfigImplTest, RSA3072Cert) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_rsa_3072_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_rsa_3072_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  ClientContextConfigImpl client_context_config(tls_context, factory_context_);
  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(time_system);
  Stats::IsolatedStoreImpl store;
  manager.createSslClientContext(store, client_context_config);
}

// Validate that 4096-bit RSA certificates load successfully in non-FIPS builds, but are rejected
// in FIPS builds.
TEST_F(ClientContextConfigImplTest, RSA4096Cert) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_rsa_4096_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_rsa_4096_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  ClientContextConfigImpl client_context_config(tls_context, factory_context_);
  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(time_system);
  Stats::IsolatedStoreImpl store;
#ifdef BORINGSSL_FIPS
  EXPECT_THROW_WITH_REGEX(
      manager.createSslClientContext(store, client_context_config), EnvoyException,
      "Failed to load certificate chain from .*selfsigned_rsa_4096_cert.pem, only RSA certificates "
      "with 2048-bit or 3072-bit keys are supported in FIPS mode");
#else
  manager.createSslClientContext(store, client_context_config);
#endif
}

// Validate that P256 ECDSA certs load.
TEST_F(ClientContextConfigImplTest, P256EcdsaCert) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_ecdsa_p256_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_ecdsa_p256_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  ClientContextConfigImpl client_context_config(tls_context, factory_context_);
  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(time_system);
  Stats::IsolatedStoreImpl store;
  manager.createSslClientContext(store, client_context_config);
}

// Validate that non-P256 ECDSA certs are rejected.
TEST_F(ClientContextConfigImplTest, NonP256EcdsaCert) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_ecdsa_p384_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_ecdsa_p384_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  ClientContextConfigImpl client_context_config(tls_context, factory_context_);
  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(time_system);
  Stats::IsolatedStoreImpl store;
  EXPECT_THROW_WITH_REGEX(manager.createSslClientContext(store, client_context_config),
                          EnvoyException,
                          "Failed to load certificate chain from .*selfsigned_ecdsa_p384_cert.pem, "
                          "only P-256 ECDSA certificates are supported");
}

// Multiple TLS certificates are not yet supported.
// TODO(PiotrSikora): Support multiple TLS certificates.
TEST_F(ClientContextConfigImplTest, MultipleTlsCertificates) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  EXPECT_THROW_WITH_MESSAGE(
      ClientContextConfigImpl client_context_config(tls_context, factory_context_), EnvoyException,
      "Multiple TLS certificates are not supported for client contexts");
}

// Validate context config does not support handling both static TLS certificate and dynamic TLS
// certificate.
TEST_F(ClientContextConfigImplTest, TlsCertificatesAndSdsConfig) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs();
  EXPECT_THROW_WITH_MESSAGE(
      ClientContextConfigImpl client_context_config(tls_context, factory_context_), EnvoyException,
      "Multiple TLS certificates are not supported for client contexts");
}

// Validate context config supports SDS, and is marked as not ready if secrets are not yet
// downloaded.
TEST_F(ClientContextConfigImplTest, SecretNotReady) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  Stats::IsolatedStoreImpl stats;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(factory_context_, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(factory_context_, stats()).WillOnce(ReturnRef(stats));
  EXPECT_CALL(factory_context_, initManager()).WillRepeatedly(ReturnRef(init_manager));
  EXPECT_CALL(factory_context_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher));
  auto sds_secret_configs =
      tls_context.mutable_common_tls_context()->mutable_tls_certificate_sds_secret_configs()->Add();
  sds_secret_configs->set_name("abc.com");
  sds_secret_configs->mutable_sds_config();
  ClientContextConfigImpl client_context_config(tls_context, factory_context_);
  // When sds secret is not downloaded, config is not ready.
  EXPECT_FALSE(client_context_config.isReady());
  // Set various callbacks to config.
  NiceMock<Secret::MockSecretCallbacks> secret_callback;
  client_context_config.setSecretUpdateCallback(
      [&secret_callback]() { secret_callback.onAddOrUpdateSecret(); });
  client_context_config.setSecretUpdateCallback([]() {});
}

// Validate client context config supports SDS, and is marked as not ready if dynamic
// certificate validation context is not yet downloaded.
TEST_F(ClientContextConfigImplTest, ValidationContextNotReady) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"));
  client_cert->mutable_private_key()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  Stats::IsolatedStoreImpl stats;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(factory_context_, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(factory_context_, stats()).WillOnce(ReturnRef(stats));
  EXPECT_CALL(factory_context_, initManager()).WillRepeatedly(ReturnRef(init_manager));
  EXPECT_CALL(factory_context_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher));
  auto sds_secret_configs =
      tls_context.mutable_common_tls_context()->mutable_validation_context_sds_secret_config();
  sds_secret_configs->set_name("abc.com");
  sds_secret_configs->mutable_sds_config();
  ClientContextConfigImpl client_context_config(tls_context, factory_context_);
  // When sds secret is not downloaded, config is not ready.
  EXPECT_FALSE(client_context_config.isReady());
  // Set various callbacks to config.
  NiceMock<Secret::MockSecretCallbacks> secret_callback;
  client_context_config.setSecretUpdateCallback(
      [&secret_callback]() { secret_callback.onAddOrUpdateSecret(); });
  client_context_config.setSecretUpdateCallback([]() {});
}

// Validate that client context config with static TLS certificates is created successfully.
TEST_F(ClientContextConfigImplTest, StaticTlsCertificates) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;

  const std::string yaml = R"EOF(
name: "abc.com"
tls_certificate:
  certificate_chain:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"
)EOF";

  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_sds_secret_configs()
      ->Add()
      ->set_name("abc.com");

  factory_context_.secretManager().addStaticSecret(secret_config);
  ClientContextConfigImpl client_context_config(tls_context, factory_context_);

  const std::string cert_pem =
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            client_context_config.tlsCertificates()[0].get().certificateChain());
  const std::string key_pem =
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(key_pem)),
            client_context_config.tlsCertificates()[0].get().privateKey());
}

// Validate that client context config with password-protected TLS certificates is created
// successfully.
TEST_F(ClientContextConfigImplTest, PasswordProtectedTlsCertificates) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;
  secret_config.set_name("abc.com");

  auto* tls_certificate = secret_config.mutable_tls_certificate();
  tls_certificate->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/password_protected_cert.pem"));
  tls_certificate->mutable_private_key()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/password_protected_key.pem"));
  tls_certificate->mutable_password()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/password_protected_password.txt"));

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_sds_secret_configs()
      ->Add()
      ->set_name("abc.com");

  factory_context_.secretManager().addStaticSecret(secret_config);
  ClientContextConfigImpl client_context_config(tls_context, factory_context_);

  const std::string cert_pem =
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/password_protected_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            client_context_config.tlsCertificates()[0].get().certificateChain());
  const std::string key_pem =
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/password_protected_key.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(key_pem)),
            client_context_config.tlsCertificates()[0].get().privateKey());
  const std::string password_file =
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/password_protected_password.txt";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(password_file)),
            client_context_config.tlsCertificates()[0].get().password());
}

// Validate that not supplying a passphrase for password-protected TLS certificates
// triggers a failure.
TEST_F(ClientContextConfigImplTest, PasswordNotSuppliedTlsCertificates) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;
  secret_config.set_name("abc.com");

  auto* tls_certificate = secret_config.mutable_tls_certificate();
  tls_certificate->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/password_protected_cert.pem"));
  const std::string private_key_path = TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/password_protected_key.pem");
  tls_certificate->mutable_private_key()->set_filename(private_key_path);
  // Don't supply the password.

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_sds_secret_configs()
      ->Add()
      ->set_name("abc.com");

  factory_context_.secretManager().addStaticSecret(secret_config);
  ClientContextConfigImpl client_context_config(tls_context, factory_context_);

  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(time_system);
  Stats::IsolatedStoreImpl store;
  EXPECT_THROW_WITH_REGEX(manager.createSslClientContext(store, client_context_config),
                          EnvoyException,
                          absl::StrCat("Failed to load private key from ", private_key_path));
}

// Validate that client context config with static certificate validation context is created
// successfully.
TEST_F(ClientContextConfigImplTest, StaticCertificateValidationContext) {
  envoy::extensions::transport_sockets::tls::v3::Secret tls_certificate_secret_config;
  const std::string tls_certificate_yaml = R"EOF(
  name: "abc.com"
  tls_certificate:
    certificate_chain:
      filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
    private_key:
      filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            tls_certificate_secret_config);
  factory_context_.secretManager().addStaticSecret(tls_certificate_secret_config);
  envoy::extensions::transport_sockets::tls::v3::Secret
      certificate_validation_context_secret_config;
  const std::string certificate_validation_context_yaml = R"EOF(
    name: "def.com"
    validation_context:
      trusted_ca: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem" }
      allow_expired_certificate: true
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(certificate_validation_context_yaml),
                            certificate_validation_context_secret_config);
  factory_context_.secretManager().addStaticSecret(certificate_validation_context_secret_config);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_sds_secret_configs()
      ->Add()
      ->set_name("abc.com");
  tls_context.mutable_common_tls_context()
      ->mutable_validation_context_sds_secret_config()
      ->set_name("def.com");
  ClientContextConfigImpl client_context_config(tls_context, factory_context_);

  const std::string cert_pem =
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            client_context_config.certificateValidationContext()->caCert());
}

// Validate that constructor of client context config throws an exception when static TLS
// certificate is missing.
TEST_F(ClientContextConfigImplTest, MissingStaticSecretTlsCertificates) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;

  const std::string yaml = R"EOF(
name: "abc.com"
tls_certificate:
  certificate_chain:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"
)EOF";

  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);

  factory_context_.secretManager().addStaticSecret(secret_config);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_sds_secret_configs()
      ->Add()
      ->set_name("missing");

  EXPECT_THROW_WITH_MESSAGE(
      ClientContextConfigImpl client_context_config(tls_context, factory_context_), EnvoyException,
      "Unknown static secret: missing");
}

// Validate that constructor of client context config throws an exception when static certificate
// validation context is missing.
TEST_F(ClientContextConfigImplTest, MissingStaticCertificateValidationContext) {
  envoy::extensions::transport_sockets::tls::v3::Secret tls_certificate_secret_config;
  const std::string tls_certificate_yaml = R"EOF(
    name: "abc.com"
    tls_certificate:
      certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"
    )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            tls_certificate_secret_config);
  factory_context_.secretManager().addStaticSecret(tls_certificate_secret_config);
  envoy::extensions::transport_sockets::tls::v3::Secret
      certificate_validation_context_secret_config;
  const std::string certificate_validation_context_yaml = R"EOF(
      name: "def.com"
      validation_context:
        trusted_ca: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem" }
        allow_expired_certificate: true
    )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(certificate_validation_context_yaml),
                            certificate_validation_context_secret_config);
  factory_context_.secretManager().addStaticSecret(certificate_validation_context_secret_config);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_sds_secret_configs()
      ->Add()
      ->set_name("abc.com");
  tls_context.mutable_common_tls_context()
      ->mutable_validation_context_sds_secret_config()
      ->set_name("missing");
  EXPECT_THROW_WITH_MESSAGE(
      ClientContextConfigImpl client_context_config(tls_context, factory_context_), EnvoyException,
      "Unknown static certificate validation context: missing");
}

class ServerContextConfigImplTest : public SslCertsTest {};

// Multiple TLS certificates are supported.
TEST_F(ServerContextConfigImplTest, MultipleTlsCertificates) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  EXPECT_THROW_WITH_MESSAGE(
      ServerContextConfigImpl client_context_config(tls_context, factory_context_), EnvoyException,
      "No TLS certificates found for server context");
  const std::string rsa_tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"
  )EOF";
  const std::string ecdsa_tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_ecdsa_p256_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_ecdsa_p256_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(rsa_tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  TestUtility::loadFromYaml(TestEnvironment::substitute(ecdsa_tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  ServerContextConfigImpl server_context_config(tls_context, factory_context_);
  auto tls_certs = server_context_config.tlsCertificates();
  ASSERT_EQ(2, tls_certs.size());
  EXPECT_THAT(tls_certs[0].get().privateKeyPath(), EndsWith("selfsigned_key.pem"));
  EXPECT_THAT(tls_certs[1].get().privateKeyPath(), EndsWith("selfsigned_ecdsa_p256_key.pem"));
}

TEST_F(ServerContextConfigImplTest, TlsCertificatesAndSdsConfig) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  EXPECT_THROW_WITH_MESSAGE(
      ServerContextConfigImpl server_context_config(tls_context, factory_context_), EnvoyException,
      "No TLS certificates found for server context");
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs();
  EXPECT_THROW_WITH_MESSAGE(
      ServerContextConfigImpl server_context_config(tls_context, factory_context_), EnvoyException,
      "SDS and non-SDS TLS certificates may not be mixed in server contexts");
}

TEST_F(ServerContextConfigImplTest, MultiSdsConfig) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs();
  tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs();
  EXPECT_THROW_WITH_REGEX(
      TestUtility::validate<envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext>(
          tls_context),
      EnvoyException, "Proto constraint validation failed");
}

TEST_F(ServerContextConfigImplTest, SecretNotReady) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  Stats::IsolatedStoreImpl stats;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(factory_context_, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(factory_context_, stats()).WillOnce(ReturnRef(stats));
  EXPECT_CALL(factory_context_, initManager()).WillRepeatedly(ReturnRef(init_manager));
  EXPECT_CALL(factory_context_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher));
  auto sds_secret_configs =
      tls_context.mutable_common_tls_context()->mutable_tls_certificate_sds_secret_configs()->Add();
  sds_secret_configs->set_name("abc.com");
  sds_secret_configs->mutable_sds_config();
  ServerContextConfigImpl server_context_config(tls_context, factory_context_);
  // When sds secret is not downloaded, config is not ready.
  EXPECT_FALSE(server_context_config.isReady());
  // Set various callbacks to config.
  NiceMock<Secret::MockSecretCallbacks> secret_callback;
  server_context_config.setSecretUpdateCallback(
      [&secret_callback]() { secret_callback.onAddOrUpdateSecret(); });
  server_context_config.setSecretUpdateCallback([]() {});
}

// Validate server context config supports SDS, and is marked as not ready if dynamic
// certificate validation context is not yet downloaded.
TEST_F(ServerContextConfigImplTest, ValidationContextNotReady) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"));
  server_cert->mutable_private_key()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  Stats::IsolatedStoreImpl stats;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(factory_context_, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(factory_context_, stats()).WillOnce(ReturnRef(stats));
  EXPECT_CALL(factory_context_, initManager()).WillRepeatedly(ReturnRef(init_manager));
  EXPECT_CALL(factory_context_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher));
  auto sds_secret_configs =
      tls_context.mutable_common_tls_context()->mutable_validation_context_sds_secret_config();
  sds_secret_configs->set_name("abc.com");
  sds_secret_configs->mutable_sds_config();
  ServerContextConfigImpl server_context_config(tls_context, factory_context_);
  // When sds secret is not downloaded, config is not ready.
  EXPECT_FALSE(server_context_config.isReady());
  // Set various callbacks to config.
  NiceMock<Secret::MockSecretCallbacks> secret_callback;
  server_context_config.setSecretUpdateCallback(
      [&secret_callback]() { secret_callback.onAddOrUpdateSecret(); });
  server_context_config.setSecretUpdateCallback([]() {});
}

// TlsCertificate messages must have a cert for servers.
TEST_F(ServerContextConfigImplTest, TlsCertificateNonEmpty) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()->add_tls_certificates();
  ServerContextConfigImpl client_context_config(tls_context, factory_context_);
  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(time_system);
  Stats::IsolatedStoreImpl store;
  EXPECT_THROW_WITH_MESSAGE(
      Envoy::Ssl::ServerContextSharedPtr server_ctx(
          manager.createSslServerContext(store, client_context_config, std::vector<std::string>{})),
      EnvoyException, "Server TlsCertificates must have a certificate specified");
}

// Cannot ignore certificate expiration without a trusted CA.
TEST_F(ServerContextConfigImplTest, InvalidIgnoreCertsNoCA) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;

  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();

  server_validation_ctx->set_allow_expired_certificate(true);

  EXPECT_THROW_WITH_MESSAGE(
      ServerContextConfigImpl server_context_config(tls_context, factory_context_), EnvoyException,
      "Certificate validity period is always ignored without trusted CA");

  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_tmpdir }}/unittestcert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_tmpdir }}/unittestkey.pem"));

  server_validation_ctx->set_allow_expired_certificate(false);

  EXPECT_NO_THROW(ServerContextConfigImpl server_context_config(tls_context, factory_context_));

  server_validation_ctx->set_allow_expired_certificate(true);

  EXPECT_THROW_WITH_MESSAGE(
      ServerContextConfigImpl server_context_config(tls_context, factory_context_), EnvoyException,
      "Certificate validity period is always ignored without trusted CA");

  // But once you add a trusted CA, you should be able to create the context.
  server_validation_ctx->mutable_trusted_ca()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"));

  EXPECT_NO_THROW(ServerContextConfigImpl server_context_config(tls_context, factory_context_));
}

TEST_F(ServerContextConfigImplTest, PrivateKeyMethodLoadFailureNoProvider) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  NiceMock<Ssl::MockContextManager> context_manager;
  NiceMock<Ssl::MockPrivateKeyMethodManager> private_key_method_manager;
  EXPECT_CALL(factory_context_, sslContextManager()).WillOnce(ReturnRef(context_manager));
  EXPECT_CALL(context_manager, privateKeyMethodManager())
      .WillOnce(ReturnRef(private_key_method_manager));
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
      private_key_provider:
        provider_name: mock_provider
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            test_value: 100
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  EXPECT_THROW_WITH_REGEX(
      ServerContextConfigImpl server_context_config(tls_context, factory_context_), EnvoyException,
      "Failed to load incomplete certificate from ");
}

TEST_F(ServerContextConfigImplTest, PrivateKeyMethodLoadFailureNoMethod) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()->add_tls_certificates();
  Stats::IsolatedStoreImpl store;
  NiceMock<Ssl::MockContextManager> context_manager;
  NiceMock<Ssl::MockPrivateKeyMethodManager> private_key_method_manager;
  auto private_key_method_provider_ptr =
      std::make_shared<NiceMock<Ssl::MockPrivateKeyMethodProvider>>();
  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(time_system);
  EXPECT_CALL(factory_context_, sslContextManager()).WillOnce(ReturnRef(context_manager));
  EXPECT_CALL(context_manager, privateKeyMethodManager())
      .WillOnce(ReturnRef(private_key_method_manager));
  EXPECT_CALL(private_key_method_manager, createPrivateKeyMethodProvider(_, _))
      .WillOnce(Return(private_key_method_provider_ptr));
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
      private_key_provider:
        provider_name: mock_provider
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            test_value: 100
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  ServerContextConfigImpl server_context_config(tls_context, factory_context_);
  EXPECT_THROW_WITH_MESSAGE(
      Envoy::Ssl::ServerContextSharedPtr server_ctx(
          manager.createSslServerContext(store, server_context_config, std::vector<std::string>{})),
      EnvoyException, "Failed to get BoringSSL private key method from provider");
}

TEST_F(ServerContextConfigImplTest, PrivateKeyMethodLoadSuccess) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  NiceMock<Ssl::MockContextManager> context_manager;
  NiceMock<Ssl::MockPrivateKeyMethodManager> private_key_method_manager;
  auto private_key_method_provider_ptr =
      std::make_shared<NiceMock<Ssl::MockPrivateKeyMethodProvider>>();
  EXPECT_CALL(factory_context_, sslContextManager()).WillOnce(ReturnRef(context_manager));
  EXPECT_CALL(context_manager, privateKeyMethodManager())
      .WillOnce(ReturnRef(private_key_method_manager));
  EXPECT_CALL(private_key_method_manager, createPrivateKeyMethodProvider(_, _))
      .WillOnce(Return(private_key_method_provider_ptr));
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
      private_key_provider:
        provider_name: mock_provider
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            test_value: 100
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  ServerContextConfigImpl server_context_config(tls_context, factory_context_);
}

TEST_F(ServerContextConfigImplTest, PrivateKeyMethodLoadFailureBothKeyAndMethod) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  NiceMock<Ssl::MockContextManager> context_manager;
  NiceMock<Ssl::MockPrivateKeyMethodManager> private_key_method_manager;
  auto private_key_method_provider_ptr =
      std::make_shared<NiceMock<Ssl::MockPrivateKeyMethodProvider>>();
  EXPECT_CALL(factory_context_, sslContextManager()).WillOnce(ReturnRef(context_manager));
  EXPECT_CALL(context_manager, privateKeyMethodManager())
      .WillOnce(ReturnRef(private_key_method_manager));
  EXPECT_CALL(private_key_method_manager, createPrivateKeyMethodProvider(_, _))
      .WillOnce(Return(private_key_method_provider_ptr));
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"
      private_key_provider:
        provider_name: mock_provider
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            test_value: 100
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  EXPECT_THROW_WITH_MESSAGE(
      ServerContextConfigImpl server_context_config(tls_context, factory_context_), EnvoyException,
      "Certificate configuration can't have both private_key and private_key_provider");
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
