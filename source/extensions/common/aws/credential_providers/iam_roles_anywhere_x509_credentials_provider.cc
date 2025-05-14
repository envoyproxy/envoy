#include "source/extensions/common/aws/credential_providers/iam_roles_anywhere_x509_credentials_provider.h"

#include <sys/types.h>

#include "source/common/common/base64.h"
#include "source/common/tls/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

using std::chrono::seconds;

constexpr uint64_t X509_CERTIFICATE_MAX_BYTES{2048};
constexpr uint64_t X509_CERTIFICATE_CHAIN_MAX_LENGTH{5};

void CachedX509CredentialsProviderBase::refreshIfNeeded() {

  // Initialize must be called for a successful refresh.
  ASSERT(is_initialized_);

  if (needsRefresh()) {
    refresh();
  }
}

IAMRolesAnywhereX509CredentialsProvider::IAMRolesAnywhereX509CredentialsProvider(
    Server::Configuration::ServerFactoryContext& context,
    const envoy::config::core::v3::DataSource& certificate_data_source,
    const envoy::config::core::v3::DataSource& private_key_data_source,
    DataSourceOptRef certificate_chain_data_source)
    : context_(context), certificate_data_source_(certificate_data_source),
      private_key_data_source_(private_key_data_source),
      certificate_chain_data_source_(certificate_chain_data_source) {};

absl::Status IAMRolesAnywhereX509CredentialsProvider::initialize() {

  is_initialized_ = true;

  absl::Status status = absl::InvalidArgumentError("IAM Roles Anywhere will not be enabled");

  auto provider_or_error = Config::DataSource::DataSourceProvider::create(
      certificate_data_source_, context_.mainThreadDispatcher(), context_.threadLocal(),
      context_.api(), false, X509_CERTIFICATE_MAX_BYTES);
  if (provider_or_error.ok()) {
    certificate_data_source_provider_ = std::move(provider_or_error.value());
  } else {
    ENVOY_LOG(error, "Invalid certificate data source - a certificate was provided but it was "
                     "unable to be loaded");
    certificate_data_source_provider_ = nullptr;
    return status;
  }

  if (certificate_chain_data_source_.has_value()) {
    auto chain_provider_or_error_ = Config::DataSource::DataSourceProvider::create(
        certificate_chain_data_source_.value(), context_.mainThreadDispatcher(),
        context_.threadLocal(), context_.api(), false, X509_CERTIFICATE_MAX_BYTES * 5);
    if (chain_provider_or_error_.ok()) {
      certificate_chain_data_source_provider_ = std::move(chain_provider_or_error_.value());
    } else {
      ENVOY_LOG(error, "Invalid certificate chain data source - a certificate chain was provided "
                       "but it was unable to be loaded");
      return status;
    }
  } else {
    certificate_chain_data_source_provider_ = absl::nullopt;
  }

  auto pkey_provider_or_error_ = Config::DataSource::DataSourceProvider::create(
      private_key_data_source_, context_.mainThreadDispatcher(), context_.threadLocal(),
      context_.api(), false, 2048);
  if (pkey_provider_or_error_.ok()) {
    private_key_data_source_provider_ = std::move(pkey_provider_or_error_.value());
  } else {
    ENVOY_LOG(error, "Invalid private key data source - a private key was provided but it was "
                     "unable to be loaded");
    private_key_data_source_provider_ = nullptr;
    return status;
  }
  refresh();
  return absl::OkStatus();
}

bool IAMRolesAnywhereX509CredentialsProvider::needsRefresh() {
  const auto now = context_.api().timeSource().systemTime();
  const auto expired = (now - last_updated_ > REFRESH_INTERVAL);

  if (expiration_time_.has_value()) {
    return expired || (expiration_time_.value() - now < REFRESH_GRACE_PERIOD);
  } else {
    return expired;
  }
}

