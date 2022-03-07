#pragma once

#include <list>

#include "envoy/common/callback.h"
#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace CertificateProvider {

struct Certpair {
  const std::string& certificate_;
  const std::string& private_key_;
};

/**
 * This is used to implement asynchronous certificate provider.
 */
class CertificateSubscriptionCallbacks {
public:
  virtual ~CertificateSubscriptionCallbacks() = default;

  virtual void onCertpairsUpdated(absl::string_view cert_name, std::list<Certpair> certpairs) PURE;
  virtual void onCACertUpdated(absl::string_view cert_name, const std::string cert) PURE;
  virtual void onUpatedFail() PURE;
};

class CertificateProvider {
public:
  struct Capabilites {
    /* whether or not a provider provides a ca cert directly */
    bool provide_ca_cert = false;

    /* whether or not a provider providers ca certpairs for issuer */
    bool provide_ca_certpairs = false;

    /* Whether or not a provider provides identity certpairs directly */
    bool provide_identity_certpairs = false;

    /* whether or not a provider supports generating identity certpairs during handshake,
     * which requires the capability provide_ca_certpairs to sign the certificates
     */
    bool generate_identity_certpairs = false;
  };

  virtual ~CertificateProvider() = default;

  virtual Capabilites capabilities() const PURE;

  /**
   * @return CA certificate from provider used for validation
   */
  virtual const std::string& caCert(absl::string_view cert_name) const PURE;

  /**
   * @return CertPairs, identity certpairs used for TLS handshake
   */
  virtual std::list<Certpair> certPairs(absl::string_view cert_name) PURE;

  /**
   * Add certificate update callback into certificate provider for asychronous usage.
   *
   * @param callback callback that is executed by certificate provider.
   * @return CallbackHandle the handle which can remove that update callback.
   */
  virtual Common::CallbackHandlePtr addUpdateCallback(absl::string_view cert_name,
                                                      std::function<void()> callback) PURE;
};

using CertificateProviderSharedPtr = std::shared_ptr<CertificateProvider>;

} // namespace CertificateProvider
} // namespace Envoy
