#pragma once

#include <list>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {

struct Capabilites {
  /* whether or not a provider provides a ca cert directly */
  bool provide_ca_cert = false;

  /* Whether or not a provider provides identity certs directly */
  bool provide_identity_certs = false;

  /* whether or not a provider supports generating identity certs during handshake,
   * which requires the capability provide_ca_cert to sign the certificates
   */
  bool generate_identity_certs = false;
};

class CertificateProvider {
public:
  struct Certpair {
    const std::string& certificate_;
    const std::string& private_key_;
  };

  struct Capabilites {
    /* whether or not a provider provides a ca cert directly */
    bool provide_ca_cert = false;

    /* Whether or not a provider provides identity certs directly */
    bool provide_identity_certs = false;

    /* whether or not a provider supports generating identity certs during handshake,
     * which requires the capability provide_ca_cert to sign the certificates
     */
    bool generate_identity_certs = false;
  };

  virtual ~CertificateProvider() = default;

  virtual Capabilites capabilities() const PURE;

  /**
   * @return CA certificate from provider
   */
  virtual const std::string& getCACertificate(absl::string_view cert_name) const PURE;

  /*
   * Get TLS identity certificates directly from provider
   */
  virtual std::list<Certpair> getIdentityCertificates(absl::string_view cert_name) PURE;

  /*
   * Generate TLS identity certificate dynamically during TLS handshake
   */
  virtual Certpair* generateTlsCertificate(absl::string_view server_name) PURE;
};

using CertificateProviderSharedPtr = std::shared_ptr<CertificateProvider>;

} // namespace CertificateProviders
} // namespace Extensions
} // namespace Envoy