absl::Status IAMRolesAnywhereX509CredentialsProvider::pemToAlgorithmSerialExpiration(
    absl::string_view pem, X509Credentials::PublicKeySignatureAlgorithm& algorithm,
    std::string& serial, SystemTime& time) {
  ASN1_INTEGER* ser = nullptr;
  BIGNUM* bnser = nullptr;
  char* bndec = nullptr;
  char error_data[256];     // OpenSSL error buffer
  unsigned long error_code; // OpenSSL error code

  absl::Status status = absl::OkStatus();

  const std::string pem_string = std::string(pem);
  auto pem_size = pem.size();

  // We should not be able to get here with an empty certificate or one larger than the max size
  // defined in the header. This is a sanity check.

  if (!pem_size || pem_size > X509_CERTIFICATE_MAX_BYTES) {
    return absl::InvalidArgumentError("Invalid certificate size");
  }

  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(pem_string.c_str(), pem_size));

  ERR_clear_error();

  bssl::UniquePtr<X509> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));

  if (cert == nullptr) {
    error_code = ERR_peek_last_error();
    ERR_error_string(error_code, error_data);
    return absl::InvalidArgumentError(
        fmt::format("Invalid certificate - PEM read x509 failed: {}", error_data));
  }

  X509_ALGOR* alg;
  int param_status =
      X509_PUBKEY_get0_param(nullptr, nullptr, nullptr, &alg, X509_get_X509_PUBKEY(cert.get()));
  if (param_status != 1) {
    error_code = ERR_peek_last_error();
    ERR_error_string(error_code, error_data);
    return absl::InvalidArgumentError(
        fmt::format("Invalid certificate - X509_PUBKEY_get0_param failed: {}", error_data));
  }

  int nid = OBJ_obj2nid(alg->algorithm);

  switch (nid) {
  case NID_rsaEncryption:
    algorithm = X509Credentials::PublicKeySignatureAlgorithm::RSA;
    break;
  case NID_X9_62_id_ecPublicKey:
    algorithm = X509Credentials::PublicKeySignatureAlgorithm::ECDSA;
    break;
  default:
    return absl::InvalidArgumentError("Invalid certificate public key signature algorithm");
  }

  ser = X509_get_serialNumber(cert.get());
  if (ser == nullptr) {
    return absl::InvalidArgumentError("Certificate serial number could not be extracted");
  }

  bnser = ASN1_INTEGER_to_BN(ser, nullptr);
  // Asserts here as we cannot stub OpenSSL
  ASSERT(bnser != nullptr);
  bndec = BN_bn2dec(bnser);
  ASSERT(bndec != nullptr);

  serial.append(bndec);

  time = Envoy::Extensions::TransportSockets::Tls::Utility::getExpirationTime(*cert.get());
  if (time < context_.api().timeSource().systemTime()) {
    status = absl::InvalidArgumentError("Certificate has already expired");
  }

  OPENSSL_free(bndec);
  BN_free(bnser);

  return status;
}

/*TODO: @nbaws split this method and move common functionality into source/common/tls/utility.h */

absl::Status IAMRolesAnywhereX509CredentialsProvider::pemToDerB64(absl::string_view pem,
                                                                  std::string& output,
                                                                  bool is_chain) {

  const std::string pem_string = std::string(pem);
  auto pem_size = pem.size();
  char error_data[256];     // OpenSSL error buffer
  unsigned long error_code; // OpenSSL error code

  // If is_chain == true, then set maximum trust chain length to 5 per
  // https://docs.aws.amazon.com/rolesanywhere/latest/userguide/authentication.html
  // Otherwise we are parsing only a single certificate in the PEM string

  const uint64_t max_certs = is_chain ? X509_CERTIFICATE_CHAIN_MAX_LENGTH : 1;
  uint64_t cert_count = 0;

  // Up to X509_CERTIFICATE_CHAIN_MAX_LENGTH elements in a certificate chain
  const uint64_t max_size = is_chain
                                ? X509_CERTIFICATE_MAX_BYTES * X509_CERTIFICATE_CHAIN_MAX_LENGTH
                                : X509_CERTIFICATE_MAX_BYTES;

  // Exit if pem is empty or too large
  if ((!pem_size) || (pem_size > max_size)) {
    return absl::InvalidArgumentError(is_chain ? "Invalid certificate chain size"
                                               : "Invalid certificate size");
  }

  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(pem_string.c_str(), pem_size));

  while (cert_count < max_certs) {
    unsigned char* cert_in_der = nullptr;

    ERR_clear_error();
    bssl::UniquePtr<X509> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));

    if (cert == nullptr) {
      error_code = ERR_peek_last_error();
      ERR_error_string(error_code, error_data);

      // Error code = PEM_R_NO_START_LINE means we reached the end of the BIO
      if (ERR_GET_REASON(error_code) == PEM_R_NO_START_LINE) {
        break;
      } else {
        // Otherwise we have a legitimate certificate parsing error
        return absl::InvalidArgumentError(
            is_chain ? fmt::format("Certificate chain PEM #{} could not be parsed: {}", cert_count,
                                   error_data)
                     : fmt::format("Certificate could not be parsed: {}", error_data));
      }
    }

    ERR_clear_error();

    int der_length = i2d_X509(cert.get(), &cert_in_der);
    if (!(der_length > 0 && cert_in_der != nullptr)) {

      error_code = ERR_peek_last_error();
      ERR_error_string(error_code, error_data);

      return absl::InvalidArgumentError(
          is_chain ? fmt::format("Certificate chain PEM #{} could not be converted to DER: {}",
                                 cert_count, error_data)
                   : fmt::format("Certificate could not be converted to DER: {}", error_data));
    }

    output.append(Base64::encode(reinterpret_cast<const char*>(cert_in_der), der_length));
    output.append(",");

    OPENSSL_free(cert_in_der);
    cert_count++;
  }

  if (!cert_count) {
    return absl::InvalidArgumentError("No certificates found in PEM data");
  }

  // Remove trailing comma
  output.erase(output.size() - 1);

  return absl::OkStatus();
}

