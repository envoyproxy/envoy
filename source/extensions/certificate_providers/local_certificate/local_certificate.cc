#include "source/extensions/certificate_providers/local_certificate/local_certificate.h"

#include "envoy/extensions/certificate_providers/local_certificate/v3/local_certificate.pb.h"

#include "source/common/config/datasource.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {
namespace LocalCertificate {

Provider::Provider(const envoy::config::core::v3::TypedExtensionConfig& config,
                   Server::Configuration::TransportSocketFactoryContext& factory_context,
                   Api::Api& api)
    : main_thread_dispatcher_(factory_context.mainThreadDispatcher()) {
  envoy::extensions::certificate_providers::local_certificate::v3::LocalCertificate message;
  Config::Utility::translateOpaqueConfig(config.typed_config(),
                                         ProtobufMessage::getStrictValidationVisitor(), message);
  ca_cert_ = Config::DataSource::read(message.rootca_cert(), true, api);
  ca_key_ = Config::DataSource::read(message.rootca_key(), true, api);
}

Envoy::CertificateProvider::CertificateProvider::Capabilities Provider::capabilities() const {
  Envoy::CertificateProvider::CertificateProvider::Capabilities cap;
  cap.provide_on_demand_identity_certs = true;
  return cap;
}

const std::string Provider::trustedCA(const std::string&) const { return ""; }

std::vector<const envoy::extensions::transport_sockets::tls::v3::TlsCertificate*>
Provider::tlsCertificates(const std::string&) const {
  std::vector<const envoy::extensions::transport_sockets::tls::v3::TlsCertificate*> result;
  absl::ReaderMutexLock reader_lock{&certificates_lock_};
  for (auto& [_, value] : certificates_) {
    result.push_back(value);
  }
  return result;
}

::Envoy::CertificateProvider::OnDemandUpdateResult Provider::addOnDemandUpdateCallback(
    const std::string&, ::Envoy::CertificateProvider::OnDemandUpdateMetadataPtr metadata,
    Event::Dispatcher& thread_local_dispatcher,
    ::Envoy::CertificateProvider::OnDemandUpdateCallbacks& callback) {

  auto handle = std::make_unique<OnDemandUpdateHandleImpl>(
      on_demand_update_callbacks_, metadata->connectionInfo()->sni(), callback);
  bool cache_hit = [&]() {
    absl::ReaderMutexLock reader_lock{&certificates_lock_};
    auto it = certificates_.find(metadata->connectionInfo()->sni());
    return it != certificates_.end() ? true : false;
  }();

  if (cache_hit) {
    // Cache hit, run on-demand update callback directly
    runOnDemandUpdateCallback(metadata->connectionInfo()->sni(), thread_local_dispatcher, true);
    return {::Envoy::CertificateProvider::OnDemandUpdateStatus::InCache, std::move(handle)};
  } else {
    // Cache miss, generate self-signed cert
    main_thread_dispatcher_.post([&] { signCertificate(metadata, thread_local_dispatcher); });
    return {::Envoy::CertificateProvider::OnDemandUpdateStatus::Loading, std::move(handle)};
  }
}

Common::CallbackHandlePtr Provider::addUpdateCallback(const std::string&,
                                                      std::function<void()> callback) {
  return update_callback_manager_.add(callback);
}

void Provider::runAddUpdateCallback() { update_callback_manager_.runCallbacks(); }

void Provider::runOnDemandUpdateCallback(const std::string& host,
                                         Event::Dispatcher& thread_local_dispatcher,
                                         bool in_cache) {
  auto host_it = on_demand_update_callbacks_.find(host);
  if (host_it != on_demand_update_callbacks_.end()) {
    for (auto* pending_callbacks : host_it->second) {
      auto& callbacks = pending_callbacks->callbacks_;
      pending_callbacks->cancel();
      if (in_cache) {
        thread_local_dispatcher.post([&callbacks, host] { callbacks.onCacheHit(host); });
      } else {
        thread_local_dispatcher.post([&callbacks, host] { callbacks.onCacheMiss(host); });
      }
    }
    on_demand_update_callbacks_.erase(host_it);
  }
}

void Provider::signCertificate(::Envoy::CertificateProvider::OnDemandUpdateMetadataPtr metadata,
                               Event::Dispatcher& thread_local_dispatcher) {
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(const_cast<char*>(ca_cert_.data()), ca_cert_.size()));
  RELEASE_ASSERT(bio != nullptr, "");
  bssl::UniquePtr<X509> ca_cert;
  ca_cert.reset(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));

  bio.reset(BIO_new_mem_buf(const_cast<char*>(ca_key_.data()), ca_key_.size()));
  RELEASE_ASSERT(bio != nullptr, "");
  bssl::UniquePtr<EVP_PKEY> ca_key;
  ca_key.reset(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));

  /********* generate identity certificate locally *****/
  EVP_PKEY* key = EVP_PKEY_new();
  X509* crt = X509_new();

  X509_REQ* req = X509_REQ_new();
  BIGNUM* bne = BN_new();
  BN_set_word(bne, RSA_F4);
  RSA* rsa = RSA_new();
  RSA_generate_key_ex(rsa, 2048, bne, nullptr);
  X509_REQ_set_version(req, 0);

  X509_NAME* x509_name = X509_REQ_get_subject_name(req);
  const char* szCommon = metadata->connectionInfo()->sni().data();
  X509_NAME_add_entry_by_txt(x509_name, "CN", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>(szCommon), -1, -1, 0);

  EVP_PKEY_assign_RSA(key, rsa);
  X509_REQ_set_pubkey(req, key);
  X509_REQ_sign(req, key, EVP_sha1()); // return x509_req->signature->length

  X509_set_version(crt, 2);
  std::string prefix = "DNS:";
  std::string subAltName = prefix + metadata->connectionInfo()->sni().data();
  X509_EXTENSION* ext =
      X509V3_EXT_nconf_nid(nullptr, nullptr, NID_subject_alt_name, subAltName.c_str());
  X509_add_ext(crt, ext, -1);
  X509_set_issuer_name(crt, X509_get_subject_name(ca_cert.get()));
  X509_gmtime_adj(X509_get_notBefore(crt), 0);
  X509_gmtime_adj(X509_get_notAfter(crt), 2 * 365 * 24 * 3600);
  X509_set_subject_name(crt, X509_REQ_get_subject_name(req));
  EVP_PKEY* req_pubkey = X509_REQ_get_pubkey(req);
  X509_set_pubkey(crt, req_pubkey);
  X509_sign(crt, ca_key.get(), EVP_sha256());
  /********* generate identity certificate locally *****/

  bssl::UniquePtr<BIO> buf(BIO_new(BIO_s_mem()));
  PEM_write_bio_X509(buf.get(), crt);
  const uint8_t* output;
  size_t length;
  BIO_mem_contents(buf.get(), &output, &length);
  std::string cert_pem(reinterpret_cast<const char*>(output), length);
  buf.reset(BIO_new(BIO_s_mem()));
  PEM_write_bio_PrivateKey(buf.get(), key, nullptr, nullptr, length, nullptr, nullptr);
  BIO_mem_contents(buf.get(), &output, &length);
  std::string key_pem(reinterpret_cast<const char*>(output), length);

  // Generate TLSCertificate
  auto tls_certificate =
      std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>();
  tls_certificate->mutable_certificate_chain()->set_inline_string(cert_pem);
  tls_certificate->mutable_private_key()->set_inline_string(key_pem);

  // Update certificates_ map
  {
    absl::WriterMutexLock writer_lock{&certificates_lock_};
    certificates_.try_emplace(
        metadata->connectionInfo()->sni(),
        const_cast<envoy::extensions::transport_sockets::tls::v3::TlsCertificate*>(
            tls_certificate.get()));
  }

  runAddUpdateCallback();
  runOnDemandUpdateCallback(metadata->connectionInfo()->sni(), thread_local_dispatcher, false);
}

} // namespace LocalCertificate
} // namespace CertificateProviders
} // namespace Extensions
} // namespace Envoy
