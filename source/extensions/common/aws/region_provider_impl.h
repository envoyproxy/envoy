#pragma once

#include "common/common/logger.h"

#include "extensions/common/aws/region_provider.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

/**
 * Retrieve AWS region name from the environment
 */
class EnvironmentRegionProvider : public RegionProvider, public Logger::Loggable<Logger::Id::aws> {
public:
  absl::optional<std::string> getRegion() override;
};

/**
 * Return statically configured AWS region name
 */
class StaticRegionProvider : public RegionProvider {
public:
  StaticRegionProvider(const std::string& region);

  absl::optional<std::string> getRegion() override;

private:
  const std::string region_;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
