#include "third_party/envoy/src/mobile/library/cc/client_engine_builder.h"

#include <sys/socket.h>

#include <memory>
#include <string>
#include <utility>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/route/v3/route.proto.h"
#include "envoy/extensions/filters/http/buffer/v3/buffer.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.proto.h"

#if defined(ENVOY_ENABLE_FULL_PROTOS)

namespace Envoy {
namespace {

using namespace Platform;

using envoy::config::bootstrap::v3::Bootstrap;
using envoy::config::cluster::v3::Cluster;
using envoy::extensions::filters::network::http_connection_manager::v3::
    EnvoyMobileHttpConnectionManager;
using testing::EqualsProto;
using testing::HasSubstr;

envoy::config::route::v3::RouteConfiguration defaultRouteConfig() {
  envoy::config::route::v3::RouteConfiguration route_configuration;
  route_configuration.set_name("route_config");
  auto* virtual_host = route_configuration.add_virtual_hosts();
  virtual_host->set_name("virtual_host");
  virtual_host->add_domains("*");
  auto* route = virtual_host->add_routes();
  route->mutable_match()->set_prefix("/");
  route->mutable_route()->set_cluster("some_cluster");
  return route_configuration;
}

envoy::config::cluster::v3::Cluster defaultCluster() {
  envoy::config::cluster::v3::Cluster cluster;
  cluster.set_name("some_cluster");
  return cluster;
}

ClientEngineBuilder defaultClientEngineBuilder() {
  ClientEngineBuilder engine_builder;
  engine_builder.setHcmRouteConfiguration(defaultRouteConfig());
  engine_builder.addCluster(defaultCluster());
  return engine_builder;
}

TEST(TestClientConfig, StreamIdleTimeout) {
  ClientEngineBuilder engine_builder = defaultClientEngineBuilder();
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(), HasSubstr("stream_idle_timeout { seconds: 15 }"));
  engine_builder.setHcmRouteConfiguration(defaultRouteConfig());
  engine_builder.addCluster(defaultCluster());
  engine_builder.setHcmStreamIdleTimeoutSeconds(42);
  bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(), HasSubstr("stream_idle_timeout { seconds: 42 }"));
}

TEST(TestClientConfig, SetPerConnectionBufferLimit) {
  ClientEngineBuilder engine_builder = defaultClientEngineBuilder();
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(),
              HasSubstr("per_connection_buffer_limit_bytes { value: 10485760 }"));
  engine_builder.setHcmRouteConfiguration(defaultRouteConfig());
  engine_builder.addCluster(defaultCluster());
  engine_builder.setPerConnectionBufferLimit(12345);
  bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(),
              HasSubstr("per_connection_buffer_limit_bytes { value: 12345 }"));
}

TEST(TestClientConfig, MultiFlag) {
  ClientEngineBuilder engine_builder = defaultClientEngineBuilder();
  engine_builder.addRuntimeGuard("test_feature_false", true)
      .addRuntimeGuard("test_feature_true", false);
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  const std::string bootstrap_str = bootstrap->ShortDebugString();
  EXPECT_THAT(bootstrap_str, HasSubstr("\"test_feature_false\" value { bool_value: true }"));
  EXPECT_THAT(bootstrap_str, HasSubstr("\"test_feature_true\" value { bool_value: false }"));
}

TEST(TestClientConfig, SetNodeId) {
  ClientEngineBuilder engine_builder = defaultClientEngineBuilder();
  const std::string default_node_id = "envoy-client";
  EXPECT_EQ(engine_builder.generateBootstrap()->node().id(), default_node_id);
  engine_builder.setHcmRouteConfiguration(defaultRouteConfig());
  engine_builder.addCluster(defaultCluster());
  const std::string test_node_id = "my_test_node";
  engine_builder.setNodeId(test_node_id);
  EXPECT_EQ(engine_builder.generateBootstrap()->node().id(), test_node_id);
}

TEST(TestClientConfig, SetNodeLocality) {
  ClientEngineBuilder engine_builder = defaultClientEngineBuilder();
  const std::string region = "us-west-1";
  const std::string zone = "some_zone";
  const std::string sub_zone = "some_sub_zone";
  engine_builder.setNodeLocality(region, zone, sub_zone);
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_EQ(bootstrap->node().locality().region(), region);
  EXPECT_EQ(bootstrap->node().locality().zone(), zone);
  EXPECT_EQ(bootstrap->node().locality().sub_zone(), sub_zone);
}

