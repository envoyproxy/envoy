#include "test/config/utility.h"
#include "test/server/config_validation/xds_verifier.h"

#include "gtest/gtest.h"

namespace Envoy {

envoy::config::listener::v3::Listener buildListener(const std::string& listener_name,
                                                    const std::string& route_name) {
  return ConfigHelper::buildListener(listener_name, route_name, "", "ads_test");
}

envoy::config::route::v3::RouteConfiguration buildRoute(const std::string& route_name) {
  return ConfigHelper::buildRouteConfig(route_name, "cluster_0");
}

// Add, warm, drain and remove a listener.
TEST(XdsVerifier, Basic) {
  XdsVerifier verifier(test::server::config_validation::Config::SOTW);
  verifier.listenerAdded(buildListener("listener_0", "route_config_0"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.WARMING));
  EXPECT_EQ(verifier.numAdded(), 1);
  EXPECT_EQ(verifier.numWarming(), 1);

  verifier.routeAdded(buildRoute("route_config_0"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.ACTIVE));
  EXPECT_TRUE(verifier.hasRoute("route_config_0") && verifier.hasActiveRoute("route_config_0"));
  EXPECT_EQ(verifier.numAdded(), 1);
  EXPECT_EQ(verifier.numWarming(), 0);
  EXPECT_EQ(verifier.numActive(), 1);

  verifier.listenerRemoved("listener_0");
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.DRAINING));
  EXPECT_EQ(verifier.numDraining(), 1);
  EXPECT_EQ(verifier.numRemoved(), 1);
  EXPECT_EQ(verifier.numActive(), 0);

  verifier.drainedListener("listener_0");
  EXPECT_FALSE(verifier.hasListener("listener_0", verifier.DRAINING));
  EXPECT_EQ(verifier.numRemoved(), 1);
}

TEST(XdsVerifier, RouteBeforeListenerSOTW) {
  XdsVerifier verifier(test::server::config_validation::Config::SOTW);
  // Send a route first, so envoy will not accept it.
  verifier.routeAdded(buildRoute("route_config_0"));
  EXPECT_TRUE(verifier.hasRoute("route_config_0"));
  EXPECT_FALSE(verifier.hasActiveRoute("route_config_0"));

  // Envoy still doesn't know about the route, so this will warm.
  verifier.listenerAdded(buildListener("listener_0", "route_config_0"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.WARMING));
  EXPECT_EQ(verifier.numAdded(), 1);
  EXPECT_EQ(verifier.numWarming(), 1);

  // Send a new route, which will include route_config_0 since SOTW, so route_config_0 will become
  // active.
  verifier.routeAdded(buildRoute("route_config_1"));
  EXPECT_TRUE(verifier.hasRoute("route_config_1"));
  EXPECT_FALSE(verifier.hasActiveRoute("route_config_1"));
  EXPECT_TRUE(verifier.hasActiveRoute("route_config_0"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.ACTIVE));
  EXPECT_EQ(verifier.numActive(), 1);
}

TEST(XdsVerifier, RouteBeforeListenerDelta) {
  XdsVerifier verifier(test::server::config_validation::Config::DELTA);
  // Send a route first, so envoy will not accept it.
  verifier.routeAdded(buildRoute("route_config_0"));
  EXPECT_FALSE(verifier.hasActiveRoute("route_config_0"));

  // Envoy still doesn't know about the route, so this will warm.
  verifier.listenerAdded(buildListener("listener_0", "route_config_0"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.WARMING));
  EXPECT_EQ(verifier.numAdded(), 1);
  EXPECT_EQ(verifier.numWarming(), 1);

  // Send a new route, which will not include route_config_0 since SOTW, so route_config_0 will not
  // become active.
  verifier.routeAdded(buildRoute("route_config_1"));
  EXPECT_FALSE(verifier.hasActiveRoute("route_config_1"));
  EXPECT_FALSE(verifier.hasActiveRoute("route_config_0"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.WARMING));
  EXPECT_EQ(verifier.numWarming(), 1);
}

TEST(XdsVerifier, UpdateWarmingListener) {
  XdsVerifier verifier(test::server::config_validation::Config::SOTW);
  verifier.listenerAdded(buildListener("listener_0", "route_config_0"));
  verifier.listenerUpdated(buildListener("listener_0", "route_config_1"));
  // The new listener should directly replace the old listener since it's warming.
  EXPECT_EQ(verifier.numModified(), 1);
  EXPECT_EQ(verifier.numAdded(), 1);

  // Send the route for the old listener, which should have been replaced with the update.
  verifier.routeAdded(buildRoute("route_config_0"));
  EXPECT_FALSE(verifier.hasListener("listener_0", verifier.ACTIVE));

  // Now the new should become active.
  verifier.routeAdded(buildRoute("route_config_1"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.ACTIVE));
}

