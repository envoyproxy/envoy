#include "source/extensions/transport_sockets/tls/cert_selectors/on_demand/config.h"

#include "source/common/config/utility.h"
#include "source/common/common/callback_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/ssl/tls_certificate_config_impl.h"
#include "source/common/tls/context_impl.h"
#include "source/server/generic_factory_context.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "envoy/filesystem/filesystem.h"

#include "openssl/bio.h"
#include "openssl/base.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/evp.h"
#include "openssl/pem.h"
#include "openssl/rand.h"
#include "openssl/rsa.h"
#include "openssl/x509.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace OnDemand {

namespace {

constexpr uint32_t DefaultLocalCertTtlDays = 30;
constexpr char DefaultLocalSubjectOrganization[] = "Envoy";
constexpr uint32_t DefaultRsaKeyBits = 2048;
constexpr uint32_t DefaultNotBeforeBackdateSeconds = 60;
constexpr char DefaultLocalRuntimeKeyPrefix[] =
    "envoy.tls.cert_selectors.on_demand_secret.local_signer";

bool isValidRsaKeyBits(uint32_t bits) { return bits >= 2048 && bits <= 8192 && bits % 256 == 0; }

absl::optional<ConfigProto::LocalSigner::KeyType> parseKeyType(uint64_t raw) {
  const auto value = static_cast<ConfigProto::LocalSigner::KeyType>(raw);
  switch (value) {
  case ConfigProto::LocalSigner::KEY_TYPE_UNSPECIFIED:
  case ConfigProto::LocalSigner::KEY_TYPE_RSA:
  case ConfigProto::LocalSigner::KEY_TYPE_ECDSA:
    return value;
  default:
    break;
  }
  return absl::nullopt;
}

absl::optional<ConfigProto::LocalSigner::EcdsaCurve> parseEcdsaCurve(uint64_t raw) {
  const auto value = static_cast<ConfigProto::LocalSigner::EcdsaCurve>(raw);
  switch (value) {
  case ConfigProto::LocalSigner::ECDSA_CURVE_UNSPECIFIED:
  case ConfigProto::LocalSigner::ECDSA_CURVE_P256:
  case ConfigProto::LocalSigner::ECDSA_CURVE_P384:
    return value;
  default:
    break;
  }
  return absl::nullopt;
}

absl::optional<ConfigProto::LocalSigner::SignatureHash> parseSignatureHash(uint64_t raw) {
  const auto value = static_cast<ConfigProto::LocalSigner::SignatureHash>(raw);
  switch (value) {
  case ConfigProto::LocalSigner::SIGNATURE_HASH_UNSPECIFIED:
  case ConfigProto::LocalSigner::SIGNATURE_HASH_SHA256:
  case ConfigProto::LocalSigner::SIGNATURE_HASH_SHA384:
  case ConfigProto::LocalSigner::SIGNATURE_HASH_SHA512:
    return value;
  default:
    break;
  }
  return absl::nullopt;
}

absl::optional<ConfigProto::LocalSigner::HostnameValidation> parseHostnameValidation(uint64_t raw) {
  const auto value = static_cast<ConfigProto::LocalSigner::HostnameValidation>(raw);
  switch (value) {
  case ConfigProto::LocalSigner::HOSTNAME_VALIDATION_UNSPECIFIED:
  case ConfigProto::LocalSigner::HOSTNAME_VALIDATION_PERMISSIVE:
  case ConfigProto::LocalSigner::HOSTNAME_VALIDATION_STRICT:
    return value;
  default:
    break;
  }
  return absl::nullopt;
}

absl::StatusOr<std::string> bioToString(BIO* bio) {
  BUF_MEM* mem = nullptr;
  BIO_get_mem_ptr(bio, &mem);
  if (mem == nullptr || mem->data == nullptr || mem->length == 0) {
    return absl::InternalError("empty BIO buffer");
  }
  return std::string(mem->data, mem->length);
}

absl::StatusOr<bssl::UniquePtr<EVP_PKEY>> generateRsaKey(uint32_t key_bits) {
  bssl::UniquePtr<BIGNUM> exponent(BN_new());
  if (!exponent || BN_set_word(exponent.get(), RSA_F4) != 1) {
    return absl::InternalError("failed to initialize RSA exponent");
  }
  bssl::UniquePtr<RSA> rsa(RSA_new());
  if (!rsa || RSA_generate_key_ex(rsa.get(), key_bits, exponent.get(), nullptr) != 1) {
    return absl::InternalError("failed to generate RSA key");
  }
  bssl::UniquePtr<EVP_PKEY> pkey(EVP_PKEY_new());
  if (!pkey || EVP_PKEY_set1_RSA(pkey.get(), rsa.get()) != 1) {
    return absl::InternalError("failed to wrap RSA key");
  }
  return pkey;
}

absl::StatusOr<bssl::UniquePtr<EVP_PKEY>>
generateEcdsaKey(ConfigProto::LocalSigner::EcdsaCurve curve) {
  int nid = NID_X9_62_prime256v1;
  switch (curve) {
  case ConfigProto::LocalSigner::ECDSA_CURVE_UNSPECIFIED:
  case ConfigProto::LocalSigner::ECDSA_CURVE_P256:
    nid = NID_X9_62_prime256v1;
    break;
  case ConfigProto::LocalSigner::ECDSA_CURVE_P384:
    nid = NID_secp384r1;
    break;
  default:
    return absl::InvalidArgumentError("unsupported ECDSA curve");
  }

  bssl::UniquePtr<EC_KEY> ec_key(EC_KEY_new_by_curve_name(nid));
  if (!ec_key || EC_KEY_generate_key(ec_key.get()) != 1) {
    return absl::InternalError("failed to generate ECDSA key");
  }

  bssl::UniquePtr<EVP_PKEY> pkey(EVP_PKEY_new());
  if (!pkey || EVP_PKEY_set1_EC_KEY(pkey.get(), ec_key.get()) != 1) {
    return absl::InternalError("failed to wrap ECDSA key");
  }
  return pkey;
}

absl::StatusOr<const EVP_MD*>
digestForSignatureHash(ConfigProto::LocalSigner::SignatureHash signature_hash) {
  switch (signature_hash) {
  case ConfigProto::LocalSigner::SIGNATURE_HASH_UNSPECIFIED:
  case ConfigProto::LocalSigner::SIGNATURE_HASH_SHA256:
    return EVP_sha256();
  case ConfigProto::LocalSigner::SIGNATURE_HASH_SHA384:
    return EVP_sha384();
  case ConfigProto::LocalSigner::SIGNATURE_HASH_SHA512:
    return EVP_sha512();
  default:
    return absl::InvalidArgumentError("unsupported signature hash");
  }
}

absl::StatusOr<bssl::UniquePtr<EVP_PKEY>>
generateLeafKey(ConfigProto::LocalSigner::KeyType key_type, uint32_t rsa_key_bits,
                ConfigProto::LocalSigner::EcdsaCurve ecdsa_curve) {
  switch (key_type) {
  case ConfigProto::LocalSigner::KEY_TYPE_UNSPECIFIED:
  case ConfigProto::LocalSigner::KEY_TYPE_RSA:
    return generateRsaKey(rsa_key_bits);
  case ConfigProto::LocalSigner::KEY_TYPE_ECDSA:
    return generateEcdsaKey(ecdsa_curve);
  default:
    return absl::InvalidArgumentError("unsupported key type");
  }
}

absl::Status addExtension(X509* cert, X509* issuer, int nid, absl::string_view value) {
  X509V3_CTX ctx;
  X509V3_set_ctx(&ctx, issuer, cert, nullptr, nullptr, 0);
  bssl::UniquePtr<X509_EXTENSION> ext(
      X509V3_EXT_nconf_nid(nullptr, &ctx, nid, std::string(value).c_str()));
  if (!ext || X509_add_ext(cert, ext.get(), -1) != 1) {
    return absl::InternalError("failed to add X509 extension");
  }
  return absl::OkStatus();
}

absl::Status addDnsSans(X509* cert, X509* issuer, const std::vector<std::string>& dns_names) {
  if (dns_names.empty()) {
    return absl::OkStatus();
  }
  std::vector<std::string> san_entries;
  san_entries.reserve(dns_names.size());
  for (const auto& name : dns_names) {
    if (!name.empty()) {
      san_entries.push_back(absl::StrCat("DNS:", name));
    }
  }
  if (san_entries.empty()) {
    return absl::OkStatus();
  }
  return addExtension(cert, issuer, NID_subject_alt_name, absl::StrJoin(san_entries, ","));
}

absl::Status addSubjectNameEntry(X509_NAME* subject, const char* field, absl::string_view value) {
  if (value.empty()) {
    return absl::OkStatus();
  }
  if (!subject ||
      X509_NAME_add_entry_by_txt(subject, field, MBSTRING_ASC,
                                 reinterpret_cast<const unsigned char*>(std::string(value).c_str()),
                                 -1, -1, 0) != 1) {
    return absl::InternalError("failed to set subject attribute");
  }
  return absl::OkStatus();
}

absl::StatusOr<absl::optional<std::string>>
keyUsageToken(ConfigProto::LocalSigner::KeyUsage usage) {
  switch (usage) {
  case ConfigProto::LocalSigner::KEY_USAGE_UNSPECIFIED:
    return absl::nullopt;
  case ConfigProto::LocalSigner::KEY_USAGE_DIGITAL_SIGNATURE:
    return std::string("digitalSignature");
  case ConfigProto::LocalSigner::KEY_USAGE_CONTENT_COMMITMENT:
    return std::string("nonRepudiation");
  case ConfigProto::LocalSigner::KEY_USAGE_KEY_ENCIPHERMENT:
    return std::string("keyEncipherment");
  case ConfigProto::LocalSigner::KEY_USAGE_DATA_ENCIPHERMENT:
    return std::string("dataEncipherment");
  case ConfigProto::LocalSigner::KEY_USAGE_KEY_AGREEMENT:
    return std::string("keyAgreement");
  case ConfigProto::LocalSigner::KEY_USAGE_KEY_CERT_SIGN:
    return std::string("keyCertSign");
  case ConfigProto::LocalSigner::KEY_USAGE_CRL_SIGN:
    return std::string("cRLSign");
  default:
    return absl::InvalidArgumentError("unsupported key usage");
  }
}

absl::StatusOr<absl::optional<std::string>>
extendedKeyUsageToken(ConfigProto::LocalSigner::ExtendedKeyUsage usage) {
  switch (usage) {
  case ConfigProto::LocalSigner::EXTENDED_KEY_USAGE_UNSPECIFIED:
    return absl::nullopt;
  case ConfigProto::LocalSigner::EXTENDED_KEY_USAGE_SERVER_AUTH:
    return std::string("serverAuth");
  case ConfigProto::LocalSigner::EXTENDED_KEY_USAGE_CLIENT_AUTH:
    return std::string("clientAuth");
  case ConfigProto::LocalSigner::EXTENDED_KEY_USAGE_CODE_SIGNING:
    return std::string("codeSigning");
  case ConfigProto::LocalSigner::EXTENDED_KEY_USAGE_EMAIL_PROTECTION:
    return std::string("emailProtection");
  case ConfigProto::LocalSigner::EXTENDED_KEY_USAGE_TIME_STAMPING:
    return std::string("timeStamping");
  case ConfigProto::LocalSigner::EXTENDED_KEY_USAGE_OCSP_SIGNING:
    return std::string("OCSPSigning");
  default:
    return absl::InvalidArgumentError("unsupported extended key usage");
  }
}

std::vector<ConfigProto::LocalSigner::KeyUsage>
toKeyUsageVector(const Protobuf::RepeatedField<int>& values) {
  std::vector<ConfigProto::LocalSigner::KeyUsage> out;
  out.reserve(values.size());
  for (const int value : values) {
    out.push_back(static_cast<ConfigProto::LocalSigner::KeyUsage>(value));
  }
  return out;
}

std::vector<ConfigProto::LocalSigner::ExtendedKeyUsage>
toExtendedKeyUsageVector(const Protobuf::RepeatedField<int>& values) {
  std::vector<ConfigProto::LocalSigner::ExtendedKeyUsage> out;
  out.reserve(values.size());
  for (const int value : values) {
    out.push_back(static_cast<ConfigProto::LocalSigner::ExtendedKeyUsage>(value));
  }
  return out;
}

absl::StatusOr<int64_t> randomPositiveSerial() {
  uint64_t raw = 0;
  if (RAND_bytes(reinterpret_cast<uint8_t*>(&raw), sizeof(raw)) != 1) {
    return absl::InternalError("failed to generate serial");
  }
  raw = raw & 0x7fffffffULL;
  if (raw == 0) {
    raw = 1;
  }
  return static_cast<int64_t>(raw);
}

absl::StatusOr<std::pair<std::string, std::string>>
mintLeafCertificatePem(X509* ca_cert, EVP_PKEY* ca_key, absl::string_view host_name,
                       absl::string_view subject_org, uint32_t ttl_days,
                       ConfigProto::LocalSigner::KeyType key_type, uint32_t rsa_key_bits,
                       ConfigProto::LocalSigner::EcdsaCurve ecdsa_curve,
                       ConfigProto::LocalSigner::SignatureHash signature_hash,
                       uint32_t not_before_backdate_seconds, absl::string_view subject_common_name,
                       absl::string_view subject_organizational_unit,
                       absl::string_view subject_country,
                       absl::string_view subject_state_or_province,
                       absl::string_view subject_locality,
                       const std::vector<std::string>& dns_sans,
                       const std::vector<ConfigProto::LocalSigner::KeyUsage>& key_usages,
                       const std::vector<ConfigProto::LocalSigner::ExtendedKeyUsage>&
                           extended_key_usages,
                       const absl::optional<bool>& basic_constraints_ca) {
  auto leaf_key_or = generateLeafKey(key_type, rsa_key_bits, ecdsa_curve);
  RETURN_IF_NOT_OK(leaf_key_or.status());
  bssl::UniquePtr<EVP_PKEY> leaf_key = std::move(leaf_key_or.value());

  bssl::UniquePtr<X509> cert(X509_new());
  if (!cert || X509_set_version(cert.get(), 2) != 1) {
    return absl::InternalError("failed to initialize leaf certificate");
  }
  auto serial_or = randomPositiveSerial();
  RETURN_IF_NOT_OK(serial_or.status());
  if (ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), serial_or.value()) != 1) {
    return absl::InternalError("failed to set leaf serial");
  }
  if (X509_gmtime_adj(X509_getm_notBefore(cert.get()),
                      -static_cast<int64_t>(not_before_backdate_seconds)) == nullptr ||
      X509_gmtime_adj(X509_getm_notAfter(cert.get()),
                      static_cast<int64_t>(ttl_days) * 24 * 60 * 60) == nullptr) {
    return absl::InternalError("failed to set certificate validity");
  }
  if (X509_set_issuer_name(cert.get(), X509_get_subject_name(ca_cert)) != 1 ||
      X509_set_pubkey(cert.get(), leaf_key.get()) != 1) {
    return absl::InternalError("failed to set issuer/public key");
  }

  X509_NAME* subject = X509_get_subject_name(cert.get());
  const auto cn = subject_common_name.empty() ? std::string(host_name) : std::string(subject_common_name);
  RETURN_IF_NOT_OK(addSubjectNameEntry(subject, "CN", cn));
  RETURN_IF_NOT_OK(addSubjectNameEntry(subject, "O", subject_org));
  RETURN_IF_NOT_OK(addSubjectNameEntry(subject, "OU", subject_organizational_unit));
  RETURN_IF_NOT_OK(addSubjectNameEntry(subject, "C", subject_country));
  RETURN_IF_NOT_OK(addSubjectNameEntry(subject, "ST", subject_state_or_province));
  RETURN_IF_NOT_OK(addSubjectNameEntry(subject, "L", subject_locality));

  RETURN_IF_NOT_OK(addDnsSans(cert.get(), ca_cert, dns_sans));
  if (!key_usages.empty()) {
    std::vector<std::string> key_usage_tokens;
    key_usage_tokens.reserve(key_usages.size());
    for (const auto usage : key_usages) {
      auto token_or = keyUsageToken(usage);
      RETURN_IF_NOT_OK(token_or.status());
      if (token_or.value().has_value()) {
        key_usage_tokens.push_back(token_or.value().value());
      }
    }
    if (!key_usage_tokens.empty()) {
      RETURN_IF_NOT_OK(addExtension(cert.get(), ca_cert, NID_key_usage,
                                    absl::StrCat("critical,", absl::StrJoin(key_usage_tokens, ","))));
    }
  }
  if (!extended_key_usages.empty()) {
    std::vector<std::string> eku_tokens;
    eku_tokens.reserve(extended_key_usages.size());
    for (const auto usage : extended_key_usages) {
      auto token_or = extendedKeyUsageToken(usage);
      RETURN_IF_NOT_OK(token_or.status());
      if (token_or.value().has_value()) {
        eku_tokens.push_back(token_or.value().value());
      }
    }
    if (!eku_tokens.empty()) {
      RETURN_IF_NOT_OK(
          addExtension(cert.get(), ca_cert, NID_ext_key_usage, absl::StrJoin(eku_tokens, ",")));
    }
  }
  if (basic_constraints_ca.has_value()) {
    RETURN_IF_NOT_OK(addExtension(cert.get(), ca_cert, NID_basic_constraints,
                                  basic_constraints_ca.value() ? "critical,CA:TRUE"
                                                               : "critical,CA:FALSE"));
  }
  auto digest_or = digestForSignatureHash(signature_hash);
  RETURN_IF_NOT_OK(digest_or.status());
  if (X509_sign(cert.get(), ca_key, digest_or.value()) <= 0) {
    return absl::InternalError("failed to sign leaf certificate");
  }

  bssl::UniquePtr<BIO> cert_bio(BIO_new(BIO_s_mem()));
  bssl::UniquePtr<BIO> key_bio(BIO_new(BIO_s_mem()));
  if (!cert_bio || !key_bio || PEM_write_bio_X509(cert_bio.get(), cert.get()) != 1 ||
      PEM_write_bio_PrivateKey(key_bio.get(), leaf_key.get(), nullptr, nullptr, 0, nullptr,
                               nullptr) != 1) {
    return absl::InternalError("failed to write leaf certificate/key PEM");
  }
  auto cert_pem_or = bioToString(cert_bio.get());
  RETURN_IF_NOT_OK(cert_pem_or.status());
  auto key_pem_or = bioToString(key_bio.get());
  RETURN_IF_NOT_OK(key_pem_or.status());
  return std::make_pair(std::move(cert_pem_or.value()), std::move(key_pem_or.value()));
}

