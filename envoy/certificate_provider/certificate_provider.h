#pragma once

#include <list>

#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"

#include "absl/strings/string_view.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace CertificateProvider {

struct Certpair {
  const std::string& certificate_;
  const std::string& private_key_;
};

class CertificateSubscription : public Envoy::Config::Subscription {};

using CertificateSubscriptionPtr = std::unique_ptr<CertificateSubscription>;

class CertificateSubscriptionCallbacks {
public:
  virtual ~CertificateSubscriptionCallbacks() = default;

  virtual void onCertpairUpdated(absl::string_view cert_name, Certpair certpair) PURE;
  virtual void onCACertUpdated(absl::string_view cert_name, const std::string cert) PURE;
  virtual void onUpatedFail() PURE;
};

class CertificateProvider {
public:
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
   * Get TLS certpair directly from provider, return identity cert and key for handshake
   * or ca cert and key for issuer.
   */
  virtual std::list<Certpair> getCertpair(absl::string_view cert_name) PURE;

  /*
   * Generate TLS identity certificate dynamically during TLS handshake
   */
  virtual Certpair* generateIdentityCertificate(const SSL_CLIENT_HELLO* ssl_client_hello) PURE;

  /*
   * CertificateSubsriptionCallbacks
   */
  virtual void onCertpairUpdated(absl::string_view cert_name, Certpair certpair) PURE;
  virtual void onCACertUpdated(absl::string_view cert_name, const std::string cert) PURE;
  virtual void onUpatedFailed() PURE;

  /*
   * Add subsription on one cert/certpair
   */
  virtual void addSubsription(CertificateSubscriptionPtr subscription, std::string cert_name) PURE;
};

using CertificateProviderSharedPtr = std::shared_ptr<CertificateProvider>;

} // namespace CertificateProvider
} // namespace Envoy
