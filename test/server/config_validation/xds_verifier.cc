#include "test/server/config_validation/xds_verifier.h"

#include "common/common/logger.h"

namespace Envoy {

XdsVerifier::XdsVerifier() : num_warming_(0), num_active_(0), num_draining_(0) {}

/**
 * get the route referenced by a listener
 */
std::string XdsVerifier::getRoute(envoy::config::listener::v3::Listener listener) {
  envoy::config::listener::v3::Filter filter0 = listener.filter_chains()[0].filters()[0];
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager conn_man;
  filter0.typed_config().UnpackTo(&conn_man);
  return conn_man.rds().route_config_name();
}

/**
 * update a listener when its route is changed, draining/removing the old
 * listener and adding the updated listener
 */
void XdsVerifier::listenerUpdated(envoy::config::listener::v3::Listener listener) {
  // a listener in listeners_ needs to drain or be removed first
  auto it = listeners_.begin();
  while (it != listeners_.end()) {
    if (it->listener.name() == listener.name()) {
      if (it->state == ACTIVE) {
        ENVOY_LOG_MISC(info, "Moving {} to DRAINING state", listener.name());
        num_active_--;
        num_draining_++;
        it->state = ListenerState::DRAINING;
        ++it;
      } else if (it->state == WARMING) {
        ENVOY_LOG_MISC(info, "Removed warming listener {}", listener.name());
        num_warming_--;
        it = listeners_.erase(it);
      } else {
        ++it;
      }
    }
  }
  listenerAdded(listener);
}

/**
 * add a new listener to listeners_ in either an active or warming state
 */
void XdsVerifier::listenerAdded(envoy::config::listener::v3::Listener listener) {
  for (auto& route : routes_) {
    if (getRoute(listener) == route.name()) {
      ENVOY_LOG_MISC(info, "Adding {} to listeners_ as ACTIVE", listener.name());
      listeners_.push_back({listener, ACTIVE});
      num_active_++;
      return;
    }
  }

  num_warming_++;
  ENVOY_LOG_MISC(info, "Adding {} to listeners_ as WARMING", listener.name());
  listeners_.push_back({listener, ListenerState::WARMING});
}

/**
 * remove a listener and drain it if it was active
 */
void XdsVerifier::listenerRemoved(std::string& name) {
  auto it = listeners_.begin();
  while (it != listeners_.end()) {
    if (it->listener.name() == name) {
      if (it->state == ACTIVE) {
        ENVOY_LOG_MISC(info, "Changing {} to DRAINING", name);
        num_active_--;
        num_draining_++;
        it->state = ListenerState::DRAINING;
        ++it;
      } else if (it->state == WARMING) {
        ENVOY_LOG_MISC(info, "Removed warming listener {}", name);
        num_warming_--;
        it = listeners_.erase(it);
      } else {
        ++it;
      }
    }
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
  routes_.push_back(route);

  for (auto& listener_rep : listeners_) {
    if (getRoute(listener_rep.listener) == route.name()) {
      if (listener_rep.state == WARMING) {
        // it should successfully warm now
        ENVOY_LOG_MISC(info, "Moving {} to ACTIVE state", listener_rep.listener.name());
        num_warming_--;
        num_active_++;
        listener_rep.state = ListenerState::ACTIVE;
      }
    }
  }
}

/**
 * try to remove a route
 * it might not be possible to remove a route, check this again
 */
void XdsVerifier::routeRemoved(std::string& name) {
  ENVOY_LOG_MISC(info, "Tried to remove {}", name);
  return;
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
