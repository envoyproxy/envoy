#include "source/extensions/certificate_providers/local/local_certificate_provider.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "envoy/filesystem/filesystem.h"
#include "envoy/thread/thread.h"

#include "source/common/common/assert.h"
#include "source/common/common/callback_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/certificate_providers/local/local_certificate_minter.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {
namespace Local {

namespace {

constexpr uint32_t DefaultLocalCertTtlDays = 30;
constexpr char DefaultLocalSubjectOrganization[] = "Envoy";
constexpr uint32_t DefaultRsaKeyBits = 2048;
constexpr uint32_t DefaultNotBeforeBackdateSeconds = 60;
constexpr char DefaultLocalRuntimeKeyPrefix[] =
    "envoy.tls.cert_selectors.on_demand_secret.local_signer";
constexpr uint32_t DefaultMintWorkerCount = 2;
constexpr uint32_t MaxMintWorkerCount = 4;
constexpr uint32_t MaxPendingMintJobs = 128;
constexpr char MintWorkerThreadName[] = "local_certmint";

bool isValidRsaKeyBits(uint32_t bits) { return bits >= 2048 && bits <= 8192 && bits % 256 == 0; }

absl::optional<LocalSignerProto::KeyType> parseKeyType(uint64_t raw) {
  const auto value = static_cast<LocalSignerProto::KeyType>(raw);
  switch (value) {
  case LocalSignerProto::KEY_TYPE_UNSPECIFIED:
  case LocalSignerProto::KEY_TYPE_RSA:
  case LocalSignerProto::KEY_TYPE_ECDSA:
    return value;
  default:
    break;
  }
  return absl::nullopt;
}

absl::optional<LocalSignerProto::EcdsaCurve> parseEcdsaCurve(uint64_t raw) {
  const auto value = static_cast<LocalSignerProto::EcdsaCurve>(raw);
  switch (value) {
  case LocalSignerProto::ECDSA_CURVE_UNSPECIFIED:
  case LocalSignerProto::ECDSA_CURVE_P256:
  case LocalSignerProto::ECDSA_CURVE_P384:
    return value;
  default:
    break;
  }
  return absl::nullopt;
}

absl::optional<LocalSignerProto::SignatureHash> parseSignatureHash(uint64_t raw) {
  const auto value = static_cast<LocalSignerProto::SignatureHash>(raw);
  switch (value) {
  case LocalSignerProto::SIGNATURE_HASH_UNSPECIFIED:
  case LocalSignerProto::SIGNATURE_HASH_SHA256:
  case LocalSignerProto::SIGNATURE_HASH_SHA384:
  case LocalSignerProto::SIGNATURE_HASH_SHA512:
    return value;
  default:
    break;
  }
  return absl::nullopt;
}

absl::optional<LocalSignerProto::HostnameValidation> parseHostnameValidation(uint64_t raw) {
  const auto value = static_cast<LocalSignerProto::HostnameValidation>(raw);
  switch (value) {
  case LocalSignerProto::HOSTNAME_VALIDATION_UNSPECIFIED:
  case LocalSignerProto::HOSTNAME_VALIDATION_PERMISSIVE:
  case LocalSignerProto::HOSTNAME_VALIDATION_STRICT:
    return value;
  default:
    break;
  }
  return absl::nullopt;
}

absl::StatusOr<Ssl::LocalCertificateMinter::KeyType>
toMinterKeyType(LocalSignerProto::KeyType key_type) {
  switch (key_type) {
  case LocalSignerProto::KEY_TYPE_UNSPECIFIED:
  case LocalSignerProto::KEY_TYPE_RSA:
    return Ssl::LocalCertificateMinter::KeyType::Rsa;
  case LocalSignerProto::KEY_TYPE_ECDSA:
    return Ssl::LocalCertificateMinter::KeyType::Ecdsa;
  default:
    return absl::InvalidArgumentError("unsupported key type");
  }
}

absl::StatusOr<Ssl::LocalCertificateMinter::EcdsaCurve>
toMinterEcdsaCurve(LocalSignerProto::EcdsaCurve curve) {
  switch (curve) {
  case LocalSignerProto::ECDSA_CURVE_UNSPECIFIED:
  case LocalSignerProto::ECDSA_CURVE_P256:
    return Ssl::LocalCertificateMinter::EcdsaCurve::P256;
  case LocalSignerProto::ECDSA_CURVE_P384:
    return Ssl::LocalCertificateMinter::EcdsaCurve::P384;
  default:
    return absl::InvalidArgumentError("unsupported ECDSA curve");
  }
}

absl::StatusOr<Ssl::LocalCertificateMinter::SignatureHash>
toMinterSignatureHash(LocalSignerProto::SignatureHash signature_hash) {
  switch (signature_hash) {
  case LocalSignerProto::SIGNATURE_HASH_UNSPECIFIED:
  case LocalSignerProto::SIGNATURE_HASH_SHA256:
    return Ssl::LocalCertificateMinter::SignatureHash::Sha256;
  case LocalSignerProto::SIGNATURE_HASH_SHA384:
    return Ssl::LocalCertificateMinter::SignatureHash::Sha384;
  case LocalSignerProto::SIGNATURE_HASH_SHA512:
    return Ssl::LocalCertificateMinter::SignatureHash::Sha512;
  default:
    return absl::InvalidArgumentError("unsupported signature hash");
  }
}

absl::StatusOr<Ssl::LocalCertificateMinter::KeyUsage>
toMinterKeyUsage(LocalSignerProto::KeyUsage usage) {
  switch (usage) {
  case LocalSignerProto::KEY_USAGE_DIGITAL_SIGNATURE:
    return Ssl::LocalCertificateMinter::KeyUsage::DigitalSignature;
  case LocalSignerProto::KEY_USAGE_CONTENT_COMMITMENT:
    return Ssl::LocalCertificateMinter::KeyUsage::ContentCommitment;
  case LocalSignerProto::KEY_USAGE_KEY_ENCIPHERMENT:
    return Ssl::LocalCertificateMinter::KeyUsage::KeyEncipherment;
  case LocalSignerProto::KEY_USAGE_DATA_ENCIPHERMENT:
    return Ssl::LocalCertificateMinter::KeyUsage::DataEncipherment;
  case LocalSignerProto::KEY_USAGE_KEY_AGREEMENT:
    return Ssl::LocalCertificateMinter::KeyUsage::KeyAgreement;
  case LocalSignerProto::KEY_USAGE_KEY_CERT_SIGN:
    return Ssl::LocalCertificateMinter::KeyUsage::KeyCertSign;
  case LocalSignerProto::KEY_USAGE_CRL_SIGN:
    return Ssl::LocalCertificateMinter::KeyUsage::CrlSign;
  case LocalSignerProto::KEY_USAGE_UNSPECIFIED:
    return absl::InvalidArgumentError("unspecified key usage is not supported");
  default:
    return absl::InvalidArgumentError("unsupported key usage");
  }
}

absl::StatusOr<Ssl::LocalCertificateMinter::ExtendedKeyUsage>
toMinterExtendedKeyUsage(LocalSignerProto::ExtendedKeyUsage usage) {
  switch (usage) {
  case LocalSignerProto::EXTENDED_KEY_USAGE_SERVER_AUTH:
    return Ssl::LocalCertificateMinter::ExtendedKeyUsage::ServerAuth;
  case LocalSignerProto::EXTENDED_KEY_USAGE_CLIENT_AUTH:
    return Ssl::LocalCertificateMinter::ExtendedKeyUsage::ClientAuth;
  case LocalSignerProto::EXTENDED_KEY_USAGE_CODE_SIGNING:
    return Ssl::LocalCertificateMinter::ExtendedKeyUsage::CodeSigning;
  case LocalSignerProto::EXTENDED_KEY_USAGE_EMAIL_PROTECTION:
    return Ssl::LocalCertificateMinter::ExtendedKeyUsage::EmailProtection;
  case LocalSignerProto::EXTENDED_KEY_USAGE_TIME_STAMPING:
    return Ssl::LocalCertificateMinter::ExtendedKeyUsage::TimeStamping;
  case LocalSignerProto::EXTENDED_KEY_USAGE_OCSP_SIGNING:
    return Ssl::LocalCertificateMinter::ExtendedKeyUsage::OcspSigning;
  case LocalSignerProto::EXTENDED_KEY_USAGE_UNSPECIFIED:
    return absl::InvalidArgumentError("unspecified extended key usage is not supported");
  default:
    return absl::InvalidArgumentError("unsupported extended key usage");
  }
}

std::vector<LocalSignerProto::KeyUsage>
toKeyUsageVector(const Protobuf::RepeatedField<int>& values) {
  std::vector<LocalSignerProto::KeyUsage> out;
  out.reserve(values.size());
  for (const int value : values) {
    out.push_back(static_cast<LocalSignerProto::KeyUsage>(value));
  }
  return out;
}

std::vector<LocalSignerProto::ExtendedKeyUsage>
toExtendedKeyUsageVector(const Protobuf::RepeatedField<int>& values) {
  std::vector<LocalSignerProto::ExtendedKeyUsage> out;
  out.reserve(values.size());
  for (const int value : values) {
    out.push_back(static_cast<LocalSignerProto::ExtendedKeyUsage>(value));
  }
  return out;
}

struct LocalSignerOptions {
  std::string key;
  std::string ca_cert_path;
  std::string ca_key_path;
  uint32_t cert_ttl_days;
  std::string subject_organization;
  LocalSignerProto::KeyType key_type;
  uint32_t rsa_key_bits;
  LocalSignerProto::EcdsaCurve ecdsa_curve;
  LocalSignerProto::SignatureHash signature_hash;
  uint32_t not_before_backdate_seconds;
  LocalSignerProto::HostnameValidation hostname_validation;
  std::string runtime_key_prefix;
  LocalSignerProto::CaReloadFailurePolicy ca_reload_failure_policy;
  bool include_primary_dns_san;
  std::vector<std::string> additional_dns_sans;
  std::vector<LocalSignerProto::KeyUsage> key_usages;
  std::vector<LocalSignerProto::ExtendedKeyUsage> extended_key_usages;
  absl::optional<bool> basic_constraints_ca;
  std::string subject_common_name;
  std::string subject_organizational_unit;
  std::string subject_country;
  std::string subject_state_or_province;
  std::string subject_locality;
};

class MintExecutor {
public:
  static MintExecutor& instance(Thread::ThreadFactory& thread_factory) {
    static MintExecutor executor(thread_factory);
    return executor;
  }

