#pragma once

#include "envoy/http/message.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/common/aws/aws_cluster_manager.h"
#include "source/extensions/common/aws/credential_provider_chains.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/metadata_credentials_provider_base.h"
#include "source/extensions/common/aws/metadata_fetcher.h"
#include "source/extensions/common/aws/signer.h"
#include "source/extensions/common/aws/signers/sigv4a_key_derivation.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class MockMetadataFetcher : public MetadataFetcher {
public:
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

  MOCK_METHOD(absl::Status, sign, (Http::RequestMessage&, bool, absl::string_view));
  MOCK_METHOD(absl::Status, sign, (Http::RequestHeaderMap&, const std::string&, absl::string_view));
  MOCK_METHOD(absl::Status, signEmptyPayload, (Http::RequestHeaderMap&, absl::string_view));
  MOCK_METHOD(absl::Status, signUnsignedPayload, (Http::RequestHeaderMap&, absl::string_view));
  MOCK_METHOD(bool, addCallbackIfCredentialsPending, (CredentialsPendingCallback &&));
};

class MockFetchMetadata {
public:
  virtual ~MockFetchMetadata() = default;

  MOCK_METHOD(absl::optional<std::string>, fetch, (Http::RequestMessage&), (const));
};

class DummyMetadataFetcher {
public:
  absl::optional<std::string> operator()(Http::RequestMessage&) { return absl::nullopt; }
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
  MOCK_METHOD(bool, addCallbackIfChainCredentialsPending, (CredentialsPendingCallback &&));
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
      (Server::Configuration::ServerFactoryContext&, AwsClusterManagerOptRef, absl::string_view,
       const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&));

  MOCK_METHOD(CredentialsProviderSharedPtr, createContainerCredentialsProvider,
              (Api::Api&, ServerFactoryContextOptRef, AwsClusterManagerOptRef,
               const MetadataCredentialsProviderBase::CurlMetadataFetcher&, CreateMetadataFetcherCb,
               absl::string_view, absl::string_view,
               MetadataFetcher::MetadataReceiver::RefreshState, std::chrono::seconds,
               absl::string_view));

  MOCK_METHOD(CredentialsProviderSharedPtr, createInstanceProfileCredentialsProvider,
              (Api::Api&, ServerFactoryContextOptRef, AwsClusterManagerOptRef,
               const MetadataCredentialsProviderBase::CurlMetadataFetcher&, CreateMetadataFetcherCb,
               MetadataFetcher::MetadataReceiver::RefreshState, std::chrono::seconds,
               absl::string_view));
};

class MockCustomCredentialsProviderChainFactories : public CustomCredentialsProviderChainFactories {
public:
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
      (Server::Configuration::ServerFactoryContext&, AwsClusterManagerOptRef, absl::string_view,
       const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&));
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
  std::shared_ptr<MetadataCredentialsProviderBase> provider_;
  bool needsRefresh() { return provider_->needsRefresh(); };
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
