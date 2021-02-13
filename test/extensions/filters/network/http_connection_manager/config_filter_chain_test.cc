#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/trace/v3/http_tracer.pb.h"
#include "envoy/config/trace/v3/opencensus.pb.h"
#include "envoy/config/trace/v3/zipkin.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.validate.h"
#include "envoy/server/request_id_extension_config.h"
#include "envoy/type/v3/percent.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/filter/http/filter_config_discovery_impl.h"
#include "common/http/date_provider_impl.h"
#include "common/http/request_id_extension_uuid_impl.h"
#include "common/network/address_impl.h"

#include "extensions/filters/network/http_connection_manager/config.h"

#include "test/extensions/filters/network/http_connection_manager/config.pb.h"
#include "test/extensions/filters/network/http_connection_manager/config.pb.validate.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/printers.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "config_test_base.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::An;
using testing::Eq;
using testing::NotNull;
using testing::Pointee;
using testing::Return;
using testing::WhenDynamicCastTo;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {

class FilterChainTest : public HttpConnectionManagerConfigTest {
public:
  const std::string basic_config_ = R"EOF(
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
http_filters:
- name: encoder-decoder-buffer-filter
- name: envoy.filters.http.router

  )EOF";
};

TEST_F(FilterChainTest, CreateFilterChain) {
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(basic_config_), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);

  Http::MockFilterChainFactoryCallbacks callbacks;
  EXPECT_CALL(callbacks, addStreamFilter(_));        // Buffer
  EXPECT_CALL(callbacks, addStreamDecoderFilter(_)); // Router
  config.createFilterChain(callbacks);
}

TEST_F(FilterChainTest, CreateDynamicFilterChain) {
  const std::string yaml_string = R"EOF(
codec_type: http1
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
http_filters:
- name: foo
  config_discovery:
    config_source: { resource_api_version: V3, ads: {} }
    type_urls:
    - type.googleapis.com/envoy.extensions.filters.http.health_check.v3.HealthCheck
- name: bar
  config_discovery:
    config_source: { resource_api_version: V3, ads: {} }
    type_urls:
    - type.googleapis.com/envoy.extensions.filters.http.health_check.v3.HealthCheck
- name: envoy.filters.http.router
  )EOF";
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);

  Http::MockFilterChainFactoryCallbacks callbacks;
  Http::StreamDecoderFilterSharedPtr missing_config_filter;
  EXPECT_CALL(callbacks, addStreamDecoderFilter(_))
      .Times(2)
      .WillOnce(testing::SaveArg<0>(&missing_config_filter))
      .WillOnce(Return()); // MissingConfigFilter (only once) and router
  config.createFilterChain(callbacks);

  Http::MockStreamDecoderFilterCallbacks decoder_callbacks;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(decoder_callbacks, streamInfo()).WillRepeatedly(ReturnRef(stream_info));
  EXPECT_CALL(decoder_callbacks, sendLocalReply(Http::Code::InternalServerError, _, _, _, _))
      .WillRepeatedly(Return());
  Http::TestRequestHeaderMapImpl headers;
  missing_config_filter->setDecoderFilterCallbacks(decoder_callbacks);
  missing_config_filter->decodeHeaders(headers, false);
  EXPECT_TRUE(stream_info.hasResponseFlag(StreamInfo::ResponseFlag::NoFilterConfigFound));
}

// Tests where upgrades are configured on via the HCM.
TEST_F(FilterChainTest, CreateUpgradeFilterChain) {
  auto hcm_config = parseHttpConnectionManagerFromYaml(basic_config_);
  hcm_config.add_upgrade_configs()->set_upgrade_type("websocket");

  HttpConnectionManagerConfig config(hcm_config, context_, date_provider_,
                                     route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);

  NiceMock<Http::MockFilterChainFactoryCallbacks> callbacks;
  // Check the case where WebSockets are configured in the HCM, and no router
  // config is present. We should create an upgrade filter chain for
  // WebSockets.
  {
    EXPECT_CALL(callbacks, addStreamFilter(_));        // Buffer
    EXPECT_CALL(callbacks, addStreamDecoderFilter(_)); // Router
    EXPECT_TRUE(config.createUpgradeFilterChain("WEBSOCKET", nullptr, callbacks));
  }

  // Check the case where WebSockets are configured in the HCM, and no router
  // config is present. We should not create an upgrade filter chain for Foo
  {
    EXPECT_CALL(callbacks, addStreamFilter(_)).Times(0);
    EXPECT_CALL(callbacks, addStreamDecoderFilter(_)).Times(0);
    EXPECT_FALSE(config.createUpgradeFilterChain("foo", nullptr, callbacks));
  }

  // Now override the HCM with a route-specific disabling of WebSocket to
  // verify route-specific disabling works.
  {
    std::map<std::string, bool> upgrade_map;
    upgrade_map.emplace(std::make_pair("WebSocket", false));
    EXPECT_FALSE(config.createUpgradeFilterChain("WEBSOCKET", &upgrade_map, callbacks));
  }

  // For paranoia's sake make sure route-specific enabling doesn't break
  // anything.
  {
    EXPECT_CALL(callbacks, addStreamFilter(_));        // Buffer
    EXPECT_CALL(callbacks, addStreamDecoderFilter(_)); // Router
    std::map<std::string, bool> upgrade_map;
    upgrade_map.emplace(std::make_pair("WebSocket", true));
    EXPECT_TRUE(config.createUpgradeFilterChain("WEBSOCKET", &upgrade_map, callbacks));
  }
}

