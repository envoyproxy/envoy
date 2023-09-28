#include "source/common/secret/sds_api.h"

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/config/api_version.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Secret {

SdsApiStats SdsApi::generateStats(Stats::Scope& scope) {
  return {ALL_SDS_API_STATS(POOL_COUNTER(scope))};
}

SdsApi::SdsApi(envoy::config::core::v3::ConfigSource sds_config, absl::string_view sds_config_name,
               Config::SubscriptionFactory& subscription_factory, TimeSource& time_source,
               ProtobufMessage::ValidationVisitor& validation_visitor, Stats::Store& stats,
               std::function<void()> destructor_cb, Event::Dispatcher& dispatcher, Api::Api& api)
    : Envoy::Config::SubscriptionBase<envoy::extensions::transport_sockets::tls::v3::Secret>(
          validation_visitor, "name"),
      init_target_(fmt::format("SdsApi {}", sds_config_name), [this] { initialize(); }),
      dispatcher_(dispatcher), api_(api),
      scope_(stats.createScope(absl::StrCat("sds.", sds_config_name, "."))),
      sds_api_stats_(generateStats(*scope_)), sds_config_(std::move(sds_config)),
      sds_config_name_(sds_config_name), clean_up_(std::move(destructor_cb)),
      subscription_factory_(subscription_factory),
      time_source_(time_source), secret_data_{sds_config_name_, "uninitialized",
                                              time_source_.systemTime()} {
  const auto resource_name = getResourceName();
  // This has to happen here (rather than in initialize()) as it can throw exceptions.
  subscription_ = subscription_factory_.subscriptionFromConfigSource(
      sds_config_, Grpc::Common::typeUrl(resource_name), *scope_, *this, resource_decoder_, {});
}

void SdsApi::resolveDataSource(const FileContentMap& files,
                               envoy::config::core::v3::DataSource& data_source) {
  if (data_source.specifier_case() ==
      envoy::config::core::v3::DataSource::SpecifierCase::kFilename) {
    const std::string& content = files.at(data_source.filename());
    data_source.set_inline_bytes(content);
  }
}

void SdsApi::onWatchUpdate() {
  // Filesystem reads and update callbacks can fail if the key material is missing or bad. We're not
  // under an onConfigUpdate() context, so we need to catch these cases explicitly here.
  TRY_ASSERT_MAIN_THREAD {
    // Obtain a stable set of files. If a rotation happens while we're reading,
    // then we need to try again.
    uint64_t prev_hash = 0;
    FileContentMap files = loadFiles();
    uint64_t next_hash = getHashForFiles(files);
    const uint64_t MaxBoundedRetries = 5;
    for (uint64_t bounded_retries = MaxBoundedRetries;
         next_hash != prev_hash && bounded_retries > 0; --bounded_retries) {
      files = loadFiles();
      prev_hash = next_hash;
      next_hash = getHashForFiles(files);
    }
    if (next_hash != prev_hash) {
      ENVOY_LOG_MISC(
          warn, "Unable to atomically refresh secrets due to > {} non-atomic rotations observed",
          MaxBoundedRetries);
    }
    const uint64_t new_hash = next_hash;
    if (new_hash != files_hash_) {
      resolveSecret(files);
      update_callback_manager_.runCallbacks();
      files_hash_ = new_hash;
    }
  }
  END_TRY
  CATCH(const EnvoyException& e, {
    ENVOY_LOG_MISC(warn, fmt::format("Failed to reload certificates: {}", e.what()));
    sds_api_stats_.key_rotation_failed_.inc();
  });
}

absl::Status SdsApi::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                    const std::string& version_info) {
  const absl::Status status = validateUpdateSize(resources.size());
  if (!status.ok()) {
    return status;
  }
  const auto& secret = dynamic_cast<const envoy::extensions::transport_sockets::tls::v3::Secret&>(
      resources[0].get().resource());

  if (secret.name() != sds_config_name_) {
    return absl::InvalidArgumentError(
        fmt::format("Unexpected SDS secret (expecting {}): {}", sds_config_name_, secret.name()));
  }

  const uint64_t new_hash = MessageUtil::hash(secret);

  if (new_hash != secret_hash_) {
    validateConfig(secret);
    secret_hash_ = new_hash;
    setSecret(secret);
    const auto files = loadFiles();
    files_hash_ = getHashForFiles(files);
    resolveSecret(files);
    update_callback_manager_.runCallbacks();

    auto* watched_directory = getWatchedDirectory();
    // Either we have a watched path and can defer the watch monitoring to a
    // WatchedDirectory object, or we need to implement per-file watches in the else
    // clause.
    if (watched_directory != nullptr) {
      watched_directory->setCallback([this]() { onWatchUpdate(); });
    } else {
      // List DataSources that refer to files
      auto files = getDataSourceFilenames();
      if (!files.empty()) {
        // Create new watch, also destroys the old watch if any.
        watcher_ = dispatcher_.createFilesystemWatcher();
        for (auto const& filename : files) {
          // Watch for directory instead of file. This allows users to do atomic renames
          // on directory level (e.g. Kubernetes secret update).
          const auto result_or_error = api_.fileSystem().splitPathFromFilename(filename);
          THROW_IF_STATUS_NOT_OK(result_or_error, throw);
          watcher_->addWatch(absl::StrCat(result_or_error.value().directory_, "/"),
                             Filesystem::Watcher::Events::MovedTo,
                             [this](uint32_t) { onWatchUpdate(); });
        }
      } else {
        watcher_.reset(); // Destroy the old watch if any
      }
    }
  }
  secret_data_.last_updated_ = time_source_.systemTime();
  secret_data_.version_info_ = version_info;
  init_target_.ready();
  return absl::OkStatus();
}

