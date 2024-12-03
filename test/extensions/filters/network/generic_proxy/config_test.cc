#include "envoy/admin/v3/config_dump_shared.pb.h"
#include "envoy/admin/v3/config_dump_shared.pb.validate.h"
#include "envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.h"
#include "envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.validate.h"
#include "envoy/extensions/filters/network/generic_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/generic_proxy/v3/route.pb.validate.h"

#include "source/extensions/filters/network/generic_proxy/config.h"

#include "test/extensions/filters/network/generic_proxy/fake_codec.h"
#include "test/extensions/filters/network/generic_proxy/mocks/codec.h"
#include "test/extensions/filters/network/generic_proxy/mocks/filter.h"
#include "test/extensions/filters/network/generic_proxy/mocks/route.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace {

using ::testing::Return;

TEST(FactoryTest, FactoryTest) {
  const std::string yaml_config = R"EOF(
    stat_prefix: config_test
    filters:
    - name: envoy.filters.generic.router
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.router.v3.Router
    codec_config:
      name: fake
      typed_config:
        "@type": type.googleapis.com/xds.type.v3.TypedStruct
        type_url: envoy.generic_proxy.codecs.fake.type
        value: {}
    route_config:
      name: test-routes
      routes:
        matcher_tree:
          input:
            name: request-service
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.ServiceMatchInput
          exact_match_map:
            map:
              service_name_0:
                matcher:
                  matcher_list:
                    matchers:
                    - predicate:
                        single_predicate:
                          input:
                            name: request-properties
                            typed_config:
                              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput
                              property_name: version
                          value_match:
                            exact: v1
                      on_match:
                        action:
                          name: route
                          typed_config:
                            "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
                            cluster: cluster_0
    )EOF";

  FakeStreamCodecFactoryConfig codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  Factory factory;
  ProxyConfig proto_config;
  TestUtility::loadFromYaml(yaml_config, proto_config);

  EXPECT_NE(nullptr, factory.createFilterFactoryFromProto(proto_config, factory_context).value());
}

TEST(FactoryTest, GenericRds) {
  const std::string config_yaml = R"EOF(
    stat_prefix: ingress
    filters:
    - name: envoy.filters.generic.router
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.router.v3.Router
    codec_config:
      name: fake
      typed_config:
        "@type": type.googleapis.com/xds.type.v3.TypedStruct
        type_url: envoy.generic_proxy.codecs.fake.type
        value: {}
    generic_rds:
      config_source: { ads: {} }
      route_config_name: test_route
    )EOF";

  const std::string response_yaml = (R"EOF(
version_info: "1"
resources:
  - "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.v3.RouteConfiguration
    name: test_route
    routes: {}
)EOF");

  FakeStreamCodecFactoryConfig codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  Factory factory;

  envoy::extensions::filters::network::generic_proxy::v3::GenericProxy config;
  TestUtility::loadFromYaml(config_yaml, config);

  Matchers::UniversalStringMatcher universal_name_matcher;
  Network::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(config, factory_context).value();
  auto response =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response_yaml);
  const auto decoded_resources = TestUtility::decodeResources<
      envoy::extensions::filters::network::generic_proxy::v3::RouteConfiguration>(response);
  EXPECT_TRUE(
      factory_context.server_factory_context_.cluster_manager_.subscription_factory_.callbacks_
          ->onConfigUpdate(decoded_resources.refvec_, response.version_info())
          .ok());
  auto message_ptr = factory_context.server_factory_context_.admin_.config_tracker_
                         .config_tracker_callbacks_["genericrds_routes"](universal_name_matcher);
  const auto& dump =
      TestUtility::downcastAndValidate<const envoy::admin::v3::RoutesConfigDump&>(*message_ptr);
  EXPECT_EQ(1, dump.dynamic_route_configs().size());
  EXPECT_EQ(0, dump.static_route_configs().size());
}

TEST(FactoryTest, GenericRdsApiConfigSource) {
  const std::string config_yaml = R"EOF(
    stat_prefix: ingress
    filters:
    - name: envoy.filters.generic.router
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.router.v3.Router
    codec_config:
      name: fake
      typed_config:
        "@type": type.googleapis.com/xds.type.v3.TypedStruct
        type_url: envoy.generic_proxy.codecs.fake.type
        value: {}
    generic_rds:
      config_source:
        api_config_source: { api_type: GRPC }
      route_config_name: test_route
    )EOF";

  FakeStreamCodecFactoryConfig codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  Factory factory;

  envoy::extensions::filters::network::generic_proxy::v3::GenericProxy config;
  TestUtility::loadFromYaml(config_yaml, config);

  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(config, factory_context).IgnoreError(), EnvoyException,
      "genericrds supports only aggregated api_type in api_config_source");
}

