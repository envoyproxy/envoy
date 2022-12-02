#include "source/extensions/certificate_providers/local_certificate/local_certificate.h"

#include <openssl/x509.h>

#include "source/common/common/logger.h"
#include "source/common/config/datasource.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {
namespace LocalCertificate {

Provider::Provider(
    const envoy::extensions::certificate_providers::local_certificate::v3::LocalCertificate& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context, Api::Api& api)
    : main_thread_dispatcher_(factory_context.mainThreadDispatcher()),
      ca_cert_(Config::DataSource::read(config.rootca_cert(), true, api)),
      ca_key_(Config::DataSource::read(config.rootca_key(), true, api)),
      default_identity_cert_(Config::DataSource::read(config.default_identity_cert(), true, api)),
      default_identity_key_(Config::DataSource::read(config.default_identity_key(), true, api)),
      pkey_(config.pkey()),
      cache_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, cache_size, CacheDefaultSize)) {
  ASSERT(main_thread_dispatcher_.isThreadSafe());
  if (config.has_expiration_time()) {
    auto seconds = google::protobuf::util::TimeUtil::TimestampToSeconds(config.expiration_time());
    expiration_config_ = std::chrono::system_clock::from_time_t(static_cast<time_t>(seconds));
  }

  // Set cert cache size
  certificates_.setMaxSize(cache_size_);

  // Set default TLS Certificate
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* tls_certificate =
      new envoy::extensions::transport_sockets::tls::v3::TlsCertificate();
  tls_certificate->mutable_certificate_chain()->set_inline_string(default_identity_cert_);
  tls_certificate->mutable_private_key()->set_inline_string(default_identity_key_);
  certificates_.insert("default", tls_certificate);
}

Envoy::CertificateProvider::CertificateProvider::Capabilities Provider::capabilities() const {
  Envoy::CertificateProvider::CertificateProvider::Capabilities cap;
  cap.provide_on_demand_identity_certs = true;
  return cap;
}

const std::string Provider::trustedCA(const std::string&) const { return ""; }

std::vector<
    std::reference_wrapper<const envoy::extensions::transport_sockets::tls::v3::TlsCertificate>>
Provider::tlsCertificates(const std::string&) const {
  ASSERT(main_thread_dispatcher_.isThreadSafe());
  return certificates_.getCertificates();
}

Envoy::CertificateProvider::OnDemandUpdateHandlePtr Provider::addOnDemandUpdateCallback(
    const std::string sni, ::Envoy::CertificateProvider::OnDemandUpdateMetadataPtr metadata,
    Event::Dispatcher& thread_local_dispatcher,
    Envoy::CertificateProvider::OnDemandUpdateCallbacks& callback) {
  ASSERT(main_thread_dispatcher_.isThreadSafe());
  auto handle =
      std::make_unique<OnDemandUpdateHandleImpl>(on_demand_update_callbacks_, sni, callback);

  // TODO: we need to improve this cache_hit check
  // It is possible that two SNIs use the same cert, so they share the same SANs.
  // if we generate two mimic certs for these SNIs, it can not pass the certs config check
  // since we do not allow duplicated SANs.
  // We need to align this cache_hit with current transport socket behavior
  bool cache_hit = [&]() {
    return certificates_.is_in_cache(sni);
  }();

  if (cache_hit) {
    ENVOY_LOG(debug, "Cache hit for {}", sni);
    // Cache hit, run on-demand update callback directly
    runOnDemandUpdateCallback(sni, thread_local_dispatcher, true);
  } else {
    // Cache miss, generate self-signed cert
    main_thread_dispatcher_.post([sni, metadata, &thread_local_dispatcher, this] {
      signCertificate(sni, metadata, thread_local_dispatcher);
    });
  }
  return handle;
}

Common::CallbackHandlePtr Provider::addUpdateCallback(const std::string&,
                                                      std::function<void()> callback) {
  ASSERT(main_thread_dispatcher_.isThreadSafe());
  return update_callback_manager_.add(callback);
}

void Provider::runAddUpdateCallback() {
  ASSERT(main_thread_dispatcher_.isThreadSafe());
  update_callback_manager_.runCallbacks();
}

void Provider::runOnDemandUpdateCallback(const std::string& host,
                                         Event::Dispatcher& thread_local_dispatcher,
                                         bool in_cache) {
  ASSERT(main_thread_dispatcher_.isThreadSafe());
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

  // create a new CSR
  X509_REQ* req = X509_REQ_new();
  X509_REQ_set_version(req, 0);
  setSubjectToCSR(metadata->connectionInfo()->subjectPeerCertificate().data(), req);
  // creates a new, empty public-key object
  EVP_PKEY* key = EVP_PKEY_new();
  setPkeyToCSR(metadata, key, req);
  // create a new certificate,
  X509* crt = X509_new();
  X509_set_version(crt, X509_VERSION_3);
  X509_set_issuer_name(crt, X509_get_subject_name(ca_cert.get()));
  X509_set_subject_name(crt, X509_REQ_get_subject_name(req));
  X509_set_pubkey(crt, X509_REQ_get_pubkey(req));
  setSANs(metadata, crt);
  setExpirationTime(metadata, crt);
  X509_sign(crt, ca_key.get(), EVP_sha256());
  // convert certificate and key to string
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

  // Generate TLS Certificate
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* tls_certificate =
      new envoy::extensions::transport_sockets::tls::v3::TlsCertificate();
  tls_certificate->mutable_certificate_chain()->set_inline_string(cert_pem);
  tls_certificate->mutable_private_key()->set_inline_string(key_pem);

  // Update certificates_ map
  certificates_.insert(sni, tls_certificate);

  runAddUpdateCallback();
  runOnDemandUpdateCallback(sni, thread_local_dispatcher, false);
}

