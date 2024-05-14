#pragma once

#include <functional>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/secret.pb.validate.h"
#include "envoy/init/manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_callbacks.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/callback_impl.h"
#include "source/common/common/cleanup.h"
#include "source/common/config/subscription_base.h"
#include "source/common/config/utility.h"
#include "source/common/config/watched_directory.h"
#include "source/common/init/target_impl.h"
#include "source/common/ssl/certificate_validation_context_config_impl.h"
#include "source/common/ssl/tls_certificate_config_impl.h"

namespace Envoy {
namespace Secret {

/**
 * All SDS API. @see stats_macros.h
 */
#define ALL_SDS_API_STATS(COUNTER) COUNTER(key_rotation_failed)

/**
 * Struct definition for all SDS API stats. @see stats_macros.h
 */
struct SdsApiStats {
  ALL_SDS_API_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * SDS API implementation that fetches secrets from SDS server via Subscription.
 */
class SdsApi : public Envoy::Config::SubscriptionBase<
                   envoy::extensions::transport_sockets::tls::v3::Secret> {
public:
  struct SecretData {
    const std::string resource_name_;
    std::string version_info_;
    SystemTime last_updated_;
  };

  SdsApi(envoy::config::core::v3::ConfigSource sds_config, absl::string_view sds_config_name,
         Config::SubscriptionFactory& subscription_factory, TimeSource& time_source,
         ProtobufMessage::ValidationVisitor& validation_visitor, Stats::Store& stats,
         std::function<void()> destructor_cb, Event::Dispatcher& dispatcher, Api::Api& api);

  SecretData secretData();

protected:
  // Ordered for hash stability.
  using FileContentMap = std::map<std::string, std::string>;

  // Creates new secrets.
  virtual void setSecret(const envoy::extensions::transport_sockets::tls::v3::Secret&) PURE;
  // Refresh secrets, e.g. re-resolve symlinks in secret paths.
  virtual void resolveSecret(const FileContentMap& /*files*/){};
  virtual void validateConfig(const envoy::extensions::transport_sockets::tls::v3::Secret&) PURE;
  Common::CallbackManager<> update_callback_manager_;

  // Config::SubscriptionCallbacks
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                              const std::string& version_info) override;
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string& system_version_info) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;
  virtual std::vector<std::string> getDataSourceFilenames() PURE;
  virtual Config::WatchedDirectory* getWatchedDirectory() PURE;

  void resolveDataSource(const FileContentMap& files,
                         envoy::config::core::v3::DataSource& data_source);

  Init::SharedTargetImpl init_target_;
  Event::Dispatcher& dispatcher_;
  Api::Api& api_;

private:
  absl::Status validateUpdateSize(uint32_t added_resources_num,
                                  uint32_t removed_resources_num) const;
  void initialize();
  FileContentMap loadFiles();
  uint64_t getHashForFiles(const FileContentMap& files);
  // Invoked for filesystem watches on update.
  void onWatchUpdate();
  SdsApiStats generateStats(Stats::Scope& scope);

  Stats::ScopeSharedPtr scope_;
  SdsApiStats sds_api_stats_;

  const envoy::config::core::v3::ConfigSource sds_config_;
  Config::SubscriptionPtr subscription_;
  const std::string sds_config_name_;

  uint64_t secret_hash_{0};
  uint64_t files_hash_;
  Cleanup clean_up_;
  Config::SubscriptionFactory& subscription_factory_;
  TimeSource& time_source_;
  SecretData secret_data_;
  std::unique_ptr<Filesystem::Watcher> watcher_;
};

class TlsCertificateSdsApi;
class CertificateValidationContextSdsApi;
class TlsSessionTicketKeysSdsApi;
class GenericSecretSdsApi;
using TlsCertificateSdsApiSharedPtr = std::shared_ptr<TlsCertificateSdsApi>;
using CertificateValidationContextSdsApiSharedPtr =
    std::shared_ptr<CertificateValidationContextSdsApi>;
using TlsSessionTicketKeysSdsApiSharedPtr = std::shared_ptr<TlsSessionTicketKeysSdsApi>;
using GenericSecretSdsApiSharedPtr = std::shared_ptr<GenericSecretSdsApi>;

/**
 * TlsCertificateSdsApi implementation maintains and updates dynamic TLS certificate secrets.
 */
class TlsCertificateSdsApi : public SdsApi, public TlsCertificateConfigProvider {
public:
  static TlsCertificateSdsApiSharedPtr
  create(Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
         const envoy::config::core::v3::ConfigSource& sds_config,
         const std::string& sds_config_name, std::function<void()> destructor_cb) {
    // We need to do this early as we invoke the subscription factory during initialization, which
    // is too late to throw.
    auto& server_context = secret_provider_context.serverFactoryContext();
    THROW_IF_NOT_OK(
        Config::Utility::checkLocalInfo("TlsCertificateSdsApi", server_context.localInfo()));
    return std::make_shared<TlsCertificateSdsApi>(
        sds_config, sds_config_name, secret_provider_context.clusterManager().subscriptionFactory(),
        server_context.mainThreadDispatcher().timeSource(),
        secret_provider_context.messageValidationVisitor(), server_context.serverScope().store(),
        destructor_cb, server_context.mainThreadDispatcher(), server_context.api());
  }

