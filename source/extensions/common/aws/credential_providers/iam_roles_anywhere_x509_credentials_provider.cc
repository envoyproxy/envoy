#include "source/extensions/common/aws/credential_providers/iam_roles_anywhere_x509_credentials_provider.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

using std::chrono::seconds;

constexpr std::chrono::hours REFRESH_INTERVAL{1};
constexpr std::chrono::seconds REFRESH_GRACE_PERIOD{5};

void CachedX509CredentialsProviderBase::refreshIfNeeded() {
  if (needsRefresh()) {
    refresh();
  }
}

IAMRolesAnywhereX509CredentialsProvider::IAMRolesAnywhereX509CredentialsProvider(
    Server::Configuration::ServerFactoryContext& context,
    envoy::config::core::v3::DataSource certificate_data_source,
    envoy::config::core::v3::DataSource private_key_data_source,
    absl::optional<envoy::config::core::v3::DataSource> certificate_chain_data_source)
    : context_(context), certificate_data_source_(certificate_data_source),
      private_key_data_source_(private_key_data_source),
      certificate_chain_data_source_(certificate_chain_data_source) {

  auto provider_or_error_ = Config::DataSource::DataSourceProvider::create(
      certificate_data_source_, context.mainThreadDispatcher(), context.threadLocal(),
      context.api(), false, 2048);
  if (provider_or_error_.ok()) {
    certificate_data_source_provider_ = std::move(provider_or_error_.value());
  } else {
    ENVOY_LOG_MISC(info, "Invalid certificate data source");
    certificate_data_source_provider_ = nullptr;
    return;
  }

  if (certificate_chain_data_source_.has_value()) {
    auto chain_provider_or_error_ = Config::DataSource::DataSourceProvider::create(
        certificate_chain_data_source_.value(), context.mainThreadDispatcher(),
        context.threadLocal(), context.api(), false, 2048);
    if (chain_provider_or_error_.ok()) {
      certificate_chain_data_source_provider_ = std::move(chain_provider_or_error_.value());
    } else {
      ENVOY_LOG_MISC(info, "Invalid certificate chain data source");
    }
  } else {
    certificate_chain_data_source_provider_ = absl::nullopt;
  }

  auto pkey_provider_or_error_ = Config::DataSource::DataSourceProvider::create(
      private_key_data_source_, context.mainThreadDispatcher(), context.threadLocal(),
      context.api(), false, 2048);
  if (pkey_provider_or_error_.ok()) {
    private_key_data_source_provider_ = std::move(pkey_provider_or_error_.value());
  } else {
    ENVOY_LOG_MISC(info, "Invalid private key data source");
    private_key_data_source_provider_ = nullptr;
    return;
  }
  refresh();
}

bool IAMRolesAnywhereX509CredentialsProvider::needsRefresh() {
  const auto now = context_.api().timeSource().systemTime();
  auto expired = (now - last_updated_ > REFRESH_INTERVAL);

  if (expiration_time_.has_value()) {
    return expired || (expiration_time_.value() - now < REFRESH_GRACE_PERIOD);
  } else {
    return expired;
  }
}

const ASN1_TIME& epochASN1Time() {
  static ASN1_TIME* e = []() -> ASN1_TIME* {
    ASN1_TIME* epoch = ASN1_TIME_new();
    const time_t epoch_time = 0;
    RELEASE_ASSERT(ASN1_TIME_set(epoch, epoch_time) != nullptr, "");
    return epoch;
  }();
  return *e;
}

absl::Status IAMRolesAnywhereX509CredentialsProvider::pemToAlgorithmSerialExpiration(
    std::string pem, X509Credentials::PublicKeySignatureAlgorithm& algorithm, std::string& serial,
    SystemTime& time) {
  ASN1_INTEGER* ser = nullptr;
  BIGNUM* bnser = nullptr;
  char* bndec = nullptr;
  absl::Status status = absl::OkStatus();

  auto pemstr = pem.c_str();
  auto pemsize = pem.size();
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(pemstr, pemsize));

  // Not checking return code - we've already validated this certificate in previous call during der
  // conversion
  bssl::UniquePtr<X509> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  X509_ALGOR* alg;
  X509_PUBKEY_get0_param(nullptr, nullptr, nullptr, &alg, X509_get_X509_PUBKEY(cert.get()));
  int nid = OBJ_obj2nid(alg->algorithm);

  if (nid != NID_undef && nid == NID_rsaEncryption) {
    algorithm = X509Credentials::PublicKeySignatureAlgorithm::RSA;
  } else if (nid != NID_undef && nid == NID_X9_62_id_ecPublicKey) {
    algorithm = X509Credentials::PublicKeySignatureAlgorithm::ECDSA;
  } else {
    status = absl::InvalidArgumentError("Invalid certificate public key signature algorithm");
  }
  ser = X509_get_serialNumber(cert.get());
  bnser = ASN1_INTEGER_to_BN(ser, nullptr);
  bndec = BN_bn2dec(bnser);
  serial.append(bndec);

  int days, seconds;

  int rc = ASN1_TIME_diff(&days, &seconds, &epochASN1Time(), X509_get0_notAfter(cert.get()));
  ASSERT(rc == 1);
  // Casting to <time_t (64bit)> to prevent multiplication overflow when certificate not-after date
  // beyond 2038-01-19T03:14:08Z.
  time = std::chrono::system_clock::from_time_t(static_cast<time_t>(days) * 24 * 60 * 60 + seconds);

  OPENSSL_free(bndec);
  BN_free(bnser);

  return status;
}

