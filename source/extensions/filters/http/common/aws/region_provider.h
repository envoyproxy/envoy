#pragma once

#include "envoy/common/pure.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace Aws {

class RegionProvider {
public:
  virtual ~RegionProvider() = default;

  virtual absl::optional<std::string> getRegion() PURE;
};

typedef std::shared_ptr<RegionProvider> RegionProviderSharedPtr;

} // namespace Aws
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy