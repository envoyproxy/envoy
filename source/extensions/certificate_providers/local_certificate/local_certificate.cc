#include "source/extensions/certificate_providers/local_certificate/local_certificate.h"

#include "envoy/extensions/certificate_providers/local_certificate/v3/local_certificate.pb.h"

#include "source/common/common/logger.h"
#include "source/common/config/datasource.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {
namespace LocalCertificate {

Provider::Provider(const envoy::extensions::certificate_providers::local_certificate::v3::LocalCertificate& config,
                   Server::Configuration::TransportSocketFactoryContext& factory_context,
                   Api::Api& api)
    : main_thread_dispatcher_(factory_context.mainThreadDispatcher()) {
  ca_cert_ = Config::DataSource::read(config.rootca_cert(), true, api);
  ca_key_ = Config::DataSource::read(config.rootca_key(), true, api);
  default_identity_cert_ = Config::DataSource::read(config.default_identity_cert(), true, api);
  default_identity_key_ = Config::DataSource::read(config.default_identity_key(), true, api);

  if (config.has_expiration_time()) {
    auto seconds = google::protobuf::util::TimeUtil::TimestampToSeconds(config.expiration_time());
    expiration_config_ = std::chrono::system_clock::from_time_t(static_cast<time_t>(seconds));
  }

  // Generate TLSCertificate
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* tls_certificate = new envoy::extensions::transport_sockets::tls::v3::TlsCertificate();
  tls_certificate->mutable_certificate_chain()->set_inline_string(default_identity_cert_);
  tls_certificate->mutable_private_key()->set_inline_string(default_identity_key_);
  // Update certificates_ map
  {
    absl::WriterMutexLock writer_lock{&certificates_lock_};
    certificates_.try_emplace("default", tls_certificate);
  }
}

Envoy::CertificateProvider::CertificateProvider::Capabilities Provider::capabilities() const {
  Envoy::CertificateProvider::CertificateProvider::Capabilities cap;
  cap.provide_on_demand_identity_certs = true;
  return cap;
}

const std::string Provider::trustedCA(const std::string&) const { return ""; }

std::vector<std::reference_wrapper<const envoy::extensions::transport_sockets::tls::v3::TlsCertificate>>
Provider::tlsCertificates(const std::string&) const {
  std::vector<std::reference_wrapper<const envoy::extensions::transport_sockets::tls::v3::TlsCertificate>> result;
  absl::ReaderMutexLock reader_lock{&certificates_lock_};
  for (auto [_, value] : certificates_) {
    result.push_back(*value);
  }
  return result;
}

Envoy::CertificateProvider::OnDemandUpdateHandlePtr Provider::addOnDemandUpdateCallback(
    const std::string sni, ::Envoy::CertificateProvider::OnDemandUpdateMetadataPtr metadata,
    Event::Dispatcher& thread_local_dispatcher,
    Envoy::CertificateProvider::OnDemandUpdateCallbacks& callback) {

  auto handle = std::make_unique<OnDemandUpdateHandleImpl>(
      on_demand_update_callbacks_, sni, callback);

  // TODO: we need to improve this cache_hit check
  // It is possible that two SNIs use the same cert, so they share the same SANs.
  // if we generate two mimic certs for these SNIs, it can not pass the certs config check
  // since we do not allow duplicated SANs.
  // We need to align this cache_hit with current transport socket behavior
  bool cache_hit = [&]() {
    absl::ReaderMutexLock reader_lock{&certificates_lock_};
    auto it = certificates_.find(sni);
    return it != certificates_.end() ? true : false;
  }();

  if (cache_hit) {
    // Cache hit, run on-demand update callback directly
    runOnDemandUpdateCallback(sni, thread_local_dispatcher, true);
  } else {
    // Cache miss, generate self-signed cert
    main_thread_dispatcher_.post([sni, metadata, &thread_local_dispatcher, this] {
      signCertificate(sni, metadata, thread_local_dispatcher); });
  }
  return handle;
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

// TODO: we meed to copy more information of original cert, such as SANs, the whole subject,
// expiration time, etc.
void Provider::signCertificate(const std::string sni,
                               ::Envoy::CertificateProvider::OnDemandUpdateMetadataPtr metadata,
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
  setSubject(metadata->connectionInfo()->subjectPeerCertificate().data(), x509_name);
  X509_set_subject_name(crt, x509_name);

  EVP_PKEY_assign_RSA(key, rsa);
  X509_REQ_set_pubkey(req, key);
  X509_REQ_sign(req, key, EVP_sha1()); // return x509_req->signature->length

  X509_set_version(crt, 2);
  auto gens = sk_GENERAL_NAME_new_null();

  for (auto& dns : metadata->connectionInfo()->dnsSansPeerCertificate()) {
    auto ia5 = ASN1_IA5STRING_new();
    ASN1_STRING_set(ia5, dns.c_str(), -1);
    auto gen = GENERAL_NAME_new();
    GENERAL_NAME_set0_value(gen, GEN_DNS, ia5);
    sk_GENERAL_NAME_push(gens, gen);
  }

  for (auto& uri : metadata->connectionInfo()->uriSanPeerCertificate()) {
    auto ia5 = ASN1_IA5STRING_new();
    ASN1_STRING_set(ia5, uri.c_str(), -1);
    auto gen = GENERAL_NAME_new();
    GENERAL_NAME_set0_value(gen, GEN_URI, ia5);
    sk_GENERAL_NAME_push(gens, gen);
  }

  X509_add1_ext_i2d(crt, NID_subject_alt_name, gens, 0, 0);
  //X509_EXTENSION* ext =
  //    X509V3_EXT_nconf_nid(nullptr, nullptr, NID_subject_alt_name, subAltName.c_str());
  //X509_add_ext(crt, ext, -1);

  X509_set_issuer_name(crt, X509_get_subject_name(ca_cert.get()));
  X509_gmtime_adj(X509_get_notBefore(crt), 0);


  // Compare expiration_time config with upstream cert expiration. Use smaller
  // value of those two dates as expiration time of mimic cert.
  auto now = std::chrono::system_clock::now();
  auto cert_expiration = metadata->connectionInfo()->expirationPeerCertificate();
  uint64_t valid_seconds;
  if (expiration_config_) {
    valid_seconds = std::chrono::duration_cast<std::chrono::seconds>(expiration_config_.value() - now).count();
  }
  if (cert_expiration) {
    if (!expiration_config_ || cert_expiration.value() <= expiration_config_.value()) {
        valid_seconds =
          std::chrono::duration_cast<std::chrono::seconds>(cert_expiration.value() - now).count();
    }
  }

  X509_gmtime_adj(X509_get_notAfter(crt), valid_seconds);
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
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* tls_certificate = new envoy::extensions::transport_sockets::tls::v3::TlsCertificate();
  tls_certificate->mutable_certificate_chain()->set_inline_string(cert_pem);
  tls_certificate->mutable_private_key()->set_inline_string(key_pem);

  // Update certificates_ map
  {
    absl::WriterMutexLock writer_lock{&certificates_lock_};
    certificates_.try_emplace(
        sni, tls_certificate);
  }

  runAddUpdateCallback();
  runOnDemandUpdateCallback(sni, thread_local_dispatcher, false);
}

void Provider::setSubject(absl::string_view subject, X509_NAME* x509_name) {
  // Parse the RFC 2253 format output of subjectPeerCertificate and set back to mimic cert.
  const std::string delim = ",";
  std::string item;
  for (absl::string_view v: absl::StrSplit(subject, delim)) {
    // This happens when peer subject from connectioninfo contains escaped comma,
    // like O=Technology Co.\\, Ltd, have to remove the double backslash.
    if (v.back() == '\\') {
      absl::StrAppend(&item, v.substr(0, v.length() - 1), delim);
    }
    else {
      absl::StrAppend(&item, v.substr(0, v.length()));
      std::vector<std::string> entries = absl::StrSplit(item, "=");
      X509_NAME_add_entry_by_txt(x509_name, entries[0].c_str(), MBSTRING_ASC,
                                 reinterpret_cast<const unsigned char*>(entries[1].c_str()),
                                 -1, -1, 0);
      item.clear();
    }
  }
}
} // namespace LocalCertificate
} // namespace CertificateProviders
} // namespace Extensions
} // namespace Envoy