TEST(FactoryTest, CustomReadFilterFactory) {
  const std::string config_yaml = R"EOF(
    stat_prefix: ingress
    filters:
    - name: envoy.filters.generic.router
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.router.v3.Router
    codec_config:
      name: mock
      typed_config:
        "@type": type.googleapis.com/xds.type.v3.TypedStruct
        type_url: envoy.generic_proxy.codecs.mock.type
        value: {}
    generic_rds:
      config_source: { ads: {} }
      route_config_name: test_route
    )EOF";

  const std::string response_yaml = (R"EOF(
    version_info: "1"
    resources:
      - "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.v3.RouteConfiguration
        name: test_route
        routes: {}
    )EOF");

  MockStreamCodecFactoryConfig codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  Factory factory;

  envoy::extensions::filters::network::generic_proxy::v3::GenericProxy config;
  TestUtility::loadFromYaml(config_yaml, config);

  auto mock_codec_factory = std::make_unique<MockCodecFactory>();

  auto mock_proxy_factory = std::make_unique<MockProxyFactory>();
  auto raw_mock_proxy_factory = mock_proxy_factory.get();
  EXPECT_CALL(*raw_mock_proxy_factory, createProxy(_, _, _));

  EXPECT_CALL(codec_factory_config, createCodecFactory(_, _))
      .WillOnce(Return(testing::ByMove(std::move(mock_codec_factory))));
  EXPECT_CALL(codec_factory_config, createProxyFactory(_, _))
      .WillOnce(Return(testing::ByMove(std::move(mock_proxy_factory))));

  Network::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(config, factory_context).value();
  EXPECT_NE(nullptr, cb);
  Network::MockFilterManager filter_manager;
  cb(filter_manager);
}

/**
 * Test creating codec factory from typed extension config.
 */
TEST(BasicFilterConfigTest, CreatingCodecFactory) {

  {
    const std::string yaml_config = R"EOF(
      name: envoy.generic_proxy.codecs.fake
      typed_config:
        "@type": type.googleapis.com/xds.type.v3.TypedStruct
        type_url: envoy.generic_proxy.codecs.fake.type
        value: {}
      )EOF";
    NiceMock<Server::Configuration::MockFactoryContext> factory_context;

    envoy::config::core::v3::TypedExtensionConfig proto_config;
    TestUtility::loadFromYaml(yaml_config, proto_config);

    EXPECT_THROW(Factory::factoriesFromProto(proto_config, factory_context), EnvoyException);
  }

  {
    FakeStreamCodecFactoryConfig codec_factory_config;
    Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

    const std::string yaml_config = R"EOF(
      name: envoy.generic_proxy.codecs.fake
      typed_config:
        "@type": type.googleapis.com/xds.type.v3.TypedStruct
        type_url: envoy.generic_proxy.codecs.fake.type
        value: {}
      )EOF";
    NiceMock<Server::Configuration::MockFactoryContext> factory_context;

    envoy::config::core::v3::TypedExtensionConfig proto_config;
    TestUtility::loadFromYaml(yaml_config, proto_config);

    EXPECT_NE(nullptr, Factory::factoriesFromProto(proto_config, factory_context).first);
    EXPECT_EQ(nullptr, Factory::factoriesFromProto(proto_config, factory_context).second);
  }
}

/**
 * Test creating L7 filter factories from proto config.
 */