struct LocalSignerOptions {
  std::string key;
  std::string ca_cert_path;
  std::string ca_key_path;
  uint32_t cert_ttl_days;
  std::string subject_organization;
  ConfigProto::LocalSigner::KeyType key_type;
  uint32_t rsa_key_bits;
  ConfigProto::LocalSigner::EcdsaCurve ecdsa_curve;
  ConfigProto::LocalSigner::SignatureHash signature_hash;
  uint32_t not_before_backdate_seconds;
  ConfigProto::LocalSigner::HostnameValidation hostname_validation;
  std::string runtime_key_prefix;
  ConfigProto::LocalSigner::CaReloadFailurePolicy ca_reload_failure_policy;
  bool include_primary_dns_san;
  std::vector<std::string> additional_dns_sans;
  std::vector<ConfigProto::LocalSigner::KeyUsage> key_usages;
  std::vector<ConfigProto::LocalSigner::ExtendedKeyUsage> extended_key_usages;
  absl::optional<bool> basic_constraints_ca;
  std::string subject_common_name;
  std::string subject_organizational_unit;
  std::string subject_country;
  std::string subject_state_or_province;
  std::string subject_locality;
};

class LocalSignerCertificateProvider
    : public Secret::TlsCertificateConfigProvider,
      protected Logger::Loggable<Logger::Id::secret> {
public:
  LocalSignerCertificateProvider(std::string secret_name,
                                 Server::Configuration::ServerFactoryContext& factory_context,
                                 const LocalSignerOptions& options)
      : secret_name_(std::move(secret_name)), factory_context_(factory_context), options_(options) {}

  const envoy::extensions::transport_sockets::tls::v3::TlsCertificate* secret() const override {
    return tls_certificate_.get();
  }

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr addValidationCallback(
      std::function<absl::Status(
          const envoy::extensions::transport_sockets::tls::v3::TlsCertificate&)> callback)
      override {
    if (tls_certificate_) {
      THROW_IF_NOT_OK(callback(*tls_certificate_));
    }
    return validation_callback_manager_.add(callback);
  }

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addUpdateCallback(std::function<absl::Status()> callback) override {
    if (tls_certificate_) {
      THROW_IF_NOT_OK(callback());
    }
    return update_callback_manager_.add(callback);
  }

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addRemoveCallback(std::function<absl::Status()> callback) override {
    return remove_callback_manager_.add(callback);
  }

  void start() override { ASSERT_IS_MAIN_OR_TEST_THREAD(); }

  absl::Status ensureReady() {
    ASSERT_IS_MAIN_OR_TEST_THREAD();
    if (tls_certificate_ != nullptr) {
      return absl::OkStatus();
    }
    return updateCertificate();
  }

  absl::Status refresh() {
    ASSERT_IS_MAIN_OR_TEST_THREAD();
    if (tls_certificate_ == nullptr) {
      return absl::OkStatus();
    }
    return updateCertificate();
  }

private:
  absl::optional<std::string> secretNameToHostname() const {
    if (secret_name_.empty()) {
      return {};
    }
    const size_t slash = secret_name_.rfind('/');
    const absl::string_view maybe_host =
        (slash == absl::string_view::npos) ? absl::string_view(secret_name_)
                                           : absl::string_view(secret_name_).substr(slash + 1);
    if (maybe_host.empty()) {
      return {};
    }

    auto effective_hostname_validation = options_.hostname_validation;
    if (!options_.runtime_key_prefix.empty()) {
      auto& snapshot = factory_context_.runtime().snapshot();
      if (const auto hostname_validation = parseHostnameValidation(snapshot.getInteger(
              absl::StrCat(options_.runtime_key_prefix, ".hostname_validation"),
              static_cast<uint64_t>(effective_hostname_validation)))) {
        effective_hostname_validation = hostname_validation.value();
      }
    }
    const bool allow_underscore =
        (effective_hostname_validation ==
             ConfigProto::LocalSigner::HOSTNAME_VALIDATION_UNSPECIFIED ||
         effective_hostname_validation == ConfigProto::LocalSigner::HOSTNAME_VALIDATION_PERMISSIVE);

    for (char c : maybe_host) {
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
          c == '.' || (allow_underscore && c == '_')) {
        continue;
      }
      return {};
    }
    return std::string(absl::AsciiStrToLower(maybe_host));
  }

  absl::Status refreshCaMaterial() {
    const auto cert_stat = factory_context_.api().fileSystem().stat(options_.ca_cert_path);
    if (!cert_stat.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("failed to stat CA cert path '", options_.ca_cert_path, "': ",
                       cert_stat.err_->getErrorDetails()));
    }
    const auto key_stat = factory_context_.api().fileSystem().stat(options_.ca_key_path);
    if (!key_stat.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("failed to stat CA key path '", options_.ca_key_path, "': ",
                       key_stat.err_->getErrorDetails()));
    }

    const bool should_reload =
        !ca_material_.loaded_ ||
        ca_material_.cert_last_modified_ != cert_stat.return_value_.time_last_modified_ ||
        ca_material_.key_last_modified_ != key_stat.return_value_.time_last_modified_;

    if (!should_reload) {
      return absl::OkStatus();
    }

    const auto ca_cert_pem_or =
        factory_context_.api().fileSystem().fileReadToEnd(options_.ca_cert_path);
    const auto ca_key_pem_or = factory_context_.api().fileSystem().fileReadToEnd(options_.ca_key_path);
    if (!ca_cert_pem_or.ok() || !ca_key_pem_or.ok()) {
      if (ca_material_.loaded_ &&
          options_.ca_reload_failure_policy ==
              ConfigProto::LocalSigner::CA_RELOAD_FAILURE_POLICY_FAIL_OPEN) {
        ENVOY_LOG_EVERY_POW_2(
            warn,
            "failed to reload local signer CA material, keeping previously loaded CA (fail-open)");
        return absl::OkStatus();
      }
      if (!ca_cert_pem_or.ok()) {
        return ca_cert_pem_or.status();
      }
      return ca_key_pem_or.status();
    }

    bssl::UniquePtr<BIO> ca_cert_bio(
        BIO_new_mem_buf(ca_cert_pem_or.value().data(), ca_cert_pem_or.value().size()));
    bssl::UniquePtr<BIO> ca_key_bio(
        BIO_new_mem_buf(ca_key_pem_or.value().data(), ca_key_pem_or.value().size()));
    if (!ca_cert_bio || !ca_key_bio) {
      if (ca_material_.loaded_ &&
          options_.ca_reload_failure_policy ==
              ConfigProto::LocalSigner::CA_RELOAD_FAILURE_POLICY_FAIL_OPEN) {
        ENVOY_LOG_EVERY_POW_2(
            warn,
            "failed to allocate CA BIO during local signer reload, keeping prior CA (fail-open)");
        return absl::OkStatus();
      }
      return absl::InternalError("failed to allocate CA BIO");
    }

    bssl::UniquePtr<X509> ca_cert(PEM_read_bio_X509(ca_cert_bio.get(), nullptr, nullptr, nullptr));
    bssl::UniquePtr<EVP_PKEY> ca_key(
        PEM_read_bio_PrivateKey(ca_key_bio.get(), nullptr, nullptr, nullptr));
    if (!ca_cert || !ca_key) {
      if (ca_material_.loaded_ &&
          options_.ca_reload_failure_policy ==
              ConfigProto::LocalSigner::CA_RELOAD_FAILURE_POLICY_FAIL_OPEN) {
        ENVOY_LOG_EVERY_POW_2(
            warn,
            "failed to parse CA PEM during local signer reload, keeping prior CA (fail-open)");
        return absl::OkStatus();
      }
      return absl::InvalidArgumentError("failed to parse CA cert/key PEM");
    }

    ca_material_.cert_ = std::move(ca_cert);
    ca_material_.key_ = std::move(ca_key);
    ca_material_.cert_last_modified_ = cert_stat.return_value_.time_last_modified_;
    ca_material_.key_last_modified_ = key_stat.return_value_.time_last_modified_;
    ca_material_.loaded_ = true;
    return absl::OkStatus();
  }

  absl::Status updateCertificate() {
    auto hostname_or = secretNameToHostname();
    if (!hostname_or) {
      return absl::InvalidArgumentError(
          absl::StrCat("cannot derive hostname from secret name: ", secret_name_));
    }

    RETURN_IF_NOT_OK(refreshCaMaterial());

    uint32_t effective_cert_ttl_days = options_.cert_ttl_days;
    uint32_t effective_not_before_backdate_seconds = options_.not_before_backdate_seconds;
    uint32_t effective_rsa_key_bits = options_.rsa_key_bits;
    auto effective_key_type = options_.key_type;
    auto effective_ecdsa_curve = options_.ecdsa_curve;
    auto effective_signature_hash = options_.signature_hash;
    auto effective_hostname_validation = options_.hostname_validation;

    if (!options_.runtime_key_prefix.empty()) {
      auto& snapshot = factory_context_.runtime().snapshot();
      effective_cert_ttl_days = snapshot.getInteger(
          absl::StrCat(options_.runtime_key_prefix, ".cert_ttl_days"), effective_cert_ttl_days);
      effective_not_before_backdate_seconds =
          snapshot.getInteger(absl::StrCat(options_.runtime_key_prefix, ".not_before_backdate_seconds"),
                              effective_not_before_backdate_seconds);
      const uint32_t runtime_rsa_key_bits =
          snapshot.getInteger(absl::StrCat(options_.runtime_key_prefix, ".rsa_key_bits"),
                              effective_rsa_key_bits);
      if (runtime_rsa_key_bits > 0 && isValidRsaKeyBits(runtime_rsa_key_bits)) {
        effective_rsa_key_bits = runtime_rsa_key_bits;
      } else if (runtime_rsa_key_bits > 0) {
        ENVOY_LOG_EVERY_POW_2(warn,
                              "Ignoring invalid runtime rsa_key_bits={} for prefix '{}', "
                              "keeping {}",
                              runtime_rsa_key_bits, options_.runtime_key_prefix,
                              effective_rsa_key_bits);
      }
      if (const auto key_type = parseKeyType(snapshot.getInteger(
              absl::StrCat(options_.runtime_key_prefix, ".key_type"),
              static_cast<uint64_t>(effective_key_type)))) {
        effective_key_type = key_type.value();
      }
      if (const auto ecdsa_curve = parseEcdsaCurve(snapshot.getInteger(
              absl::StrCat(options_.runtime_key_prefix, ".ecdsa_curve"),
              static_cast<uint64_t>(effective_ecdsa_curve)))) {
        effective_ecdsa_curve = ecdsa_curve.value();
      }
      if (const auto signature_hash = parseSignatureHash(snapshot.getInteger(
              absl::StrCat(options_.runtime_key_prefix, ".signature_hash"),
              static_cast<uint64_t>(effective_signature_hash)))) {
        effective_signature_hash = signature_hash.value();
      }
      if (const auto hostname_validation = parseHostnameValidation(snapshot.getInteger(
              absl::StrCat(options_.runtime_key_prefix, ".hostname_validation"),
              static_cast<uint64_t>(effective_hostname_validation)))) {
        effective_hostname_validation = hostname_validation.value();
      }
    }

    const bool allow_underscore =
        (effective_hostname_validation ==
             ConfigProto::LocalSigner::HOSTNAME_VALIDATION_UNSPECIFIED ||
         effective_hostname_validation == ConfigProto::LocalSigner::HOSTNAME_VALIDATION_PERMISSIVE);
    if (!allow_underscore && hostname_or->find('_') != std::string::npos) {
      return absl::InvalidArgumentError(
          absl::StrCat("invalid hostname for strict validation: ", *hostname_or));
    }

    std::vector<std::string> dns_sans;
    dns_sans.reserve(options_.additional_dns_sans.size() + 1);
    if (options_.include_primary_dns_san) {
      dns_sans.push_back(*hostname_or);
    }
    dns_sans.insert(dns_sans.end(), options_.additional_dns_sans.begin(),
                    options_.additional_dns_sans.end());

    auto leaf_pems_or = mintLeafCertificatePem(
        ca_material_.cert_.get(), ca_material_.key_.get(), *hostname_or, options_.subject_organization,
        effective_cert_ttl_days, effective_key_type, effective_rsa_key_bits, effective_ecdsa_curve,
        effective_signature_hash, effective_not_before_backdate_seconds, options_.subject_common_name,
        options_.subject_organizational_unit, options_.subject_country,
        options_.subject_state_or_province, options_.subject_locality, dns_sans,
        options_.key_usages, options_.extended_key_usages, options_.basic_constraints_ca);
    RETURN_IF_NOT_OK(leaf_pems_or.status());

    auto tls_certificate =
        std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>();
    tls_certificate->mutable_certificate_chain()->set_inline_bytes(leaf_pems_or->first);
    tls_certificate->mutable_private_key()->set_inline_bytes(leaf_pems_or->second);

    tls_certificate_ = std::move(tls_certificate);
    RETURN_IF_NOT_OK(update_callback_manager_.runCallbacks());
    return absl::OkStatus();
  }

  struct LocalCaMaterial {
    bssl::UniquePtr<X509> cert_;
    bssl::UniquePtr<EVP_PKEY> key_;
    absl::optional<SystemTime> cert_last_modified_;
    absl::optional<SystemTime> key_last_modified_;
    bool loaded_{false};
  };

  const std::string secret_name_;
  Server::Configuration::ServerFactoryContext& factory_context_;
  const LocalSignerOptions options_;
  LocalCaMaterial ca_material_;
  Secret::TlsCertificatePtr tls_certificate_;
  Common::CallbackManager<absl::Status,
                          const envoy::extensions::transport_sockets::tls::v3::TlsCertificate&>
      validation_callback_manager_;
  Common::CallbackManager<absl::Status> update_callback_manager_;
  Common::CallbackManager<absl::Status> remove_callback_manager_;
};

