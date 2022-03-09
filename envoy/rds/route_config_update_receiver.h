#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/rds/route_config_provider.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Rds {

/**
 * A primitive that keeps track of updates to a RouteConfiguration.
 */
class RouteConfigUpdateReceiver {
public:
  virtual ~RouteConfigUpdateReceiver() = default;

  /**
   * Called on updates via RDS.
   * @param rc supplies the RouteConfiguration.
   * @param version_info supplies RouteConfiguration version.
   * @return bool whether the hash of the new config has been different than
   * the hash of the current one and RouteConfiguration has been updated.
   * @throw EnvoyException if the new config is invalid and can't be applied.
   */
  virtual bool onRdsUpdate(const Protobuf::Message& rc, const std::string& version_info) PURE;

  /**
   * @return uint64_t the hash value of RouteConfiguration.
   */
  virtual uint64_t configHash() const PURE;

  /**
   * @return absl::optional<RouteConfigProvider::ConfigInfo> containing an instance of
   * RouteConfigProvider::ConfigInfo if RouteConfiguration has been updated at least once. Otherwise
   * returns an empty absl::optional<RouteConfigProvider::ConfigInfo>.
   */
  virtual const absl::optional<RouteConfigProvider::ConfigInfo>& configInfo() const PURE;

  /**
   * @return Protobuf::Message& current RouteConfiguration.
   */
  virtual const Protobuf::Message& protobufConfiguration() const PURE;

  /**
   * @return ConfigConstSharedPtr a parsed and validated copy of current RouteConfiguration.
   * @see protobufConfiguration()
   */
  virtual ConfigConstSharedPtr parsedConfiguration() const PURE;

  /**
   * @return SystemTime the time of the last update.
   */
  virtual SystemTime lastUpdated() const PURE;
};

using RouteConfigUpdatePtr = std::unique_ptr<RouteConfigUpdateReceiver>;

} // namespace Rds
} // namespace Envoy