absl::Status SdsApi::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                                    const Protobuf::RepeatedPtrField<std::string>&,
                                    const std::string&) {
  const absl::Status status = validateUpdateSize(added_resources.size());
  if (!status.ok()) {
    return status;
  }
  return onConfigUpdate(added_resources, added_resources[0].get().version());
}

void SdsApi::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                  const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // We need to allow server startup to continue, even if we have a bad config.
  init_target_.ready();
}

absl::Status SdsApi::validateUpdateSize(int num_resources) {
  if (num_resources == 0) {
    return absl::InvalidArgumentError(
        fmt::format("Missing SDS resources for {} in onConfigUpdate()", sds_config_name_));
  }
  if (num_resources != 1) {
    return absl::InvalidArgumentError(
        fmt::format("Unexpected SDS secrets length: {}", num_resources));
  }
  return absl::OkStatus();
}

void SdsApi::initialize() {
  // Don't put any code here that can throw exceptions, this has been the cause of multiple
  // hard-to-diagnose regressions.
  subscription_->start({sds_config_name_});
}

SdsApi::SecretData SdsApi::secretData() { return secret_data_; }

SdsApi::FileContentMap SdsApi::loadFiles() {
  FileContentMap files;
  for (auto const& filename : getDataSourceFilenames()) {
    auto file_or_error = api_.fileSystem().fileReadToEnd(filename);
    THROW_IF_STATUS_NOT_OK(file_or_error, throw);
    files[filename] = file_or_error.value();
  }
  return files;
}

uint64_t SdsApi::getHashForFiles(const FileContentMap& files) {
  uint64_t hash = 0;
  for (const auto& it : files) {
    hash = HashUtil::xxHash64(it.second, hash);
  }
  return hash;
}

std::vector<std::string> TlsCertificateSdsApi::getDataSourceFilenames() {
  std::vector<std::string> files;
  if (sds_tls_certificate_secrets_ && sds_tls_certificate_secrets_->has_certificate_chain() &&
      sds_tls_certificate_secrets_->certificate_chain().specifier_case() ==
          envoy::config::core::v3::DataSource::SpecifierCase::kFilename) {
    files.push_back(sds_tls_certificate_secrets_->certificate_chain().filename());
  }
  if (sds_tls_certificate_secrets_ && sds_tls_certificate_secrets_->has_private_key() &&
      sds_tls_certificate_secrets_->private_key().specifier_case() ==
          envoy::config::core::v3::DataSource::SpecifierCase::kFilename) {
    files.push_back(sds_tls_certificate_secrets_->private_key().filename());
  }
  return files;
}

std::vector<std::string> CertificateValidationContextSdsApi::getDataSourceFilenames() {
  std::vector<std::string> files;
  if (sds_certificate_validation_context_secrets_) {
    if (sds_certificate_validation_context_secrets_->has_trusted_ca() &&
        sds_certificate_validation_context_secrets_->trusted_ca().specifier_case() ==
            envoy::config::core::v3::DataSource::SpecifierCase::kFilename) {
      files.push_back(sds_certificate_validation_context_secrets_->trusted_ca().filename());
    }
    if (sds_certificate_validation_context_secrets_->has_crl() &&
        sds_certificate_validation_context_secrets_->crl().specifier_case() ==
            envoy::config::core::v3::DataSource::SpecifierCase::kFilename) {
      files.push_back(sds_certificate_validation_context_secrets_->crl().filename());
    }
  }
  return files;
}

std::vector<std::string> TlsSessionTicketKeysSdsApi::getDataSourceFilenames() { return {}; }

std::vector<std::string> GenericSecretSdsApi::getDataSourceFilenames() { return {}; }

} // namespace Secret
} // namespace Envoy
