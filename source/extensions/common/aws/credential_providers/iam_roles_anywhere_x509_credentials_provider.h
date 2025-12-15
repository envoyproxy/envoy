#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/logger.h"
#include "source/common/config/datasource.h"
#include "source/extensions/common/aws/credentials_provider.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

using DataSourceOptRef = OptRef<const envoy::config::core::v3::DataSource>;

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
  bool is_initialized_ = false;

  /**
   * @brief Cached credential wrapper function for calling refresh only if past the expiration time
   *
   * @param cm the cluster manager to use during AWS Metadata retrieval
   * @param provider the AWS Metadata provider
   * @return a MetadataFetcher instance
   */
  void refreshIfNeeded();

  /**
   * @brief Checks the current time against the expiration time of the certificate, to determine if
   * we need to attempt to retrieve a new certificate
   *
   * @return bool value which indicates if credential refresh is required
   */
  virtual bool needsRefresh() PURE;

  /**
   * @brief Perform the actual load and validation of certificates, then convert them to a usable
   * X509 credentials object for use by the IAMRolesAnywhereCredentialsProvider
   *
   */
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
  /**
   * @brief Constructs an IAM Roles Anywhere X509 credentials provider
   *
   * @param context The server factory context
   * @param certificate_data_source The Envoy DataSource for the signing certificate
   * @param private_key_data_source The Envoy DataSource for the matching private key
   * @param certificate_chain_data_source An optional DataSource for the certificate chain. If no
   *        certificate chain is provided, the IAM Roles Anywhere signing algorithm will not add
   *        the X-Amz-X509-Chain header
   */
  IAMRolesAnywhereX509CredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      const envoy::config::core::v3::DataSource& certificate_data_source,
      const envoy::config::core::v3::DataSource& private_key_data_source,
      DataSourceOptRef certificate_chain_data_source);

  // Friend class for testing private pem functions
  friend class IAMRolesAnywhereX509CredentialsProviderFriend;

  absl::Status initialize();

private:
  Server::Configuration::ServerFactoryContext& context_;
  envoy::config::core::v3::DataSource certificate_data_source_;
  envoy::config::core::v3::DataSource private_key_data_source_;
  DataSourceOptRef certificate_chain_data_source_;
  Config::DataSource::DataSourceProviderPtr<std::string> certificate_data_source_provider_;
  Config::DataSource::DataSourceProviderPtr<std::string> private_key_data_source_provider_;
  absl::optional<Config::DataSource::DataSourceProviderPtr<std::string>>
      certificate_chain_data_source_provider_;
  absl::optional<SystemTime> expiration_time_;
  std::chrono::seconds cache_duration_;

  bool needsRefresh() override;
  void refresh() override;

  /**
   * @brief Convert a PEM certificate to DER format
   *
   * @param [in] pem The PEM formatted certificate(s)
   * @param [out] output The DER + Base64 encoded certificate(s), comma separated
   * @param [in] is_chain If true, this is a certificate chain. We will parse a maximum of 5
   * certificates in a provided chain
   *
   * @return Status indicating success or failure. If at least one certificate can be parsed from
   * the provided PEM or PEM chain, the status will be returned as ok
   */

  absl::Status pemToDerB64(absl::string_view pem, std::string& output, bool is_chain);

  /**
   * @brief Extract algorithm, serial and expiration values from a PEM formatted certificate
   *
   * @param [in] pem The PEM formatted certificate
   * @param [out] algorithm The algorithm of the certificate
   * @param [out] serial The serial of the certificate
   * @param [out] time The expiration time of the certificate
   */

  absl::Status
  pemToAlgorithmSerialExpiration(absl::string_view pem,
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
