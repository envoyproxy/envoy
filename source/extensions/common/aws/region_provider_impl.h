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
  EnvironmentRegionProvider();

  absl::optional<std::string> getRegion() override;
};

/**
 * Return statically configured AWS region name
 */
class EnvoyConfigRegionProvider : public RegionProvider, public Logger::Loggable<Logger::Id::aws>  {
public:
  EnvoyConfigRegionProvider();

  absl::optional<std::string> getRegion() override;

private:
  const std::string region_;
};

class AWSProfileRegionProvider : public RegionProvider, public Logger::Loggable<Logger::Id::aws>  {
public:
  AWSProfileRegionProvider();

  absl::optional<std::string> getRegion() override;

private:
  const std::string region_;
};

class AWSConfigRegionProvider : public RegionProvider, public Logger::Loggable<Logger::Id::aws>  {
public:
  AWSConfigRegionProvider();

  absl::optional<std::string> getRegion() override;

private:
  const std::string region_;
};

class RegionProviderChainFactories {
public:
  virtual ~RegionProviderChainFactories() = default;

  virtual RegionProviderSharedPtr createEnvironmentRegionProvider() const PURE;
  virtual RegionProviderSharedPtr createEnvoyConfigRegionProvider() const PURE;
  virtual RegionProviderSharedPtr createAWSProfileRegionProvider() const PURE;
  virtual RegionProviderSharedPtr createAWSConfigRegionProvider() const PURE;
};


/**
 * AWS region provider chain, supporting environment, envoy configuration, AWS config and AWS profile.
 */
class RegionProviderChain : public RegionProvider, public RegionProviderChainFactories,
                                 public Logger::Loggable<Logger::Id::aws> {
public:
RegionProviderChain(
      const RegionProviderChainFactories& factories);

RegionProviderChain()
      : RegionProviderChain(*this) {}

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
  RegionProviderSharedPtr createAWSProfileRegionProvider() const override {
    return std::make_shared<AWSProfileRegionProvider>();
  }
  RegionProviderSharedPtr createAWSConfigRegionProvider() const override {
    return std::make_shared<AWSConfigRegionProvider>();
  }

protected:
  std::list<RegionProviderSharedPtr> providers_;
};

using EnvoyConfigRegionProviderPtr = std::shared_ptr<EnvoyConfigRegionProvider>;
using EnvironmentRegionProviderPtr = std::shared_ptr<EnvironmentRegionProvider>;
using AWSProfileRegionProviderPtr = std::shared_ptr<AWSProfileRegionProvider>;
using AWSConfigRegionProviderPtr = std::shared_ptr<AWSConfigRegionProvider>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