class LocalSignerCertificateProviderStore {
public:
  static LocalSignerCertificateProviderStore& instance() {
    static LocalSignerCertificateProviderStore store;
    return store;
  }

  absl::StatusOr<Secret::TlsCertificateConfigProviderSharedPtr>
  findOrCreate(absl::string_view secret_name,
               Server::Configuration::ServerFactoryContext& factory_context,
               const LocalSignerOptions& options) {
    ASSERT_IS_MAIN_OR_TEST_THREAD();
    const std::string cache_key = absl::StrCat(options.key, "#", secret_name);
    if (auto it = providers_.find(cache_key); it != providers_.end()) {
      if (auto existing = it->second.lock(); existing) {
        auto provider = std::static_pointer_cast<LocalSignerCertificateProvider>(existing);
        RETURN_IF_NOT_OK(provider->ensureReady());
        return existing;
      }
      providers_.erase(it);
    }
    auto provider =
        std::make_shared<LocalSignerCertificateProvider>(std::string(secret_name), factory_context, options);
    RETURN_IF_NOT_OK(provider->ensureReady());
    providers_[cache_key] = provider;
    return provider;
  }

  absl::Status refreshAll(absl::string_view options_key) {
    ASSERT_IS_MAIN_OR_TEST_THREAD();
    for (auto it = providers_.begin(); it != providers_.end();) {
      if (!absl::StartsWith(it->first, options_key)) {
        ++it;
        continue;
      }
      auto provider = it->second.lock();
      if (!provider) {
        providers_.erase(it++);
        continue;
      }
      auto typed_provider = std::static_pointer_cast<LocalSignerCertificateProvider>(provider);
      RETURN_IF_NOT_OK(typed_provider->refresh());
      ++it;
    }
    return absl::OkStatus();
  }

private:
  absl::flat_hash_map<std::string, std::weak_ptr<Secret::TlsCertificateConfigProvider>> providers_;
};

} // namespace

