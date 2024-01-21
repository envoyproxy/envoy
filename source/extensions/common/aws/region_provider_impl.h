#pragma once

#include "source/common/common/logger.h"
#include "source/extensions/common/aws/region_provider.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

/**
 * Retrieve AWS region name from the environment
 */
class EnvironmentRegionProvider : public RegionProvider, public Logger::Loggable<Logger::Id::aws> {
public:
  EnvironmentRegionProvider() = default;

  absl::optional<std::string> getRegion() override;
};

/**
 * Return statically configured AWS region name
 */
class EnvoyConfigRegionProvider : public RegionProvider, public Logger::Loggable<Logger::Id::aws> {
public:
  EnvoyConfigRegionProvider() = default;

  absl::optional<std::string> getRegion() override;

private:
  const std::string region_;
};

class AWSCredentialsFileRegionProvider : public RegionProvider,
                                         public Logger::Loggable<Logger::Id::aws> {
public:
  AWSCredentialsFileRegionProvider() = default;

  absl::optional<std::string> getRegion() override;

private:
  const std::string region_;
};

class AWSConfigFileRegionProvider : public RegionProvider,
                                    public Logger::Loggable<Logger::Id::aws> {
public:
  AWSConfigFileRegionProvider() = default;

  absl::optional<std::string> getRegion() override;

private:
  const std::string region_;
};

class RegionProviderChainFactories {
public:
  virtual ~RegionProviderChainFactories() = default;

  virtual RegionProviderSharedPtr createEnvironmentRegionProvider() const PURE;
  virtual RegionProviderSharedPtr createEnvoyConfigRegionProvider() const PURE;
  virtual RegionProviderSharedPtr createAWSCredentialsFileRegionProvider() const PURE;
  virtual RegionProviderSharedPtr createAWSConfigFileRegionProvider() const PURE;
};

/**
 * AWS region provider chain, supporting environment, envoy configuration, AWS config and AWS
 * profile.
 */
class RegionProviderChain : public RegionProvider,
                            public RegionProviderChainFactories,
                            public Logger::Loggable<Logger::Id::aws> {
public:
  RegionProviderChain(const RegionProviderChainFactories& factories);

  RegionProviderChain() : RegionProviderChain(*this) {}

  ~RegionProviderChain() override = default;

  void add(const RegionProviderSharedPtr& credentials_provider) {
    providers_.emplace_back(credentials_provider);
  }

  absl::optional<std::string> getRegion() override;

  RegionProviderSharedPtr createEnvoyConfigRegionProvider() const override {
    return std::make_shared<EnvoyConfigRegionProvider>();
  }
  RegionProviderSharedPtr createEnvironmentRegionProvider() const override {
    return std::make_shared<EnvironmentRegionProvider>();
  }
  RegionProviderSharedPtr createAWSCredentialsFileRegionProvider() const override {
    return std::make_shared<AWSCredentialsFileRegionProvider>();
  }
  RegionProviderSharedPtr createAWSConfigFileRegionProvider() const override {
    return std::make_shared<AWSConfigFileRegionProvider>();
  }

protected:
  std::list<RegionProviderSharedPtr> providers_;
};

using EnvoyConfigRegionProviderPtr = std::shared_ptr<EnvoyConfigRegionProvider>;
using EnvironmentRegionProviderPtr = std::shared_ptr<EnvironmentRegionProvider>;
using AWSCredentialsFileRegionProviderPtr = std::shared_ptr<AWSCredentialsFileRegionProvider>;
using AWSConfigFileRegionProviderPtr = std::shared_ptr<AWSConfigFileRegionProvider>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
