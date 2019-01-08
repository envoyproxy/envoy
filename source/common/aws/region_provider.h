#pragma once

#include "envoy/common/pure.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Aws {
namespace Auth {

class RegionProvider {
public:
  virtual ~RegionProvider() = default;

  virtual absl::optional<std::string> getRegion() PURE;
};

class StaticRegionProvider : public RegionProvider {
public:
  StaticRegionProvider(const std::string& region) : region_(region) {}

  absl::optional<std::string> getRegion() override { return absl::optional<std::string>(region_); }

private:
  const std::string region_;
};

typedef std::shared_ptr<RegionProvider> RegionProviderSharedPtr;

} // namespace Auth
} // namespace Aws
} // namespace Envoy