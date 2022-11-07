#pragma once

#include <list>

#include "envoy/common/callback.h"
#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/ssl/connection.h"

#include "absl/strings/string_view.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace CertificateProvider {

class Metadata {
public:
  virtual ~Metadata() = default;
};

using MetadataPtr = std::unique_ptr<Metadata>;

class OnDemandUpdateCallbacks {
public:
  virtual ~OnDemandUpdateCallbacks() = default;

  /**
   * Called when cert is already in cache.
   * @param cert_name supplies the name used to look for cert.
   */
  virtual void onCacheHit(const std::string cert_name) const PURE;
  /**
   * Called when cert cache is missed.
   * @param cert_name supplies name used to look for cert.
   */
  virtual void onCacheMiss(const std::string cert_name) const PURE;
};

class CertificateProvider {
public:
  struct Capabilites {
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
   * Certificate provider instance which used to get tls certificates
   * should provide at least one tls certificate.
   * @return Identity certificates used for handshake
   */
  virtual std::vector<
      std::reference_wrapper<const envoy::extensions::transport_sockets::tls::v3::TlsCertificate>>
  tlsCertificates(const std::string& cert_name) const PURE;

  /**
   * Add on-demand callback into certificate provider, this function might be invoked from worker
   * thread during runtime
   *
   * @param cert_name is certificate provider name in commontlscontext configuration.
   * @param metadata is passed to provider for certs fetching/refreshing.
   * @param thread_local_dispatcher is the dispatcher from callee's thread.
   * @param callbacks registers callback to be executed for on demand update.
   * @return CallbackHandle the handle which can remove that update callback.
   */
  virtual Common::CallbackHandlePtr addOnDemandUpdateCallback(
      const std::string cert_name, Envoy::CertificateProvider::MetadataPtr metadata,
      Event::Dispatcher& thread_local_dispatcher, OnDemandUpdateCallbacks& callbacks) PURE;

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
