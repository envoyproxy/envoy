#include "source/extensions/common/aws/iam_roles_anywhere_credentials_provider_impl.h"

#include <chrono>
#include <memory>

#include "envoy/common/exception.h"

#include "source/common/common/base64.h"
#include "source/common/common/lock_guard.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/credentials_provider_impl.h"
#include "source/extensions/common/aws/iam_roles_anywhere_sigv4_signer_impl.h"
#include "source/extensions/common/aws/metadata_fetcher.h"
#include "source/extensions/common/aws/utility.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "fmt/chrono.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {
using std::chrono::seconds;

namespace {

constexpr char EXPIRATION_FORMAT[] = "%E4Y-%m-%dT%H:%M:%S%z";

// IAM Roles Anywhere credential strings
constexpr char CREDENTIALSET[] = "credentialSet";
constexpr char CREDENTIALS_LOWER[] = "credentials";
constexpr char ACCESS_KEY_ID_LOWER[] = "accessKeyId";
constexpr char SECRET_ACCESS_KEY_LOWER[] = "secretAccessKey";
constexpr char EXPIRATION_LOWER[] = "expiration";
constexpr char SESSION_TOKEN_LOWER[] = "sessionToken";

constexpr std::chrono::hours REFRESH_INTERVAL{1};
constexpr std::chrono::seconds REFRESH_GRACE_PERIOD{5};

constexpr char ROLESANYWHERE_SERVICE[] = "rolesanywhere";

} // namespace

void CachedX509CredentialsProviderBase::refreshIfNeeded() {
  if (needsRefresh()) {
    refresh();
  }
}

