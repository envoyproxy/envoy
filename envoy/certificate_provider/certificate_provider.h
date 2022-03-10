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

  /* Called when the certpairs update of cert name is received */
  virtual void onCertpairsUpdated(absl::string_view cert_name, std::list<Certpair> certpairs) PURE;
  /* Called when the ca cert update of cert name is received */
  virtual void onCACertUpdated(absl::string_view cert_name, const std::string cert) PURE;
  /* Called when the subscription is unable to fetch the update or
   * onCertpairsUpdated/onCACertUpdated throws the exception */
  virtual void onUpatedFail() PURE;
};

class CertificateProvider {
public:
  struct Capabilites {
    /* whether or not a provider provides a trusted ca cert for validation */
    bool provide_trusted_ca = false;

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
   * @return CertPairs, identity certpairs or ca certpairs
   */
  virtual std::list<Certpair> certPairs(absl::string_view cert_name, bool generate) PURE;

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
