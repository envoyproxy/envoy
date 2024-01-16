#pragma once

#include "source/common/filter/config_discovery_impl.h"
#include "source/common/http/date_provider_impl.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/network/http_connection_manager/config.h"

#include "test/extensions/filters/network/http_connection_manager/config.pb.h"
#include "test/extensions/filters/network/http_connection_manager/config.pb.validate.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {

envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
parseHttpConnectionManagerFromYaml(const std::string& yaml) {
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
      http_connection_manager;
  TestUtility::loadFromYamlAndValidate(yaml, http_connection_manager);
  return http_connection_manager;
}

class HttpConnectionManagerConfigTest : public testing::Test {
public:
  HttpConnectionManagerConfigTest() {
    scoped_runtime_.mergeValues(
        {{"envoy.reloadable_features.no_extension_lookup_by_name", "false"}});
    ON_CALL(context_, listenerInfo()).WillByDefault(testing::ReturnRef(listener_info_));
  }
  NiceMock<Network::MockListenerInfo> listener_info_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Http::SlowDateProviderImpl date_provider_{
      context_.server_factory_context_.mainThreadDispatcher().timeSource()};
  NiceMock<Router::MockRouteConfigProviderManager> route_config_provider_manager_;
  NiceMock<Config::MockConfigProviderManager> scoped_routes_config_provider_manager_;
  NiceMock<Tracing::MockTracerManager> tracer_manager_;
  Filter::HttpFilterConfigProviderManagerImpl filter_config_provider_manager_;
  std::shared_ptr<NiceMock<Tracing::MockTracer>> tracer_{
      std::make_shared<NiceMock<Tracing::MockTracer>>()};
  TestScopedRuntime scoped_runtime_;
  void createHttpConnectionManagerConfig(const std::string& yaml) {
    HttpConnectionManagerConfig(parseHttpConnectionManagerFromYaml(yaml), context_, date_provider_,
                                route_config_provider_manager_,
                                scoped_routes_config_provider_manager_, tracer_manager_,
                                filter_config_provider_manager_);
  }
};

class PassThroughFilterFactory : public Extensions::HttpFilters::Common::FactoryBase<
                                     test::http_connection_manager::FilterDependencyTestFilter> {
public:
  PassThroughFilterFactory(std::string name) : FactoryBase(name) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::http_connection_manager::FilterDependencyTestFilter&, const std::string&,
      Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(std::make_shared<Http::PassThroughDecoderFilter>());
    };
  }
};

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