  ~MintExecutor() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      shutting_down_ = true;
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
      if (worker != nullptr) {
        worker->join();
      }
    }
  }

  absl::Status submit(std::function<void()> job) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (shutting_down_) {
        return absl::FailedPreconditionError("local certificate mint executor is shutting down");
      }
      if (queue_.size() >= MaxPendingMintJobs) {
        return absl::ResourceExhaustedError("local certificate mint queue is full");
      }
      queue_.push_back(std::move(job));
    }
    cv_.notify_one();
    return absl::OkStatus();
  }

private:
  explicit MintExecutor(Thread::ThreadFactory& thread_factory) {
    const uint32_t worker_count = std::min<uint32_t>(
        std::max<uint32_t>(DefaultMintWorkerCount, std::thread::hardware_concurrency()),
        MaxMintWorkerCount);
    workers_.reserve(worker_count);
    const Thread::Options options{MintWorkerThreadName, absl::nullopt};
    for (uint32_t i = 0; i < worker_count; ++i) {
      workers_.push_back(thread_factory.createThread([this]() { workerLoop(); }, options));
    }
  }

  void workerLoop() {
    while (true) {
      std::function<void()> job;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return shutting_down_ || !queue_.empty(); });
        if (shutting_down_ && queue_.empty()) {
          return;
        }
        job = std::move(queue_.front());
        queue_.pop_front();
      }
      job();
    }
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> queue_;
  bool shutting_down_{false};
  std::vector<Thread::ThreadPtr> workers_;
};

