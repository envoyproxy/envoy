#include "test/server/config_validation/xds_verifier.h"

#include "envoy/common/exception.h"

#include "source/common/common/logger.h"

namespace Envoy {

XdsVerifier::XdsVerifier(test::server::config_validation::Config::SotwOrDelta sotw_or_delta) {
  if (sotw_or_delta == test::server::config_validation::Config::SOTW) {
    sotw_or_delta_ = SOTW;
  } else {
    sotw_or_delta_ = DELTA;
  }
  ENVOY_LOG_MISC(debug, "sotw_or_delta_ = {}", sotw_or_delta_);
}

/**
 * Get the route referenced by a listener.
 */
std::string XdsVerifier::getRoute(const envoy::config::listener::v3::Listener& listener) {
  envoy::config::listener::v3::Filter filter0 = listener.filter_chains()[0].filters()[0];
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager conn_man;
  filter0.typed_config().UnpackTo(&conn_man);
  return conn_man.rds().route_config_name();
}

/**
 * @return true iff the route listener refers to is in all_routes_
 */
bool XdsVerifier::hasRoute(const envoy::config::listener::v3::Listener& listener) {
  return hasRoute(getRoute(listener));
}

bool XdsVerifier::hasRoute(const std::string& name) { return all_routes_.contains(name); }

bool XdsVerifier::hasActiveRoute(const envoy::config::listener::v3::Listener& listener) {
  return hasActiveRoute(getRoute(listener));
}

bool XdsVerifier::hasActiveRoute(const std::string& name) { return active_routes_.contains(name); }

bool XdsVerifier::hasListener(const std::string& name, ListenerState state) {
  return std::any_of(listeners_.begin(), listeners_.end(), [&](const auto& rep) {
    return rep.listener.name() == name && state == rep.state;
  });
}

/**
 * Pretty prints the currently stored listeners and their states.
 */
void XdsVerifier::dumpState() {
  ENVOY_LOG_MISC(debug, "Listener Dump:");
  for (const auto& rep : listeners_) {
    ENVOY_LOG_MISC(debug, "Name: {}, Route {}, State: {}", rep.listener.name(),
                   getRoute(rep.listener), rep.state);
  }
}

/*
 * If a listener is added for the first time, it will be added as active/warming depending on if
 * Envoy knows about its route config.
 *
 * If a listener is updated (i.e. there is a already a listener by this name), there are 3 cases:
 * 1. The old listener is active and the new is warming:
 *    - Old will remain active
 *    - New will be added as warming, to replace the old when it gets its route
 * 2. The old listener is active and new is active:
 *    - Old is drained (seemingly instantaneously)
 *    - New is added as active
 * 3. The old listener is warming and new is active/warming:
 *    - Old is completely removed
 *    - New is added as warming/active as normal
 */

/**
 * Update a listener when its route is changed, draining/removing the old listener and adding the
 * updated listener.
 */
void XdsVerifier::listenerUpdated(const envoy::config::listener::v3::Listener& listener) {
  ENVOY_LOG_MISC(debug, "About to update listener {} to {}", listener.name(), getRoute(listener));
  dumpState();

  const auto existing_warming =
      std::find_if(listeners_.begin(), listeners_.end(), [&](const auto& rep) {
        return rep.listener.name() == listener.name() &&
               getRoute(rep.listener) == getRoute(listener) && rep.state == WARMING;
      });
  const auto existing_active =
      std::find_if(listeners_.begin(), listeners_.end(), [&](const auto& rep) {
        return rep.listener.name() == listener.name() &&
               getRoute(rep.listener) == getRoute(listener) && rep.state == ACTIVE;
      });

  // Return early if the listener is a duplicate.
  if (existing_active != listeners_.end()) {
    const auto warming_to_remove =
        std::find_if(listeners_.begin(), listeners_.end(), [&](const auto& rep) {
          return rep.listener.name() == listener.name() && rep.state == WARMING;
        });

    // The listener should be updated back to its original state and the warming removed.
    if (warming_to_remove != listeners_.end()) {
      ENVOY_LOG_MISC(debug, "Removing warming listener {} after update", listener.name());
      num_modified_++;
      num_warming_--;
      listeners_.erase(warming_to_remove);
      return;
    }
    ENVOY_LOG_MISC(debug, "Ignoring duplicate add of {}", listener.name());
    return;
  }

  if (existing_warming != listeners_.end()) {
    ENVOY_LOG_MISC(debug, "Ignoring duplicate add of {}", listener.name());
    return;
  }

  bool found = false;
  for (auto it = listeners_.begin(); it != listeners_.end();) {
    const auto& rep = *it;
    ENVOY_LOG_MISC(debug, "checking {} for update", rep.listener.name());
    if (rep.listener.name() == listener.name()) {
      // If we're updating a warming/active listener, num_modified_ must be incremented.
      if (rep.state != DRAINING && !found) {
        num_modified_++;
        found = true;
      }

      if (rep.state == ACTIVE) {
        if (hasActiveRoute(listener)) {
          // If the new listener is ready to take traffic, the old listener will be removed. It
          // seems to be directly removed without being added to the config dump as draining.
          ENVOY_LOG_MISC(debug, "Removing {} after update", listener.name());
          num_active_--;
          it = listeners_.erase(it);
          continue;
        } else {
          // If the new listener has not gotten its route yet, the old listener will remain active
          // until that happens.
          ENVOY_LOG_MISC(debug, "Keeping {} as ACTIVE", listener.name());
        }
      } else if (rep.state == WARMING) {
        // If the old listener is warming, it will be removed and replaced with the new.
        ENVOY_LOG_MISC(debug, "Removed warming listener {}", listener.name());
        num_warming_--;
        it = listeners_.erase(it);
        // Don't increment it.
        continue;
      }
    }
    ++it;
  }
  dumpState();
  listenerAdded(listener, true);
}

/**
 * Add a new listener to listeners_ in either an active or warming state.
 * @param listener the listener to be added
 * @param from_update whether this function was called from listenerUpdated, in which case
 * num_added_ should not be incremented
 */
void XdsVerifier::listenerAdded(const envoy::config::listener::v3::Listener& listener,
                                bool from_update) {
  if (!from_update) {
    num_added_++;
  }

  if (hasActiveRoute(listener)) {
    ENVOY_LOG_MISC(debug, "Adding {} to listeners_ as ACTIVE", listener.name());
    listeners_.push_back({listener, ACTIVE});
    num_active_++;
  } else {
    num_warming_++;
    ENVOY_LOG_MISC(debug, "Adding {} to listeners_ as WARMING", listener.name());
    listeners_.push_back({listener, WARMING});
  }

  ENVOY_LOG_MISC(debug, "listenerAdded({})", listener.name());
  dumpState();
}

/**
 * Remove a listener and drain it if it was active.
 * @param name the name of the listener to be removed
 */
void XdsVerifier::listenerRemoved(const std::string& name) {
  bool found = false;

  for (auto it = listeners_.begin(); it != listeners_.end();) {
    auto& rep = *it;
    if (rep.listener.name() == name) {
      if (rep.state == ACTIVE) {
        // The listener will be drained before being removed.
        ENVOY_LOG_MISC(debug, "Changing {} to DRAINING", name);
        found = true;
        num_active_--;
        num_draining_++;
        rep.state = DRAINING;
      } else if (rep.state == WARMING) {
        // The listener will be removed immediately.
        ENVOY_LOG_MISC(debug, "Removed warming listener {}", name);
        found = true;
        num_warming_--;
        it = listeners_.erase(it);
        // Don't increment it.
        continue;
      }
    }
    ++it;
  }

  if (found) {
    num_removed_++;
  }
}

/**
 * After a SOTW update, see if any listeners that are currently warming can become active.
 */
void XdsVerifier::updateSotwListeners() {
  ASSERT(sotw_or_delta_ == SOTW);
  for (auto& rep : listeners_) {
    // Check all_routes_, not active_routes_ since this is SOTW, so any inactive routes will become
    // active if this listener refers to them.
    if (hasRoute(rep.listener) && rep.state == WARMING) {
      // It should successfully warm now.
      ENVOY_LOG_MISC(debug, "Moving {} to ACTIVE state", rep.listener.name());

      // If the route was not originally added as active, change it now.
      if (!hasActiveRoute(rep.listener)) {
        std::string route_name = getRoute(rep.listener);
        auto it = all_routes_.find(route_name);
        // all added routes should be in all_routes_ in SOTW
        ASSERT(it != all_routes_.end());
        active_routes_.insert({route_name, it->second});
      }

      // If there were any active listeners that were waiting to be updated, they will now be
      // removed and the warming listener will take their place.
      markForRemoval(rep);
      num_warming_--;
      num_active_++;
      rep.state = ACTIVE;
    }
  }
  listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                  [&](auto& listener) { return listener.state == REMOVED; }),
                   listeners_.end());
}