  TlsCertificateSdsApi(const envoy::config::core::v3::ConfigSource& sds_config,
                       const std::string& sds_config_name,
                       Config::SubscriptionFactory& subscription_factory, TimeSource& time_source,
                       ProtobufMessage::ValidationVisitor& validation_visitor, Stats::Store& stats,
                       std::function<void()> destructor_cb, Event::Dispatcher& dispatcher,
                       Api::Api& api)
      : SdsApi(sds_config, sds_config_name, subscription_factory, time_source, validation_visitor,
               stats, std::move(destructor_cb), dispatcher, api) {}

  // SecretProvider
  const envoy::extensions::transport_sockets::tls::v3::TlsCertificate* secret() const override {
    return resolved_tls_certificate_secrets_.get();
  }
  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr addValidationCallback(
      std::function<absl::Status(
          const envoy::extensions::transport_sockets::tls::v3::TlsCertificate&)>) override {
    return nullptr;
  }
  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addUpdateCallback(std::function<absl::Status()> callback) override {
    if (secret()) {
      THROW_IF_NOT_OK(callback());
    }
    return update_callback_manager_.add(callback);
  }
  const Init::Target* initTarget() override { return &init_target_; }

protected:
  void setSecret(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) override {
    sds_tls_certificate_secrets_ =
        std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>(
            secret.tls_certificate());
    resolved_tls_certificate_secrets_ = nullptr;
    if (secret.tls_certificate().has_watched_directory()) {
      watched_directory_ = std::make_unique<Config::WatchedDirectory>(
          secret.tls_certificate().watched_directory(), dispatcher_);
    } else {
      watched_directory_.reset();
    }
  }
  void resolveSecret(const FileContentMap& files) override {
    resolved_tls_certificate_secrets_ =
        std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>(
            *sds_tls_certificate_secrets_);
    // We replace path based secrets with inlined secrets on update.
    resolveDataSource(files, *resolved_tls_certificate_secrets_->mutable_certificate_chain());
    if (sds_tls_certificate_secrets_->has_private_key()) {
      resolveDataSource(files, *resolved_tls_certificate_secrets_->mutable_private_key());
    }
  }
  void validateConfig(const envoy::extensions::transport_sockets::tls::v3::Secret&) override {}
  std::vector<std::string> getDataSourceFilenames() override;
  Config::WatchedDirectory* getWatchedDirectory() override { return watched_directory_.get(); }

private:
  // Path to watch for rotation.
  Config::WatchedDirectoryPtr watched_directory_;
  // TlsCertificate according to SDS source.
  TlsCertificatePtr sds_tls_certificate_secrets_;
  // TlsCertificate after reloading. Path based certificates are inlined for
  // future read consistency.
  TlsCertificatePtr resolved_tls_certificate_secrets_;
};

/**
 * CertificateValidationContextSdsApi implementation maintains and updates dynamic certificate
 * validation context secrets.
 */
class CertificateValidationContextSdsApi : public SdsApi,
                                           public CertificateValidationContextConfigProvider {
public:
  static CertificateValidationContextSdsApiSharedPtr
  create(Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
         const envoy::config::core::v3::ConfigSource& sds_config,
         const std::string& sds_config_name, std::function<void()> destructor_cb) {
    // We need to do this early as we invoke the subscription factory during initialization, which
    // is too late to throw.
    auto& server_context = secret_provider_context.serverFactoryContext();
    THROW_IF_NOT_OK(Config::Utility::checkLocalInfo("CertificateValidationContextSdsApi",
                                                    server_context.localInfo()));
    return std::make_shared<CertificateValidationContextSdsApi>(
        sds_config, sds_config_name, secret_provider_context.clusterManager().subscriptionFactory(),
        server_context.mainThreadDispatcher().timeSource(),
        secret_provider_context.messageValidationVisitor(), server_context.serverScope().store(),
        destructor_cb, server_context.mainThreadDispatcher(), server_context.api());
  }
  CertificateValidationContextSdsApi(const envoy::config::core::v3::ConfigSource& sds_config,
                                     const std::string& sds_config_name,
                                     Config::SubscriptionFactory& subscription_factory,
                                     TimeSource& time_source,
                                     ProtobufMessage::ValidationVisitor& validation_visitor,
                                     Stats::Store& stats, std::function<void()> destructor_cb,
                                     Event::Dispatcher& dispatcher, Api::Api& api)
      : SdsApi(sds_config, sds_config_name, subscription_factory, time_source, validation_visitor,
               stats, std::move(destructor_cb), dispatcher, api) {}

  // SecretProvider
  const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
  secret() const override {
    return resolved_certificate_validation_context_secrets_.get();
  }
  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addUpdateCallback(std::function<absl::Status()> callback) override {
    if (secret()) {
      THROW_IF_NOT_OK(callback());
    }
    return update_callback_manager_.add(callback);
  }
  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr addValidationCallback(
      std::function<absl::Status(
          const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&)>
          callback) override {
    return validation_callback_manager_.add(callback);
  }
  const Init::Target* initTarget() override { return &init_target_; }

protected:
  void setSecret(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) override {
    sds_certificate_validation_context_secrets_ = std::make_unique<
        envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext>(
        secret.validation_context());
    resolved_certificate_validation_context_secrets_ = nullptr;
    if (secret.validation_context().has_watched_directory()) {
      watched_directory_ = std::make_unique<Config::WatchedDirectory>(
          secret.validation_context().watched_directory(), dispatcher_);
    } else {
      watched_directory_.reset();
    }
  }

  void resolveSecret(const FileContentMap& files) override {
    // Copy existing CertificateValidationContext.
    resolved_certificate_validation_context_secrets_ = std::make_unique<
        envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext>(
        *sds_certificate_validation_context_secrets_);
    // We replace path based secrets with inlined secrets on update.
    resolveDataSource(files,
                      *resolved_certificate_validation_context_secrets_->mutable_trusted_ca());
    if (sds_certificate_validation_context_secrets_->has_crl()) {
      resolveDataSource(files, *resolved_certificate_validation_context_secrets_->mutable_crl());
    }
  }

  void
  validateConfig(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) override {
    THROW_IF_NOT_OK(validation_callback_manager_.runCallbacks(secret.validation_context()));
  }
  std::vector<std::string> getDataSourceFilenames() override;
  Config::WatchedDirectory* getWatchedDirectory() override { return watched_directory_.get(); }

private:
  // Directory to watch for rotation.
  Config::WatchedDirectoryPtr watched_directory_;
  // CertificateValidationContext according to SDS source;
  CertificateValidationContextPtr sds_certificate_validation_context_secrets_;
  // CertificateValidationContext after resolving paths via watched_directory_.
  CertificateValidationContextPtr resolved_certificate_validation_context_secrets_;
  // Path based certificates are inlined for future read consistency.
  Common::CallbackManager<
      const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&>
      validation_callback_manager_;
};

/**
 * TlsSessionTicketKeysSdsApi implementation maintains and updates dynamic tls session ticket keys
 * secrets.
 */
class TlsSessionTicketKeysSdsApi : public SdsApi, public TlsSessionTicketKeysConfigProvider {
public:
  static TlsSessionTicketKeysSdsApiSharedPtr
  create(Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
         const envoy::config::core::v3::ConfigSource& sds_config,
         const std::string& sds_config_name, std::function<void()> destructor_cb) {
    // We need to do this early as we invoke the subscription factory during initialization, which
    // is too late to throw.
    auto& server_context = secret_provider_context.serverFactoryContext();
    THROW_IF_NOT_OK(
        Config::Utility::checkLocalInfo("TlsSessionTicketKeysSdsApi", server_context.localInfo()));
    return std::make_shared<TlsSessionTicketKeysSdsApi>(
        sds_config, sds_config_name, secret_provider_context.clusterManager().subscriptionFactory(),
        server_context.mainThreadDispatcher().timeSource(),
        secret_provider_context.messageValidationVisitor(), server_context.serverScope().store(),
        destructor_cb, server_context.mainThreadDispatcher(), server_context.api());
  }