TEST(XdsVerifier, UpdateActiveListener) {
  XdsVerifier verifier(test::server::config_validation::Config::SOTW);

  // Add an active listener.
  verifier.listenerAdded(buildListener("listener_0", "route_config_0"));
  verifier.routeAdded(buildRoute("route_config_0"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.ACTIVE));

  // Send an update, which should keep the old listener active until the new warms.
  verifier.listenerUpdated(buildListener("listener_0", "route_config_1"));
  EXPECT_EQ(verifier.numModified(), 1);
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.ACTIVE));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.WARMING));

  // Warm the new listener, which should remove the old.
  verifier.routeAdded(buildRoute("route_config_1"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.ACTIVE));
  EXPECT_FALSE(verifier.hasListener("listener_0", verifier.DRAINING));
  EXPECT_FALSE(verifier.hasListener("listener_0", verifier.WARMING));

  EXPECT_EQ(verifier.numActive(), 1);
}

TEST(XdsVerifier, UpdateActiveToActive) {
  XdsVerifier verifier(test::server::config_validation::Config::SOTW);

  // Add two active listeners to different routes.
  verifier.listenerAdded(buildListener("listener_0", "route_config_0"));
  verifier.routeAdded(buildRoute("route_config_0"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.ACTIVE));

  // Add an active listener.
  verifier.listenerAdded(buildListener("listener_1", "route_config_1"));
  verifier.routeAdded(buildRoute("route_config_1"));
  EXPECT_TRUE(verifier.hasListener("listener_1", verifier.ACTIVE));
  EXPECT_EQ(verifier.numAdded(), 2);

  // Send an update, which should make the new listener active straight away and remove the old
  // since its route is already active.
  verifier.listenerUpdated(buildListener("listener_0", "route_config_1"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.ACTIVE));
  EXPECT_FALSE(verifier.hasListener("listener_0", verifier.WARMING));
  EXPECT_EQ(verifier.numActive(), 2);
}

TEST(XdsVerifier, WarmMultipleListenersSOTW) {
  XdsVerifier verifier(test::server::config_validation::Config::SOTW);

  // Add two warming listener to the same route.
  verifier.listenerAdded(buildListener("listener_0", "route_config_0"));
  verifier.listenerAdded(buildListener("listener_1", "route_config_0"));

  // Send the route, make sure both are active.
  verifier.routeAdded(buildRoute("route_config_0"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.ACTIVE));
  EXPECT_TRUE(verifier.hasListener("listener_1", verifier.ACTIVE));
  EXPECT_EQ(verifier.numActive(), 2);
}

TEST(XdsVerifier, WarmMultipleListenersDelta) {
  XdsVerifier verifier(test::server::config_validation::Config::DELTA);

  // Add two warming listener to the same route.
  verifier.listenerAdded(buildListener("listener_0", "route_config_0"));
  verifier.listenerAdded(buildListener("listener_1", "route_config_0"));

  // Send the route, make sure both are active.
  verifier.routeAdded(buildRoute("route_config_0"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.ACTIVE));
  EXPECT_TRUE(verifier.hasListener("listener_1", verifier.ACTIVE));
  EXPECT_EQ(verifier.numActive(), 2);
}

TEST(XdsVerifier, ResendRouteSOTW) {
  XdsVerifier verifier(test::server::config_validation::Config::SOTW);

  // Send a route that will be ignored.
  verifier.routeAdded(buildRoute("route_config_0"));

  // Add a warming listener that refers to this route.
  verifier.listenerAdded(buildListener("listener_0", "route_config_0"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.WARMING));

  // Send the same route again, make sure listener becomes active.
  verifier.routeAdded(buildRoute("route_config_0"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.ACTIVE));
}

TEST(XdsVerifier, ResendRouteDelta) {
  XdsVerifier verifier(test::server::config_validation::Config::DELTA);

  // Send a route that will be ignored.
  verifier.routeAdded(buildRoute("route_config_0"));

  // Add a warming listener that refers to this route.
  verifier.listenerAdded(buildListener("listener_0", "route_config_0"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.WARMING));

  // Send the same route again, make sure listener becomes active.
  verifier.routeAdded(buildRoute("route_config_0"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.ACTIVE));
}

TEST(XdsVerifier, RemoveThenAddListener) {
  XdsVerifier verifier(test::server::config_validation::Config::SOTW);

  // Add an active listener.
  verifier.listenerAdded(buildListener("listener_0", "route_config_0"));
  verifier.routeAdded(buildRoute("route_config_0"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.ACTIVE));

  // Remove it.
  verifier.listenerRemoved("listener_0");
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.DRAINING));

  // And add it back, it should now be draining and active.
  verifier.listenerAdded(buildListener("listener_0", "route_config_0"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.DRAINING));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.ACTIVE));
}

TEST(XdsVerifier, UpdateBackToOriginal) {
  XdsVerifier verifier(test::server::config_validation::Config::SOTW);

  // Add an active listener.
  verifier.listenerAdded(buildListener("listener_0", "route_config_0"));
  verifier.routeAdded(buildRoute("route_config_0"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.ACTIVE));

  // Update it to a new route, should warm.
  verifier.listenerUpdated(buildListener("listener_0", "route_config_1"));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.ACTIVE));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.WARMING));

  // Update it back to original, should remove warming listener.
  verifier.listenerUpdated(buildListener("listener_0", "route_config_0"));
  EXPECT_FALSE(verifier.hasListener("listener_0", verifier.WARMING));
  EXPECT_TRUE(verifier.hasListener("listener_0", verifier.ACTIVE));
}

} // namespace Envoy
