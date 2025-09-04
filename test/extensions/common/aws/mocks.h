#pragma once

#include "envoy/http/message.h"

#include "source/extensions/common/aws/aws_cluster_manager.h"
#include "source/extensions/common/aws/credential_provider_chains.h"
#include "source/extensions/common/aws/credential_providers/iam_roles_anywhere_x509_credentials_provider.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/metadata_credentials_provider_base.h"
#include "source/extensions/common/aws/metadata_fetcher.h"
#include "source/extensions/common/aws/signer.h"
#include "source/extensions/common/aws/signers/iam_roles_anywhere_sigv4_signer_impl.h"
#include "source/extensions/common/aws/signers/sigv4a_key_derivation.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class MockMetadataFetcher : public MetadataFetcher {
public:
  MockMetadataFetcher();
  ~MockMetadataFetcher() override;

  MOCK_METHOD(void, cancel, ());
  MOCK_METHOD(absl::string_view, failureToString, (MetadataFetcher::MetadataReceiver::Failure));
  MOCK_METHOD(void, fetch,
              (Http::RequestMessage & message, Tracing::Span& parent_span,
               MetadataFetcher::MetadataReceiver& receiver));
};

class MockMetadataReceiver : public MetadataFetcher::MetadataReceiver {
public:
  MOCK_METHOD(void, onMetadataSuccess, (const std::string&& body));
  MOCK_METHOD(void, onMetadataError, (MetadataFetcher::MetadataReceiver::Failure reason));
};

class MockCredentialsProvider : public CredentialsProvider {
public:
  MockCredentialsProvider();
  ~MockCredentialsProvider() override;

  MOCK_METHOD(Credentials, getCredentials, ());
  MOCK_METHOD(bool, credentialsPending, ());
  MOCK_METHOD(std::string, providerName, ());
};

class MockSigner : public Signer {
public:
  MockSigner();
  ~MockSigner() override;

  MOCK_METHOD(absl::Status, sign, (Http::RequestMessage&, bool, const absl::string_view));
  MOCK_METHOD(absl::Status, sign,
              (Http::RequestHeaderMap&, const std::string&, const absl::string_view));
  MOCK_METHOD(absl::Status, signEmptyPayload, (Http::RequestHeaderMap&, const absl::string_view));
  MOCK_METHOD(absl::Status, signUnsignedPayload,
              (Http::RequestHeaderMap&, const absl::string_view));
  MOCK_METHOD(bool, addCallbackIfCredentialsPending, (CredentialsPendingCallback&&));
};

class MockFetchMetadata {
public:
  virtual ~MockFetchMetadata() = default;

  MOCK_METHOD(absl::optional<std::string>, fetch, (Http::RequestMessage&), (const));
};

class MockAwsClusterManager : public AwsClusterManager {
public:
  ~MockAwsClusterManager() override = default;

  MOCK_METHOD(absl::Status, addManagedCluster,
              (absl::string_view cluster_name,
               const envoy::config::cluster::v3::Cluster::DiscoveryType cluster_type,
               absl::string_view uri));

  MOCK_METHOD(absl::StatusOr<AwsManagedClusterUpdateCallbacksHandlePtr>,
              addManagedClusterUpdateCallbacks,
              (absl::string_view cluster_name, AwsManagedClusterUpdateCallbacks& cb));
  MOCK_METHOD(absl::StatusOr<std::string>, getUriFromClusterName, (absl::string_view cluster_name));
  MOCK_METHOD(void, createQueuedClusters, ());
};

class MockAwsManagedClusterUpdateCallbacks : public AwsManagedClusterUpdateCallbacks {
public:
  MOCK_METHOD(void, onClusterAddOrUpdate, ());
};

class MockCredentialsProviderChain : public CredentialsProviderChain {
public:
  MOCK_METHOD(Credentials, chainGetCredentials, ());
  MOCK_METHOD(bool, addCallbackIfChainCredentialsPending, (CredentialsPendingCallback&&));
  MOCK_METHOD(void, onCredentialUpdate, ());
};

class MockCredentialsProviderChainFactories : public CredentialsProviderChainFactories {
public:
  MOCK_METHOD(CredentialsProviderSharedPtr, createEnvironmentCredentialsProvider, (), (const));
  MOCK_METHOD(
      CredentialsProviderSharedPtr, mockCreateCredentialsFileCredentialsProvider,
      (Server::Configuration::ServerFactoryContext&,
       (const envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider& config)),
      (const));

