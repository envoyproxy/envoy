#pragma once

#include <functional>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/api/v2/core/config_source.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/server/instance.h"

namespace Envoy {
namespace Secret {

/**
 * SDS API implementation that fetches secrets from SDS server via Subscription.
 */
class SdsApi : public Init::Target,
               public DynamicSecretProvider,
               public Config::SubscriptionCallbacks<envoy::api::v2::auth::Secret> {
public:
  SdsApi(Server::Instance& server, const envoy::api::v2::core::ConfigSource& sds_config,
         std::string sds_config_name);

  // Init::Target
  void initialize(std::function<void()> callback) override;

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const ResourceVector& resources, const std::string& version_info) override;
  void onConfigUpdateFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::auth::Secret>(resource).name();
  }

  // DynamicSecretProvider
  const Ssl::TlsCertificateConfigSharedPtr secret() const override {
    return tls_certificate_secrets_;
  }
  void addUpdateCallback(SecretCallbacks& callback) override {
    update_callbacks_.push_back(&callback);
  }
  void removeUpdateCallback(SecretCallbacks& callback) override {
    update_callbacks_.remove(&callback);
  }

private:
  void runInitializeCallbackIfAny();

  Server::Instance& server_;
  const envoy::api::v2::core::ConfigSource sds_config_;
  std::unique_ptr<Config::Subscription<envoy::api::v2::auth::Secret>> subscription_;
  std::function<void()> initialize_callback_;
  std::string sds_config_name_;

  std::size_t secret_hash_;
  Ssl::TlsCertificateConfigSharedPtr tls_certificate_secrets_;
  std::list<SecretCallbacks*> update_callbacks_;
};

typedef std::unique_ptr<SdsApi> SdsApiPtr;

} // namespace Secret
} // namespace Envoy