class LocalSignerCertificateProvider
    : public Secret::TlsCertificateConfigProvider,
      public std::enable_shared_from_this<LocalSignerCertificateProvider>,
      protected Logger::Loggable<Logger::Id::secret> {
public:
  LocalSignerCertificateProvider(std::string secret_name,
                                 Server::Configuration::ServerFactoryContext& factory_context,
                                 const LocalSignerOptions& options,
                                 Ssl::LocalCertificateMinterSharedPtr minter)
      : secret_name_(std::move(secret_name)), factory_context_(factory_context), options_(options),
        minter_(std::move(minter)) {
    ASSERT(minter_ != nullptr);
  }

  ~LocalSignerCertificateProvider() override = default;

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
    if (terminal_failure_) {
      THROW_IF_NOT_OK(callback());
    }
    return remove_callback_manager_.add(callback);
  }

  void start() override { ASSERT_IS_MAIN_OR_TEST_THREAD(); }

  absl::Status ensureReady();
  absl::Status refresh();

private:
  absl::optional<std::string> secretNameToHostname() const;
  absl::Status refreshCaMaterial();
  absl::StatusOr<Ssl::LocalCertificateMinter::MintRequest> buildMintRequest();
  absl::Status
  applyMintedCertificate(const Ssl::LocalCertificateMinter::MintedCertificate& minted);
  absl::Status scheduleMint();
  void onMintFinished(uint64_t mint_id,
                      const absl::StatusOr<Ssl::LocalCertificateMinter::MintedCertificate>& result);

  struct LocalCaMaterial {
    std::string cert_pem_;
    std::string key_pem_;
    absl::optional<SystemTime> cert_last_modified_;
    absl::optional<SystemTime> key_last_modified_;
    bool loaded_{false};
  };

  const std::string secret_name_;
  Server::Configuration::ServerFactoryContext& factory_context_;
  const LocalSignerOptions options_;
  const Ssl::LocalCertificateMinterSharedPtr minter_;
  LocalCaMaterial ca_material_;
  Secret::TlsCertificatePtr tls_certificate_;
  Common::CallbackManager<absl::Status,
                          const envoy::extensions::transport_sockets::tls::v3::TlsCertificate&>
      validation_callback_manager_;
  Common::CallbackManager<absl::Status> update_callback_manager_;
  Common::CallbackManager<absl::Status> remove_callback_manager_;
  bool mint_in_flight_{false};
  bool refresh_requested_{false};
  bool terminal_failure_{false};
  uint64_t next_mint_id_{1};
};