AsyncContextConfig::AsyncContextConfig(absl::string_view cert_name,
                                       Server::Configuration::ServerFactoryContext& factory_context,
                                       const envoy::config::core::v3::ConfigSource& config_source,
                                       OptRef<Init::Manager> init_manager, UpdateCb update_cb,
                                       RemoveCb remove_cb)
    : AsyncContextConfig(
          cert_name, factory_context,
          factory_context.secretManager().findOrCreateTlsCertificateProvider(
              config_source, std::string(cert_name), factory_context, init_manager, false),
          update_cb, remove_cb) {}

AsyncContextConfig::AsyncContextConfig(
    absl::string_view cert_name, Server::Configuration::ServerFactoryContext& factory_context,
    Secret::TlsCertificateConfigProviderSharedPtr cert_provider, UpdateCb update_cb,
    RemoveCb remove_cb)
    : factory_context_(factory_context), cert_name_(cert_name),
      cert_provider_(std::move(cert_provider)),
      update_cb_(update_cb),
      update_cb_handle_(cert_provider_->addUpdateCallback([this]() { return loadCert(); })),
      remove_cb_(remove_cb), remove_cb_handle_(cert_provider_->addRemoveCallback(
                                 [this]() { return remove_cb_(cert_name_); })) {}

absl::Status AsyncContextConfig::loadCert() {
  // Called on main, possibly during the constructor.
  auto* secret = cert_provider_->secret();
  if (secret != nullptr) {
    Server::GenericFactoryContextImpl generic_context(factory_context_,
                                                      factory_context_.messageValidationVisitor());
    auto config_or_error = Ssl::TlsCertificateConfigImpl::create(
        *secret, generic_context, factory_context_.api(), cert_name_);
    RETURN_IF_NOT_OK(config_or_error.status());
    cert_config_.emplace(*std::move(config_or_error));
    return update_cb_(cert_name_, *cert_config_);
  }
  return absl::OkStatus();
}

