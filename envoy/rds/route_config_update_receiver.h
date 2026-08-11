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
   * @param version supplies RouteConfiguration version.
   * @return a failure status if the update couldn't be applied. An update whose configuration is
   * unchanged is applied as a no-op, which leaves an update that is still warming up alone; use
   * configWarming() to tell whether anything is warming up.
   * @throw EnvoyException if the new config is invalid and can't be applied.
   */
  virtual absl::Status onRdsUpdate(const Protobuf::Message& rc, const std::string& version) PURE;

  /**
   * Called when the subscription stops waiting for a RouteConfiguration and signals that whatever
   * warms up with it may proceed, without any configuration having been published - because the
   * initial fetch timed out, because the update was rejected, or because the resource list was
   * empty.
   *
   * This matters because the owning init manager is no longer waiting afterwards: a later update
   * must not hold its configuration back in order to warm resources up, since nothing is warming
   * on it any more and doing so would only starve a listener that is already serving.
   */
  virtual void onRdsFailure() PURE;

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