IAMRolesAnywhereX509CredentialsProvider::IAMRolesAnywhereX509CredentialsProvider(
    Api::Api& api, ThreadLocal::SlotAllocator& tls, Event::Dispatcher& dispatcher,
    envoy::config::core::v3::DataSource certificate_data_source,
    envoy::config::core::v3::DataSource private_key_data_source,
    absl::optional<envoy::config::core::v3::DataSource> certificate_chain_data_source)
    : api_(api), certificate_data_source_(certificate_data_source),
      private_key_data_source_(private_key_data_source),
      certificate_chain_data_source_(certificate_chain_data_source), dispatcher_(dispatcher),
      tls_(tls) {

  auto provider_or_error_ = Config::DataSource::DataSourceProvider::create(
      certificate_data_source_, dispatcher_, tls_, api_, false, 2048);
  if (provider_or_error_.ok()) {
    certificate_data_source_provider_ = std::move(provider_or_error_.value());
  } else {
    ENVOY_LOG_MISC(info, "Invalid certificate data source");
    certificate_data_source_provider_ = nullptr;
    return;
  }

  if (certificate_chain_data_source_.has_value()) {
    auto chain_provider_or_error_ = Config::DataSource::DataSourceProvider::create(
        certificate_chain_data_source_.value(), dispatcher_, tls_, api_, false, 2048);
    if (chain_provider_or_error_.ok()) {
      certificate_chain_data_source_provider_ = std::move(chain_provider_or_error_.value());
    } else {
      ENVOY_LOG_MISC(info, "Invalid certificate chain data source");
    }
  } else {
    certificate_chain_data_source_provider_ = absl::nullopt;
  }

  auto pkey_provider_or_error_ = Config::DataSource::DataSourceProvider::create(
      private_key_data_source_, dispatcher_, tls_, api_, false, 2048);
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
  const auto now = api_.timeSource().systemTime();
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

IAMRolesAnywhereCredentialsProvider::IAMRolesAnywhereCredentialsProvider(
    Api::Api& api, ServerFactoryContextOptRef context,
    CreateMetadataFetcherCb create_metadata_fetcher_cb,
    MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer, absl::string_view role_arn,
    absl::string_view profile_arn, absl::string_view trust_anchor_arn,
    absl::string_view role_session_name, absl::optional<uint16_t> session_duration,
    absl::string_view region, absl::string_view cluster_name,
    envoy::config::core::v3::DataSource certificate_data_source,
    envoy::config::core::v3::DataSource private_key_data_source,
    absl::optional<envoy::config::core::v3::DataSource> cert_chain_data_source

    )
    : MetadataCredentialsProviderBase(api, context, nullptr, create_metadata_fetcher_cb,
                                      cluster_name,
                                      envoy::config::cluster::v3::Cluster::LOGICAL_DNS,
                                      cluster_name, refresh_state, initialization_timer),
      role_arn_(role_arn), role_session_name_(role_session_name), profile_arn_(profile_arn),
      trust_anchor_arn_(trust_anchor_arn), region_(region), session_duration_(session_duration),
      server_factory_context_(context) {

  auto roles_anywhere_certificate_provider =
      std::make_shared<IAMRolesAnywhereX509CredentialsProvider>(
          api, context->threadLocal(), context->mainThreadDispatcher(), certificate_data_source,
          private_key_data_source, cert_chain_data_source);
  // Create our own x509 signer just for IAM Roles Anywhere
  roles_anywhere_signer_ =
      std::make_unique<Extensions::Common::Aws::IAMRolesAnywhereSigV4SignerImpl>(
          absl::string_view(ROLESANYWHERE_SERVICE), absl::string_view(region_),
          roles_anywhere_certificate_provider, context_->mainThreadDispatcher().timeSource());
}

void IAMRolesAnywhereCredentialsProvider::onMetadataSuccess(const std::string&& body) {
  ENVOY_LOG(debug, "AWS IAM Roles Anywhere fetch success, calling callback func");
  on_async_fetch_cb_(std::move(body));
}

void IAMRolesAnywhereCredentialsProvider::onMetadataError(Failure reason) {
  stats_->credential_refreshes_failed_.inc();
  ENVOY_LOG(error, "AWS IAM Roles Anywhere  fetch failure: {}",
            metadata_fetcher_->failureToString(reason));
  handleFetchDone();
}

// TODO: @nbaws Unused and will be removed when curl is deprecated
bool IAMRolesAnywhereCredentialsProvider::needsRefresh() { return true; }

void IAMRolesAnywhereCredentialsProvider::refresh() {

  ENVOY_LOG(debug, "Getting AWS credentials from the rolesanywhere service at URI: {}", uri_);

  Http::RequestMessageImpl message;
  message.headers().setScheme(Http::Headers::get().SchemeValues.Http);
  message.headers().setMethod(Http::Headers::get().MethodValues.Post);
  message.headers().setHost(uri_);
  message.headers().setPath("/sessions");
  message.headers().setContentType("application/json");

  std::string body_data;
  body_data.append("{");
  if (session_duration_.has_value()) {
    body_data.append(fmt::format("\"durationSeconds\": {}, ", session_duration_.value()));
  }
  body_data.append(fmt::format("\"profileArn\": \"{}\", ", profile_arn_));
  body_data.append(fmt::format("\"roleArn\": \"{}\", ", role_arn_));
  body_data.append(fmt::format("\"trustAnchorArn\": \"{}\"", trust_anchor_arn_));
  if (!role_session_name_.empty()) {
    body_data.append(fmt::format(", \"roleSessionName\": \"{}\"", role_session_name_));
  }
  body_data.append("}");
  message.body().add(body_data);
  ENVOY_LOG(debug, "IAM Roles Anywhere /sessions payload: {}", body_data);

  auto status = roles_anywhere_signer_->sign(message, true, region_);
  if (!status.ok()) {
    ENVOY_LOG_MISC(debug, status.message());
    setCredentialsToAllThreads(std::make_unique<Credentials>());
    return;
  }
  // Stop any existing timer.
  if (cache_duration_timer_ && cache_duration_timer_->enabled()) {
    cache_duration_timer_->disableTimer();
  }
  // Using Http async client to fetch the AWS credentials.
  if (!metadata_fetcher_) {
    metadata_fetcher_ = create_metadata_fetcher_cb_(context_->clusterManager(), clusterName());
  } else {
    metadata_fetcher_->cancel(); // Cancel if there is any inflight request.
  }
  on_async_fetch_cb_ = [this](const std::string&& arg) {
    return this->extractCredentials(std::move(arg));
  };
  metadata_fetcher_->fetch(message, Tracing::NullSpan::instance(), *this);
}

void IAMRolesAnywhereCredentialsProvider::extractCredentials(
    const std::string&& credential_document_value) {
  absl::StatusOr<Json::ObjectSharedPtr> document_json_or_error;

  document_json_or_error = Json::Factory::loadFromString(credential_document_value);
  if (!document_json_or_error.ok()) {
    ENVOY_LOG(error, "Could not parse AWS credentials document from rolesanywhere service: {}",
              document_json_or_error.status().message());
    handleFetchDone();
    return;
  }

  auto credentialset_object_or_error =
      document_json_or_error.value()->getObjectArray(CREDENTIALSET, false);
  if (!credentialset_object_or_error.ok()) {
    ENVOY_LOG(error, "Could not parse AWS credentials document from rolesanywhere service: {}",
              credentialset_object_or_error.status().message());
    handleFetchDone();
    return;
  }

  // We only consider the first credential returned in a CredentialSet
  auto credential_object_or_error =
      credentialset_object_or_error.value()[0]->getObject(CREDENTIALS_LOWER);
  if (!credential_object_or_error.ok()) {
    ENVOY_LOG(error, "Could not parse AWS credentials document from rolesanywhere service: {}",
              credential_object_or_error.status().message());
    handleFetchDone();
    return;
  }

  const auto access_key_id = Utility::getStringFromJsonOrDefault(credential_object_or_error.value(),
                                                                 ACCESS_KEY_ID_LOWER, "");
  const auto secret_access_key = Utility::getStringFromJsonOrDefault(
      credential_object_or_error.value(), SECRET_ACCESS_KEY_LOWER, "");
  const auto session_token = Utility::getStringFromJsonOrDefault(credential_object_or_error.value(),
                                                                 SESSION_TOKEN_LOWER, "");

  ENVOY_LOG(debug,
            "Found following AWS credentials from rolesanywhere service: {}={}, {}={}, {}={}",
            ACCESS_KEY_ID_LOWER, access_key_id, SECRET_ACCESS_KEY_LOWER,
            secret_access_key.empty() ? "" : "*****", SESSION_TOKEN_LOWER,
            session_token.empty() ? "" : "*****");

  const auto expiration_str =
      Utility::getStringFromJsonOrDefault(credential_object_or_error.value(), EXPIRATION_LOWER, "");

  if (!expiration_str.empty()) {
    absl::Time expiration_time;
    if (absl::ParseTime(EXPIRATION_FORMAT, expiration_str, &expiration_time, nullptr)) {
      ENVOY_LOG(debug, "Rolesanywhere role AWS credentials expiration time: {}", expiration_str);
      expiration_time_ = absl::ToChronoTime(expiration_time);
    }
  }

  last_updated_ = api_.timeSource().systemTime();
  if (useHttpAsyncClient() && context_) {
    setCredentialsToAllThreads(
        std::make_unique<Credentials>(access_key_id, secret_access_key, session_token));
    stats_->credential_refreshes_succeeded_.inc();
    ENVOY_LOG(debug, "Metadata receiver {} moving to Ready state", cluster_name_);
    refresh_state_ = MetadataFetcher::MetadataReceiver::RefreshState::Ready;
    // Set receiver state in statistics
    stats_->metadata_refresh_state_.set(uint64_t(refresh_state_));
  } else {
    cached_credentials_ = Credentials(access_key_id, secret_access_key, session_token);
  }
  handleFetchDone();
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