absl::Status IAMRolesAnywhereX509CredentialsProvider::pemToDerB64(std::string pem,
                                                                  std::string& output, bool chain) {
  absl::Status status = absl::OkStatus();

  auto pemstr = pem.c_str();
  auto pemsize = pem.size();
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(pemstr, pemsize));

  auto max_certs = 1;
  if (chain) {
    max_certs = 5;
  }
  for (int i = 0; i < max_certs; i++) {
    unsigned char* cert_in_der = nullptr;
    bssl::UniquePtr<X509> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));

    if (cert == nullptr) {
      break;
    }
    int der_length = i2d_X509(cert.get(), &cert_in_der);
    if (!(der_length > 0 && cert_in_der != nullptr)) {
      status = absl::InvalidArgumentError("PEM could not be parsed");
    }
    output.append(Base64::encode(reinterpret_cast<const char*>(cert_in_der), der_length));
    output.append(",");
    OPENSSL_free(cert_in_der);
  }
  if (!output.empty()) {
    output.erase(output.size() - 1);
  } else {
    status = absl::InvalidArgumentError("PEM could not be parsed");
  }

  return status;
}

void IAMRolesAnywhereX509CredentialsProvider::refresh() {

  std::string cert_der_b64;
  std::string cert_chain_der_b64;
  std::string cert_serial;
  std::string private_key_pem;
  X509Credentials::PublicKeySignatureAlgorithm cert_algorithm;
  std::string pem;
  absl::Status status;
  SystemTime time;

  if (certificate_data_source_provider_ == nullptr ||
      private_key_data_source_provider_ == nullptr) {
    return;
  }

  auto cert_pem = certificate_data_source_provider_->data();
  if (!cert_pem.empty()) {
    status = pemToDerB64(cert_pem, cert_der_b64);
    if (!status.ok()) {
      ENVOY_LOG_MISC(error, "IAMRolesAnywhere: Certificate PEM decoding failed: {}",
                     status.message());
      cached_credentials_ = X509Credentials();
      return;
    }

    status = pemToAlgorithmSerialExpiration(cert_pem, cert_algorithm, cert_serial, time);
    if (!status.ok()) {
      ENVOY_LOG_MISC(error,
                     "IAMRolesAnywhere: Certificate algorithm and serial decoding failed: {}",
                     status.message());
      cached_credentials_ = X509Credentials();
      return;
    }
    expiration_time_ = time;
  }

  // Certificate Chain
  if (certificate_chain_data_source_provider_.has_value()) {
    auto chain_pem = certificate_chain_data_source_provider_.value()->data();
    if (!chain_pem.empty()) {
      status = pemToDerB64(chain_pem, cert_chain_der_b64, true);
      if (!status.ok()) {
        ENVOY_LOG_MISC(error, "IAMRolesAnywhere: Certificate chain decoding failed: {}",
                       status.message());
      }
    }
  }

  // Private Key
  private_key_pem = private_key_data_source_provider_->data();
  auto keysize = private_key_pem.size();
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(private_key_pem.c_str(), keysize));
  bssl::UniquePtr<EVP_PKEY> pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
  if (pkey == nullptr) {
    ENVOY_LOG_MISC(error, "IAMRolesAnywhere: Private key decoding failed");
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
              fmt::format("{:%Y-%m-%d %H:%M}", time));

    cached_credentials_ = X509Credentials(cert_der_b64, cert_algorithm, cert_serial,
                                          cert_chain_der_b64, private_key_pem, time);
  } else {
    ENVOY_LOG(info,
              "IAMRolesAnywhere: Setting certificate credentials with cert, serial and private "
              "key (algorithm: {}) with expiration time {}",
              cert_algorithm == X509Credentials::PublicKeySignatureAlgorithm::RSA ? "RSA" : "ECDSA",
              fmt::format("{:%Y-%m-%d %H:%M}", time));
    cached_credentials_ = X509Credentials(cert_der_b64, cert_algorithm, cert_serial, absl::nullopt,
                                          private_key_pem, time);
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
