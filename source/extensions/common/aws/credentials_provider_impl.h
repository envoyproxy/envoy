#pragma once

#include <list>

#include "envoy/api/api.h"
#include "envoy/event/timer.h"

#include "common/common/logger.h"
#include "common/common/thread.h"

#include "extensions/common/aws/credentials_provider.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

/**
 * Retrieve AWS credentials from the environment variables.
 *
 * Adheres to conventions specified in:
 * https://docs.aws.amazon.com/cli/latest/userguide/cli-configure-envvars.html
 */
class EnvironmentCredentialsProvider : public CredentialsProvider,
                                       public Logger::Loggable<Logger::Id::aws> {
public:
  Credentials getCredentials() override;
};

class MetadataCredentialsProviderBase : public CredentialsProvider,
                                        public Logger::Loggable<Logger::Id::aws> {
public:
  using MetadataFetcher = std::function<absl::optional<std::string>(
      const std::string& host, const std::string& path, const std::string& auth_token)>;

  MetadataCredentialsProviderBase(Api::Api& api, const MetadataFetcher& metadata_fetcher)
      : api_(api), metadata_fetcher_(metadata_fetcher) {}

  Credentials getCredentials() override {
    refreshIfNeeded();
    return cached_credentials_;
  }

protected:
  Api::Api& api_;
  MetadataFetcher metadata_fetcher_;
  SystemTime last_updated_;
  Credentials cached_credentials_;
  Thread::MutexBasicLockable lock_;

  void refreshIfNeeded();

  virtual bool needsRefresh() PURE;
  virtual void refresh() PURE;
};

/**
 * Retrieve AWS credentials from the instance metadata.
 *
 * https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/iam-roles-for-amazon-ec2.html#instance-metadata-security-credentials
 */
class InstanceProfileCredentialsProvider : public MetadataCredentialsProviderBase {
public:
  InstanceProfileCredentialsProvider(Api::Api& api, const MetadataFetcher& metadata_fetcher)
      : MetadataCredentialsProviderBase(api, metadata_fetcher) {}

private:
  bool needsRefresh() override;
  void refresh() override;
};

/**
 * Retrieve AWS credentials from the task metadata.
 *
 * https://docs.aws.amazon.com/AmazonECS/latest/developerguide/task-iam-roles.html#enable_task_iam_roles
 */
class TaskRoleCredentialsProvider : public MetadataCredentialsProviderBase {
public:
  TaskRoleCredentialsProvider(Api::Api& api, const MetadataFetcher& metadata_fetcher,
                              absl::string_view credential_uri,
                              absl::string_view authorization_token = {})
      : MetadataCredentialsProviderBase(api, metadata_fetcher), credential_uri_(credential_uri),
        authorization_token_(authorization_token) {}

private:
  SystemTime expiration_time_;
  std::string credential_uri_;
  std::string authorization_token_;

  bool needsRefresh() override;
  void refresh() override;
};

/**
 * AWS credentials provider chain, able to fallback between multiple credential providers.
 */
class CredentialsProviderChain : public CredentialsProvider,
                                 public Logger::Loggable<Logger::Id::aws> {
public:
  ~CredentialsProviderChain() override = default;

  void add(const CredentialsProviderSharedPtr& credentials_provider) {
    providers_.emplace_back(credentials_provider);
  }

  Credentials getCredentials() override;

protected:
  std::list<CredentialsProviderSharedPtr> providers_;
};

class CredentialsProviderChainFactories {
public:
  virtual ~CredentialsProviderChainFactories() = default;

  virtual CredentialsProviderSharedPtr createEnvironmentCredentialsProvider() const PURE;

  virtual CredentialsProviderSharedPtr createTaskRoleCredentialsProvider(
      Api::Api& api, const MetadataCredentialsProviderBase::MetadataFetcher& metadata_fetcher,
      absl::string_view credential_uri, absl::string_view authorization_token = {}) const PURE;

  virtual CredentialsProviderSharedPtr createInstanceProfileCredentialsProvider(
      Api::Api& api,
      const MetadataCredentialsProviderBase::MetadataFetcher& metadata_fetcher) const PURE;
};

/**
 * Default AWS credentials provider chain.
 *
 * Reference implementation:
 * https://github.com/aws/aws-sdk-cpp/blob/master/aws-cpp-sdk-core/source/auth/AWSCredentialsProviderChain.cpp#L44
 */
class DefaultCredentialsProviderChain : public CredentialsProviderChain,
                                        public CredentialsProviderChainFactories {
public:
  DefaultCredentialsProviderChain(
      Api::Api& api, const MetadataCredentialsProviderBase::MetadataFetcher& metadata_fetcher)
      : DefaultCredentialsProviderChain(api, metadata_fetcher, *this) {}

  DefaultCredentialsProviderChain(
      Api::Api& api, const MetadataCredentialsProviderBase::MetadataFetcher& metadata_fetcher,
      const CredentialsProviderChainFactories& factories);

private:
  CredentialsProviderSharedPtr createEnvironmentCredentialsProvider() const override {
    return std::make_shared<EnvironmentCredentialsProvider>();
  }

  CredentialsProviderSharedPtr createTaskRoleCredentialsProvider(
      Api::Api& api, const MetadataCredentialsProviderBase::MetadataFetcher& metadata_fetcher,
      absl::string_view credential_uri, absl::string_view authorization_token = {}) const override {
    return std::make_shared<TaskRoleCredentialsProvider>(api, metadata_fetcher, credential_uri,
                                                         authorization_token);
  }

  CredentialsProviderSharedPtr createInstanceProfileCredentialsProvider(
      Api::Api& api,
      const MetadataCredentialsProviderBase::MetadataFetcher& metadata_fetcher) const override {
    return std::make_shared<InstanceProfileCredentialsProvider>(api, metadata_fetcher);
  }
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