void Provider::setSubjectToCSR(absl::string_view subject, X509_REQ* req) {
  X509_NAME* x509_name = X509_NAME_new();
  // Parse the RFC 2253 format output of subjectPeerCertificate and set back to mimic cert.
  const std::string delim = ",";
  std::string item;
  for (absl::string_view v : absl::StrSplit(subject, delim)) {
    // This happens when peer subject from connectioninfo contains escaped comma,
    // like O=Technology Co.\\, Ltd, have to remove the double backslash.
    if (v.back() == '\\') {
      absl::StrAppend(&item, v.substr(0, v.length() - 1), delim);
    } else {
      absl::StrAppend(&item, v.substr(0, v.length()));
      std::vector<std::string> entries = absl::StrSplit(item, "=");
      X509_NAME_add_entry_by_txt(x509_name, entries[0].c_str(), MBSTRING_ASC,
                                 reinterpret_cast<const unsigned char*>(entries[1].c_str()), -1, -1,
                                 0);
      item.clear();
    }
  }
  X509_REQ_set_subject_name(req, x509_name);
}

void Provider::setPkeyToCSR(Envoy::CertificateProvider::OnDemandUpdateMetadataPtr metadata,
                            EVP_PKEY* key, X509_REQ* req) {
  auto pkey_rsa = [&](const int size) {
    // BN provides support for working with arbitrary sized integers
    BIGNUM* bne = BN_new();
    BN_set_word(bne, RSA_F4);
    RSA* rsa = RSA_new();
    // generates a new RSA key where the modulus has size |bits|=2048 and the public exponent is
    // |e|=bne
    RSA_generate_key_ex(rsa, size, bne, nullptr);

    // set the underlying public key in an |EVP_PKEY| object
    EVP_PKEY_assign_RSA(key, rsa);
    ENVOY_LOG(debug, "Generating RSA key");
  };

  auto pkey_ecdsa = [&]() {
    EC_KEY* ec_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    EC_KEY_generate_key(ec_key);
    EVP_PKEY_assign_EC_KEY(key, ec_key);
    ENVOY_LOG(debug, "Generating ECDSA key");
  };

  auto pkey = pkey_;
  if (pkey == envoy::extensions::certificate_providers::local_certificate::v3::
                  LocalCertificate_Pkey_UNSPECIFIED) {
    if (metadata->connectionInfo()->pkeyTypePeerCertificate() == EVP_PKEY_RSA) {
      switch (auto size = metadata->connectionInfo()->pkeySizePeerCertificate()) {
      case 2048:
        pkey = envoy::extensions::certificate_providers::local_certificate::v3::
            LocalCertificate_Pkey_RSA_2048;
        break;
      case 3072:
        pkey = envoy::extensions::certificate_providers::local_certificate::v3::
            LocalCertificate_Pkey_RSA_3072;
        break;
      case 4096:
        pkey = envoy::extensions::certificate_providers::local_certificate::v3::
            LocalCertificate_Pkey_RSA_4096;
        break;
      default:
        ENVOY_LOG(debug, "Not supported PKEY RSA size '{}'", size);
      }
    }
    if (metadata->connectionInfo()->pkeyTypePeerCertificate() == EVP_PKEY_EC) {
      switch (auto size = metadata->connectionInfo()->pkeySizePeerCertificate()) {
      case NID_X9_62_prime256v1:
        pkey = envoy::extensions::certificate_providers::local_certificate::v3::
            LocalCertificate_Pkey_ECDSA_P256;
        break;
      default:
        ENVOY_LOG(debug, "Not supported PKEY ECDSA nid '{}'", size);
      }
    }
  }

  switch (pkey) {
  case envoy::extensions::certificate_providers::local_certificate::v3::
      LocalCertificate_Pkey_RSA_2048:
    pkey_rsa(2048);
    break;
  case envoy::extensions::certificate_providers::local_certificate::v3::
      LocalCertificate_Pkey_RSA_3072:
    pkey_rsa(3072);
    break;
  case envoy::extensions::certificate_providers::local_certificate::v3::
      LocalCertificate_Pkey_RSA_4096:
    pkey_rsa(4096);
    break;
  case envoy::extensions::certificate_providers::local_certificate::v3::
      LocalCertificate_Pkey_ECDSA_P256:
    pkey_ecdsa();
    break;
  default:
    pkey_rsa(2048);
    break;
  }

  X509_REQ_set_pubkey(req, key);
  // signs |req| with |pkey| and replaces the signature algorithm and signature fields
  X509_REQ_sign(req, key, EVP_sha256()); // return x509_req->signature->length
}

void Provider::setExpirationTime(Envoy::CertificateProvider::OnDemandUpdateMetadataPtr metadata,
                                 X509* crt) {
  X509_gmtime_adj(X509_get_notBefore(crt), 0);
  // Compare expiration_time config with upstream cert expiration. Use smaller
  // value of those two dates as expiration time of mimic cert.
  auto now = std::chrono::system_clock::now();
  auto cert_expiration = metadata->connectionInfo()->expirationPeerCertificate();
  uint64_t valid_seconds;
  if (expiration_config_) {
    valid_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(expiration_config_.value() - now).count();
  }
  if (cert_expiration) {
    if (!expiration_config_ || cert_expiration.value() <= expiration_config_.value()) {
      valid_seconds =
          std::chrono::duration_cast<std::chrono::seconds>(cert_expiration.value() - now).count();
    }
  }
  X509_gmtime_adj(X509_get_notAfter(crt), valid_seconds);
}

void Provider::setSANs(Envoy::CertificateProvider::OnDemandUpdateMetadataPtr metadata, X509* crt) {
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
}
} // namespace LocalCertificate
} // namespace CertificateProviders
} // namespace Extensions
} // namespace Envoy