const Ssl::TlsContext& ServerAsyncContext::tlsContext() const { return tls_contexts_[0]; }
const Ssl::TlsContext& ClientAsyncContext::tlsContext() const { return tls_contexts_[0]; }

void Handle::notify(AsyncContextConstSharedPtr cert_ctx) {
  ASSERT(cb_);
  bool staple = false;
  if (cert_ctx) {
    active_context_ = cert_ctx;
    staple =
        (ocspStapleAction(active_context_->tlsContext(), client_ocsp_capable_,
                          active_context_->ocspStaplePolicy()) == Ssl::OcspStapleAction::Staple);
  }
  Event::Dispatcher& dispatcher = cb_->dispatcher();
  // TODO: This could benefit from batching events by the dispatcher in the outer loop.
  dispatcher.post([cb = std::move(cb_), cert_ctx, staple] {
    cb->onCertificateSelectionResult(
        makeOptRefFromPtr(cert_ctx ? &cert_ctx->tlsContext() : nullptr), staple);
  });
  cb_ = nullptr;
}

CertSelectionStatsSharedPtr generateCertSelectionStats(Stats::Scope& store) {
  return std::make_shared<CertSelectionStats>(CertSelectionStats{
      ALL_CERT_SELECTION_STATS(POOL_COUNTER(store), POOL_GAUGE(store), POOL_HISTOGRAM(store))});
}

