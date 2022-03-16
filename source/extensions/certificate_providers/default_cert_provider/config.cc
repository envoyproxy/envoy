#include "source/extensions/certificate_providers/default_cert_provider/config.h"

#include <iterator>
#include <set>

#include "envoy/common/exception.h"
#include "envoy/extensions/certificate_providers/default_cert_provider/v3/config.pb.h"

#include "source/common/config/datasource.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {

DefaultCertificateProvider::DefaultCertificateProvider(
    const envoy::config::core::v3::TypedExtensionConfig& config, Api::Api& api) {

  envoy::extensions::certificate_providers::default_cert_provider::v3::
      DefaultCertificateProviderConfig message;
  Config::Utility::translateOpaqueConfig(config.typed_config(),
                                         ProtobufMessage::getStrictValidationVisitor(), message);

  capabilities_.provide_trusted_ca = true;
  capabilities_.provide_ca_certpairs = true;
  capabilities_.provide_identity_certpairs = true;
  capabilities_.generate_identity_certpairs = true;

  std::string icert = Config::DataSource::read(message.identity_cert(), true, api);
  std::string ikey = Config::DataSource::read(message.identity_key(), true, api);
  std::list<Envoy::CertificateProvider::Certpair> identity_certpairs;
  Envoy::CertificateProvider::Certpair identity_certpair = {icert, ikey};
  identity_certpairs.emplace_back(identity_certpair);
  cert_pairs_.emplace("IDENTITY_CERTPAIR", identity_certpairs);

  std::string ccert = Config::DataSource::read(message.ca_cert(), true, api);
  std::string ckey = Config::DataSource::read(message.ca_key(), true, api);
  std::list<Envoy::CertificateProvider::Certpair> ca_certpairs;
  Envoy::CertificateProvider::Certpair ca_certpair = {ccert, ckey};
  ca_certpairs.emplace_back(ca_certpair);
  cert_pairs_.emplace("CA_CERTPAIR", ca_certpairs);

  trusted_ca_ = Config::DataSource::read(message.trusted_ca(), true, api);
}

std::list<Envoy::CertificateProvider::Certpair>
DefaultCertificateProvider::certPairs(absl::string_view cert_name, bool generate) {
  auto it = cert_pairs_.find(cert_name);
  if (it != cert_pairs_.end()) {
    return it->second;
  }
  if (generate) {
    generateCertpair(cert_name);
  }
  std::list<Envoy::CertificateProvider::Certpair> empty_list;
  return empty_list;
}

Common::CallbackHandlePtr
DefaultCertificateProvider::addUpdateCallback(absl::string_view cert_name,
                                             std::function<void()> callback) {
  if (!update_callback_managers_.contains(cert_name)) {
    Common::CallbackManager<> update_callback_manager;
    update_callback_managers_.emplace(cert_name, update_callback_manager);
  }
  auto it = update_callback_managers_.find(cert_name);
  return it->second.add(callback);
}

void DefaultCertificateProvider::generateCertpair(absl::string_view cert_name) {
  auto ca_certpairs = cert_pairs_.at("CA_CERTPAIR");
  auto ca_cert_str = ca_certpairs.begin()->certificate_;
  auto ca_key_str = ca_certpairs.begin()->private_key_;

  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(const_cast<char*>(ca_cert_str.data()), ca_cert_str.size()));
  RELEASE_ASSERT(bio != nullptr, "");
  bssl::UniquePtr<X509> ca_cert;
  ca_cert.reset(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));

  bio.reset(BIO_new_mem_buf(const_cast<char*>(ca_key_str.data()), ca_key_str.size()));
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
  const char* szCommon = cert_name.data();
  X509_NAME_add_entry_by_txt(x509_name, "CN", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>(szCommon), -1, -1, 0);

  EVP_PKEY_assign_RSA(key, rsa);
  X509_REQ_set_pubkey(req, key);
  X509_REQ_sign(req, key, EVP_sha1()); // return x509_req->signature->length

  X509_set_version(crt, 2);
  std::string prefix = "DNS:";
  std::string subAltName = prefix + cert_name.data();
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

  Envoy::CertificateProvider::Certpair certpair{cert_pem, key_pem};
  std::list<Envoy::CertificateProvider::Certpair> generate_certpairs;
  generate_certpairs.emplace_back(certpair);
  onCertpairsUpdated(cert_name, generate_certpairs);
}

void DefaultCertificateProvider::onCertpairsUpdated(
    absl::string_view cert_name, std::list<Envoy::CertificateProvider::Certpair> certpairs) {
  auto cert_it = cert_pairs_.find(cert_name);
  if (cert_it != cert_pairs_.end()) {
    cert_it->second.clear();
    for (auto it : certpairs) {
      cert_it->second.emplace_back(it);
    }
  }
  auto callback_it = update_callback_managers_.find(cert_name);
  if (callback_it != update_callback_managers_.end()) {
    callback_it->second.runCallbacks();
  }
}

} // namespace CertificateProviders
} // namespace Extensions
} // namespace Envoy
