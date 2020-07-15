#include "test/server/config_validation/xds_verifier.h"

#include "common/common/logger.h"

namespace Envoy {

XdsVerifier::XdsVerifier()
    : num_warming_(0), num_active_(0), num_draining_(0), num_added_(0), num_modified_(0),
      num_removed_(0) {}

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
 * @return true iff the route listener refers to is in routes_
 */
bool XdsVerifier::hasRoute(const envoy::config::listener::v3::Listener& listener) {
  return routes_.contains(getRoute(listener));
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
        if (hasRoute(listener)) {
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

  if (hasRoute(listener)) {
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
 * update a route
 * might be redundant, not sure if its possible to update a route
 */
void XdsVerifier::routeUpdated(envoy::config::route::v3::RouteConfiguration route) {
  ENVOY_LOG_MISC(info, "Tried to update {}", route.name());
  return;
}

/**
 * add a new route and update any listeners that refer to this route
 */
void XdsVerifier::routeAdded(envoy::config::route::v3::RouteConfiguration route) {
  routes_.insert({route.name(), route});
  /* bool added = false; */
  for (auto& rep : listeners_) {
    if (getRoute(rep.listener) == route.name() && rep.state == WARMING) {
      // it should successfully warm now
      /* if (!added) { */
      /*   routes_.insert({route.name(), route}); */
      /*   added = true; */
      /* } */
      ENVOY_LOG_MISC(info, "Moving {} to ACTIVE state", rep.listener.name());

      // if there were any active listeners that were waiting to be updated, they will now be
      // removed and the warming listener will take their place
      for (unsigned long i = 0; i < listeners_.size(); ++i) {
        auto& old_listener = listeners_[i];
        if (old_listener.listener.name() == rep.listener.name() &&
            getRoute(old_listener.listener) != route.name() && old_listener.state == ACTIVE) {
          // mark it as removed to remove it after the loop so as not to invalidate the first
          // iterator
          old_listener.state = REMOVED;
          num_active_--;
          num_modified_++;
        }
      }
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
