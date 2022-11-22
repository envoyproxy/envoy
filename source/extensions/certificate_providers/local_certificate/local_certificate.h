#pragma once

#include "envoy/certificate_provider/certificate_provider.h"
#include "envoy/common/callback.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/certificate_providers/local_certificate/v3/local_certificate.pb.h"
#include "envoy/server/transport_socket_config.h"

#include "envoy/extensions/certificate_providers/local_certificate/v3/local_certificate.pb.h"

#include "source/common/common/callback_impl.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {
namespace LocalCertificate {

// Local cert provider
class Provider : public CertificateProvider::CertificateProvider,
                 Logger::Loggable<Logger::Id::cert_provider> {
public:
  Provider(const envoy::extensions::certificate_providers::local_certificate::v3::LocalCertificate& config,
           Server::Configuration::TransportSocketFactoryContext& factory_context, Api::Api& api);

  // CertificateProvider::CertificateProvider
  Envoy::CertificateProvider::CertificateProvider::Capabilities capabilities() const override;
  const std::string trustedCA(const std::string& cert_name) const override;
  std::vector<std::reference_wrapper<const envoy::extensions::transport_sockets::tls::v3::TlsCertificate>>
  tlsCertificates(const std::string& cert_name) const override;
  Envoy::CertificateProvider::OnDemandUpdateHandlePtr addOnDemandUpdateCallback(
      const std::string cert_name,
      Envoy::CertificateProvider::OnDemandUpdateMetadataPtr metadata,
      Event::Dispatcher& thread_local_dispatcher,
      ::Envoy::CertificateProvider::OnDemandUpdateCallbacks& callback) override;
  Common::CallbackHandlePtr addUpdateCallback(const std::string& cert_name,
                                              std::function<void()> callback) override;

private:
  struct OnDemandUpdateHandleImpl : public ::Envoy::CertificateProvider::OnDemandUpdateHandle,
                                    RaiiMapOfListElement<std::string, OnDemandUpdateHandleImpl*> {
    OnDemandUpdateHandleImpl(
        absl::flat_hash_map<std::string, std::list<OnDemandUpdateHandleImpl*>>& parent,
        absl::string_view host, ::Envoy::CertificateProvider::OnDemandUpdateCallbacks& callbacks)
        : RaiiMapOfListElement<std::string, OnDemandUpdateHandleImpl*>(parent, host, this),
          callbacks_(callbacks) {}

    ::Envoy::CertificateProvider::OnDemandUpdateCallbacks& callbacks_;
  };

  mutable absl::Mutex certificates_lock_;
  absl::flat_hash_map<std::string,
                      const envoy::extensions::transport_sockets::tls::v3::TlsCertificate*>
      certificates_ ABSL_GUARDED_BY(certificates_lock_);

  void runAddUpdateCallback();
  void runOnDemandUpdateCallback(const std::string& host,
                                 Event::Dispatcher& thread_local_dispatcher, bool in_cache = true);
  // void signCertificate(std::string sni, absl::Span<const std::string> dns_sans, const std::string
  // subject,
  //                      Event::Dispatcher& thread_local_dispatcher);
  void signCertificate(const std::string sni,
                       Envoy::CertificateProvider::OnDemandUpdateMetadataPtr metadata,
                       Event::Dispatcher& thread_local_dispatcher);

  void setSubjectToCSR(absl::string_view subject, X509_REQ* req);
  void setPkeyToCSR(Envoy::CertificateProvider::OnDemandUpdateMetadataPtr metadata, EVP_PKEY* key,
                    X509_REQ* req);
  void setExpirationTime(Envoy::CertificateProvider::OnDemandUpdateMetadataPtr metadata, X509* crt);
  void setSANs(Envoy::CertificateProvider::OnDemandUpdateMetadataPtr metadata, X509* crt);
  Event::Dispatcher& main_thread_dispatcher_;
  std::string ca_cert_;
  std::string ca_key_;
  std::string default_identity_cert_;
  std::string default_identity_key_;
  absl::optional<SystemTime> expiration_config_;
  envoy::extensions::certificate_providers::local_certificate::v3::LocalCertificate_Pkey pkey_;

  Common::CallbackManager<> update_callback_manager_;
  absl::flat_hash_map<std::string, std::list<OnDemandUpdateHandleImpl*>>
      on_demand_update_callbacks_;
};
} // namespace LocalCertificate
} // namespace CertificateProviders
} // namespace Extensions
} // namespace Envoy
