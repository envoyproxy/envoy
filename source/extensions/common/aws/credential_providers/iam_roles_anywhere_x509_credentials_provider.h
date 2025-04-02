#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "source/common/common/logger.h"
#include "source/common/config/datasource.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "envoy/server/factory_context.h"
#include "source/common/common/base64.h" // IWYU pragma: export

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class CachedX509CredentialsProviderBase : public X509CredentialsProvider,
                                          public Logger::Loggable<Logger::Id::aws> {
public:
  X509Credentials getCredentials() override {
    refreshIfNeeded();
    return cached_credentials_;
  }

protected:
  SystemTime last_updated_;
  X509Credentials cached_credentials_;

  void refreshIfNeeded();

  virtual bool needsRefresh() PURE;
  virtual void refresh() PURE;
};

/**
 * Retrieve IAM Roles Certificate for use in signing.
 *
 * IAMRolesAnywhereX509CredentialsProvider purpose is to retrieve certificate, private key and chain
 * from an Envoy DataSource
 *
 * This class is referenced via IAMRolesAnywhereCredentialsProvider, which is the provider that
 * returns normal AWS Access Key Credentials to any of the other SigV4/SigV4A signing extension
 * components
 *
 */
class IAMRolesAnywhereX509CredentialsProvider : public CachedX509CredentialsProviderBase {
public:
  IAMRolesAnywhereX509CredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      envoy::config::core::v3::DataSource certificate_data_source,
      envoy::config::core::v3::DataSource private_key_data_source,
      absl::optional<envoy::config::core::v3::DataSource> certificate_chain_data_source);

private:
  Server::Configuration::ServerFactoryContext& context_;
  envoy::config::core::v3::DataSource certificate_data_source_;
  Config::DataSource::DataSourceProviderPtr certificate_data_source_provider_;
  Config::DataSource::DataSourceProviderPtr private_key_data_source_provider_;
  absl::optional<Config::DataSource::DataSourceProviderPtr> certificate_chain_data_source_provider_;
  envoy::config::core::v3::DataSource private_key_data_source_;
  absl::optional<envoy::config::core::v3::DataSource> certificate_chain_data_source_;
  absl::optional<SystemTime> expiration_time_;
  std::chrono::seconds cache_duration_;

  bool needsRefresh() override;
  void refresh() override;

  absl::Status pemToDerB64(std::string pem, std::string& output, bool chain = false);
  absl::Status
  pemToAlgorithmSerialExpiration(std::string pem,
                                 X509Credentials::PublicKeySignatureAlgorithm& algorithm,
                                 std::string& serial, SystemTime& time);
  std::chrono::seconds getCacheDuration();
};

using IAMRolesAnywhereX509CredentialsProviderPtr =
    std::shared_ptr<IAMRolesAnywhereX509CredentialsProvider>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