  TlsSessionTicketKeysSdsApi(const envoy::config::core::v3::ConfigSource& sds_config,
                             const std::string& sds_config_name,
                             Config::SubscriptionFactory& subscription_factory,
                             TimeSource& time_source,
                             ProtobufMessage::ValidationVisitor& validation_visitor,
                             Stats::Store& stats, std::function<void()> destructor_cb,
                             Event::Dispatcher& dispatcher, Api::Api& api)
      : SdsApi(sds_config, sds_config_name, subscription_factory, time_source, validation_visitor,
               stats, std::move(destructor_cb), dispatcher, api) {}

  // SecretProvider
  const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys*
  secret() const override {
    return tls_session_ticket_keys_.get();
  }

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addUpdateCallback(std::function<absl::Status()> callback) override {
    if (secret()) {
      THROW_IF_NOT_OK(callback());
    }
    return update_callback_manager_.add(callback);
  }

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr addValidationCallback(
      std::function<
          absl::Status(const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys&)>
          callback) override {
    return validation_callback_manager_.add(callback);
  }
  const Init::Target* initTarget() override { return &init_target_; }

protected:
  void setSecret(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) override {
    tls_session_ticket_keys_ =
        std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys>(
            secret.session_ticket_keys());
  }

  void
  validateConfig(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) override {
    THROW_IF_NOT_OK(validation_callback_manager_.runCallbacks(secret.session_ticket_keys()));
  }
  std::vector<std::string> getDataSourceFilenames() override;
  Config::WatchedDirectory* getWatchedDirectory() override { return nullptr; }

private:
  Secret::TlsSessionTicketKeysPtr tls_session_ticket_keys_;
  Common::CallbackManager<
      const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys&>
      validation_callback_manager_;
};

/**
 * GenericSecretSdsApi implementation maintains and updates dynamic generic secret.
 */
class GenericSecretSdsApi : public SdsApi, public GenericSecretConfigProvider {
public:
  static GenericSecretSdsApiSharedPtr
  create(Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
         const envoy::config::core::v3::ConfigSource& sds_config,
         const std::string& sds_config_name, std::function<void()> destructor_cb) {
    // We need to do this early as we invoke the subscription factory during initialization, which
    // is too late to throw.
    auto& server_context = secret_provider_context.serverFactoryContext();
    THROW_IF_NOT_OK(
        Config::Utility::checkLocalInfo("GenericSecretSdsApi", server_context.localInfo()));
    return std::make_shared<GenericSecretSdsApi>(
        sds_config, sds_config_name, secret_provider_context.clusterManager().subscriptionFactory(),
        server_context.mainThreadDispatcher().timeSource(),
        secret_provider_context.messageValidationVisitor(), server_context.serverScope().store(),
        destructor_cb, server_context.mainThreadDispatcher(), server_context.api());
  }

