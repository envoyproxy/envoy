#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.validate.h"

#include "source/common/tls/server_context_config_impl.h"

#include "test/common/tls/ssl_certs_test.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class TlsCertificateConfigImplTest : public SslCertsTest {};

TEST_F(TlsCertificateConfigImplTest, NoCertLevelParamsIsNull) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);

  EXPECT_EQ(nullptr, cfg->tlsCertificates()[0].get().tlsParams());
}

TEST_F(TlsCertificateConfigImplTest, CertLevelParamsParsed) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_3
        cipher_suites: "ECDHE-RSA-AES128-GCM-SHA256"
        ecdh_curves: "P-256"
        signature_algorithms: "rsa_pss_rsae_sha256"
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);

  const Ssl::TlsParams* params = cfg->tlsCertificates()[0].get().tlsParams();
  using TlsProto = envoy::extensions::transport_sockets::tls::v3::TlsParameters;
  ASSERT_NE(nullptr, params);
  EXPECT_EQ(TlsProto::TLSv1_2, params->min_protocol_version);
  EXPECT_EQ(TlsProto::TLSv1_3, params->max_protocol_version);
  EXPECT_EQ("ECDHE-RSA-AES128-GCM-SHA256", params->cipher_suites);
  EXPECT_EQ("P-256", params->ecdh_curves);
  EXPECT_EQ("rsa_pss_rsae_sha256", params->signature_algorithms);
  EXPECT_FALSE(params->compliance_policy.has_value());
}

TEST_F(TlsCertificateConfigImplTest, CertLevelTLSv1_0And1_1Parsed) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
      tls_params:
        tls_minimum_protocol_version: TLSv1_0
        tls_maximum_protocol_version: TLSv1_1
        cipher_suites: "ECDHE-RSA-AES128-GCM-SHA256"
        ecdh_curves: "P-256"
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);

  const Ssl::TlsParams* params = cfg->tlsCertificates()[0].get().tlsParams();
  using TlsProto = envoy::extensions::transport_sockets::tls::v3::TlsParameters;
  ASSERT_NE(nullptr, params);
  EXPECT_EQ(TlsProto::TLSv1_0, params->min_protocol_version);
  EXPECT_EQ(TlsProto::TLSv1_1, params->max_protocol_version);
}

TEST_F(TlsCertificateConfigImplTest, CertLevelEcdhCurvesAndMaxVersionParsed) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_3
        ecdh_curves: "P-384:P-256"
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);

  const Ssl::TlsParams* params = cfg->tlsCertificates()[0].get().tlsParams();
  using TlsProto = envoy::extensions::transport_sockets::tls::v3::TlsParameters;
  ASSERT_NE(nullptr, params);
  EXPECT_EQ(TlsProto::TLSv1_2, params->min_protocol_version);
  EXPECT_EQ(TlsProto::TLSv1_3, params->max_protocol_version);
  EXPECT_EQ("P-384:P-256", params->ecdh_curves);
}

TEST_F(TlsCertificateConfigImplTest, CertLevelCompliancePolicyParsed) {
  using TlsProto = envoy::extensions::transport_sockets::tls::v3::TlsParameters;
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
      tls_params:
        cipher_suites: "ECDHE-RSA-AES128-GCM-SHA256"
        ecdh_curves: "P-256"
        compliance_policies: FIPS_202205
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);

  const Ssl::TlsParams* params = cfg->tlsCertificates()[0].get().tlsParams();
  ASSERT_NE(nullptr, params);
  ASSERT_TRUE(params->compliance_policy.has_value());
  EXPECT_EQ(TlsProto::FIPS_202205, params->compliance_policy.value());
}

// More than one compliance policy is rejected by the proto's max_items rule, so it never reaches
// the config code. Enforcing it there instead would also reject it on client certificates, where
// tls_params is documented as ignored.
TEST_F(TlsCertificateConfigImplTest, MultipleCompliancePoliciesRejectedByProtoValidation) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
      tls_params:
        cipher_suites: "ECDHE-RSA-AES128-GCM-SHA256"
        ecdh_curves: "P-256"
        compliance_policies: [FIPS_202205, FIPS_202205]
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  EXPECT_THROW_WITH_REGEX(TestUtility::validate(tls_context), EnvoyException,
                          "no more than 1 item");
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
