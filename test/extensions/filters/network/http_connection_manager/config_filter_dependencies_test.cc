#include "envoy/extensions/filters/common/dependency/v3/dependency.pb.h"

#include "source/extensions/filters/network/http_connection_manager/config.h"

#include "test/extensions/filters/network/http_connection_manager/config_test_base.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using envoy::extensions::filters::common::dependency::v3::Dependency;
using envoy::extensions::filters::common::dependency::v3::FilterDependencies;
using Envoy::Server::Configuration::FilterDependenciesPtr;
using Envoy::Server::Configuration::NamedHttpFilterConfigFactory;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {
namespace {

const std::string ConfigTemplate = R"EOF(
codec_type: http1
server_name: foo
stat_prefix: router
route_config:
  virtual_hosts:
  - name: service
    domains:
    - "*"
    routes:
    - match:
        prefix: "/"
      route:
        cluster: cluster
)EOF";

/**
 * Testing scenario for testing filter dependencies.
 * ChefFilter requires a "potato" dependency, which PantryFilter provides.
 */

Dependency potato() {
  Dependency d;
  d.set_name("potato");
  d.set_type(Dependency::FILTER_STATE_KEY);
  return d;
}

// Test filter that provides a potato.
class PantryFilterFactory : public PassThroughFilterFactory {
public:
  PantryFilterFactory() : PassThroughFilterFactory("test.pantry") {}

  FilterDependenciesPtr dependencies() override {
    FilterDependencies d;
    *d.add_decode_provided() = potato();
    return std::make_unique<FilterDependencies>(d);
  }
  std::set<std::string> configTypes() override { return {}; };
};

// Test filter that requires a potato.
class ChefFilterFactory : public PassThroughFilterFactory {
public:
  ChefFilterFactory() : PassThroughFilterFactory("test.chef") {}

  FilterDependenciesPtr dependencies() override {
    FilterDependencies d;
    *d.add_decode_required() = potato();
    return std::make_unique<FilterDependencies>(d);
  }
  std::set<std::string> configTypes() override { return {}; };
};

TEST_F(HttpConnectionManagerConfigTest, UnregisteredFilterException) {
  auto hcm_config = parseHttpConnectionManagerFromYaml(ConfigTemplate);
  hcm_config.add_http_filters()->set_name("test.pantry");
  hcm_config.add_http_filters()->set_name("envoy.filters.http.router");

  EXPECT_THROW_WITH_MESSAGE(
      HttpConnectionManagerConfig(
          hcm_config, context_, date_provider_, route_config_provider_manager_,
          scoped_routes_config_provider_manager_, tracer_manager_, filter_config_provider_manager_),
      EnvoyException, "Didn't find a registered implementation for name: 'test.pantry'");
}

// ChefFilter requires a potato, and PantryFilter provides it.
TEST_F(HttpConnectionManagerConfigTest, AllDependenciesSatisfiedOk) {
  auto hcm_config = parseHttpConnectionManagerFromYaml(ConfigTemplate);
  hcm_config.add_http_filters()->set_name("test.pantry");
  hcm_config.add_http_filters()->set_name("test.chef");
  hcm_config.add_http_filters()->set_name("envoy.filters.http.router");

  PantryFilterFactory pf;
  Registry::InjectFactory<NamedHttpFilterConfigFactory> rf(pf);
  ChefFilterFactory cf;
  Registry::InjectFactory<NamedHttpFilterConfigFactory> rc(cf);

  HttpConnectionManagerConfig(hcm_config, context_, date_provider_, route_config_provider_manager_,
                              scoped_routes_config_provider_manager_, tracer_manager_,
                              filter_config_provider_manager_);
}

// PantryFilter provides a potato, which is not required by any other filter.
TEST_F(HttpConnectionManagerConfigTest, UnusedProvidencyOk) {
  auto hcm_config = parseHttpConnectionManagerFromYaml(ConfigTemplate);
  hcm_config.add_http_filters()->set_name("test.pantry");
  hcm_config.add_http_filters()->set_name("envoy.filters.http.router");

  PantryFilterFactory pf;
  Registry::InjectFactory<NamedHttpFilterConfigFactory> rf(pf);

  HttpConnectionManagerConfig(hcm_config, context_, date_provider_, route_config_provider_manager_,
                              scoped_routes_config_provider_manager_, tracer_manager_,
                              filter_config_provider_manager_);
}

// ChefFilter requires a potato, but no filter provides it.
TEST_F(HttpConnectionManagerConfigTest, UnmetDependencyError) {
  auto hcm_config = parseHttpConnectionManagerFromYaml(ConfigTemplate);
  hcm_config.add_http_filters()->set_name("test.chef");
  hcm_config.add_http_filters()->set_name("envoy.filters.http.router");

  ChefFilterFactory cf;
  Registry::InjectFactory<NamedHttpFilterConfigFactory> rc(cf);

  EXPECT_THROW_WITH_MESSAGE(
      HttpConnectionManagerConfig(
          hcm_config, context_, date_provider_, route_config_provider_manager_,
          scoped_routes_config_provider_manager_, tracer_manager_, filter_config_provider_manager_),
      EnvoyException,
      "Dependency violation: filter 'test.chef' requires a FILTER_STATE_KEY named 'potato'");
}

// ChefFilter requires a potato, but no preceding filter provides it.
TEST_F(HttpConnectionManagerConfigTest, MisorderedDependenciesError) {
  auto hcm_config = parseHttpConnectionManagerFromYaml(ConfigTemplate);
  hcm_config.add_http_filters()->set_name("test.chef");
  hcm_config.add_http_filters()->set_name("test.pantry");
  hcm_config.add_http_filters()->set_name("envoy.filters.http.router");

  // Registration order does not matter.
  PantryFilterFactory pf;
  Registry::InjectFactory<NamedHttpFilterConfigFactory> rf(pf);
  ChefFilterFactory cf;
  Registry::InjectFactory<NamedHttpFilterConfigFactory> rc(cf);

  EXPECT_THROW_WITH_MESSAGE(
      HttpConnectionManagerConfig(
          hcm_config, context_, date_provider_, route_config_provider_manager_,
          scoped_routes_config_provider_manager_, tracer_manager_, filter_config_provider_manager_),
      EnvoyException,
      "Dependency violation: filter 'test.chef' requires a FILTER_STATE_KEY named 'potato'");
}

TEST_F(HttpConnectionManagerConfigTest, UpgradeUnmetDependencyError) {
  auto hcm_config = parseHttpConnectionManagerFromYaml(ConfigTemplate);
  auto upgrade_config = hcm_config.add_upgrade_configs();
  upgrade_config->set_upgrade_type("websocket");

  upgrade_config->add_filters()->set_name("test.chef");
  upgrade_config->add_filters()->set_name("envoy.filters.http.router");

  ChefFilterFactory cf;
  Registry::InjectFactory<NamedHttpFilterConfigFactory> rc(cf);

  EXPECT_THROW_WITH_MESSAGE(
      HttpConnectionManagerConfig(
          hcm_config, context_, date_provider_, route_config_provider_manager_,
          scoped_routes_config_provider_manager_, tracer_manager_, filter_config_provider_manager_),
      EnvoyException,
      "Dependency violation: filter 'test.chef' requires a FILTER_STATE_KEY named 'potato'");
}

TEST_F(HttpConnectionManagerConfigTest, UpgradeDependencyOK) {
  auto hcm_config = parseHttpConnectionManagerFromYaml(ConfigTemplate);
  auto upgrade_config = hcm_config.add_upgrade_configs();
  upgrade_config->set_upgrade_type("websocket");
  upgrade_config->add_filters()->set_name("test.pantry");
  upgrade_config->add_filters()->set_name("test.chef");
  upgrade_config->add_filters()->set_name("envoy.filters.http.router");

  PantryFilterFactory pf;
  Registry::InjectFactory<NamedHttpFilterConfigFactory> rf(pf);
  ChefFilterFactory cf;
  Registry::InjectFactory<NamedHttpFilterConfigFactory> rc(cf);

  HttpConnectionManagerConfig(hcm_config, context_, date_provider_, route_config_provider_manager_,
                              scoped_routes_config_provider_manager_, tracer_manager_,
                              filter_config_provider_manager_);
}

// Dependencies provided in the HCM config filter chain do not satisfy
// requirements in the upgrade filter chain.
TEST_F(HttpConnectionManagerConfigTest, UpgradeFilterChainDependenciesIsolatedFromHcmConfig) {
  auto hcm_config = parseHttpConnectionManagerFromYaml(ConfigTemplate);
  hcm_config.add_http_filters()->set_name("test.pantry");
  hcm_config.add_http_filters()->set_name("envoy.filters.http.router");

  auto upgrade_config = hcm_config.add_upgrade_configs();
  upgrade_config->set_upgrade_type("websocket");
  upgrade_config->add_filters()->set_name("test.chef");
  upgrade_config->add_filters()->set_name("envoy.filters.http.router");

  PantryFilterFactory pf;
  Registry::InjectFactory<NamedHttpFilterConfigFactory> rf(pf);
  ChefFilterFactory cf;
  Registry::InjectFactory<NamedHttpFilterConfigFactory> rc(cf);

  EXPECT_THROW_WITH_MESSAGE(
      HttpConnectionManagerConfig(
          hcm_config, context_, date_provider_, route_config_provider_manager_,
          scoped_routes_config_provider_manager_, tracer_manager_, filter_config_provider_manager_),
      EnvoyException,
      "Dependency violation: filter 'test.chef' requires a FILTER_STATE_KEY named 'potato'");
}

// Dependencies provided in one upgrade filter chain do not satisfy
// requirements in another.
TEST_F(HttpConnectionManagerConfigTest, UpgradeFilterChainDependenciesIsolatedFromOtherUpgrades) {
  auto hcm_config = parseHttpConnectionManagerFromYaml(ConfigTemplate);
  auto upgrade_config1 = hcm_config.add_upgrade_configs();
  upgrade_config1->set_upgrade_type("websocket");
  upgrade_config1->add_filters()->set_name("test.pantry");
  upgrade_config1->add_filters()->set_name("envoy.filters.http.router");

  auto upgrade_config2 = hcm_config.add_upgrade_configs();
  upgrade_config2->set_upgrade_type("CONNECT");
  upgrade_config2->add_filters()->set_name("test.chef");
  upgrade_config2->add_filters()->set_name("envoy.filters.http.router");

  PantryFilterFactory pf;
  Registry::InjectFactory<NamedHttpFilterConfigFactory> rf(pf);
  ChefFilterFactory cf;
  Registry::InjectFactory<NamedHttpFilterConfigFactory> rc(cf);

  EXPECT_THROW_WITH_MESSAGE(
      HttpConnectionManagerConfig(
          hcm_config, context_, date_provider_, route_config_provider_manager_,
          scoped_routes_config_provider_manager_, tracer_manager_, filter_config_provider_manager_),
      EnvoyException,
      "Dependency violation: filter 'test.chef' requires a FILTER_STATE_KEY named 'potato'");
}

} // namespace
} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