absl::Status LocalSignerCertificateProvider::ensureReady() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (tls_certificate_ != nullptr || mint_in_flight_) {
    return absl::OkStatus();
  }
  return scheduleMint();
}

absl::Status LocalSignerCertificateProvider::refresh() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (tls_certificate_ == nullptr) {
    return ensureReady();
  }
  if (mint_in_flight_) {
    refresh_requested_ = true;
    return absl::OkStatus();
  }
  return scheduleMint();
}

absl::optional<std::string> LocalSignerCertificateProvider::secretNameToHostname() const {
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
      (effective_hostname_validation == LocalSignerProto::HOSTNAME_VALIDATION_UNSPECIFIED ||
       effective_hostname_validation == LocalSignerProto::HOSTNAME_VALIDATION_PERMISSIVE);

  for (char c : maybe_host) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '.' || (allow_underscore && c == '_')) {
      continue;
    }
    return {};
  }
  return std::string(absl::AsciiStrToLower(maybe_host));
}

absl::Status LocalSignerCertificateProvider::refreshCaMaterial() {
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
  const auto ca_key_pem_or =
      factory_context_.api().fileSystem().fileReadToEnd(options_.ca_key_path);
  if (!ca_cert_pem_or.ok() || !ca_key_pem_or.ok()) {
    if (ca_material_.loaded_ &&
        options_.ca_reload_failure_policy == LocalSignerProto::CA_RELOAD_FAILURE_POLICY_FAIL_OPEN) {
      ENVOY_LOG(warn,
                "failed to reload local signer CA material, keeping previously loaded CA "
                "(fail-open)");
      return absl::OkStatus();
    }
    if (!ca_cert_pem_or.ok()) {
      return ca_cert_pem_or.status();
    }
    return ca_key_pem_or.status();
  }

  const auto ca_validation_status =
      minter_->validateCaMaterial(ca_cert_pem_or.value(), ca_key_pem_or.value());
  if (!ca_validation_status.ok()) {
    if (ca_material_.loaded_ &&
        options_.ca_reload_failure_policy == LocalSignerProto::CA_RELOAD_FAILURE_POLICY_FAIL_OPEN) {
      ENVOY_LOG(warn,
                "failed to parse CA PEM during local signer reload, keeping prior CA (fail-open)");
      return absl::OkStatus();
    }
    return ca_validation_status;
  }

  ca_material_.cert_pem_ = std::move(ca_cert_pem_or.value());
  ca_material_.key_pem_ = std::move(ca_key_pem_or.value());
  ca_material_.cert_last_modified_ = cert_stat.return_value_.time_last_modified_;
  ca_material_.key_last_modified_ = key_stat.return_value_.time_last_modified_;
  ca_material_.loaded_ = true;
  return absl::OkStatus();
}