  CredentialsProviderSharedPtr createCredentialsFileCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      const envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider& config)
      const override {
    return mockCreateCredentialsFileCredentialsProvider(context, config);
  }

  MOCK_METHOD(
      CredentialsProviderSharedPtr, createWebIdentityCredentialsProvider,
      (Server::Configuration::ServerFactoryContext&, AwsClusterManagerPtr, absl::string_view,
       const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&));

  MOCK_METHOD(CredentialsProviderSharedPtr, createContainerCredentialsProvider,
              (Server::Configuration::ServerFactoryContext&, AwsClusterManagerPtr,
               CreateMetadataFetcherCb, absl::string_view, absl::string_view,
               MetadataFetcher::MetadataReceiver::RefreshState, std::chrono::seconds,
               absl::string_view));

  MOCK_METHOD(CredentialsProviderSharedPtr, createInstanceProfileCredentialsProvider,
              (Server::Configuration::ServerFactoryContext&, AwsClusterManagerPtr,
               CreateMetadataFetcherCb, MetadataFetcher::MetadataReceiver::RefreshState,
               std::chrono::seconds, absl::string_view));

  MOCK_METHOD(
      CredentialsProviderSharedPtr, createAssumeRoleCredentialsProvider,
      (Server::Configuration::ServerFactoryContext & context,
       AwsClusterManagerPtr aws_cluster_manager, absl::string_view region,
       const envoy::extensions::common::aws::v3::AssumeRoleCredentialProvider& assume_role_config));

  MOCK_METHOD(CredentialsProviderSharedPtr, createIAMRolesAnywhereCredentialsProvider,
              (Server::Configuration::ServerFactoryContext & context,
               AwsClusterManagerPtr aws_cluster_manager, absl::string_view region,
               const envoy::extensions::common::aws::v3::IAMRolesAnywhereCredentialProvider&
                   iam_roles_anywhere_config),
              (const));
};

class MockSigV4AKeyDerivation : public SigV4AKeyDerivationBase {
public:
  MOCK_METHOD(absl::StatusOr<EC_KEY*>, derivePrivateKey,
              (absl::string_view access_key_id, absl::string_view secret_access_key));
  MOCK_METHOD(bool, derivePublicKey, (EC_KEY * ec_key));
};

// Friend class for testing callbacks
class MetadataCredentialsProviderBaseFriend {
public:
  MetadataCredentialsProviderBaseFriend(std::shared_ptr<MetadataCredentialsProviderBase> provider)
      : provider_(provider) {}

  void onClusterAddOrUpdate() { return provider_->onClusterAddOrUpdate(); }
  void setMetadataFetcher(MetadataFetcherPtr fetcher) {
    provider_->metadata_fetcher_ = std::move(fetcher);
  }
  void setCacheDurationTimer(Event::Timer* timer) { provider_->cache_duration_timer_.reset(timer); }
  void setCredentialsToAllThreads(CredentialsConstUniquePtr&& creds) {
    provider_->setCredentialsToAllThreads(std::move(creds));
  }
  void invalidateStats() { provider_->stats_.reset(); }

  std::shared_ptr<MetadataCredentialsProviderBase> provider_;
};

// Friend class for testing private pem functionality
class IAMRolesAnywhereX509CredentialsProviderFriend {
public:
  IAMRolesAnywhereX509CredentialsProviderFriend(
      std::unique_ptr<IAMRolesAnywhereX509CredentialsProvider> provider)
      : provider_(std::move(provider)) {}

  absl::Status pemToDerB64(absl::string_view pem, std::string& output, bool chain = false) {
    return provider_->pemToDerB64(pem, output, chain);
  }

  absl::Status
  pemToAlgorithmSerialExpiration(absl::string_view pem,
                                 X509Credentials::PublicKeySignatureAlgorithm& algorithm,
                                 std::string& serial, SystemTime& time) {
    return provider_->pemToAlgorithmSerialExpiration(pem, algorithm, serial, time);
  }

  std::chrono::seconds getCacheDuration() { return provider_->getCacheDuration(); }
  void refresh() { return provider_->refresh(); }
  X509Credentials getCredentials() { return provider_->getCredentials(); }

  std::unique_ptr<IAMRolesAnywhereX509CredentialsProvider> provider_;
};

class MockIAMRolesAnywhereSigV4Signer : public IAMRolesAnywhereSigV4Signer {

public:
  MockIAMRolesAnywhereSigV4Signer(absl::string_view service_name, absl::string_view region,
                                  const X509CredentialsProviderSharedPtr& credentials_provider,
                                  TimeSource& timesource)
      : IAMRolesAnywhereSigV4Signer(service_name, region, credentials_provider, timesource) {}
  ~MockIAMRolesAnywhereSigV4Signer() override = default;
  MOCK_METHOD(absl::Status, sign,
              (Http::RequestMessage & message, bool sign_body,
               const absl::string_view override_region));
  MOCK_METHOD(absl::Status, sign,
              (Http::RequestHeaderMap&, const std::string&, const absl::string_view));

private:
  MOCK_METHOD(std::string, createCredentialScope,
              (const absl::string_view short_date, const absl::string_view override_region),
              (const));

  MOCK_METHOD(absl::StatusOr<std::string>, createSignature,
              (const X509Credentials& credentials, const absl::string_view string_to_sign),
              (const));

  MOCK_METHOD(std::string, createAuthorizationHeader,
              (const X509Credentials& x509_credentials, const absl::string_view credential_scope,
               (const std::map<std::string, std::string>& canonical_headers),
               const absl::string_view signature),
              (const));

  MOCK_METHOD(std::string, createStringToSign,
              (const X509Credentials& x509_credentials, const absl::string_view canonical_request,
               const absl::string_view long_date, const absl::string_view credential_scope),
              (const));
};

class MockX509CredentialsProvider : public X509CredentialsProvider {
public:
  ~MockX509CredentialsProvider() override = default;

  MOCK_METHOD(X509Credentials, getCredentials, ());
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