SecretManager::SecretManager(const ConfigProto& config,
                             Server::Configuration::GenericFactoryContext& factory_context,
                             AsyncContextFactory&& context_factory)
    : stats_scope_(factory_context.scope().createScope("on_demand_secret.")),
      stats_(generateCertSelectionStats(*stats_scope_)),
      factory_context_(factory_context.serverFactoryContext()),
      config_source_(config.config_source()), context_factory_(std::move(context_factory)),
      local_signer_enabled_(config.has_local_signer()),
      local_signer_key_(config.has_local_signer() ? config.local_signer().SerializeAsString() : ""),
      local_ca_cert_path_(config.has_local_signer() ? config.local_signer().ca_cert_path() : ""),
      local_ca_key_path_(config.has_local_signer() ? config.local_signer().ca_key_path() : ""),
      local_cert_ttl_days_(config.has_local_signer() && config.local_signer().cert_ttl_days() > 0
                               ? config.local_signer().cert_ttl_days()
                               : DefaultLocalCertTtlDays),
      local_subject_organization_(config.has_local_signer() &&
                                          !config.local_signer().subject_organization().empty()
                                      ? config.local_signer().subject_organization()
                                      : DefaultLocalSubjectOrganization),
      local_key_type_(config.has_local_signer() ? config.local_signer().key_type()
                                                : ConfigProto::LocalSigner::KEY_TYPE_UNSPECIFIED),
      local_rsa_key_bits_(config.has_local_signer() && config.local_signer().rsa_key_bits() > 0
                              ? config.local_signer().rsa_key_bits()
                              : DefaultRsaKeyBits),
      local_ecdsa_curve_(config.has_local_signer()
                             ? config.local_signer().ecdsa_curve()
                             : ConfigProto::LocalSigner::ECDSA_CURVE_UNSPECIFIED),
      local_signature_hash_(config.has_local_signer()
                                ? config.local_signer().signature_hash()
                                : ConfigProto::LocalSigner::SIGNATURE_HASH_UNSPECIFIED),
      local_not_before_backdate_seconds_(
          config.has_local_signer() && config.local_signer().not_before_backdate_seconds() > 0
              ? config.local_signer().not_before_backdate_seconds()
              : DefaultNotBeforeBackdateSeconds),
      local_hostname_validation_(
          config.has_local_signer()
              ? config.local_signer().hostname_validation()
              : ConfigProto::LocalSigner::HOSTNAME_VALIDATION_UNSPECIFIED),
      local_runtime_key_prefix_(
          config.has_local_signer() && !config.local_signer().runtime_key_prefix().empty()
              ? config.local_signer().runtime_key_prefix()
              : DefaultLocalRuntimeKeyPrefix),
      local_ca_reload_failure_policy_(
          config.has_local_signer()
              ? config.local_signer().ca_reload_failure_policy()
              : ConfigProto::LocalSigner::CA_RELOAD_FAILURE_POLICY_UNSPECIFIED),
      local_include_primary_dns_san_(
          !config.has_local_signer() || !config.local_signer().has_include_primary_dns_san()
              ? true
              : config.local_signer().include_primary_dns_san().value()),
      local_additional_dns_sans_(config.has_local_signer()
                                     ? std::vector<std::string>(
                                           config.local_signer().additional_dns_sans().begin(),
                                           config.local_signer().additional_dns_sans().end())
                                     : std::vector<std::string>{}),
      local_key_usages_(config.has_local_signer()
                            ? toKeyUsageVector(config.local_signer().key_usages())
                            : std::vector<ConfigProto::LocalSigner::KeyUsage>{}),
      local_extended_key_usages_(
          config.has_local_signer()
              ? toExtendedKeyUsageVector(config.local_signer().extended_key_usages())
              : std::vector<ConfigProto::LocalSigner::ExtendedKeyUsage>{}),
      local_basic_constraints_ca_(
          config.has_local_signer() && config.local_signer().has_basic_constraints_ca()
              ? absl::optional<bool>(config.local_signer().basic_constraints_ca().value())
              : absl::nullopt),
      local_subject_common_name_(
          config.has_local_signer() ? config.local_signer().subject_common_name() : ""),
      local_subject_organizational_unit_(
          config.has_local_signer() ? config.local_signer().subject_organizational_unit() : ""),
      local_subject_country_(config.has_local_signer() ? config.local_signer().subject_country()
                                                       : ""),
      local_subject_state_or_province_(
          config.has_local_signer() ? config.local_signer().subject_state_or_province() : ""),
      local_subject_locality_(config.has_local_signer() ? config.local_signer().subject_locality()
                                                        : ""),
      cert_contexts_(factory_context_.threadLocal()) {
  cert_contexts_.set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalCerts>(); });
  if (local_signer_enabled_) {
    ENVOY_LOG(info, "on-demand selector using local signer (cert_ttl_days={})", local_cert_ttl_days_);
  }
  for (const auto& name : config.prefetch_secret_names()) {
    const OptRef<Init::Manager> init_manager =
        local_signer_enabled_ ? OptRef<Init::Manager>() : factory_context.initManager();
    addCertificateConfig(name, nullptr, init_manager);
  }
}

void SecretManager::addCertificateConfig(absl::string_view secret_name, HandleSharedPtr handle,
                                         OptRef<Init::Manager> init_manager) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  CacheEntry& entry = cache_[secret_name];
  if (handle) {
    if (entry.cert_context_) {
      handle->notify(entry.cert_context_);
    } else {
      entry.callbacks_.push_back(handle);
    }
  }

  // Should be last to trigger the callback since constructor can fire the update event for an
  // existing SDS subscription.
  if (entry.cert_config_ == nullptr) {
    stats_->cert_requested_.inc();
    stats_->cert_active_.inc();
    if (local_signer_enabled_) {
      auto provider = createLocalCertificateProvider(secret_name);
      if (!provider) {
        ENVOY_LOG(error, "failed to create local certificate provider for '{}'", secret_name);
        for (auto fetch_handle : entry.callbacks_) {
          if (auto cb_handle = fetch_handle.lock(); cb_handle) {
            cb_handle->notify(nullptr);
          }
        }
        entry.callbacks_.clear();
        cache_.erase(std::string(secret_name));
        stats_->cert_active_.dec();
      } else {
        entry.cert_config_ = std::make_unique<AsyncContextConfig>(
            secret_name, factory_context_, std::move(provider),
            [this](absl::string_view secret_name, const Ssl::TlsCertificateConfig& cert_config)
                -> absl::Status { return updateCertificate(secret_name, cert_config); },
            [this](absl::string_view secret_name) -> absl::Status {
              return removeCertificateConfig(secret_name);
            });
      }
      return;
    }
    entry.cert_config_ = std::make_unique<AsyncContextConfig>(
        secret_name, factory_context_, config_source_, init_manager,
        [this](absl::string_view secret_name, const Ssl::TlsCertificateConfig& cert_config)
            -> absl::Status { return updateCertificate(secret_name, cert_config); },
        [this](absl::string_view secret_name) -> absl::Status {
          return removeCertificateConfig(secret_name);
        });
  }
}

absl::Status SecretManager::updateCertificate(absl::string_view secret_name,
                                              const Ssl::TlsCertificateConfig& cert_config) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  absl::Status creation_status = absl::OkStatus();
  auto cert_context =
      context_factory_(*stats_scope_, factory_context_, cert_config, creation_status);
  RETURN_IF_NOT_OK(creation_status);

  // Update the future lookups and notify pending callbacks.
  setContext(secret_name, cert_context);
  CacheEntry& entry = cache_[secret_name];
  entry.cert_context_ = cert_context;
  size_t notify_count = 0;
  for (auto fetch_handle : entry.callbacks_) {
    if (auto handle = fetch_handle.lock(); handle) {
      handle->notify(cert_context);
      notify_count++;
    }
  }
  ENVOY_LOG(trace, "Notified {} pending connections about certificate '{}', out of queued {}",
            notify_count, secret_name, entry.callbacks_.size());
  entry.callbacks_.clear();
  return absl::OkStatus();
}

absl::Status SecretManager::updateAll() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (local_signer_enabled_) {
    return LocalSignerCertificateProviderStore::instance().refreshAll(local_signer_key_);
  }
  for (auto& [secret_name, entry] : cache_) {
    const auto& cert_config = entry.cert_config_->certConfig();
    // Refresh only if there is a certificate present and skip notifying.
    if (cert_config) {
      absl::Status creation_status = absl::OkStatus();
      entry.cert_context_ =
          context_factory_(*stats_scope_, factory_context_, *cert_config, creation_status);
      setContext(secret_name, entry.cert_context_);
      RETURN_IF_NOT_OK(creation_status);
    }
  }
  return absl::OkStatus();
}

Secret::TlsCertificateConfigProviderSharedPtr
SecretManager::createLocalCertificateProvider(absl::string_view secret_name) const {
  ASSERT(local_signer_enabled_);
  LocalSignerOptions options;
  options.key = local_signer_key_;
  options.ca_cert_path = local_ca_cert_path_;
  options.ca_key_path = local_ca_key_path_;
  options.cert_ttl_days = local_cert_ttl_days_;
  options.subject_organization = local_subject_organization_;
  options.key_type = local_key_type_;
  options.rsa_key_bits = local_rsa_key_bits_;
  options.ecdsa_curve = local_ecdsa_curve_;
  options.signature_hash = local_signature_hash_;
  options.not_before_backdate_seconds = local_not_before_backdate_seconds_;
  options.hostname_validation = local_hostname_validation_;
  options.runtime_key_prefix = local_runtime_key_prefix_;
  options.ca_reload_failure_policy = local_ca_reload_failure_policy_;
  options.include_primary_dns_san = local_include_primary_dns_san_;
  options.additional_dns_sans = local_additional_dns_sans_;
  options.key_usages = local_key_usages_;
  options.extended_key_usages = local_extended_key_usages_;
  options.basic_constraints_ca = local_basic_constraints_ca_;
  options.subject_common_name = local_subject_common_name_;
  options.subject_organizational_unit = local_subject_organizational_unit_;
  options.subject_country = local_subject_country_;
  options.subject_state_or_province = local_subject_state_or_province_;
  options.subject_locality = local_subject_locality_;
  auto provider_or_error = LocalSignerCertificateProviderStore::instance().findOrCreate(
      secret_name, factory_context_, options);
  if (!provider_or_error.ok()) {
    ENVOY_LOG(error, "failed to create local signer provider for '{}': {}", secret_name,
              provider_or_error.status().message());
    return nullptr;
  }
  return *std::move(provider_or_error);
}

