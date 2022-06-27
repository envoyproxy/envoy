#pragma once

#include <list>

#include "envoy/common/callback.h"
#include "envoy/common/pure.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/ssl/connection.h"

#include "absl/strings/string_view.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace CertificateProvider {

class OnDemandUpdateMetadata {
public:
  virtual ~OnDemandUpdateMetadata() = default;

  virtual Envoy::Ssl::ConnectionInfoConstSharedPtr connectionInfo() const PURE;
};

using OnDemandUpdateMetadataPtr = std::unique_ptr<OnDemandUpdateMetadata>;

class CertificateProvider {
public:
  struct Capabilites {
    /* whether or not a provider provides a trusted ca cert for validation */
    bool provide_trusted_ca = false;

    /* Whether or not a provider provides identity certpairs */
    bool provide_identity_certs = false;

    /* whether or not a provider supports generating identity certificates on demand */
    bool provide_on_demand_identity_certs = false;
  };

  virtual ~CertificateProvider() = default;

  virtual Capabilites capabilities() const PURE;

  /**
   * @return CA certificate used for validation
   */
  virtual const std::string trustedCA(const std::string& cert_name) const PURE;

  /**
   * @return Identity certificates used for handshake
   */
  virtual std::vector<const envoy::extensions::transport_sockets::tls::v3::TlsCertificate*>
  tlsCertificates(const std::string& cert_name) const PURE;

  /**
   * Add on-demand callback into certificate provider, this function might be invoked from worker
   * thread during runtime
   *
   * @param metadata is passed to provider for certs fetching/refreshing
   * @return CallbackHandle the handle which can remove that update callback.
   */
  virtual Common::CallbackHandlePtr
  addOnDemandUpdateCallback(const std::string& cert_name, OnDemandUpdateMetadataPtr metadata,
                            std::function<void()> thread_local_callback) PURE;

  /**
   * Add certificate update callback into certificate provider for asychronous usage.
   *
   * @param callback callback that is executed by certificate provider.
   * @return CallbackHandle the handle which can remove that update callback.
   */
  virtual Common::CallbackHandlePtr addUpdateCallback(const std::string& cert_name,
                                                      std::function<void()> callback) PURE;
};

using CertificateProviderSharedPtr = std::shared_ptr<CertificateProvider>;

} // namespace CertificateProvider
} // namespace Envoy