absl::StatusOr<Ssl::LocalCertificateMinter::MintRequest>
LocalSignerCertificateProvider::buildMintRequest() {
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
    const uint32_t runtime_rsa_key_bits = snapshot.getInteger(
        absl::StrCat(options_.runtime_key_prefix, ".rsa_key_bits"), effective_rsa_key_bits);
    if (runtime_rsa_key_bits > 0 && isValidRsaKeyBits(runtime_rsa_key_bits)) {
      effective_rsa_key_bits = runtime_rsa_key_bits;
    } else if (runtime_rsa_key_bits > 0) {
      ENVOY_LOG(warn,
                "Ignoring invalid runtime rsa_key_bits={} for prefix '{}', keeping {}",
                runtime_rsa_key_bits, options_.runtime_key_prefix, effective_rsa_key_bits);
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
      (effective_hostname_validation == LocalSignerProto::HOSTNAME_VALIDATION_UNSPECIFIED ||
       effective_hostname_validation == LocalSignerProto::HOSTNAME_VALIDATION_PERMISSIVE);
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

  std::vector<Ssl::LocalCertificateMinter::KeyUsage> minter_key_usages;
  minter_key_usages.reserve(options_.key_usages.size());
  for (const auto usage : options_.key_usages) {
    if (usage == LocalSignerProto::KEY_USAGE_UNSPECIFIED) {
      continue;
    }
    auto mapped_or = toMinterKeyUsage(usage);
    RETURN_IF_NOT_OK(mapped_or.status());
    minter_key_usages.push_back(mapped_or.value());
  }

  std::vector<Ssl::LocalCertificateMinter::ExtendedKeyUsage> minter_extended_key_usages;
  minter_extended_key_usages.reserve(options_.extended_key_usages.size());
  for (const auto usage : options_.extended_key_usages) {
    if (usage == LocalSignerProto::EXTENDED_KEY_USAGE_UNSPECIFIED) {
      continue;
    }
    auto mapped_or = toMinterExtendedKeyUsage(usage);
    RETURN_IF_NOT_OK(mapped_or.status());
    minter_extended_key_usages.push_back(mapped_or.value());
  }

  auto key_type_or = toMinterKeyType(effective_key_type);
  RETURN_IF_NOT_OK(key_type_or.status());
  auto ecdsa_curve_or = toMinterEcdsaCurve(effective_ecdsa_curve);
  RETURN_IF_NOT_OK(ecdsa_curve_or.status());
  auto signature_hash_or = toMinterSignatureHash(effective_signature_hash);
  RETURN_IF_NOT_OK(signature_hash_or.status());

  Ssl::LocalCertificateMinter::MintRequest mint_request;
  mint_request.ca_cert_pem = ca_material_.cert_pem_;
  mint_request.ca_key_pem = ca_material_.key_pem_;
  mint_request.host_name = *hostname_or;
  mint_request.subject_organization = options_.subject_organization;
  mint_request.ttl_days = effective_cert_ttl_days;
  mint_request.key_type = key_type_or.value();
  mint_request.rsa_key_bits = effective_rsa_key_bits;
  mint_request.ecdsa_curve = ecdsa_curve_or.value();
  mint_request.signature_hash = signature_hash_or.value();
  mint_request.not_before_backdate_seconds = effective_not_before_backdate_seconds;
  mint_request.subject_common_name = options_.subject_common_name;
  mint_request.subject_organizational_unit = options_.subject_organizational_unit;
  mint_request.subject_country = options_.subject_country;
  mint_request.subject_state_or_province = options_.subject_state_or_province;
  mint_request.subject_locality = options_.subject_locality;
  mint_request.dns_sans = std::move(dns_sans);
  mint_request.key_usages = std::move(minter_key_usages);
  mint_request.extended_key_usages = std::move(minter_extended_key_usages);
  mint_request.basic_constraints_ca = options_.basic_constraints_ca;
  return mint_request;
}

absl::Status LocalSignerCertificateProvider::applyMintedCertificate(
    const Ssl::LocalCertificateMinter::MintedCertificate& minted) {
  auto tls_certificate =
      std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>();
  tls_certificate->mutable_certificate_chain()->set_inline_bytes(minted.certificate_pem);
  tls_certificate->mutable_private_key()->set_inline_bytes(minted.private_key_pem);

  tls_certificate_ = std::move(tls_certificate);
  terminal_failure_ = false;
  RETURN_IF_NOT_OK(update_callback_manager_.runCallbacks());
  return absl::OkStatus();
}

absl::Status LocalSignerCertificateProvider::scheduleMint() {
  auto mint_request_or = buildMintRequest();
  RETURN_IF_NOT_OK(mint_request_or.status());
  mint_in_flight_ = true;
  const uint64_t mint_id = next_mint_id_++;
  auto weak_this = weak_from_this();
  Event::Dispatcher* main_dispatcher = &factory_context_.mainThreadDispatcher();
  auto minter = minter_;
  auto mint_request =
      std::make_shared<Ssl::LocalCertificateMinter::MintRequest>(std::move(mint_request_or.value()));
  const absl::Status status = MintExecutor::instance(factory_context_.api().threadFactory()).submit(
      [weak_this, main_dispatcher, minter, mint_request, mint_id]() {
        using MintResult = absl::StatusOr<Ssl::LocalCertificateMinter::MintedCertificate>;
        auto result = std::make_shared<MintResult>(minter->mint(*mint_request));
        if (weak_this.expired()) {
          return;
        }
        main_dispatcher->post([weak_this, result, mint_id]() {
          if (auto self = weak_this.lock(); self) {
            self->onMintFinished(mint_id, *result);
          }
        });
      });
  if (!status.ok()) {
    mint_in_flight_ = false;
    return status;
  }
  return absl::OkStatus();
}

void LocalSignerCertificateProvider::onMintFinished(
    uint64_t mint_id,
    const absl::StatusOr<Ssl::LocalCertificateMinter::MintedCertificate>& result) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  (void)mint_id;
  mint_in_flight_ = false;
  const bool should_refresh = refresh_requested_;
  refresh_requested_ = false;

  if (!result.ok()) {
    ENVOY_LOG(error, "local certificate mint failed for '{}': {}", secret_name_,
              result.status().message());
    if (tls_certificate_ == nullptr) {
      terminal_failure_ = true;
      if (const absl::Status status = remove_callback_manager_.runCallbacks(); !status.ok()) {
        ENVOY_LOG(error, "local certificate provider remove callback failed for '{}': {}",
                  secret_name_, status.message());
      }
    }
  } else if (const absl::Status status = applyMintedCertificate(result.value()); !status.ok()) {
    ENVOY_LOG(error, "local certificate provider apply failed for '{}': {}", secret_name_,
              status.message());
  }

  if (should_refresh && !mint_in_flight_) {
    const absl::Status status = scheduleMint();
    if (!status.ok() && tls_certificate_ == nullptr) {
      terminal_failure_ = true;
      if (const absl::Status remove_status = remove_callback_manager_.runCallbacks();
          !remove_status.ok()) {
        ENVOY_LOG(error, "local certificate provider remove callback failed for '{}': {}",
                  secret_name_, remove_status.message());
      }
    } else if (!status.ok()) {
      ENVOY_LOG(error, "local certificate provider refresh scheduling failed for '{}': {}",
                secret_name_, status.message());
    }
  }
}

