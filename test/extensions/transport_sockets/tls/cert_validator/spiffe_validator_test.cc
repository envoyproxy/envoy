#include <string>
#include <vector>

#include "extensions/transport_sockets/tls/cert_validator/spiffe_validator.h"

#include "test/extensions/transport_sockets/tls/ssl_test_utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

TEST(SPIFFEValidator, TestExtractTrustDomain) {
  EXPECT_EQ("abc.com", SPIFFEValidator::extractTrustDomain("spiffe://abc.com/"));
  EXPECT_EQ("dev.envoy.com",
            SPIFFEValidator::extractTrustDomain("spiffe://dev.envoy.com/workload1"));
  EXPECT_EQ("k8s-west.example.com", SPIFFEValidator::extractTrustDomain(
                                        "spiffe://k8s-west.example.com/ns/staging/sa/default"));
}

TEST(SPIFFEValidator, TestCertificatePrecheck) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      // basicConstraints: CA:True,
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"));
  EXPECT_EQ(0, SPIFFEValidator::certificatePrecheck(cert.get()));

  cert = readCertFromFile(TestEnvironment::substitute(
      // basicConstraints CA:False, keyUsage has keyCertSign
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/keyusage_cert_sign_cert.pem"));
  EXPECT_EQ(0, SPIFFEValidator::certificatePrecheck(cert.get()));

  cert = readCertFromFile(TestEnvironment::substitute(
      // basicConstraints CA:False, keyUsage has cRLSign
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/keyusage_crl_sign_cert.pem"));
  EXPECT_EQ(0, SPIFFEValidator::certificatePrecheck(cert.get()));

  cert = readCertFromFile(TestEnvironment::substitute(
      // basicConstraints CA:False, keyUsage does not have keyCertSign and cRLSign
      // should be considered valid (i.e. return 1)
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/extensions_cert.pem"));
  EXPECT_EQ(1, SPIFFEValidator::certificatePrecheck(cert.get()));
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
