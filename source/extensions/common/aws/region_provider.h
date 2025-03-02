#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

/**
 * Interface for classes capable of discovering the AWS region from the execution environment.
 */
class RegionProvider {
public:
  virtual ~RegionProvider() = default;

  /**
   * Discover and return the AWS region.
   * @return AWS region, or nullopt if unable to discover the region.
   */
  virtual absl::optional<std::string> getRegion() PURE;

  /**
   * Discover and return the AWS region set string.
   * @return AWS region, or nullopt if unable to discover the region set.
   */
  virtual absl::optional<std::string> getRegionSet() PURE;
};

using RegionProviderPtr = std::unique_ptr<RegionProvider>;
using RegionProviderSharedPtr = std::shared_ptr<RegionProvider>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