absl::Status SecretManager::removeCertificateConfig(absl::string_view secret_name) {
  // We cannot remove the subscription caller directly because this is called during a callback
  // which continues later. Instead, we post to the main as a completion.
  factory_context_.mainThreadDispatcher().post(
      [weak_this = std::weak_ptr<SecretManager>(shared_from_this()),
       name = std::string(secret_name)] {
        if (auto that = weak_this.lock(); that) {
          that->doRemoveCertificateConfig(name);
        }
      });
  return absl::OkStatus();
}

void SecretManager::doRemoveCertificateConfig(absl::string_view secret_name) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  auto it = cache_.find(secret_name);
  if (it == cache_.end()) {
    return;
  }
  size_t notify_count = 0;
  for (auto fetch_handle : it->second.callbacks_) {
    if (auto handle = fetch_handle.lock(); handle) {
      handle->notify(nullptr);
      notify_count++;
    }
  }
  cache_.erase(it);
  setContext(secret_name, nullptr);
  stats_->cert_active_.dec();
  ENVOY_LOG(trace, "Removed certificate subscription for '{}', notified {} pending connections",
            secret_name, notify_count);
}

HandleSharedPtr SecretManager::fetchCertificate(absl::string_view secret_name,
                                                Ssl::CertificateSelectionCallbackPtr&& cb,
                                                bool client_ocsp_capable) {
  HandleSharedPtr handle = std::make_shared<Handle>(std::move(cb), client_ocsp_capable);
  // The manager might need to be destroyed after posting from a worker because
  // the filter chain is being removed. Therefore, use a weak_ptr and ignore
  // the request to fetch a secret. Handle can also be destroyed because the
  // underlying connection is reset, and handshake is cancelled.
  factory_context_.mainThreadDispatcher().post(
      [weak_this = std::weak_ptr<SecretManager>(shared_from_this()),
       name = std::string(secret_name), weak_handle = std::weak_ptr<Handle>(handle)]() mutable {
        auto that = weak_this.lock();
        auto handle = weak_handle.lock();
        if (that && handle) {
          that->addCertificateConfig(name, handle, {});
        }
      });
  return handle;
}

void SecretManager::setContext(absl::string_view secret_name, AsyncContextConstSharedPtr cert_ctx) {
  cert_contexts_.runOnAllThreads(
      [name = std::string(secret_name),
       cert_ctx = std::move(cert_ctx)](OptRef<ThreadLocalCerts> certs) {
        if (cert_ctx) {
          certs->ctx_by_name_[name] = cert_ctx;
        } else {
          certs->ctx_by_name_.erase(name);
        }
      },
      [stats_scope = stats_scope_, stats = stats_] { stats->cert_updated_.inc(); });
}

absl::optional<AsyncContextConstSharedPtr>
SecretManager::getContext(absl::string_view secret_name) const {
  OptRef<ThreadLocalCerts> current = cert_contexts_.get();
  if (current) {
    const auto it = current->ctx_by_name_.find(secret_name);
    if (it != current->ctx_by_name_.end()) {
      return it->second;
    };
  }
  return {};
}

Ssl::SelectionResult
BaseAsyncSelector::doSelectTlsContext(const std::string& name, const bool client_ocsp_capable,
                                      Ssl::CertificateSelectionCallbackPtr cb) {
  auto current_context = secret_manager_->getContext(name);
  if (current_context) {
    ENVOY_LOG(trace, "Using an existing certificate '{}'", name);
    const Ssl::TlsContext* tls_context = &current_context.value()->tlsContext();
    const auto staple_action = ocspStapleAction(*tls_context, client_ocsp_capable,
                                                current_context.value()->ocspStaplePolicy());
    auto handle = std::make_shared<Handle>(*std::move(current_context));
    return Ssl::SelectionResult{
        .status = Ssl::SelectionResult::SelectionStatus::Success,
        .selected_ctx = tls_context,
        .staple = (staple_action == Ssl::OcspStapleAction::Staple),
        .handle = std::move(handle),
    };
  }
  ENVOY_LOG(trace, "Requesting a certificate '{}'", name);
  return Ssl::SelectionResult{
      .status = Ssl::SelectionResult::SelectionStatus::Pending,
      .handle = secret_manager_->fetchCertificate(name, std::move(cb), client_ocsp_capable),
  };
}

Ssl::SelectionResult AsyncSelector::selectTlsContext(const SSL_CLIENT_HELLO& ssl_client_hello,
                                                     Ssl::CertificateSelectionCallbackPtr cb) {
  const std::string name = mapper_->deriveFromClientHello(ssl_client_hello);
  const bool client_ocsp_capable = isClientOcspCapable(ssl_client_hello);
  return doSelectTlsContext(name, client_ocsp_capable, std::move(cb));
}

Ssl::SelectionResult UpstreamAsyncSelector::selectTlsContext(
    const SSL& ssl, const Network::TransportSocketOptionsConstSharedPtr& options,
    Ssl::CertificateSelectionCallbackPtr cb) {
  const std::string name = mapper_->deriveFromServerHello(ssl, options);
  return doSelectTlsContext(name, false, std::move(cb));
}

Ssl::TlsCertificateSelectorPtr
OnDemandTlsCertificateSelectorFactory::create(Ssl::TlsCertificateSelectorContext&) {
  return std::make_unique<AsyncSelector>(mapper_factory_(), secret_manager_);
}

Ssl::UpstreamTlsCertificateSelectorPtr
UpstreamOnDemandTlsCertificateSelectorFactory::createUpstreamTlsCertificateSelector(
    Ssl::TlsCertificateSelectorContext&) {
  return std::make_unique<UpstreamAsyncSelector>(mapper_factory_(), secret_manager_);
}

absl::Status BaseCertificateSelectorFactory::onConfigUpdate() {
  return secret_manager_->updateAll();
}