TEST(BasicFilterConfigTest, CreatingFilterFactories) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  ProtobufWkt::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> filters_proto_config;
  envoy::config::core::v3::TypedExtensionConfig codec_config;

  const std::string yaml_config_0 = R"EOF(
    name: mock_generic_proxy_filter_name_0
    typed_config:
      "@type": type.googleapis.com/xds.type.v3.TypedStruct
      type_url: mock_generic_proxy_filter_name_0
      value: {}
  )EOF";

  const std::string yaml_config_1 = R"EOF(
    name: mock_generic_proxy_filter_name_1
    typed_config:
      "@type": type.googleapis.com/xds.type.v3.TypedStruct
      type_url: mock_generic_proxy_filter_name_1
      value: {}
  )EOF";

  TestUtility::loadFromYaml(yaml_config_0, *filters_proto_config.Add());
  TestUtility::loadFromYaml(yaml_config_1, *filters_proto_config.Add());

  NiceMock<MockStreamFilterConfig> mock_filter_config_0;
  NiceMock<MockStreamFilterConfig> mock_filter_config_1;

  ON_CALL(mock_filter_config_0, name()).WillByDefault(Return("mock_generic_proxy_filter_name_0"));
  ON_CALL(mock_filter_config_1, name()).WillByDefault(Return("mock_generic_proxy_filter_name_1"));
  ON_CALL(mock_filter_config_0, configTypes())
      .WillByDefault(Return(std::set<std::string>{"mock_generic_proxy_filter_name_0"}));
  ON_CALL(mock_filter_config_1, configTypes())
      .WillByDefault(Return(std::set<std::string>{"mock_generic_proxy_filter_name_1"}));

  Registry::InjectFactory<NamedFilterConfigFactory> registration_0(mock_filter_config_0);
  Registry::InjectFactory<NamedFilterConfigFactory> registration_1(mock_filter_config_1);

  // No terminal filter.
  {
    EXPECT_THROW_WITH_MESSAGE(Factory::filtersFactoryFromProto(filters_proto_config, codec_config,
                                                               "test", factory_context),
                              EnvoyException,
                              "A terminal L7 filter is necessary for generic proxy");
  }

  // Error terminal filter position.
  {
    ON_CALL(mock_filter_config_0, isTerminalFilter()).WillByDefault(Return(true));

    EXPECT_THROW_WITH_MESSAGE(
        Factory::filtersFactoryFromProto(filters_proto_config, codec_config, "test",
                                         factory_context),
        EnvoyException,
        "Terminal filter: mock_generic_proxy_filter_name_0 must be the last generic L7 "
        "filter");
  }

  // Codec validation error.
  {
    ON_CALL(mock_filter_config_0, isTerminalFilter()).WillByDefault(Return(false));
    ON_CALL(mock_filter_config_0, validateCodec(_))
        .WillByDefault(Return(absl::InvalidArgumentError("codec validation error")));

    EXPECT_THROW_WITH_MESSAGE(Factory::filtersFactoryFromProto(filters_proto_config, codec_config,
                                                               "test", factory_context),
                              EnvoyException, "codec validation error");
  }

  {
    ON_CALL(mock_filter_config_0, isTerminalFilter()).WillByDefault(Return(false));
    ON_CALL(mock_filter_config_1, isTerminalFilter()).WillByDefault(Return(true));
    ON_CALL(mock_filter_config_0, validateCodec(_)).WillByDefault(Return(absl::OkStatus()));
    ON_CALL(mock_filter_config_1, validateCodec(_)).WillByDefault(Return(absl::OkStatus()));

    auto factories = Factory::filtersFactoryFromProto(filters_proto_config, codec_config, "test",
                                                      factory_context);
    EXPECT_EQ(2, factories.size());
  }
}

TEST(BasicFilterConfigTest, TestConfigurationWithTracing) {
  const std::string config_yaml = R"EOF(
    stat_prefix: ingress
    filters:
    - name: envoy.filters.generic.router
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.router.v3.Router
    codec_config:
      name: mock
      typed_config:
        "@type": type.googleapis.com/xds.type.v3.TypedStruct
        type_url: envoy.generic_proxy.codecs.mock.type
        value: {}
    generic_rds:
      config_source: { ads: {} }
      route_config_name: test_route
    tracing:
      max_path_tag_length: 128
      provider:
        name: zipkin
        typed_config:
          "@type": type.googleapis.com/envoy.config.trace.v3.ZipkinConfig
          collector_cluster: zipkin
          collector_endpoint: "/api/v2/spans"
          collector_endpoint_version: HTTP_JSON
    )EOF";

  NiceMock<MockStreamCodecFactoryConfig> codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  NiceMock<Network::MockListenerInfo> listener_info;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  ON_CALL(factory_context, listenerInfo()).WillByDefault(testing::ReturnRef(listener_info));
  factory_context.server_factory_context_.cluster_manager_.initializeClusters({"zipkin"}, {});
  factory_context.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
      {"zipkin"});

  Factory factory;

  envoy::extensions::filters::network::generic_proxy::v3::GenericProxy config;
  TestUtility::loadFromYaml(config_yaml, config);

  auto mock_codec_factory = std::make_unique<NiceMock<MockCodecFactory>>();

  EXPECT_CALL(codec_factory_config, createCodecFactory(_, _))
      .WillOnce(Return(testing::ByMove(std::move(mock_codec_factory))));

  Network::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(config, factory_context).value();
  EXPECT_NE(nullptr, cb);
  NiceMock<Network::MockFilterManager> filter_manager;
  cb(filter_manager);
}

