#pragma once

#include <memory>
#include <optional>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/init/manager.h"
#include "envoy/rds/route_config_provider.h"

namespace Envoy {
namespace Rds {

/**
 * An observer of updates to a RouteConfiguration. This is used when the Receiver completes
 * initialization and it's ready to expose the new configuration to the rest of the system.
 */
class RouteConfigUpdateObserver {
public:
  virtual ~RouteConfigUpdateObserver() = default;

  /**
   * Called when the RouteConfiguration has been updated and the new configuration is ready to be
   * exposed to the rest of the system.
   */
  virtual void onConfigWarmed() PURE;
};

/**
 * A primitive that keeps track of updates to a RouteConfiguration.
 */
class RouteConfigUpdateReceiver {
public:
  virtual ~RouteConfigUpdateReceiver() = default;

  /**
   * Called on updates via RDS.
   * The new RouteConfiguration isn't handed to the observer right away: the resources that it owns
   * are warmed up first, and only then is the observer notified. Note that the observer may be
   * notified before this method returns, i.e. synchronously, if there is nothing to warm up.
   * @param rc supplies the RouteConfiguration.
   * @param version_info supplies RouteConfiguration version.
   * @return bool whether the hash of the new config has been different than
   * the hash of the current one and RouteConfiguration has been updated.
   * @throw EnvoyException if the new config is invalid and can't be applied.
   */
  virtual bool onRdsUpdate(const Protobuf::Message& rc, const std::string& version_info) PURE;

  /**
   * Sets the observer of updates to the RouteConfiguration.
   * @param observer supplies the observer. This should have a lifetime that is at least as long as
   * the lifetime of this receiver.
   */
  virtual void setObserver(RouteConfigUpdateObserver& observer) PURE;

  /**
   * @return bool whether the RouteConfiguration built by a previous RDS update is still warming up,
   * i.e. whether the observer hasn't been notified about it yet. An update that turns out to be a
   * no-op leaves such an update alone, so this may be true even if the last onRdsUpdate() call
   * returned false.
   */
  virtual bool configWarming() const PURE;

  /**
   * @return uint64_t the hash value of RouteConfiguration.
   */
  virtual uint64_t configHash() const PURE;

  /**
   * @return std::optional<RouteConfigProvider::ConfigInfo> containing an instance of
   * RouteConfigProvider::ConfigInfo if RouteConfiguration has been updated at least once. Otherwise
   * returns an empty std::optional<RouteConfigProvider::ConfigInfo>.
   */
  virtual const std::optional<RouteConfigProvider::ConfigInfo>& configInfo() const PURE;

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
