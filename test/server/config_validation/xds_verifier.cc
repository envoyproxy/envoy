#include "test/server/config_validation/xds_verifier.h"

#include "common/common/logger.h"

namespace Envoy {

XdsVerifier::XdsVerifier(test::server::config_validation::Config::SotwOrDelta sotw_or_delta)
    : num_warming_(0), num_active_(0), num_draining_(0), num_added_(0), num_modified_(0),
      num_removed_(0) {
  if (sotw_or_delta == test::server::config_validation::Config::SOTW) {
    sotw_or_delta_ = SOTW;
  } else {
    sotw_or_delta_ = DELTA;
  }
  ENVOY_LOG_MISC(info, "sotw_or_delta_ = {}", sotw_or_delta_);
}

/**
 * get the route referenced by a listener
 */
std::string XdsVerifier::getRoute(const envoy::config::listener::v3::Listener& listener) {
  envoy::config::listener::v3::Filter filter0 = listener.filter_chains()[0].filters()[0];
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager conn_man;
  filter0.typed_config().UnpackTo(&conn_man);
  return conn_man.rds().route_config_name();
}

/**
 * @return true iff the route listener refers to is in all_routes_
 */
bool XdsVerifier::hasRoute(const envoy::config::listener::v3::Listener& listener) {
  return all_routes_.contains(getRoute(listener));
}

bool XdsVerifier::hasActiveRoute(const envoy::config::listener::v3::Listener& listener) {
  return active_routes_.contains(getRoute(listener));
}

/**
 * prints the currently stored listeners and their states
 */
void XdsVerifier::dumpState() {
  ENVOY_LOG_MISC(info, "Listener Dump:");
  for (auto rep : listeners_) {
    ENVOY_LOG_MISC(info, "Name: {}, Route {}, State: {}", rep.listener.name(),
                   getRoute(rep.listener), rep.state);
  }
}

/*
 * if you add a new listener that is not being updated, there are two cases:
 * 1. the listener is draining
 *    - if the routes match, this route will be added back to active
 *    - if the routes don't match, the listener will be added as active/warming as relevant
 * 2. the listener has not been added before
 *    - the listener will be added as active/warming as relevant
 *
 * if a listener is being updated, there are 3 cases:
 * 1. the old listener is active and the new is warming:
 *    - the old listener will remain active and the new will be added as warming
 * 2. the old listener is active and new is active:
 *    - the old listener is moved to draining and the new is added as active
 *    - listener_manager.listener_modified++
 * 3. the old listener is warming and new is active/warming:
 *    - old is completely removed
 *    - normal listenerAdded behaviour after
 */

/**
 * update a listener when its route is changed, draining/removing the old listener and adding the
 * updated listener
 */
void XdsVerifier::listenerUpdated(envoy::config::listener::v3::Listener listener) {
  ENVOY_LOG_MISC(info, "About to update listener {}", listener.name());
  dumpState();

  if (std::any_of(listeners_.begin(), listeners_.end(), [&](auto& rep) {
        return rep.listener.name() == listener.name() &&
               getRoute(listener) == getRoute(rep.listener) && rep.state != DRAINING;
      })) {
    ENVOY_LOG_MISC(info, "Ignoring duplicate add of {}", listener.name());
    return;
  }

  for (unsigned long i = 0; i < listeners_.size(); ++i) {
    auto& rep = listeners_[i];
    if (rep.listener.name() == listener.name()) {
      if (rep.state == ACTIVE) {
        if (hasActiveRoute(listener)) {
          // if the new listener is ready to take traffic, the old listener will be removed
          // it seems to be directly removed without being added to the config dump as draining
          ENVOY_LOG_MISC(info, "Removing {} after update", listener.name());
          num_modified_++;
          num_active_--;
          /* num_draining_++; */
          /* rep.state = DRAINING; */
          listeners_.erase(listeners_.begin() + i);
        } else {
          // if the new listener has not gotten its route yet, the old listener will remain active
          // until that happens
          ENVOY_LOG_MISC(info, "Keeping {} as ACTIVE", listener.name());
        }
      } else if (rep.state == WARMING) {
        // if the old listener is warming, it will be removed and replaced with the new
        ENVOY_LOG_MISC(info, "Removed warming listener {}", listener.name());
        num_warming_--;
        listeners_.erase(listeners_.begin() + i);
      }
    }
  }
  dumpState();
  listenerAdded(listener, true);
}

/**
 * add a new listener to listeners_ in either an active or warming state
 * @param listener the listener to be added
 * @param from_update whether this was called from the listenerUpdated function, in which case
 * num_added_ should not be incremented
 */
void XdsVerifier::listenerAdded(envoy::config::listener::v3::Listener listener, bool from_update) {
  // if the same listener being added is already draining, it will be moved back to active
  //
  for (auto& rep : listeners_) {
    if (rep.listener.name() == listener.name() && getRoute(rep.listener) == getRoute(listener) &&
        rep.state == DRAINING) {
      num_draining_--;
      ENVOY_LOG_MISC(info, "Changing {} from DRAINING back to ACTIVE", listener.name());
      rep.state = ACTIVE;
      num_active_++;
      return;
    }
  }

  if (!from_update) {
    // don't want to increment this as it was already incremented when old listener was added
    num_added_++;
  }

  if (hasActiveRoute(listener)) {
    ENVOY_LOG_MISC(info, "Adding {} to listeners_ as ACTIVE", listener.name());
    listeners_.push_back({listener, ACTIVE});
    num_active_++;
  } else {
    num_warming_++;
    ENVOY_LOG_MISC(info, "Adding {} to listeners_ as WARMING", listener.name());
    listeners_.push_back({listener, WARMING});
  }

  ENVOY_LOG_MISC(info, "listenerAdded({})", listener.name());
  dumpState();
}

/**
 * remove a listener and drain it if it was active
 * @param name the name of the listener to be removed
 */
void XdsVerifier::listenerRemoved(const std::string& name) {
  bool found = false;
  for (unsigned long i = 0; i < listeners_.size(); ++i) {
    auto& rep = listeners_[i];
    if (rep.state == ACTIVE) {
      // the listener will be drained before being removed
      ENVOY_LOG_MISC(info, "Changing {} to DRAINING", name);
      found = true;
      num_active_--;
      num_draining_++;
      rep.state = DRAINING;
    } else if (rep.state == WARMING) {
      // the listener will be removed immediately
      ENVOY_LOG_MISC(info, "Removed warming listener {}", name);
      found = true;
      listeners_.erase(listeners_.begin() + i);
      num_warming_--;
    }
  }
  if (found) {
    num_removed_++;
  }
}

/**
 * after a SOTW update, see if any listeners that are currently warming can become active
 */
void XdsVerifier::updateSotwListeners() {
  ASSERT(sotw_or_delta_ == SOTW);
  for (auto& rep : listeners_) {
    // check all_routes_, not active_routes_ since this is SOTW, so any inactive routes will become
    // active if this listener refers to them
    if (hasRoute(rep.listener) && rep.state == WARMING) {
      // it should successfully warm now
      ENVOY_LOG_MISC(info, "Moving {} to ACTIVE state", rep.listener.name());

      // if the route was not originally added as active, change it now
      if (!hasActiveRoute(rep.listener)) {
        std::string route_name = getRoute(rep.listener);
        auto it = all_routes_.find(route_name);
        // all added routes should be in all_routes_ in SOTW
        ASSERT(it != all_routes_.end());
        active_routes_.insert({route_name, it->second});
      }

      // if there were any active listeners that were waiting to be updated, they will now be
      // removed and the warming listener will take their place
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
 * @param listener a warming listener that has a corresponding active listener of the same name
 * called after listener receives its route, so it will be moved to active and the old listener will
 * be removed
 */
void XdsVerifier::markForRemoval(ListenerRepresentation& rep) {
  ASSERT(rep.state == WARMING);
  // find the old listener and mark it for removal
  for (unsigned long i = 0; i < listeners_.size(); ++i) {
    auto& old_listener = listeners_[i];
    if (old_listener.listener.name() == rep.listener.name() &&
        getRoute(old_listener.listener) != getRoute(rep.listener) &&
        old_listener.state == ACTIVE) {
      // mark it as removed to remove it after the loop so as not to invalidate the iterator in
      // the caller function
      old_listener.state = REMOVED;
      num_active_--;
      num_modified_++;
    }
  }

}

/**
 * called when a route that was previously added is re-added
 * the original route might have been ignored if no resources refer to it, so we can add it here
 */
void XdsVerifier::routeUpdated(envoy::config::route::v3::RouteConfiguration route) {
  if (!all_routes_.contains(route.name()) &&
      std::any_of(listeners_.begin(), listeners_.end(),
                  [&](auto& rep) { return getRoute(rep.listener) == route.name(); })) {
    all_routes_.insert({route.name(), route});
    active_routes_.insert({route.name(), route});
  }
  if (sotw_or_delta_ == DELTA) {
    // sending a new RDS update with the same route name that was sent before and ignored in delta
    // does not seem to apply updates to any listeners
    // TODO(samflattery): investigate this
    ENVOY_LOG_MISC(info, "Tried to update {}", route.name());
  } else {
    // more routes might have been sent and updates not applied to listeners, so update them
    ENVOY_LOG_MISC(info, "Updating {}", route.name());
    updateSotwListeners();
  }
}

/**
 * add a new route and update any listeners that refer to this route
 */
void XdsVerifier::routeAdded(envoy::config::route::v3::RouteConfiguration route) {
  // routes that are not referenced by any resource are ignored, so this creates a distinction
  // between SOTW and delta
  // if an unreferenced route is sent in delta, it is ignored forever as it will not be sent in
  // future RDS updates, whereas in SOTW it will be present in all future RDS updates, so if a
  // listener that refers to it is added in the meantime, it will become active

  // in delta, active_routes_ and all_routes_ should be the same as we only send one route at a
  // time, so it either becomes active or not
  if (sotw_or_delta_ == DELTA && std::any_of(listeners_.begin(), listeners_.end(), [&](auto& rep) {
        return getRoute(rep.listener) == route.name();
      })) {
    active_routes_.insert({route.name(), route});
    all_routes_.insert({route.name(), route});
  } else if (sotw_or_delta_ == SOTW) {
    all_routes_.insert({route.name(), route});
  }

  // find any listeners that reference this route and update them
  if (sotw_or_delta_ == DELTA) {
    for (auto& rep : listeners_) {
      if (getRoute(rep.listener) == route.name() && rep.state == WARMING) {
        // it should successfully warm now
        ENVOY_LOG_MISC(info, "Moving {} to ACTIVE state", rep.listener.name());

        // if there were any active listeners that were waiting to be updated, they will now be
        // removed and the warming listener will take their place
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
  } else {
    updateSotwListeners();
  }
}

/**
 * called after draining a listener, will remove it from listeners_
 */
void XdsVerifier::drainedListener(const std::string& name) {
  for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
    if (it->listener.name() == name && it->state == DRAINING) {
      ENVOY_LOG_MISC(info, "Drained and removed {}", name);
      num_draining_--;
      listeners_.erase(it);
      return;
    }
  }
  throw EnvoyException(fmt::format("Tried to drain {} which is not draining", name));
}

} // namespace Envoy