class LocalSignerCertificateProviderStore {
public:
  static LocalSignerCertificateProviderStore& instance() {
    static LocalSignerCertificateProviderStore store;
    return store;
  }

  std::shared_ptr<LocalNamedTlsCertificateProvider> findOrCreate(const LocalSignerProto& config) {
    ASSERT_IS_MAIN_OR_TEST_THREAD();
    const std::string cache_key = config.SerializeAsString();
    if (auto it = providers_.find(cache_key); it != providers_.end()) {
      if (auto existing = it->second.lock(); existing) {
        return existing;
      }
      providers_.erase(it);
    }
    auto provider = std::make_shared<LocalNamedTlsCertificateProvider>(config);
    providers_[cache_key] = provider;
    return provider;
  }

private:
  absl::flat_hash_map<std::string, std::weak_ptr<LocalNamedTlsCertificateProvider>> providers_;
};

LocalSignerOptions localSignerOptionsFromConfig(const LocalSignerProto& local) {
  LocalSignerOptions options;
  options.key = local.SerializeAsString();
  options.ca_cert_path = local.ca_cert_path();
  options.ca_key_path = local.ca_key_path();
  options.cert_ttl_days =
      local.cert_ttl_days() > 0 ? local.cert_ttl_days() : DefaultLocalCertTtlDays;
  options.subject_organization = !local.subject_organization().empty()
                                     ? local.subject_organization()
                                     : DefaultLocalSubjectOrganization;
  options.key_type = local.key_type();
  options.rsa_key_bits = local.rsa_key_bits() > 0 ? local.rsa_key_bits() : DefaultRsaKeyBits;
  options.ecdsa_curve = local.ecdsa_curve();
  options.signature_hash = local.signature_hash();
  options.not_before_backdate_seconds = local.not_before_backdate_seconds() > 0
                                            ? local.not_before_backdate_seconds()
                                            : DefaultNotBeforeBackdateSeconds;
  options.hostname_validation = local.hostname_validation();
  options.runtime_key_prefix = !local.runtime_key_prefix().empty() ? local.runtime_key_prefix()
                                                                   : DefaultLocalRuntimeKeyPrefix;
  options.ca_reload_failure_policy = local.ca_reload_failure_policy();
  options.include_primary_dns_san =
      local.has_include_primary_dns_san() ? local.include_primary_dns_san().value() : true;
  options.additional_dns_sans =
      std::vector<std::string>(local.additional_dns_sans().begin(), local.additional_dns_sans().end());
  options.key_usages = toKeyUsageVector(local.key_usages());
  options.extended_key_usages = toExtendedKeyUsageVector(local.extended_key_usages());
  options.basic_constraints_ca = local.has_basic_constraints_ca()
                                     ? absl::optional<bool>(local.basic_constraints_ca().value())
                                     : absl::nullopt;
  options.subject_common_name = local.subject_common_name();
  options.subject_organizational_unit = local.subject_organizational_unit();
  options.subject_country = local.subject_country();
  options.subject_state_or_province = local.subject_state_or_province();
  options.subject_locality = local.subject_locality();
  return options;
}

} // namespace