/**
 * After a delta update, update any listeners that refer to the added route.
 */
void XdsVerifier::updateDeltaListeners(const envoy::config::route::v3::RouteConfiguration& route) {
  for (auto& rep : listeners_) {
    if (getRoute(rep.listener) == route.name() && rep.state == WARMING) {
      // It should successfully warm now.
      ENVOY_LOG_MISC(debug, "Moving {} to ACTIVE state", rep.listener.name());

      // If there were any active listeners that were waiting to be updated, they will now be
      // removed and the warming listener will take their place.
      markForRemoval(rep);
      num_warming_--;
      num_active_++;
      rep.state = ACTIVE;
    }
  }
  // erase any active listeners that were replaced
  listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                  [&](auto& listener) { return listener.state == REMOVED; }),
                   listeners_.end());
}

/**
 * @param listener a warming listener that has a corresponding active listener of the same name
 * called after listener receives its route, so it will be moved to active and the old listener will
 * be removed
 */
void XdsVerifier::markForRemoval(ListenerRepresentation& rep) {
  ASSERT(rep.state == WARMING);
  // Find the old listener and mark it for removal.
  for (auto& old_rep : listeners_) {
    if (old_rep.listener.name() == rep.listener.name() &&
        getRoute(old_rep.listener) != getRoute(rep.listener) && old_rep.state == ACTIVE) {
      // Mark it as removed to remove it after the loop so as not to invalidate the iterator in
      // the caller function.
      old_rep.state = REMOVED;
      num_active_--;
    }
  }
}