void IAMRolesAnywhereX509CredentialsProvider::refresh() {

  std::string cert_der_b64;
  std::string cert_chain_der_b64;
  std::string cert_serial;
  std::string private_key_pem;
  X509Credentials::PublicKeySignatureAlgorithm cert_algorithm;
  std::string pem;
  absl::Status status;
  SystemTime expiration_time;

  if (certificate_data_source_provider_ == nullptr ||
      private_key_data_source_provider_ == nullptr) {
    cached_credentials_ = X509Credentials();
    return;
  }

  auto cert_pem = certificate_data_source_provider_->data();
  if (!cert_pem.empty()) {
    status = pemToDerB64(cert_pem, cert_der_b64, false);
    if (!status.ok()) {
      ENVOY_LOG(error, "IAMRolesAnywhere: Certificate PEM decoding failed: {}", status.message());
      cached_credentials_ = X509Credentials();
      return;
    }

    status = pemToAlgorithmSerialExpiration(cert_pem, cert_algorithm, cert_serial, expiration_time);
    if (!status.ok()) {
      ENVOY_LOG(error, "IAMRolesAnywhere: Certificate algorithm and serial decoding failed: {}",
                status.message());
      cached_credentials_ = X509Credentials();
      return;
    }
    expiration_time_ = expiration_time;
  }

  // Certificate Chain
  if (certificate_chain_data_source_provider_.has_value()) {
    auto chain_pem = certificate_chain_data_source_provider_.value()->data();
    if (!chain_pem.empty()) {
      // If a certificate chain is provided, it must be valid
      status = pemToDerB64(chain_pem, cert_chain_der_b64, true);
      if (!status.ok()) {
        ENVOY_LOG(error, "IAMRolesAnywhere: Certificate chain decoding failed: {}",
                  status.message());
        cached_credentials_ = X509Credentials();
        return;
      }
    }
  }

  // Private Key
  private_key_pem = private_key_data_source_provider_->data();
  auto keysize = private_key_pem.size();
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(private_key_pem.c_str(), keysize));
  bssl::UniquePtr<EVP_PKEY> pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
  if (pkey == nullptr) {
    ENVOY_LOG(error, "IAMRolesAnywhere: Private key decoding failed");
    cached_credentials_ = X509Credentials();
    return;
  }

  // We may have a cert chain or not, but we must always have a private key and certificate
  cache_duration_ = getCacheDuration();
  if (!cert_chain_der_b64.empty()) {
    ENVOY_LOG(info,
              "IAMRolesAnywhere: Setting certificate credentials with cert, serial, private key "
              "(algorithm: {}) "
              "and cert chain, with expiration time {}",
              cert_algorithm == X509Credentials::PublicKeySignatureAlgorithm::RSA ? "RSA" : "ECDSA",
              fmt::format("{:%Y-%m-%d %H:%M}", expiration_time));

    cached_credentials_ = X509Credentials(cert_der_b64, cert_algorithm, cert_serial,
                                          cert_chain_der_b64, private_key_pem, expiration_time);
  } else {
    ENVOY_LOG(info,
              "IAMRolesAnywhere: Setting certificate credentials with cert, serial and private "
              "key (algorithm: {}) with expiration time {}",
              cert_algorithm == X509Credentials::PublicKeySignatureAlgorithm::RSA ? "RSA" : "ECDSA",
              fmt::format("{:%Y-%m-%d %H:%M}", expiration_time));
    cached_credentials_ = X509Credentials(cert_der_b64, cert_algorithm, cert_serial, absl::nullopt,
                                          private_key_pem, expiration_time);
  }
}

std::chrono::seconds IAMRolesAnywhereX509CredentialsProvider::getCacheDuration() {
  return std::chrono::seconds(
      REFRESH_INTERVAL -
      REFRESH_GRACE_PERIOD /*TODO: Add jitter from context.api().randomGenerator()*/);
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
