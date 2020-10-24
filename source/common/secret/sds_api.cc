#include "common/secret/sds_api.h"

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/common/assert.h"
#include "common/config/api_version.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Secret {

SdsApi::SdsApi(envoy::config::core::v3::ConfigSource sds_config, absl::string_view sds_config_name,
               Config::SubscriptionFactory& subscription_factory, TimeSource& time_source,
               ProtobufMessage::ValidationVisitor& validation_visitor, Stats::Store& stats,
               std::function<void()> destructor_cb, Event::Dispatcher& dispatcher, Api::Api& api)
    : Envoy::Config::SubscriptionBase<envoy::extensions::transport_sockets::tls::v3::Secret>(
          sds_config.resource_api_version(), validation_visitor, "name"),
      init_target_(fmt::format("SdsApi {}", sds_config_name), [this] { initialize(); }),
      dispatcher_(dispatcher), api_(api), stats_(stats), sds_config_(std::move(sds_config)),
      sds_config_name_(sds_config_name), secret_hash_(0), clean_up_(std::move(destructor_cb)),
      subscription_factory_(subscription_factory),
      time_source_(time_source), secret_data_{sds_config_name_, "uninitialized",
                                              time_source_.systemTime()} {
  const auto resource_name = getResourceName();
  // This has to happen here (rather than in initialize()) as it can throw exceptions.
  subscription_ = subscription_factory_.subscriptionFromConfigSource(
      sds_config_, Grpc::Common::typeUrl(resource_name), stats_, *this, resource_decoder_);

  // TODO(JimmyCYJ): Implement chained_init_manager, so that multiple init_manager
  // can be chained together to behave as one init_manager. In that way, we let
  // two listeners which share same SdsApi to register at separate init managers, and
  // each init manager has a chance to initialize its targets.
}

void SdsApi::onWatchUpdate() {
  const uint64_t new_hash = getHashForFiles();
  if (new_hash != files_hash_) {
    update_callback_manager_.runCallbacks();
    files_hash_ = new_hash;
  }
}

void SdsApi::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                            const std::string& version_info) {
  validateUpdateSize(resources.size());
  const auto& secret = dynamic_cast<const envoy::extensions::transport_sockets::tls::v3::Secret&>(
      resources[0].get().resource());

  if (secret.name() != sds_config_name_) {
    throw EnvoyException(
        fmt::format("Unexpected SDS secret (expecting {}): {}", sds_config_name_, secret.name()));
  }

  const uint64_t new_hash = MessageUtil::hash(secret);

  if (new_hash != secret_hash_) {
    validateConfig(secret);
    secret_hash_ = new_hash;
    setSecret(secret);
    update_callback_manager_.runCallbacks();

    auto* watched_directory = getWatchedDirectory();
    files_hash_ = getHashForFiles();
    // Either we have a watched directory and can defer the watch monitoring to
    // a WatchedDirectory object, or we need to implement per-file watches
    // in the else clause.
    if (watched_directory != nullptr) {
      watched_directory->setCallback([this]() {
        resolveSecret();
        onWatchUpdate();
      });
    } else {
      // List DataSources that refer to files
      auto files = getDataSourceFilenames();
      if (!files.empty()) {
        // Create new watch, also destroys the old watch if any.
        watcher_ = dispatcher_.createFilesystemWatcher();
        for (auto const& filename : files) {
          // Watch for directory instead of file. This allows users to do atomic renames
          // on directory level (e.g. Kubernetes secret update).
          const auto result = api_.fileSystem().splitPathFromFilename(filename);
          watcher_->addWatch(absl::StrCat(result.directory_, "/"),
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
}

void SdsApi::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                            const Protobuf::RepeatedPtrField<std::string>&, const std::string&) {
  validateUpdateSize(added_resources.size());
  onConfigUpdate(added_resources, added_resources[0].get().version());
}

void SdsApi::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                  const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // We need to allow server startup to continue, even if we have a bad config.
  init_target_.ready();
}

void SdsApi::validateUpdateSize(int num_resources) {
  if (num_resources == 0) {
    throw EnvoyException(
        fmt::format("Missing SDS resources for {} in onConfigUpdate()", sds_config_name_));
  }
  if (num_resources != 1) {
    throw EnvoyException(fmt::format("Unexpected SDS secrets length: {}", num_resources));
  }
}

void SdsApi::initialize() {
  // Don't put any code here that can throw exceptions, this has been the cause of multiple
  // hard-to-diagnose regressions.
  subscription_->start({sds_config_name_});
}

SdsApi::SecretData SdsApi::secretData() { return secret_data_; }

uint64_t SdsApi::getHashForFiles() {
  uint64_t hash = 0;
  for (auto const& filename : getDataSourceFilenames()) {
    hash = HashUtil::xxHash64(api_.fileSystem().fileReadToEnd(filename), hash);
  }
  return hash;
}

std::vector<std::string> TlsCertificateSdsApi::getDataSourceFilenames() {
  std::vector<std::string> files;
  if (resolved_tls_certificate_secrets_ &&
      resolved_tls_certificate_secrets_->has_certificate_chain() &&
      resolved_tls_certificate_secrets_->certificate_chain().specifier_case() ==
          envoy::config::core::v3::DataSource::SpecifierCase::kFilename) {
    files.push_back(resolved_tls_certificate_secrets_->certificate_chain().filename());
  }
  if (resolved_tls_certificate_secrets_ && resolved_tls_certificate_secrets_->has_private_key() &&
      resolved_tls_certificate_secrets_->private_key().specifier_case() ==
          envoy::config::core::v3::DataSource::SpecifierCase::kFilename) {
    files.push_back(resolved_tls_certificate_secrets_->private_key().filename());
  }
  return files;
}

std::vector<std::string> CertificateValidationContextSdsApi::getDataSourceFilenames() {
  std::vector<std::string> files;
  if (resolved_certificate_validation_context_secrets_ &&
      resolved_certificate_validation_context_secrets_->has_trusted_ca() &&
      resolved_certificate_validation_context_secrets_->trusted_ca().specifier_case() ==
          envoy::config::core::v3::DataSource::SpecifierCase::kFilename) {
    files.push_back(resolved_certificate_validation_context_secrets_->trusted_ca().filename());
  }
  return files;
}

std::vector<std::string> TlsSessionTicketKeysSdsApi::getDataSourceFilenames() { return {}; }

std::vector<std::string> GenericSecretSdsApi::getDataSourceFilenames() { return {}; }

} // namespace Secret
} // namespace Envoy