namespace {
template <typename MapperFactory, typename SelectorFactory>
absl::StatusOr<std::unique_ptr<SelectorFactory>>
createCertificateSelectorFactory(const Protobuf::Message& proto_config,
                                 Server::Configuration::GenericFactoryContext& factory_context,
                                 AsyncContextFactory&& context_factory) {
  const ConfigProto& config = MessageUtil::downcastAndValidate<const ConfigProto&>(
      proto_config, factory_context.messageValidationVisitor());
  if (!config.has_local_signer() && !config.has_config_source()) {
    return absl::InvalidArgumentError("either config_source or local_signer must be configured");
  }
  if (config.has_local_signer()) {
    if (config.local_signer().ca_cert_path().empty() || config.local_signer().ca_key_path().empty()) {
      return absl::InvalidArgumentError("local_signer requires both ca_cert_path and ca_key_path");
    }
    const auto key_type = config.local_signer().key_type();
    if (key_type != ConfigProto::LocalSigner::KEY_TYPE_UNSPECIFIED &&
        key_type != ConfigProto::LocalSigner::KEY_TYPE_RSA &&
        key_type != ConfigProto::LocalSigner::KEY_TYPE_ECDSA) {
      return absl::InvalidArgumentError("unsupported local_signer.key_type");
    }
    if (config.local_signer().rsa_key_bits() > 0) {
      const uint32_t bits = config.local_signer().rsa_key_bits();
      if (!isValidRsaKeyBits(bits)) {
        return absl::InvalidArgumentError(
            "local_signer.rsa_key_bits must be a multiple of 256 in [2048, 8192]");
      }
    }
    const auto ecdsa_curve = config.local_signer().ecdsa_curve();
    if (ecdsa_curve != ConfigProto::LocalSigner::ECDSA_CURVE_UNSPECIFIED &&
        ecdsa_curve != ConfigProto::LocalSigner::ECDSA_CURVE_P256 &&
        ecdsa_curve != ConfigProto::LocalSigner::ECDSA_CURVE_P384) {
      return absl::InvalidArgumentError("unsupported local_signer.ecdsa_curve");
    }
    const auto signature_hash = config.local_signer().signature_hash();
    if (signature_hash != ConfigProto::LocalSigner::SIGNATURE_HASH_UNSPECIFIED &&
        signature_hash != ConfigProto::LocalSigner::SIGNATURE_HASH_SHA256 &&
        signature_hash != ConfigProto::LocalSigner::SIGNATURE_HASH_SHA384 &&
        signature_hash != ConfigProto::LocalSigner::SIGNATURE_HASH_SHA512) {
      return absl::InvalidArgumentError("unsupported local_signer.signature_hash");
    }
    const auto hostname_validation = config.local_signer().hostname_validation();
    if (hostname_validation != ConfigProto::LocalSigner::HOSTNAME_VALIDATION_UNSPECIFIED &&
        hostname_validation != ConfigProto::LocalSigner::HOSTNAME_VALIDATION_PERMISSIVE &&
        hostname_validation != ConfigProto::LocalSigner::HOSTNAME_VALIDATION_STRICT) {
      return absl::InvalidArgumentError("unsupported local_signer.hostname_validation");
    }
    const auto ca_reload_failure_policy = config.local_signer().ca_reload_failure_policy();
    if (ca_reload_failure_policy !=
            ConfigProto::LocalSigner::CA_RELOAD_FAILURE_POLICY_UNSPECIFIED &&
        ca_reload_failure_policy !=
            ConfigProto::LocalSigner::CA_RELOAD_FAILURE_POLICY_FAIL_CLOSED &&
        ca_reload_failure_policy !=
            ConfigProto::LocalSigner::CA_RELOAD_FAILURE_POLICY_FAIL_OPEN) {
      return absl::InvalidArgumentError("unsupported local_signer.ca_reload_failure_policy");
    }
    for (const auto& dns_san : config.local_signer().additional_dns_sans()) {
      if (dns_san.empty()) {
        return absl::InvalidArgumentError("local_signer.additional_dns_sans cannot contain empty");
      }
    }
    for (const auto key_usage : config.local_signer().key_usages()) {
      if (key_usage != ConfigProto::LocalSigner::KEY_USAGE_UNSPECIFIED &&
          key_usage != ConfigProto::LocalSigner::KEY_USAGE_DIGITAL_SIGNATURE &&
          key_usage != ConfigProto::LocalSigner::KEY_USAGE_CONTENT_COMMITMENT &&
          key_usage != ConfigProto::LocalSigner::KEY_USAGE_KEY_ENCIPHERMENT &&
          key_usage != ConfigProto::LocalSigner::KEY_USAGE_DATA_ENCIPHERMENT &&
          key_usage != ConfigProto::LocalSigner::KEY_USAGE_KEY_AGREEMENT &&
          key_usage != ConfigProto::LocalSigner::KEY_USAGE_KEY_CERT_SIGN &&
          key_usage != ConfigProto::LocalSigner::KEY_USAGE_CRL_SIGN) {
        return absl::InvalidArgumentError("unsupported local_signer.key_usages value");
      }
    }
    for (const auto extended_key_usage : config.local_signer().extended_key_usages()) {
      if (extended_key_usage != ConfigProto::LocalSigner::EXTENDED_KEY_USAGE_UNSPECIFIED &&
          extended_key_usage != ConfigProto::LocalSigner::EXTENDED_KEY_USAGE_SERVER_AUTH &&
          extended_key_usage != ConfigProto::LocalSigner::EXTENDED_KEY_USAGE_CLIENT_AUTH &&
          extended_key_usage != ConfigProto::LocalSigner::EXTENDED_KEY_USAGE_CODE_SIGNING &&
          extended_key_usage != ConfigProto::LocalSigner::EXTENDED_KEY_USAGE_EMAIL_PROTECTION &&
          extended_key_usage != ConfigProto::LocalSigner::EXTENDED_KEY_USAGE_TIME_STAMPING &&
          extended_key_usage != ConfigProto::LocalSigner::EXTENDED_KEY_USAGE_OCSP_SIGNING) {
        return absl::InvalidArgumentError("unsupported local_signer.extended_key_usages value");
      }
    }
  }
  MapperFactory& mapper_config =
      Config::Utility::getAndCheckFactory<MapperFactory>(config.certificate_mapper());
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      config.certificate_mapper().typed_config(), factory_context.messageValidationVisitor(),
      mapper_config);
  auto mapper_factory = mapper_config.createTlsCertificateMapperFactory(*message, factory_context);
  RETURN_IF_NOT_OK(mapper_factory.status());
  // Doing this last since it can kick-start SDS fetches.
  // Envoy ensures that per-worker TLS sockets are destroyed before the filter
  // chain holding the TLS socket factory using a completion. This means the
  // TLS context config in the lambda will outlive each AsyncSelector, and it
  // is safe to refer to TLS context config by reference.
  auto secret_manager =
      std::make_shared<SecretManager>(config, factory_context, std::move(context_factory));
  return std::make_unique<SelectorFactory>(*std::move(mapper_factory), std::move(secret_manager));
}
} // namespace

absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr>
OnDemandTlsCertificateSelectorConfigFactory::createTlsCertificateSelectorFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context,
    const Ssl::ServerContextConfig& tls_config, bool for_quic) {
  if (for_quic) {
    return absl::InvalidArgumentError("Does not support QUIC listeners.");
  }
  // Session ID is currently generated from server names and the included TLS
  // certificates in the parent TLS context. It would not be safe to allow
  // resuming with this ID for on-demand TLS certificates which are not present
  // in the parent TLS context.
  if (!tls_config.disableStatelessSessionResumption() ||
      !tls_config.disableStatefulSessionResumption()) {
    return absl::InvalidArgumentError(
        "On demand certificates are not integrated with session resumption support.");
  }
  return createCertificateSelectorFactory<Ssl::TlsCertificateMapperConfigFactory,
                                          OnDemandTlsCertificateSelectorFactory>(
      proto_config, factory_context,
      [&tls_config](Stats::Scope& scope,
                    Server::Configuration::ServerFactoryContext& server_factory_context,
                    const Ssl::TlsCertificateConfig& cert_config, absl::Status& creation_status) {
        return std::make_shared<ServerAsyncContext>(scope, server_factory_context, tls_config,
                                                    cert_config, creation_status);
      });
}

absl::StatusOr<Ssl::UpstreamTlsCertificateSelectorFactoryPtr>
UpstreamOnDemandTlsCertificateSelectorConfigFactory::createUpstreamTlsCertificateSelectorFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context,
    const Ssl::ClientContextConfig& tls_config) {
  return createCertificateSelectorFactory<Ssl::UpstreamTlsCertificateMapperConfigFactory,
                                          UpstreamOnDemandTlsCertificateSelectorFactory>(
      proto_config, factory_context,
      [&tls_config](Stats::Scope& scope,
                    Server::Configuration::ServerFactoryContext& server_factory_context,
                    const Ssl::TlsCertificateConfig& cert_config, absl::Status& creation_status) {
        return std::make_shared<ClientAsyncContext>(scope, server_factory_context, tls_config,
                                                    cert_config, creation_status);
      });
}

REGISTER_FACTORY(OnDemandTlsCertificateSelectorConfigFactory,
                 Ssl::TlsCertificateSelectorConfigFactory);

REGISTER_FACTORY(UpstreamOnDemandTlsCertificateSelectorConfigFactory,
                 Ssl::UpstreamTlsCertificateSelectorConfigFactory);

} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