/**
 * Add a new route and update any listeners that refer to this route.
 */
void XdsVerifier::routeAdded(const envoy::config::route::v3::RouteConfiguration& route) {
  // Routes that are not referenced by any resource are ignored, so this creates a distinction
  // between SOTW and delta.
  // If an unreferenced route is sent in delta, it is ignored forever as it will not be sent in
  // future RDS updates, whereas in SOTW it will be present in all future RDS updates, so if a
  // listener that refers to it is added in the meantime, it will become active.
  if (!hasRoute(route.name())) {
    all_routes_.insert({route.name(), route});
  }

  if (sotw_or_delta_ == DELTA && std::any_of(listeners_.begin(), listeners_.end(), [&](auto& rep) {
        return getRoute(rep.listener) == route.name();
      })) {
    if (!hasActiveRoute(route.name())) {
      active_routes_.insert({route.name(), route});
      updateDeltaListeners(route);
    }
    updateDeltaListeners(route);
  } else if (sotw_or_delta_ == SOTW) {
    updateSotwListeners();
  }
}

/**
 * Called after draining a listener, will remove it from listeners_.
 */
void XdsVerifier::drainedListener(const std::string& name) {
  for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
    if (it->listener.name() == name && it->state == DRAINING) {
      ENVOY_LOG_MISC(debug, "Drained and removed {}", name);
      num_draining_--;
      listeners_.erase(it);
      return;
    }
  }
  throw EnvoyException(fmt::format("Tried to drain {} which is not draining", name));
}

} // namespace Envoy
