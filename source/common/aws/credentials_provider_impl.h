#pragma once

#include <list>

#include "envoy/api/api.h"
#include "envoy/event/timer.h"

#include "common/aws/credentials_provider.h"
#include "common/aws/metadata_fetcher.h"
#include "common/common/logger.h"
#include "common/common/thread.h"

namespace Envoy {
namespace Aws {
namespace Auth {

class EnvironmentCredentialsProvider : public CredentialsProvider,
                                       public Logger::Loggable<Logger::Id::aws> {
public:
  Credentials getCredentials() override;
};

class MetadataCredentialsProviderBase : public CredentialsProvider,
                                        public Logger::Loggable<Logger::Id::aws> {
public:
  MetadataCredentialsProviderBase(Api::Api& api, Event::TimeSystem& time_system,
                                  MetadataFetcherPtr&& fetcher)
      : api_(api), time_system_(time_system), fetcher_(std::move(fetcher)) {}

  Credentials getCredentials() override {
    refreshIfNeeded();
    return cached_credentials_;
  }

protected:
  Api::Api& api_;
  Event::TimeSystem& time_system_;
  MetadataFetcherPtr fetcher_;
  SystemTime last_updated_;
  Credentials cached_credentials_;
  Thread::MutexBasicLockable lock_;

  void refreshIfNeeded();

  virtual bool needsRefresh() PURE;
  virtual void refresh() PURE;
};

class InstanceProfileCredentialsProvider : public MetadataCredentialsProviderBase {
public:
  InstanceProfileCredentialsProvider(Api::Api& api, Event::TimeSystem& time_system,
                                     MetadataFetcherPtr&& fetcher)
      : MetadataCredentialsProviderBase(api, time_system, std::move(fetcher)) {}

private:
  bool needsRefresh() override;
  void refresh() override;
};

class TaskRoleCredentialsProvider : public MetadataCredentialsProviderBase {
public:
  TaskRoleCredentialsProvider(
      Api::Api& api, Event::TimeSystem& time_system, MetadataFetcherPtr&& fetcher,
      const std::string& credential_uri,
      const absl::optional<std::string>& authorization_token = absl::optional<std::string>())
      : MetadataCredentialsProviderBase(api, time_system, std::move(fetcher)),
        credential_uri_(credential_uri), authorization_token_(authorization_token) {}

private:
  SystemTime expiration_time_;
  std::string credential_uri_;
  absl::optional<std::string> authorization_token_;

  bool needsRefresh() override;
  void refresh() override;
};

class CredentialsProviderChain : public CredentialsProvider,
                                 public Logger::Loggable<Logger::Id::aws> {
public:
  virtual ~CredentialsProviderChain() = default;

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

  virtual MetadataFetcherPtr createMetadataFetcher() const PURE;

  virtual CredentialsProviderSharedPtr createEnvironmentCredentialsProvider() const PURE;

  virtual CredentialsProviderSharedPtr createTaskRoleCredentialsProvider(
      Api::Api& api, Event::TimeSystem& time_system, MetadataFetcherPtr&& fetcher,
      const std::string& credential_uri,
      const absl::optional<std::string>& authorization_token) const PURE;

  virtual CredentialsProviderSharedPtr
  createInstanceProfileCredentialsProvider(Api::Api& api, Event::TimeSystem& time_system,
                                           MetadataFetcherPtr&& fetcher) const PURE;
};

class DefaultCredentialsProviderChain : public CredentialsProviderChain,
                                        public CredentialsProviderChainFactories {
public:
  DefaultCredentialsProviderChain(Api::Api& api, Event::TimeSystem& time_system)
      : DefaultCredentialsProviderChain(api, time_system, *this) {}

  DefaultCredentialsProviderChain(Api::Api& api, Event::TimeSystem& time_system,
                                  const CredentialsProviderChainFactories& factories);

private:
  virtual MetadataFetcherPtr createMetadataFetcher() const override;

  CredentialsProviderSharedPtr createEnvironmentCredentialsProvider() const override {
    return std::make_shared<EnvironmentCredentialsProvider>();
  }

  CredentialsProviderSharedPtr createTaskRoleCredentialsProvider(
      Api::Api& api, Event::TimeSystem& time_system, MetadataFetcherPtr&& fetcher,
      const std::string& credential_uri,
      const absl::optional<std::string>& authorization_token) const override {
    return std::make_shared<TaskRoleCredentialsProvider>(api, time_system, std::move(fetcher),
                                                         credential_uri, authorization_token);
  }

  CredentialsProviderSharedPtr
  createInstanceProfileCredentialsProvider(Api::Api& api, Event::TimeSystem& time_system,
                                           MetadataFetcherPtr&& fetcher) const override {
    return std::make_shared<InstanceProfileCredentialsProvider>(api, time_system,
                                                                std::move(fetcher));
  }
};

} // namespace Auth
} // namespace Aws
} // namespace Envoy