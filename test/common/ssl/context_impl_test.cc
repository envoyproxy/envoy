#include "common/json/json_loader.h"
#include "common/ssl/context_config_impl.h"
#include "common/ssl/context_impl.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/runtime/mocks.h"

namespace Ssl {

TEST(SslContextImplTest, TestdNSNameMatching) {
  EXPECT_TRUE(ContextImpl::dNSNameMatch("lyft.com", "lyft.com"));
  EXPECT_TRUE(ContextImpl::dNSNameMatch("a.lyft.com", "*.lyft.com"));
  EXPECT_TRUE(ContextImpl::dNSNameMatch("a.b.lyft.com", "*.lyft.com"));
  EXPECT_FALSE(ContextImpl::dNSNameMatch("foo.test.com", "*.lyft.com"));
  EXPECT_FALSE(ContextImpl::dNSNameMatch("lyft.com", "*.lyft.com"));
  EXPECT_FALSE(ContextImpl::dNSNameMatch("alyft.com", "*.lyft.com"));
  EXPECT_FALSE(ContextImpl::dNSNameMatch("alyft.com", "*lyft.com"));
  EXPECT_FALSE(ContextImpl::dNSNameMatch("lyft.com", "*lyft.com"));
  EXPECT_FALSE(ContextImpl::dNSNameMatch("", "*lyft.com"));
  EXPECT_FALSE(ContextImpl::dNSNameMatch("lyft.com", ""));
}

TEST(SslContextImplTest, TestVerifySubjectAltNameDNSMatched) {
  FILE* fp = fopen("test/common/ssl/test_data/san_dns.crt", "r");
  EXPECT_TRUE(fp != nullptr);
  X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
  EXPECT_TRUE(cert != nullptr);
  std::vector<std::string> verify_subject_alt_name_list = {"foo.com", "test.com"};
  EXPECT_TRUE(ContextImpl::verifySubjectAltName(cert, verify_subject_alt_name_list));
  X509_free(cert);
  fclose(fp);
}

TEST(SslContextImplTest, TestVerifySubjectAltNameURIMatched) {
  FILE* fp = fopen("test/common/ssl/test_data/san_uri.crt", "r");
  EXPECT_TRUE(fp != nullptr);
  X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
  EXPECT_TRUE(cert != nullptr);
  std::vector<std::string> verify_subject_alt_name_list = {"istio:account.test.com",
                                                           "istio:account2.test.com"};
  EXPECT_TRUE(ContextImpl::verifySubjectAltName(cert, verify_subject_alt_name_list));
  X509_free(cert);
  fclose(fp);
}

TEST(SslContextImplTest, TestVerifySubjectAltNameNotMatched) {
  FILE* fp = fopen("test/common/ssl/test_data/san_dns.crt", "r");
  EXPECT_TRUE(fp != nullptr);
  X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
  EXPECT_TRUE(cert != nullptr);
  std::vector<std::string> verify_subject_alt_name_list = {"foo", "bar"};
  EXPECT_FALSE(ContextImpl::verifySubjectAltName(cert, verify_subject_alt_name_list));
  X509_free(cert);
  fclose(fp);
}

TEST(SslContextImplTest, TestCipherSuites) {
  std::string json = R"EOF(
  {
    "cipher_suites": "AES128-SHA:BOGUS:AES256-SHA"
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  ContextConfigImpl cfg(*loader);
  Runtime::MockLoader runtime;
  ContextManagerImpl manager(runtime);
  Stats::IsolatedStoreImpl store;
  EXPECT_THROW(manager.createSslClientContext(store, cfg), EnvoyException);
}

TEST(SslContextImplTest, TestExpiringCert) {
  std::string json = R"EOF(
  {
      "cert_chain_file": "/tmp/envoy_test/unittestcert.pem",
      "private_key_file": "/tmp/envoy_test/unittestkey.pem"
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  ContextConfigImpl cfg(*loader);
  Runtime::MockLoader runtime;
  ContextManagerImpl manager(runtime);
  Stats::IsolatedStoreImpl store;
  ClientContextPtr context(manager.createSslClientContext(store, cfg));

  // This is a total hack, but right now we generate the cert and it expires in 15 days only in the
  // first second that it's valid. This can become invalid and then cause slower tests to fail.
  // Optimally we would make the cert valid for 15 days and 23 hours, but that is not easy to do
  // with the command line so we have this for now. Good enough.
  EXPECT_TRUE(15 == context->daysUntilFirstCertExpires() ||
              14 == context->daysUntilFirstCertExpires());
}

TEST(SslContextImplTest, TestExpiredCert) {
  std::string json = R"EOF(
  {
      "cert_chain_file": "/tmp/envoy_test/unittestcert_expired.pem",
      "private_key_file": "/tmp/envoy_test/unittestkey_expired.pem"
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  ContextConfigImpl cfg(*loader);
  Runtime::MockLoader runtime;
  ContextManagerImpl manager(runtime);
  Stats::IsolatedStoreImpl store;
  ClientContextPtr context(manager.createSslClientContext(store, cfg));
  EXPECT_EQ(0U, context->daysUntilFirstCertExpires());
}

TEST(SslContextImplTest, TestGetCertInformation) {
  std::string json = R"EOF(
  {
    "cert_chain_file": "/tmp/envoy_test/unittestcert.pem",
    "private_key_file": "/tmp/envoy_test/unittestkey.pem",
    "ca_cert_file": "test/common/ssl/test_data/ca.crt"
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  ContextConfigImpl cfg(*loader);
  Runtime::MockLoader runtime;
  ContextManagerImpl manager(runtime);
  Stats::IsolatedStoreImpl store;

  ClientContextPtr context(manager.createSslClientContext(store, cfg));
  // This is similar to the hack above, but right now we generate the ca_cert and it expires in 15
  // days only in the first second that it's valid. We will partially match for up until Days until
  // Expiration: 1.
  // For the cert_chain, it is dynamically created when we run_envoy_test.sh which changes the
  // serial number with
  // every build. For cert_chain output, we check only for the certificate path.
  std::string ca_cert_partial_output(
      "Certificate Path: test/common/ssl/test_data/ca.crt, Serial Number: F0DE921A0515EB45, "
      "Days until Expiration: ");
  std::string cert_chain_partial_output("Certificate Path: /tmp/envoy_test/unittestcert.pem");

  EXPECT_TRUE(context->getCaCertInformation().find(ca_cert_partial_output) != std::string::npos);
  EXPECT_TRUE(context->getCertChainInformation().find(cert_chain_partial_output) !=
              std::string::npos);
}

TEST(SslContextImplTest, TestNoCert) {
  Json::ObjectPtr loader = Json::Factory::LoadFromString("{}");
  ContextConfigImpl cfg(*loader);
  Runtime::MockLoader runtime;
  ContextManagerImpl manager(runtime);
  Stats::IsolatedStoreImpl store;
  ClientContextPtr context(manager.createSslClientContext(store, cfg));
  EXPECT_EQ("", context->getCaCertInformation());
  EXPECT_EQ("", context->getCertChainInformation());
}

} // Ssl