TEST(BasicFilterConfigTest, TestConfigurationWithAccessLog) {
  const std::string config_yaml = R"EOF(
    stat_prefix: ingress
    filters:
    - name: envoy.filters.generic.router
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.router.v3.Router
    codec_config:
      name: mock
      typed_config:
        "@type": type.googleapis.com/xds.type.v3.TypedStruct
        type_url: envoy.generic_proxy.codecs.mock.type
        value: {}
    generic_rds:
      config_source: { ads: {} }
      route_config_name: test_route
    access_log:
    - name: envoy.generic_proxy.access_loggers.file
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
        path: "/dev/stdout"
        log_format:
          text_format_source:
            inline_string: "%METHOD% %PATH% %HOST% %PROTOCOL% %REQUEST_PROPERTY(key)% RESPONSE_PROPERTY(key)\n"
    )EOF";

  NiceMock<MockStreamCodecFactoryConfig> codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  Factory factory;

  envoy::extensions::filters::network::generic_proxy::v3::GenericProxy config;
  TestUtility::loadFromYaml(config_yaml, config);

  auto mock_codec_factory = std::make_unique<NiceMock<MockCodecFactory>>();

  EXPECT_CALL(codec_factory_config, createCodecFactory(_, _))
      .WillOnce(Return(testing::ByMove(std::move(mock_codec_factory))));

  Network::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(config, factory_context).value();
  EXPECT_NE(nullptr, cb);
  NiceMock<Network::MockFilterManager> filter_manager;
  cb(filter_manager);
}

TEST(BasicFilterConfigTest, TestConfigurationWithAccessLogAndLogFilter1) {
  const std::string config_yaml = R"EOF(
    stat_prefix: ingress
    filters:
    - name: envoy.filters.generic.router
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.router.v3.Router
    codec_config:
      name: mock
      typed_config:
        "@type": type.googleapis.com/xds.type.v3.TypedStruct
        type_url: envoy.generic_proxy.codecs.mock.type
        value: {}
    generic_rds:
      config_source: { ads: {} }
      route_config_name: test_route
    access_log:
    - name: envoy.generic_proxy.access_loggers.file
      filter:
        not_health_check_filter: {}
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
        path: "/dev/stdout"
        log_format:
          text_format_source:
            inline_string: "%METHOD% %PATH% %HOST% %PROTOCOL% %REQUEST_PROPERTY(key)% RESPONSE_PROPERTY(key)\n"
    )EOF";

  NiceMock<MockStreamCodecFactoryConfig> codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  Factory factory;

  envoy::extensions::filters::network::generic_proxy::v3::GenericProxy config;
  TestUtility::loadFromYaml(config_yaml, config);

  auto mock_codec_factory = std::make_unique<NiceMock<MockCodecFactory>>();

  EXPECT_CALL(codec_factory_config, createCodecFactory(_, _))
      .WillOnce(Return(testing::ByMove(std::move(mock_codec_factory))));

  EXPECT_THROW_WITH_MESSAGE(
      { auto status_or = factory.createFilterFactoryFromProto(config, factory_context); },
      EnvoyException,
      "Access log filter: only extension filter is supported by non-HTTP access loggers.");
}

TEST(BasicFilterConfigTest, TestConfigurationWithAccessLogAndLogFilter2) {
  const std::string config_yaml = R"EOF(
    stat_prefix: ingress
    filters:
    - name: envoy.filters.generic.router
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.router.v3.Router
    codec_config:
      name: mock
      typed_config:
        "@type": type.googleapis.com/xds.type.v3.TypedStruct
        type_url: envoy.generic_proxy.codecs.mock.type
        value: {}
    generic_rds:
      config_source: { ads: {} }
      route_config_name: test_route
    access_log:
    - name: envoy.generic_proxy.access_loggers.file
      filter:
        extension_filter:
          name: envoy.generic_proxy.access_log.fake
          typed_config:
            "@type": type.googleapis.com/xds.type.v3.TypedStruct
            type_url: envoy.generic_proxy.access_log.fake.type
            value: {}
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
        path: "/dev/stdout"
        log_format:
          text_format_source:
            inline_string: "%METHOD% %PATH% %HOST% %PROTOCOL% %REQUEST_PROPERTY(key)% RESPONSE_PROPERTY(key)\n"
    )EOF";

  NiceMock<MockStreamCodecFactoryConfig> codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  FakeAccessLogExtensionFilterFactory fake_access_log_extension_filter_factory;
  Registry::InjectFactory<AccessLogFilterFactory> registration_log(
      fake_access_log_extension_filter_factory);

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  Factory factory;

  envoy::extensions::filters::network::generic_proxy::v3::GenericProxy config;
  TestUtility::loadFromYaml(config_yaml, config);

  auto mock_codec_factory = std::make_unique<NiceMock<MockCodecFactory>>();

  EXPECT_CALL(codec_factory_config, createCodecFactory(_, _))
      .WillOnce(Return(testing::ByMove(std::move(mock_codec_factory))));

  Network::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(config, factory_context).value();
  EXPECT_NE(nullptr, cb);
  NiceMock<Network::MockFilterManager> filter_manager;
  cb(filter_manager);
}

} // namespace
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