TEST(TestClientConfig, SetNodeMetadata) {
  ClientEngineBuilder engine_builder = defaultClientEngineBuilder();
  Protobuf::Struct node_metadata;
  (*node_metadata.mutable_fields())["string_field"].set_string_value("some_string");
  (*node_metadata.mutable_fields())["bool_field"].set_bool_value(true);
  (*node_metadata.mutable_fields())["number_field"].set_number_value(28.3);
  engine_builder.setNodeMetadata(node_metadata);
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_EQ(bootstrap->node().metadata().fields().at("string_field").string_value(), "some_string");
  EXPECT_EQ(bootstrap->node().metadata().fields().at("bool_field").bool_value(), true);
  EXPECT_DOUBLE_EQ(bootstrap->node().metadata().fields().at("number_field").number_value(), 28.3);
}

TEST(TestClientConfig, AddCluster) {
  ClientEngineBuilder engine_builder;
  engine_builder.setHcmRouteConfiguration(defaultRouteConfig());
  envoy::config::cluster::v3::Cluster cluster;
  cluster.set_name("test_cluster");
  cluster.mutable_connect_timeout()->set_seconds(5);
  cluster.set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
  // Keep a copy for comparison.
  envoy::config::cluster::v3::Cluster expected_cluster = cluster;
  engine_builder.addCluster(std::move(cluster));
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_EQ(bootstrap->static_resources().clusters_size(), 1);
  EXPECT_THAT(bootstrap->static_resources().clusters(0), EqualsProto(expected_cluster));
}

TEST(TestClientConfig, AddHcmHttpFilter) {
  ClientEngineBuilder engine_builder = defaultClientEngineBuilder();
  envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter filter;
  filter.set_name("envoy.filters.http.buffer");
  envoy::extensions::filters::http::buffer::v3::Buffer buffer_config;
  buffer_config.mutable_max_request_bytes()->set_value(12345);
  filter.mutable_typed_config()->PackFrom(buffer_config);
  envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter expected_filter =
      filter;
  engine_builder.addHcmHttpFilter(std::move(filter));
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EnvoyMobileHttpConnectionManager api_listener_config;
  bootstrap->static_resources().listeners(0).api_listener().api_listener().UnpackTo(
      &api_listener_config);
  ASSERT_EQ(api_listener_config.config().http_filters_size(), 2);
  EXPECT_THAT(api_listener_config.config().http_filters(0), EqualsProto(expected_filter));
}

TEST(TestClientConfig, SetRouterConfig) {
  ClientEngineBuilder engine_builder = defaultClientEngineBuilder();
  envoy::extensions::filters::http::router::v3::Router router_config;
  router_config.set_suppress_envoy_headers(true);
  router_config.add_strict_check_headers("x-foo");
  envoy::extensions::filters::http::router::v3::Router expected_router_config = router_config;
  engine_builder.setRouterConfig(std::move(router_config));
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EnvoyMobileHttpConnectionManager api_listener_config;
  bootstrap->static_resources().listeners(0).api_listener().api_listener().UnpackTo(
      &api_listener_config);
  ASSERT_EQ(api_listener_config.config().http_filters_size(), 1);
  EXPECT_EQ(api_listener_config.config().http_filters(0).name(), "envoy.router");
  envoy::extensions::filters::http::router::v3::Router actual_router_config;
  api_listener_config.config().http_filters(0).typed_config().UnpackTo(&actual_router_config);
  EXPECT_THAT(actual_router_config, EqualsProto(expected_router_config));
}

TEST(TestClientConfig, SetWatchdog) {
  ClientEngineBuilder engine_builder;
  engine_builder.setHcmRouteConfiguration(defaultRouteConfig());
  engine_builder.addCluster(defaultCluster());
  envoy::config::bootstrap::v3::Watchdogs watchdogs;
  watchdogs.mutable_main_thread_watchdog()->mutable_miss_timeout()->set_seconds(123);
  watchdogs.mutable_main_thread_watchdog()->mutable_megamiss_timeout()->set_seconds(456);
  watchdogs.mutable_worker_watchdog()->mutable_miss_timeout()->set_seconds(789);
  envoy::config::bootstrap::v3::Watchdogs expected_watchdogs = watchdogs;
  engine_builder.setWatchdog(std::move(watchdogs));
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->watchdogs(), EqualsProto(expected_watchdogs));
}

} // namespace
} // namespace Envoy

#endif // ENVOY_ENABLE_FULL_PROTOS
