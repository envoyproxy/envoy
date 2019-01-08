#pragma once

#include "common/aws/region_provider.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Aws {
namespace Auth {

class EnvironmentRegionProvider : public RegionProvider, public Logger::Loggable<Logger::Id::aws> {
public:
  absl::optional<std::string> getRegion() override;
};

} // namespace Auth
} // namespace Aws
} // namespace Envoy