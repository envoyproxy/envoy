#pragma once

#include <memory>

#include "envoy/api/v2/rds.pb.h"

namespace Envoy {
namespace Router {

struct LastConfigInfo {
  uint64_t last_config_hash_;
  std::string last_config_version_;
};

class RouteConfigUpdateInfo {
public:
  virtual ~RouteConfigUpdateInfo() = default;

  virtual absl::optional<LastConfigInfo> configInfo() const PURE;
  virtual envoy::api::v2::RouteConfiguration& routeConfiguration() PURE;
  virtual SystemTime lastUpdated() const PURE;
};

} // namespace Router
} // namespace Envoy