// Tests where upgrades are configured off via the HCM.
TEST_F(FilterChainTest, CreateUpgradeFilterChainHCMDisabled) {
  auto hcm_config = parseHttpConnectionManagerFromYaml(basic_config_);
  hcm_config.add_upgrade_configs()->set_upgrade_type("websocket");
  hcm_config.mutable_upgrade_configs(0)->mutable_enabled()->set_value(false);

  HttpConnectionManagerConfig config(hcm_config, context_, date_provider_,
                                     route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);

  NiceMock<Http::MockFilterChainFactoryCallbacks> callbacks;
  // Check the case where WebSockets are off in the HCM, and no router config is present.
  { EXPECT_FALSE(config.createUpgradeFilterChain("WEBSOCKET", nullptr, callbacks)); }

  // Check the case where WebSockets are off in the HCM and in router config.
  {
    std::map<std::string, bool> upgrade_map;
    upgrade_map.emplace(std::make_pair("WebSocket", false));
    EXPECT_FALSE(config.createUpgradeFilterChain("WEBSOCKET", &upgrade_map, callbacks));
  }

  // With a route-specific enabling for WebSocket, WebSocket should work.
  {
    std::map<std::string, bool> upgrade_map;
    upgrade_map.emplace(std::make_pair("WebSocket", true));
    EXPECT_TRUE(config.createUpgradeFilterChain("WEBSOCKET", &upgrade_map, callbacks));
  }

  // With only a route-config we should do what the route config says.
  {
    std::map<std::string, bool> upgrade_map;
    upgrade_map.emplace(std::make_pair("foo", true));
    upgrade_map.emplace(std::make_pair("bar", false));
    EXPECT_TRUE(config.createUpgradeFilterChain("foo", &upgrade_map, callbacks));
    EXPECT_FALSE(config.createUpgradeFilterChain("bar", &upgrade_map, callbacks));
    EXPECT_FALSE(config.createUpgradeFilterChain("eep", &upgrade_map, callbacks));
  }
}

TEST_F(FilterChainTest, CreateCustomUpgradeFilterChain) {
  auto hcm_config = parseHttpConnectionManagerFromYaml(basic_config_);
  auto websocket_config = hcm_config.add_upgrade_configs();
  websocket_config->set_upgrade_type("websocket");

  ASSERT_TRUE(websocket_config->add_filters()->ParseFromString("\n"
                                                               "\x19"
                                                               "envoy.filters.http.router"));

  auto foo_config = hcm_config.add_upgrade_configs();
  foo_config->set_upgrade_type("foo");
  foo_config->add_filters()->ParseFromString("\n"
                                             "\x1D"
                                             "encoder-decoder-buffer-filter");
  foo_config->add_filters()->ParseFromString("\n"
                                             "\x1D"
                                             "encoder-decoder-buffer-filter");
  foo_config->add_filters()->ParseFromString("\n"
                                             "\x19"
                                             "envoy.filters.http.router");

  HttpConnectionManagerConfig config(hcm_config, context_, date_provider_,
                                     route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);

  {
    Http::MockFilterChainFactoryCallbacks callbacks;
    EXPECT_CALL(callbacks, addStreamFilter(_));        // Buffer
    EXPECT_CALL(callbacks, addStreamDecoderFilter(_)); // Router
    config.createFilterChain(callbacks);
  }

  {
    Http::MockFilterChainFactoryCallbacks callbacks;
    EXPECT_CALL(callbacks, addStreamDecoderFilter(_));
    EXPECT_TRUE(config.createUpgradeFilterChain("websocket", nullptr, callbacks));
  }

  {
    Http::MockFilterChainFactoryCallbacks callbacks;
    EXPECT_CALL(callbacks, addStreamDecoderFilter(_));
    EXPECT_CALL(callbacks, addStreamFilter(_)).Times(2); // Buffer
    EXPECT_TRUE(config.createUpgradeFilterChain("Foo", nullptr, callbacks));
  }
}

TEST_F(FilterChainTest, CreateCustomUpgradeFilterChainWithRouterNotLast) {
  auto hcm_config = parseHttpConnectionManagerFromYaml(basic_config_);
  auto websocket_config = hcm_config.add_upgrade_configs();
  websocket_config->set_upgrade_type("websocket");

  ASSERT_TRUE(websocket_config->add_filters()->ParseFromString("\n"
                                                               "\x19"
                                                               "envoy.filters.http.router"));

  auto foo_config = hcm_config.add_upgrade_configs();
  foo_config->set_upgrade_type("foo");
  foo_config->add_filters()->ParseFromString("\n"
                                             "\x19"
                                             "envoy.filters.http.router");
  foo_config->add_filters()->ParseFromString("\n"
                                             "\x1D"
                                             "encoder-decoder-buffer-filter");

  EXPECT_THROW_WITH_MESSAGE(
      HttpConnectionManagerConfig(hcm_config, context_, date_provider_,
                                  route_config_provider_manager_,
                                  scoped_routes_config_provider_manager_, http_tracer_manager_,
                                  filter_config_provider_manager_),
      EnvoyException,
      "Error: terminal filter named envoy.filters.http.router of type envoy.filters.http.router "
      "must be the last filter in a http upgrade filter chain.");
}

TEST_F(FilterChainTest, InvalidConfig) {
  auto hcm_config = parseHttpConnectionManagerFromYaml(basic_config_);
  hcm_config.add_upgrade_configs()->set_upgrade_type("WEBSOCKET");
  hcm_config.add_upgrade_configs()->set_upgrade_type("websocket");

  EXPECT_THROW_WITH_MESSAGE(
      HttpConnectionManagerConfig(hcm_config, context_, date_provider_,
                                  route_config_provider_manager_,
                                  scoped_routes_config_provider_manager_, http_tracer_manager_,
                                  filter_config_provider_manager_),
      EnvoyException, "Error: multiple upgrade configs with the same name: 'websocket'");
}

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
