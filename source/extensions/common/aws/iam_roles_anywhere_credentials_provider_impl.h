#pragma once

#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

#include "source/common/config/datasource.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/credentials_provider_impl.h"
#include "source/extensions/common/aws/iam_roles_anywhere_credentials_provider_impl.h"
#include "source/extensions/common/aws/iam_roles_anywhere_sigv4_signer_impl.h"
#include "source/extensions/common/aws/metadata_fetcher.h"
#include "source/extensions/common/aws/signer.h"

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
      Api::Api& api, ThreadLocal::SlotAllocator& tls, Event::Dispatcher& dispatcher,
      envoy::config::core::v3::DataSource certificate_data_source,
      envoy::config::core::v3::DataSource private_key_data_source,
      absl::optional<envoy::config::core::v3::DataSource> certificate_chain_data_source);

private:
  Api::Api& api_;
  envoy::config::core::v3::DataSource certificate_data_source_;
  Config::DataSource::DataSourceProviderPtr certificate_data_source_provider_;
  Config::DataSource::DataSourceProviderPtr private_key_data_source_provider_;
  absl::optional<Config::DataSource::DataSourceProviderPtr> certificate_chain_data_source_provider_;
  envoy::config::core::v3::DataSource private_key_data_source_;
  absl::optional<envoy::config::core::v3::DataSource> certificate_chain_data_source_;
  Event::Dispatcher& dispatcher_;
  absl::optional<SystemTime> expiration_time_;
  ThreadLocal::SlotAllocator& tls_;
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

/**
 *
 * IAMRolesAnywhereCredentialsProvider purpose is to Exchange X509 Credentials for Temporary AWS
 * Credentials
 *
 * When instantiated via config, it will create an IAMRolesAnywhereX509CredentialsProvider, which
 * manages the X509 Credentials. IAMRolesAnywhereCredentialsProvider works in the same way as
 * WebIdentityCredentialsProvider, by using the async HTTP client to send requests to the AWS IAM
 * Roles Anywhere service and retrieve temporary AWS Credentials. It is therefore as subclass of
 * MetadataCredentialsProviderBase, which handles the async credential fetch and cluster creation
 * for the IAM Roles Anywhere endpoint.
 *
 * The X509 SigV4 signing process is performed via IAMRolesAnywhereSigV4SignerImpl, which is a
 * modification of standard SigV4 signing to use X509 credentials as the signing input.
 * IAMRolesAnywhereCredentialsProvider is the only consumer of IAMRolesAnywhereSigV4SignerImpl.
 *
 * The logic is as follows:
 *   1. IAMRolesAnywhereX509CredentialsProvider retrieves X509 credentials and converts them to
 * required format
 *
 *   2. IAMRolesAnywhereCredentialsProvider uses credentials from
 * IAMRolesAnywhereX509CredentialsProvider, and uses them as input to
 * IAMRolesAnywhereSigV4SignerImpl
 *
 *   3. Once signing has completed, IAMRolesAnywhereCredentialsProvider requests temporary
 * credentials from IAM Roles Anywhere endpoint using the signed payload
 *
 *   4. Temporary credentials are returned, which then can be used in normal AWS SigV4/SigV4A
 * signing
 *
 */

class IAMRolesAnywhereCredentialsProvider : public MetadataCredentialsProviderBase,
                                            public Envoy::Singleton::Instance,
                                            public MetadataFetcher::MetadataReceiver {
public:
  IAMRolesAnywhereCredentialsProvider(
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

  );

  // Following functions are for MetadataFetcher::MetadataReceiver interface
  void onMetadataSuccess(const std::string&& body) override;
  void onMetadataError(Failure reason) override;

private:
  bool needsRefresh() override;
  void refresh() override;
  void fetchCredentialFromRolesAnywhere(const std::string&& instance_role,
                                        const std::string&& token);
  void extractCredentials(const std::string&& credential_document_value);

  const std::string role_arn_;
  const std::string role_session_name_;
  const std::string profile_arn_;
  const std::string trust_anchor_arn_;
  const std::string region_;
  absl::optional<uint16_t> session_duration_;
  ServerFactoryContextOptRef server_factory_context_;
  std::unique_ptr<Extensions::Common::Aws::IAMRolesAnywhereSigV4SignerImpl> roles_anywhere_signer_;
};

using IAMRolesAnywhereX509CredentialsProviderPtr =
    std::shared_ptr<IAMRolesAnywhereX509CredentialsProvider>;
using IAMRolesAnywhereCredentialsProviderPtr = std::shared_ptr<IAMRolesAnywhereCredentialsProvider>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