  GenericSecretSdsApi(const envoy::config::core::v3::ConfigSource& sds_config,
                      const std::string& sds_config_name,
                      Config::SubscriptionFactory& subscription_factory, TimeSource& time_source,
                      ProtobufMessage::ValidationVisitor& validation_visitor, Stats::Store& stats,
                      std::function<void()> destructor_cb, Event::Dispatcher& dispatcher,
                      Api::Api& api)
      : SdsApi(sds_config, sds_config_name, subscription_factory, time_source, validation_visitor,
               stats, std::move(destructor_cb), dispatcher, api) {}

  // SecretProvider
  const envoy::extensions::transport_sockets::tls::v3::GenericSecret* secret() const override {
    return generic_secret_.get();
  }
  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addUpdateCallback(std::function<absl::Status()> callback) override {
    return update_callback_manager_.add(callback);
  }
  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addValidationCallback(std::function<absl::Status(
                            const envoy::extensions::transport_sockets::tls::v3::GenericSecret&)>
                            callback) override {
    return validation_callback_manager_.add(callback);
  }
  const Init::Target* initTarget() override { return &init_target_; }

protected:
  void setSecret(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) override {
    generic_secret_ =
        std::make_unique<envoy::extensions::transport_sockets::tls::v3::GenericSecret>(
            secret.generic_secret());
  }
  void
  validateConfig(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) override {
    THROW_IF_NOT_OK(validation_callback_manager_.runCallbacks(secret.generic_secret()));
  }
  std::vector<std::string> getDataSourceFilenames() override;
  Config::WatchedDirectory* getWatchedDirectory() override { return nullptr; }

private:
  GenericSecretPtr generic_secret_;
  Common::CallbackManager<const envoy::extensions::transport_sockets::tls::v3::GenericSecret&>
      validation_callback_manager_;
};

} // namespace Secret
} // namespace Envoy