LocalNamedTlsCertificateProvider::LocalNamedTlsCertificateProvider(const LocalSignerProto& config)
    : config_(config) {}

Secret::TlsCertificateConfigProviderSharedPtr LocalNamedTlsCertificateProvider::getProvider(
    const std::string& certificate_name,
    Server::Configuration::ServerFactoryContext& server_context) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  const auto options = localSignerOptionsFromConfig(config_);
  if (auto it = providers_.find(certificate_name); it != providers_.end()) {
    if (auto existing = it->second.lock(); existing) {
      auto provider = std::static_pointer_cast<LocalSignerCertificateProvider>(existing);
      if (const absl::Status status = provider->ensureReady(); !status.ok()) {
        ENVOY_LOG_MISC(error, "failed to refresh local certificate provider for '{}': {}",
                       certificate_name, status.message());
        return nullptr;
      }
      return existing;
    }
    providers_.erase(it);
  }

  auto provider = std::make_shared<LocalSignerCertificateProvider>(
      certificate_name, server_context, options, Ssl::getDefaultLocalCertificateMinter());
  if (const absl::Status status = provider->ensureReady(); !status.ok()) {
    ENVOY_LOG_MISC(error, "failed to create local certificate provider for '{}': {}",
                   certificate_name, status.message());
    return nullptr;
  }
  providers_[certificate_name] = provider;
  return provider;
}

absl::Status LocalNamedTlsCertificateProvider::refreshProviders() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  for (auto it = providers_.begin(); it != providers_.end();) {
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

absl::StatusOr<Secret::TlsCertificateConfigProviderSharedPtr>
findOrCreateLocalSignerCertificateProvider(
    absl::string_view secret_name, Server::Configuration::ServerFactoryContext& factory_context,
    const LocalSignerProto& local_signer_config) {
  auto provider = LocalSignerCertificateProviderStore::instance().findOrCreate(local_signer_config);
  return provider->getProvider(std::string(secret_name), factory_context);
}

absl::Status
refreshLocalSignerCertificateProviders(const LocalSignerProto& local_signer_config) {
  return LocalSignerCertificateProviderStore::instance()
      .findOrCreate(local_signer_config)
      ->refreshProviders();
}

} // namespace Local
} // namespace CertificateProviders
} // namespace Extensions
} // namespace Envoy
