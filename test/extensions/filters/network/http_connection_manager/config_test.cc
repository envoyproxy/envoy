#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/trace/v3/http_tracer.pb.h"
#include "envoy/config/trace/v3/opencensus.pb.h"
#include "envoy/config/trace/v3/zipkin.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.validate.h"
#include "envoy/server/request_id_extension_config.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/random_generator.h"
#include "source/common/http/conn_manager_utility.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/filters/network/http_connection_manager/config.h"
#include "source/extensions/request_id/uuid/config.h"

#include "test/extensions/filters/network/http_connection_manager/config.pb.h"
#include "test/extensions/filters/network/http_connection_manager/config.pb.validate.h"
#include "test/extensions/filters/network/http_connection_manager/config_test_base.h"
#include "test/mocks/common.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::An;
using testing::AnyNumber;
using testing::Eq;
using testing::NotNull;
using testing::Pointee;
using testing::Return;
using testing::WhenDynamicCastTo;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {
namespace {

TEST_F(HttpConnectionManagerConfigTest, ValidateFail) {
  EXPECT_THROW(
      HttpConnectionManagerFilterConfigFactory().createFilterFactoryFromProto(
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager(),
          context_),
      ProtoValidationException);
}

TEST_F(HttpConnectionManagerConfigTest, InvalidFilterName) {
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
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(createHttpConnectionManagerConfig(yaml_string), EnvoyException,
                            "Didn't find a registered implementation for name: 'foo'");
}

TEST_F(HttpConnectionManagerConfigTest, InvalidServerName) {
  const std::string yaml_string = R"EOF(
server_name: >
  foo
route_config:
  name: local_route
stat_prefix: router
  )EOF";

  EXPECT_THROW(createHttpConnectionManagerConfig(yaml_string), ProtoValidationException);
}

TEST_F(HttpConnectionManagerConfigTest, RouterInverted) {
  const std::string yaml_string = R"EOF(
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
- name: envoy.filters.http.router
- name: health_check
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.health_check.v3.HealthCheck
    pass_through_mode: false
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      createHttpConnectionManagerConfig(yaml_string), EnvoyException,
      "Error: terminal filter named envoy.filters.http.router of type envoy.filters.http.router "
      "must be the last filter in a http filter chain.");
}

TEST_F(HttpConnectionManagerConfigTest, NonTerminalFilter) {
  const std::string yaml_string = R"EOF(
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
- name: health_check
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.health_check.v3.HealthCheck
    pass_through_mode: false
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(createHttpConnectionManagerConfig(yaml_string), EnvoyException,
                            "Error: non-terminal filter named health_check of type "
                            "envoy.filters.http.health_check is the last filter in a http filter "
                            "chain.");
}

// When deprecating v2, remove the old style "operation_name: egress" config
// but retain the rest of the test.
TEST_F(HttpConnectionManagerConfigTest, DEPRECATED_FEATURE_TEST(MiscConfig)) {
  TestDeprecatedV2Api _deprecated_v2_api;
  const std::string yaml_string = R"EOF(
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
tracing:
  operation_name: egress
  max_path_tag_length: 128
http_filters:
- name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string, false),
                                     context_, date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);

  EXPECT_EQ(128, config.tracingConfig()->max_path_tag_length_);
  EXPECT_EQ(*context_.local_info_.address_, config.localAddress());
  EXPECT_EQ("foo", config.serverName());
  EXPECT_EQ(HttpConnectionManagerConfig::HttpConnectionManagerProto::OVERWRITE,
            config.serverHeaderTransformation());
  EXPECT_EQ(5 * 60 * 1000, config.streamIdleTimeout().count());
}

TEST_F(HttpConnectionManagerConfigTest, Http3Configured) {
  const std::string yaml_string = R"EOF(
codec_type: http3
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
- name: envoy.filters.http.router
  )EOF";

#ifdef ENVOY_ENABLE_QUIC
  {
    EXPECT_CALL(context_, isQuicListener()).WillOnce(Return(false));

    EXPECT_THROW_WITH_MESSAGE(
        HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string),
                                           context_, date_provider_, route_config_provider_manager_,
                                           scoped_routes_config_provider_manager_,
                                           http_tracer_manager_, filter_config_provider_manager_),
        EnvoyException, "HTTP/3 codec configured on non-QUIC listener.");
  }
  {
    EXPECT_CALL(context_, isQuicListener()).WillOnce(Return(true));
    HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                       date_provider_, route_config_provider_manager_,
                                       scoped_routes_config_provider_manager_, http_tracer_manager_,
                                       filter_config_provider_manager_);
  }
#else
  EXPECT_THROW_WITH_MESSAGE(
      HttpConnectionManagerConfig(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                  date_provider_, route_config_provider_manager_,
                                  scoped_routes_config_provider_manager_, http_tracer_manager_,
                                  filter_config_provider_manager_),
      EnvoyException, "HTTP3 configured but not enabled in the build.");
#endif
}

TEST_F(HttpConnectionManagerConfigTest, Http3HalfConfigured) {
  const std::string yaml_string = R"EOF(
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
- name: envoy.filters.http.router
  )EOF";

  EXPECT_CALL(context_, isQuicListener()).WillOnce(Return(true));

  EXPECT_THROW_WITH_MESSAGE(
      HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                         date_provider_, route_config_provider_manager_,
                                         scoped_routes_config_provider_manager_,
                                         http_tracer_manager_, filter_config_provider_manager_),
      EnvoyException, "Non-HTTP/3 codec configured on QUIC listener.");
}

TEST_F(HttpConnectionManagerConfigTest, TracingNotEnabledAndNoTracingConfigInBootstrap) {
  const std::string yaml_string = R"EOF(
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
- name: envoy.filters.http.router
  )EOF";

  // When tracing is not enabled on a given "envoy.filters.network.http_connection_manager" filter,
  // there is no reason to obtain an actual HttpTracer.
  EXPECT_CALL(http_tracer_manager_, getOrCreateHttpTracer(_)).Times(0);

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);

  // By default, tracer must be a null object (Tracing::HttpNullTracer) rather than nullptr.
  EXPECT_THAT(config.tracer().get(), WhenDynamicCastTo<Tracing::HttpNullTracer*>(NotNull()));
}

TEST_F(HttpConnectionManagerConfigTest, TracingNotEnabledWhileThereIsTracingConfigInBootstrap) {
  const std::string yaml_string = R"EOF(
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
- name: envoy.filters.http.router
  )EOF";

  // Simulate tracer provider configuration in the bootstrap config.
  envoy::config::trace::v3::Tracing tracing_config;
  tracing_config.mutable_http()->set_name("zipkin");
  tracing_config.mutable_http()->mutable_typed_config()->PackFrom(
      envoy::config::trace::v3::ZipkinConfig{});
  context_.http_context_.setDefaultTracingConfig(tracing_config);

  // When tracing is not enabled on a given "envoy.filters.network.http_connection_manager" filter,
  // there is no reason to obtain an actual HttpTracer.
  EXPECT_CALL(http_tracer_manager_, getOrCreateHttpTracer(_)).Times(0);

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);

  // Even though tracer provider is configured in the bootstrap config, a given filter instance
  // should not have a tracer associated with it.

  // By default, tracer must be a null object (Tracing::HttpNullTracer) rather than nullptr.
  EXPECT_THAT(config.tracer().get(), WhenDynamicCastTo<Tracing::HttpNullTracer*>(NotNull()));
}

TEST_F(HttpConnectionManagerConfigTest, TracingIsEnabledWhileThereIsNoTracingConfigInBootstrap) {
  const std::string yaml_string = R"EOF(
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
tracing: {} # notice that tracing is enabled
http_filters:
- name: envoy.filters.http.router
  )EOF";

  // When tracing is enabled on a given "envoy.filters.network.http_connection_manager" filter,
  // an actual HttpTracer must be obtained from the HttpTracerManager.
  EXPECT_CALL(http_tracer_manager_, getOrCreateHttpTracer(nullptr)).WillOnce(Return(http_tracer_));

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);

  // Actual HttpTracer must be obtained from the HttpTracerManager.
  EXPECT_THAT(config.tracer(), Eq(http_tracer_));
}

TEST_F(HttpConnectionManagerConfigTest, TracingIsEnabledAndThereIsTracingConfigInBootstrap) {
  const std::string yaml_string = R"EOF(
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
tracing: {} # notice that tracing is enabled
http_filters:
- name: envoy.filters.http.router
  )EOF";

  // Simulate tracer provider configuration in the bootstrap config.
  envoy::config::trace::v3::Tracing tracing_config;
  tracing_config.mutable_http()->set_name("zipkin");
  tracing_config.mutable_http()->mutable_typed_config()->PackFrom(
      envoy::config::trace::v3::ZipkinConfig{});
  context_.http_context_.setDefaultTracingConfig(tracing_config);

  // When tracing is enabled on a given "envoy.filters.network.http_connection_manager" filter,
  // an actual HttpTracer must be obtained from the HttpTracerManager.
  EXPECT_CALL(http_tracer_manager_, getOrCreateHttpTracer(Pointee(ProtoEq(tracing_config.http()))))
      .WillOnce(Return(http_tracer_));

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);

  // Actual HttpTracer must be obtained from the HttpTracerManager.
  EXPECT_THAT(config.tracer(), Eq(http_tracer_));
}

TEST_F(HttpConnectionManagerConfigTest, TracingIsEnabledAndThereIsInlinedTracerProvider) {
  const std::string yaml_string = R"EOF(
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
tracing:
  max_path_tag_length: 128
  provider:                # notice inlined tracing provider configuration
    name: zipkin
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.ZipkinConfig
      collector_cluster: zipkin
      collector_endpoint: "/api/v1/spans"
      collector_endpoint_version: HTTP_JSON
http_filters:
- name: envoy.filters.http.router
  )EOF";

  // Simulate tracer provider configuration in the bootstrap config.
  envoy::config::trace::v3::Tracing bootstrap_tracing_config;
  bootstrap_tracing_config.mutable_http()->set_name("opencensus");
  bootstrap_tracing_config.mutable_http()->mutable_typed_config()->PackFrom(
      envoy::config::trace::v3::OpenCensusConfig{});
  context_.http_context_.setDefaultTracingConfig(bootstrap_tracing_config);

  // Set up expected tracer provider configuration.
  envoy::config::trace::v3::Tracing_Http inlined_tracing_config;
  inlined_tracing_config.set_name("zipkin");
  envoy::config::trace::v3::ZipkinConfig zipkin_config;
  zipkin_config.set_collector_cluster("zipkin");
  zipkin_config.set_collector_endpoint("/api/v1/spans");
  zipkin_config.set_collector_endpoint_version(envoy::config::trace::v3::ZipkinConfig::HTTP_JSON);
  inlined_tracing_config.mutable_typed_config()->PackFrom(zipkin_config);

  // When tracing is enabled on a given "envoy.filters.network.http_connection_manager" filter,
  // an actual HttpTracer must be obtained from the HttpTracerManager.
  // Expect inlined tracer provider configuration to take precedence over bootstrap configuration.
  EXPECT_CALL(http_tracer_manager_, getOrCreateHttpTracer(Pointee(ProtoEq(inlined_tracing_config))))
      .WillOnce(Return(http_tracer_));

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string, false),
                                     context_, date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);

  // Actual HttpTracer must be obtained from the HttpTracerManager.
  EXPECT_THAT(config.tracer(), Eq(http_tracer_));
}

TEST_F(HttpConnectionManagerConfigTest, TracingCustomTagsConfig) {
  const std::string yaml_string = R"EOF(
stat_prefix: router
route_config:
  name: local_route
tracing:
  custom_tags:
  - tag: ltag
    literal:
      value: lvalue
  - tag: etag
    environment:
      name: E_TAG
  - tag: rtag
    request_header:
      name: X-Tag
  - tag: mtag
    metadata:
      kind: { request: {} }
      metadata_key:
        key: com.bar.foo
        path: [ { key: xx }, { key: yy } ]
  )EOF";
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);

  std::vector<std::string> custom_tags{"ltag", "etag", "rtag", "mtag"};
  const Tracing::CustomTagMap& custom_tag_map = config.tracingConfig()->custom_tags_;
  for (const std::string& custom_tag : custom_tags) {
    EXPECT_NE(custom_tag_map.find(custom_tag), custom_tag_map.end());
  }
}

TEST_F(HttpConnectionManagerConfigTest, SamplingDefault) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  internal_address_config:
    unix_sockets: true
  route_config:
    name: local_route
  tracing: {}
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string, false),
                                     context_, date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);

  EXPECT_EQ(100, config.tracingConfig()->client_sampling_.numerator());
  EXPECT_EQ(Tracing::DefaultMaxPathTagLength, config.tracingConfig()->max_path_tag_length_);
  EXPECT_EQ(envoy::type::v3::FractionalPercent::HUNDRED,
            config.tracingConfig()->client_sampling_.denominator());
  EXPECT_EQ(10000, config.tracingConfig()->random_sampling_.numerator());
  EXPECT_EQ(envoy::type::v3::FractionalPercent::TEN_THOUSAND,
            config.tracingConfig()->random_sampling_.denominator());
  EXPECT_EQ(10000, config.tracingConfig()->overall_sampling_.numerator());
  EXPECT_EQ(envoy::type::v3::FractionalPercent::TEN_THOUSAND,
            config.tracingConfig()->overall_sampling_.denominator());
}

TEST_F(HttpConnectionManagerConfigTest, SamplingConfigured) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  internal_address_config:
    unix_sockets: true
  route_config:
    name: local_route
  tracing:
    client_sampling:
      value: 1
    random_sampling:
      value: 2
    overall_sampling:
      value: 3
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string, false),
                                     context_, date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);

  EXPECT_EQ(1, config.tracingConfig()->client_sampling_.numerator());
  EXPECT_EQ(envoy::type::v3::FractionalPercent::HUNDRED,
            config.tracingConfig()->client_sampling_.denominator());
  EXPECT_EQ(200, config.tracingConfig()->random_sampling_.numerator());
  EXPECT_EQ(envoy::type::v3::FractionalPercent::TEN_THOUSAND,
            config.tracingConfig()->random_sampling_.denominator());
  EXPECT_EQ(300, config.tracingConfig()->overall_sampling_.numerator());
  EXPECT_EQ(envoy::type::v3::FractionalPercent::TEN_THOUSAND,
            config.tracingConfig()->overall_sampling_.denominator());
}

TEST_F(HttpConnectionManagerConfigTest, FractionalSamplingConfigured) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  internal_address_config:
    unix_sockets: true
  route_config:
    name: local_route
  tracing:
    client_sampling:
      value: 0.1
    random_sampling:
      value: 0.2
    overall_sampling:
      value: 0.3
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string, false),
                                     context_, date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);

  EXPECT_EQ(0, config.tracingConfig()->client_sampling_.numerator());
  EXPECT_EQ(envoy::type::v3::FractionalPercent::HUNDRED,
            config.tracingConfig()->client_sampling_.denominator());
  EXPECT_EQ(20, config.tracingConfig()->random_sampling_.numerator());
  EXPECT_EQ(envoy::type::v3::FractionalPercent::TEN_THOUSAND,
            config.tracingConfig()->random_sampling_.denominator());
  EXPECT_EQ(30, config.tracingConfig()->overall_sampling_.numerator());
  EXPECT_EQ(envoy::type::v3::FractionalPercent::TEN_THOUSAND,
            config.tracingConfig()->overall_sampling_.denominator());
}

TEST_F(HttpConnectionManagerConfigTest, OverallSampling) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  internal_address_config:
    unix_sockets: true
  route_config:
    name: local_route
  tracing:
    client_sampling:
      value: 0.1
    random_sampling:
      value: 0.1
    overall_sampling:
      value: 0.1
  http_filters:
  - name: envoy.filters.http.router
 )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string, false),
                                     context_, date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);

  Stats::TestUtil::TestStore store;
  Api::ApiPtr api = Api::createApiForTest(store);

  Event::MockDispatcher dispatcher;
  NiceMock<ThreadLocal::MockInstance> tls;
  Random::MockRandomGenerator generator;
  envoy::config::bootstrap::v3::LayeredRuntime runtime_config;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;
  Runtime::LoaderImpl runtime(dispatcher, tls, runtime_config, local_info, store, generator,
                              validation_visitor, *api);

  int sampled_count = 0;
  NiceMock<Router::MockRoute> route;
  Envoy::Random::RandomGeneratorImpl rand;
  for (int i = 0; i < 1000000; i++) {
    Envoy::Http::TestRequestHeaderMapImpl header{{"x-request-id", rand.uuid()}};
    config.requestIDExtension()->setTraceReason(header, Envoy::Tracing::Reason::Sampling);
    auto reason = Envoy::Http::ConnectionManagerUtility::mutateTracingRequestHeader(header, runtime,
                                                                                    config, &route);
    if (reason == Envoy::Tracing::Reason::Sampling) {
      sampled_count++;
    }
  }

  EXPECT_LE(900, sampled_count);
  EXPECT_GE(1100, sampled_count);
}

TEST_F(HttpConnectionManagerConfigTest, UnixSocketInternalAddress) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  internal_address_config:
    unix_sockets: true
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  Network::Address::PipeInstance unixAddress{"/foo"};
  Network::Address::Ipv4Instance internalIpAddress{"127.0.0.1", 0, nullptr};
  Network::Address::Ipv4Instance externalIpAddress{"12.0.0.1", 0, nullptr};
  EXPECT_TRUE(config.internalAddressConfig().isInternalAddress(unixAddress));
  EXPECT_TRUE(config.internalAddressConfig().isInternalAddress(internalIpAddress));
  EXPECT_FALSE(config.internalAddressConfig().isInternalAddress(externalIpAddress));
}

TEST_F(HttpConnectionManagerConfigTest, MaxRequestHeadersKbDefault) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(60, config.maxRequestHeadersKb());
}

TEST_F(HttpConnectionManagerConfigTest, MaxRequestHeadersKbConfigured) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  max_request_headers_kb: 16
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(16, config.maxRequestHeadersKb());
}

TEST_F(HttpConnectionManagerConfigTest, MaxRequestHeadersKbMaxConfigurable) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  max_request_headers_kb: 8192
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(8192, config.maxRequestHeadersKb());
}

// Validated that an explicit zero stream idle timeout disables.
TEST_F(HttpConnectionManagerConfigTest, DisabledStreamIdleTimeout) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  stream_idle_timeout: 0s
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(0, config.streamIdleTimeout().count());
}

// Validate that idle_timeout set in common_http_protocol_options is used.
TEST_F(HttpConnectionManagerConfigTest, CommonHttpProtocolIdleTimeout) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  common_http_protocol_options:
    idle_timeout: 1s
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(1000, config.idleTimeout().value().count());
}

// Validate that idle_timeout defaults to 1h
TEST_F(HttpConnectionManagerConfigTest, CommonHttpProtocolIdleTimeoutDefault) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(std::chrono::hours(1), config.idleTimeout().value());
}

// Validate that idle_timeouts can be turned off
TEST_F(HttpConnectionManagerConfigTest, CommonHttpProtocolIdleTimeoutOff) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  common_http_protocol_options:
    idle_timeout: 0s
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_FALSE(config.idleTimeout().has_value());
}

// Check that the default max request header count is 100.
TEST_F(HttpConnectionManagerConfigTest, DefaultMaxRequestHeaderCount) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(100, config.maxRequestHeadersCount());
}

// Check that max request header count is configured.
TEST_F(HttpConnectionManagerConfigTest, MaxRequestHeaderCountConfigurable) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  common_http_protocol_options:
    max_headers_count: 200
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(200, config.maxRequestHeadersCount());
}

// Checking that default max_requests_per_connection is 0.
TEST_F(HttpConnectionManagerConfigTest, DefaultMaxRequestPerConnection) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(0, config.maxRequestsPerConnection());
}

// Check that max_requests_per_connection is configured.
TEST_F(HttpConnectionManagerConfigTest, MaxRequestPerConnectionConfigurable) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  common_http_protocol_options:
    max_requests_per_connection: 5
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(5, config.maxRequestsPerConnection());
}

TEST_F(HttpConnectionManagerConfigTest, ServerOverwrite) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  server_header_transformation: OVERWRITE
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_CALL(context_.runtime_loader_.snapshot_, featureEnabled(_, An<uint64_t>()))
      .WillRepeatedly(Invoke(&context_.runtime_loader_.snapshot_,
                             &Runtime::MockSnapshot::featureEnabledDefault));
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(HttpConnectionManagerConfig::HttpConnectionManagerProto::OVERWRITE,
            config.serverHeaderTransformation());
}

TEST_F(HttpConnectionManagerConfigTest, ServerAppendIfAbsent) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  server_header_transformation: APPEND_IF_ABSENT
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_CALL(context_.runtime_loader_.snapshot_, featureEnabled(_, An<uint64_t>()))
      .WillRepeatedly(Invoke(&context_.runtime_loader_.snapshot_,
                             &Runtime::MockSnapshot::featureEnabledDefault));
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(HttpConnectionManagerConfig::HttpConnectionManagerProto::APPEND_IF_ABSENT,
            config.serverHeaderTransformation());
}

TEST_F(HttpConnectionManagerConfigTest, ServerPassThrough) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  server_header_transformation: PASS_THROUGH
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_CALL(context_.runtime_loader_.snapshot_, featureEnabled(_, An<uint64_t>()))
      .WillRepeatedly(Invoke(&context_.runtime_loader_.snapshot_,
                             &Runtime::MockSnapshot::featureEnabledDefault));
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(HttpConnectionManagerConfig::HttpConnectionManagerProto::PASS_THROUGH,
            config.serverHeaderTransformation());
}

TEST_F(HttpConnectionManagerConfigTest, SchemeOverwrite) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  scheme_header_transformation:
    scheme_to_overwrite: http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_CALL(context_.runtime_loader_.snapshot_, featureEnabled(_, An<uint64_t>()))
      .WillRepeatedly(Invoke(&context_.runtime_loader_.snapshot_,
                             &Runtime::MockSnapshot::featureEnabledDefault));
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(config.schemeToSet(), "http");
}

// Validated that by default we don't normalize paths
// unless set build flag path_normalization_by_default=true
TEST_F(HttpConnectionManagerConfigTest, NormalizePathDefault) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_CALL(context_.runtime_loader_.snapshot_, featureEnabled(_, An<uint64_t>()))
      .WillRepeatedly(Invoke(&context_.runtime_loader_.snapshot_,
                             &Runtime::MockSnapshot::featureEnabledDefault));
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
#ifdef ENVOY_NORMALIZE_PATH_BY_DEFAULT
  EXPECT_TRUE(config.shouldNormalizePath());
#else
  EXPECT_FALSE(config.shouldNormalizePath());
#endif
}

// Validated that we normalize paths with runtime override when not specified.
TEST_F(HttpConnectionManagerConfigTest, NormalizePathRuntime) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_CALL(context_.runtime_loader_.snapshot_, featureEnabled(_, An<uint64_t>()))
      .WillRepeatedly(Invoke(&context_.runtime_loader_.snapshot_,
                             &Runtime::MockSnapshot::featureEnabledDefault));
  EXPECT_CALL(context_.runtime_loader_.snapshot_,
              featureEnabled("http_connection_manager.normalize_path", An<uint64_t>()))
      .WillOnce(Return(true));
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_TRUE(config.shouldNormalizePath());
}

// Validated that when configured, we normalize paths, ignoring runtime.
TEST_F(HttpConnectionManagerConfigTest, NormalizePathTrue) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  normalize_path: true
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_CALL(context_.runtime_loader_.snapshot_, featureEnabled(_, An<uint64_t>()))
      .WillRepeatedly(Invoke(&context_.runtime_loader_.snapshot_,
                             &Runtime::MockSnapshot::featureEnabledDefault));
  EXPECT_CALL(context_.runtime_loader_.snapshot_,
              featureEnabled("http_connection_manager.normalize_path", An<uint64_t>()))
      .Times(0);
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_TRUE(config.shouldNormalizePath());
}

// Validated that when explicitly set false, we don't normalize, ignoring runtime.
TEST_F(HttpConnectionManagerConfigTest, NormalizePathFalse) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  normalize_path: false
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_CALL(context_.runtime_loader_.snapshot_, featureEnabled(_, An<uint64_t>()))
      .WillRepeatedly(Invoke(&context_.runtime_loader_.snapshot_,
                             &Runtime::MockSnapshot::featureEnabledDefault));
  EXPECT_CALL(context_.runtime_loader_.snapshot_,
              featureEnabled("http_connection_manager.normalize_path", An<uint64_t>()))
      .Times(0);
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_FALSE(config.shouldNormalizePath());
}

// Validated that by default we don't merge slashes.
TEST_F(HttpConnectionManagerConfigTest, MergeSlashesDefault) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_FALSE(config.shouldMergeSlashes());
}

// Validated that when configured, we merge slashes.
TEST_F(HttpConnectionManagerConfigTest, MergeSlashesTrue) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  merge_slashes: true
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_TRUE(config.shouldMergeSlashes());
}

// Validated that when explicitly set false, we don't merge slashes.
TEST_F(HttpConnectionManagerConfigTest, MergeSlashesFalse) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  merge_slashes: false
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_FALSE(config.shouldMergeSlashes());
}

// Validated that by default we don't remove port.
TEST_F(HttpConnectionManagerConfigTest, RemovePortDefault) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(Http::StripPortType::None, config.stripPortType());
}

// Validated that when configured, we remove port.
TEST_F(HttpConnectionManagerConfigTest, RemovePortTrue) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  strip_matching_host_port: true
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(Http::StripPortType::MatchingHost, config.stripPortType());
}

// Validated that when both strip options are configured, we throw exception.
TEST_F(HttpConnectionManagerConfigTest, BothStripOptionsAreSet) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  strip_matching_host_port: true
  strip_any_host_port: true
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      HttpConnectionManagerConfig(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                  date_provider_, route_config_provider_manager_,
                                  scoped_routes_config_provider_manager_, http_tracer_manager_,
                                  filter_config_provider_manager_),
      EnvoyException,
      "Error: Only one of `strip_matching_host_port` or `strip_any_host_port` can be set.");
}

// Validated that when explicitly set false, we don't remove port.
TEST_F(HttpConnectionManagerConfigTest, RemovePortFalse) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  strip_matching_host_port: false
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(Http::StripPortType::None, config.stripPortType());
}

// Validated that when configured, we remove any port.
TEST_F(HttpConnectionManagerConfigTest, RemoveAnyPortTrue) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  strip_any_host_port: true
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(Http::StripPortType::Any, config.stripPortType());
}

// Validated that when explicitly set false, we don't remove any port.
TEST_F(HttpConnectionManagerConfigTest, RemoveAnyPortFalse) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  strip_any_host_port: false
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(Http::StripPortType::None, config.stripPortType());
}

// Validated that by default we don't remove host's trailing dot.
TEST_F(HttpConnectionManagerConfigTest, RemoveTrailingDotDefault) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(false, config.shouldStripTrailingHostDot());
}

// Validated that when configured, we remove host's trailing dot.
TEST_F(HttpConnectionManagerConfigTest, RemoveTrailingDotTrue) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  strip_trailing_host_dot: true
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(true, config.shouldStripTrailingHostDot());
}

// Validated that when explicitly set false, then we don't remove trailing host dot.
TEST_F(HttpConnectionManagerConfigTest, RemoveTrailingDotFalse) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  strip_trailing_host_dot: false
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(false, config.shouldStripTrailingHostDot());
}

// Validated that by default we allow requests with header names containing underscores.
TEST_F(HttpConnectionManagerConfigTest, HeadersWithUnderscoresAllowedByDefault) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(envoy::config::core::v3::HttpProtocolOptions::ALLOW,
            config.headersWithUnderscoresAction());
}

// Validated that when configured, we drop headers with underscores.
TEST_F(HttpConnectionManagerConfigTest, HeadersWithUnderscoresDroppedByConfig) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  common_http_protocol_options:
    headers_with_underscores_action: DROP_HEADER
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(envoy::config::core::v3::HttpProtocolOptions::DROP_HEADER,
            config.headersWithUnderscoresAction());
}

// Validated that when configured, we reject requests with header names containing underscores.
TEST_F(HttpConnectionManagerConfigTest, HeadersWithUnderscoresRequestRejectedByConfig) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  common_http_protocol_options:
    headers_with_underscores_action: REJECT_REQUEST
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(envoy::config::core::v3::HttpProtocolOptions::REJECT_REQUEST,
            config.headersWithUnderscoresAction());
}

TEST_F(HttpConnectionManagerConfigTest, ConfiguredRequestTimeout) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  request_timeout: 53s
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(53 * 1000, config.requestTimeout().count());
}

TEST_F(HttpConnectionManagerConfigTest, DisabledRequestTimeout) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  request_timeout: 0s
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(0, config.requestTimeout().count());
}

TEST_F(HttpConnectionManagerConfigTest, UnconfiguredRequestTimeout) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(0, config.requestTimeout().count());
}

TEST_F(HttpConnectionManagerConfigTest, SingleDateProvider) {
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
- name: encoder-decoder-buffer-filter
  typed_config:
    "@type": type.googleapis.com/google.protobuf.Empty
- name: envoy.filters.http.router
  )EOF";

  context_.server_factory_context_.cluster_manager_.initializeClusters({"cluster"}, {});
  auto proto_config = parseHttpConnectionManagerFromYaml(yaml_string);
  HttpConnectionManagerFilterConfigFactory factory;
  // We expect a single slot allocation vs. multiple.
  EXPECT_CALL(context_.thread_local_, allocateSlot());
  Network::FilterFactoryCb cb1 = factory.createFilterFactoryFromProto(proto_config, context_);
  Network::FilterFactoryCb cb2 = factory.createFilterFactoryFromProto(proto_config, context_);
  EXPECT_TRUE(factory.isTerminalFilterByProto(proto_config, context_));
}

TEST_F(HttpConnectionManagerConfigTest, BadHttpConnectionMangerConfig) {
  std::string yaml_string = R"EOF(
codec_type: http1
stat_prefix: my_stat_prefix
route_config:
  virtual_hosts:
  - name: default
    domains:
    - "*"
    routes:
    - match:
        prefix: "/"
      route:
        cluster: fake_cluster
filter:
- {}
  )EOF";

  EXPECT_THROW(parseHttpConnectionManagerFromYaml(yaml_string), EnvoyException);
}

TEST_F(HttpConnectionManagerConfigTest, BadAccessLogConfig) {
  std::string yaml_string = R"EOF(
codec_type: http1
stat_prefix: my_stat_prefix
route_config:
  virtual_hosts:
  - name: default
    domains:
    - "*"
    routes:
    - match:
        prefix: "/"
      route:
        cluster: fake_cluster
http_filters:
- name: encoder-decoder-buffer-filter
  config: {}
access_log:
- name: accesslog
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
    path: "/dev/null"
  filter: []
  )EOF";

  EXPECT_THROW_WITH_REGEX(parseHttpConnectionManagerFromYaml(yaml_string), EnvoyException,
                          "filter: Proto field is not repeating, cannot start list.");
}

TEST_F(HttpConnectionManagerConfigTest, BadAccessLogType) {
  std::string yaml_string = R"EOF(
codec_type: http1
stat_prefix: my_stat_prefix
route_config:
  virtual_hosts:
  - name: default
    domains:
    - "*"
    routes:
    - match:
        prefix: "/"
      route:
        cluster: fake_cluster
http_filters:
- name: encoder-decoder-buffer-filter
access_log:
- name: accesslog
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
    path: "/dev/null"
  filter:
    bad_type: {}
  )EOF";

  EXPECT_THROW_WITH_REGEX(parseHttpConnectionManagerFromYaml(yaml_string), EnvoyException,
                          "bad_type: Cannot find field");
}

TEST_F(HttpConnectionManagerConfigTest, BadAccessLogNestedTypes) {
  std::string yaml_string = R"EOF(
codec_type: http1
stat_prefix: my_stat_prefix
route_config:
  virtual_hosts:
  - name: default
    domains:
    - "*"
    routes:
    - match:
        prefix: "/"
      route:
        cluster: fake_cluster
http_filters:
- name: encoder-decoder-buffer-filter
access_log:
- name: accesslog
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
    path: "/dev/null"
  filter:
    and_filter:
      filters:
      - or_filter:
          filters:
          - duration_filter:
              op: ">="
              value: 10000
          - bad_type: {}
      - not_health_check_filter: {}
  )EOF";

  EXPECT_THROW_WITH_REGEX(parseHttpConnectionManagerFromYaml(yaml_string), EnvoyException,
                          "bad_type: Cannot find field");
}

// Validates that HttpConnectionManagerConfig construction succeeds when there are no collisions
// between named and user defined parameters, and server push is not modified.
TEST_F(HttpConnectionManagerConfigTest, UserDefinedSettingsNoCollision) {
  const std::string yaml_string = R"EOF(
codec_type: http2
stat_prefix: my_stat_prefix
route_config:
  virtual_hosts:
  - name: default
    domains:
    - "*"
    routes:
    - match:
        prefix: "/"
      route:
        cluster: fake_cluster
http_filters:
- name: envoy.filters.http.router
http2_protocol_options:
  hpack_table_size: 1024
  custom_settings_parameters: { identifier: 3, value: 2048 }
  )EOF";
  // This will throw when Http2ProtocolOptions validation fails.
  createHttpConnectionManagerConfig(yaml_string);
}

// Validates that named and user defined parameter collisions will trigger a config validation
// failure.
TEST_F(HttpConnectionManagerConfigTest, UserDefinedSettingsNamedParameterCollision) {
  // Override both hpack_table_size (id = 0x1) and max_concurrent_streams (id = 0x3) with custom
  // parameters of the same and different values (respectively).
  const std::string yaml_string = R"EOF(
codec_type: http2
stat_prefix: my_stat_prefix
route_config:
  virtual_hosts:
  - name: default
    domains:
    - "*"
    routes:
    - match:
        prefix: "/"
      route:
        cluster: fake_cluster
http_filters:
- name: encoder-decoder-buffer-filter
http2_protocol_options:
  hpack_table_size: 2048
  max_concurrent_streams: 4096
  custom_settings_parameters:
    - { identifier: 1, value: 2048 }
    - { identifier: 3, value: 1024 }
  )EOF";
  EXPECT_THROW_WITH_REGEX(
      createHttpConnectionManagerConfig(yaml_string), EnvoyException,
      R"(the \{hpack_table_size,max_concurrent_streams\} HTTP/2 SETTINGS parameter\(s\) can not be)"
      " configured");
}

// Validates that `allow_connect` can only be configured through the named field. All other
// SETTINGS parameters can be set via the named _or_ custom parameters fields, but `allow_connect`
// required an exception due to the use of a primitive type which does not support presence
// checks.
TEST_F(HttpConnectionManagerConfigTest, UserDefinedSettingsAllowConnectOnlyViaNamedField) {
  const std::string yaml_string = R"EOF(
codec_type: http2
stat_prefix: my_stat_prefix
route_config:
  virtual_hosts:
  - name: default
    domains:
    - "*"
    routes:
    - match:
        prefix: "/"
      route:
        cluster: fake_cluster
http_filters:
- name: envoy.filters.http.router
http2_protocol_options:
  custom_settings_parameters:
    - { identifier: 8, value: 0 }
  )EOF";
  EXPECT_THROW_WITH_REGEX(
      createHttpConnectionManagerConfig(yaml_string), EnvoyException,
      "the \"allow_connect\" SETTINGS parameter must only be configured through the named field");

  const std::string yaml_string2 = R"EOF(
codec_type: http2
stat_prefix: my_stat_prefix
route_config:
  virtual_hosts:
  - name: default
    domains:
    - "*"
    routes:
    - match:
        prefix: "/"
      route:
        cluster: fake_cluster
http_filters:
- name: envoy.filters.http.router
http2_protocol_options:
  allow_connect: true
  )EOF";
  createHttpConnectionManagerConfig(yaml_string2);
}

// Validates that setting the server push parameter via user defined parameters is disallowed.
TEST_F(HttpConnectionManagerConfigTest, UserDefinedSettingsDisallowServerPush) {
  const std::string yaml_string = R"EOF(
codec_type: http2
stat_prefix: my_stat_prefix
route_config:
  virtual_hosts:
  - name: default
    domains:
    - "*"
    routes:
    - match:
        prefix: "/"
      route:
        cluster: fake_cluster
http_filters:
- name: encoder-decoder-buffer-filter
http2_protocol_options:
  custom_settings_parameters: { identifier: 2, value: 1 }
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      createHttpConnectionManagerConfig(yaml_string), EnvoyException,
      "server push is not supported by Envoy and can not be enabled via a SETTINGS parameter.");

  // Specify both the server push parameter and colliding named and user defined parameters.
  const std::string yaml_string2 = R"EOF(
codec_type: http2
stat_prefix: my_stat_prefix
route_config:
  virtual_hosts:
  - name: default
    domains:
    - "*"
    routes:
    - match:
        prefix: "/"
      route:
        cluster: fake_cluster
http_filters:
- name: encoder-decoder-buffer-filter
http2_protocol_options:
  hpack_table_size: 2048
  max_concurrent_streams: 4096
  custom_settings_parameters:
    - { identifier: 1, value: 2048 }
    - { identifier: 2, value: 1 }
    - { identifier: 3, value: 1024 }
  )EOF";

  // The server push exception is thrown first.
  EXPECT_THROW_WITH_REGEX(
      createHttpConnectionManagerConfig(yaml_string), EnvoyException,
      "server push is not supported by Envoy and can not be enabled via a SETTINGS parameter.");
}

// Validates that inconsistent custom parameters are rejected.
TEST_F(HttpConnectionManagerConfigTest, UserDefinedSettingsRejectInconsistentCustomParameters) {
  const std::string yaml_string = R"EOF(
codec_type: http2
stat_prefix: my_stat_prefix
route_config:
  virtual_hosts:
  - name: default
    domains:
    - "*"
    routes:
    - match:
        prefix: "/"
      route:
        cluster: fake_cluster
http_filters:
- name: envoy.filters.http.router
http2_protocol_options:
  custom_settings_parameters:
    - { identifier: 10, value: 0 }
    - { identifier: 10, value: 1 }
    - { identifier: 12, value: 10 }
    - { identifier: 14, value: 1 }
    - { identifier: 12, value: 10 }
  )EOF";
  EXPECT_THROW_WITH_REGEX(
      createHttpConnectionManagerConfig(yaml_string), EnvoyException,
      R"(inconsistent HTTP/2 custom SETTINGS parameter\(s\) detected; identifiers = \{0x0a\})");
}

// Test that the deprecated extension name still functions.
TEST_F(HttpConnectionManagerConfigTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.http_connection_manager";

  ASSERT_NE(
      nullptr,
      Registry::FactoryRegistry<Server::Configuration::NamedNetworkFilterConfigFactory>::getFactory(
          deprecated_name));
}

TEST_F(HttpConnectionManagerConfigTest, AlwaysSetRequestIdInResponseDefault) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_FALSE(config.alwaysSetRequestIdInResponse());
}

TEST_F(HttpConnectionManagerConfigTest, AlwaysSetRequestIdInResponseConfigured) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  always_set_request_id_in_response: true
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_TRUE(config.alwaysSetRequestIdInResponse());
}

namespace {

class TestRequestIDExtension : public Http::RequestIDExtension {
public:
  TestRequestIDExtension(const test::http_connection_manager::CustomRequestIDExtension& config)
      : config_(config) {}

  void set(Http::RequestHeaderMap&, bool) override {}
  void setInResponse(Http::ResponseHeaderMap&, const Http::RequestHeaderMap&) override {}
  absl::optional<uint64_t> toInteger(const Http::RequestHeaderMap&) const override {
    return absl::nullopt;
  }
  Tracing::Reason getTraceReason(const Http::RequestHeaderMap&) override {
    return Tracing::Reason::Sampling;
  }
  void setTraceReason(Http::RequestHeaderMap&, Tracing::Reason) override {}
  bool useRequestIdForTraceSampling() const override { return true; }
  std::string testField() { return config_.test_field(); }

private:
  test::http_connection_manager::CustomRequestIDExtension config_;
};

class TestRequestIDExtensionFactory : public Server::Configuration::RequestIDExtensionFactory {
public:
  std::string name() const override {
    return "test.http_connection_manager.CustomRequestIDExtension";
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::http_connection_manager::CustomRequestIDExtension>();
  }

  Http::RequestIDExtensionSharedPtr
  createExtensionInstance(const Protobuf::Message& config,
                          Server::Configuration::FactoryContext& context) override {
    const auto& custom_config = MessageUtil::downcastAndValidate<
        const test::http_connection_manager::CustomRequestIDExtension&>(
        config, context.messageValidationVisitor());
    return std::make_shared<TestRequestIDExtension>(custom_config);
  }
};

} // namespace

TEST_F(HttpConnectionManagerConfigTest, CustomRequestIDExtension) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  request_id_extension:
    typed_config:
      "@type": type.googleapis.com/test.http_connection_manager.CustomRequestIDExtension
      test_field: example
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  TestRequestIDExtensionFactory factory;
  Registry::InjectFactory<Server::Configuration::RequestIDExtensionFactory> registration(factory);

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  auto request_id_extension =
      dynamic_cast<TestRequestIDExtension*>(config.requestIDExtension().get());
  ASSERT_NE(nullptr, request_id_extension);
  EXPECT_EQ("example", request_id_extension->testField());
}

TEST_F(HttpConnectionManagerConfigTest, UnknownRequestIDExtension) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  request_id_extension:
    typed_config:
      "@type": type.googleapis.com/test.http_connection_manager.UnknownRequestIDExtension
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_THROW_WITH_REGEX(createHttpConnectionManagerConfig(yaml_string), EnvoyException,
                          "Didn't find a registered implementation for type");
}

TEST_F(HttpConnectionManagerConfigTest, UnknownHttpFilterWithException) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.unknown
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      createHttpConnectionManagerConfig(yaml_string), EnvoyException,
      "Didn't find a registered implementation for name: 'envoy.filters.http.unknown");
}

TEST_F(HttpConnectionManagerConfigTest, UnknownOptionalHttpFilterWithIgnore) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.unknown
    is_optional: true
  )EOF";

  createHttpConnectionManagerConfig(yaml_string);
}

TEST_F(HttpConnectionManagerConfigTest, DefaultRequestIDExtension) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  auto request_id_extension = dynamic_cast<Extensions::RequestId::UUIDRequestIDExtension*>(
      config.requestIDExtension().get());
  ASSERT_NE(nullptr, request_id_extension);
  EXPECT_TRUE(request_id_extension->packTraceReason());
  EXPECT_EQ(request_id_extension->useRequestIdForTraceSampling(), true);
}

TEST_F(HttpConnectionManagerConfigTest, DefaultRequestIDExtensionWithParams) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  request_id_extension:
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.request_id.uuid.v3.UuidRequestIdConfig
      pack_trace_reason: false
      use_request_id_for_trace_sampling: false
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  auto request_id_extension = dynamic_cast<Extensions::RequestId::UUIDRequestIDExtension*>(
      config.requestIDExtension().get());
  ASSERT_NE(nullptr, request_id_extension);
  EXPECT_FALSE(request_id_extension->packTraceReason());
  EXPECT_EQ(request_id_extension->useRequestIdForTraceSampling(), false);
}

TEST_F(HttpConnectionManagerConfigTest, UnknownOriginalIPDetectionExtension) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  original_ip_detection_extensions:
  - name: envoy.http.original_ip_detection.UnknownOriginalIPDetectionExtension
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_THROW_WITH_REGEX(createHttpConnectionManagerConfig(yaml_string), EnvoyException,
                          "Original IP detection extension not found: "
                          "'envoy.http.original_ip_detection.UnknownOriginalIPDetectionExtension'");
}

namespace {

class OriginalIPDetectionExtensionNotCreatedFactory : public Http::OriginalIPDetectionFactory {
public:
  Http::OriginalIPDetectionSharedPtr
  createExtension(const Protobuf::Message&, Server::Configuration::FactoryContext&) override {
    return nullptr;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::UInt32Value>();
  }

  std::string name() const override {
    return "envoy.http.original_ip_detection.OriginalIPDetectionExtensionNotCreated";
  }
};

} // namespace

TEST_F(HttpConnectionManagerConfigTest, OriginalIPDetectionExtensionNotCreated) {
  OriginalIPDetectionExtensionNotCreatedFactory factory;
  Registry::InjectFactory<Http::OriginalIPDetectionFactory> registration(factory);

  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  original_ip_detection_extensions:
  - name: envoy.http.original_ip_detection.OriginalIPDetectionExtensionNotCreated
    typed_config:
      "@type": type.googleapis.com/google.protobuf.UInt32Value
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      createHttpConnectionManagerConfig(yaml_string), EnvoyException,
      "Original IP detection extension could not be created: "
      "'envoy.http.original_ip_detection.OriginalIPDetectionExtensionNotCreated'");
}

TEST_F(HttpConnectionManagerConfigTest, OriginalIPDetectionExtension) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  original_ip_detection_extensions:
  - name: envoy.http.original_ip_detection.custom_header
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.original_ip_detection.custom_header.v3.CustomHeaderConfig
      header_name: x-ip-header
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);

  const auto& original_ip_detection_extensions = config.originalIpDetectionExtensions();
  EXPECT_EQ(1, original_ip_detection_extensions.size());
}

TEST_F(HttpConnectionManagerConfigTest, OriginalIPDetectionExtensionMixedWithUseRemoteAddress) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  use_remote_address: true
  original_ip_detection_extensions:
  - name: envoy.http.original_ip_detection.custom_header
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.original_ip_detection.custom_header.v3.CustomHeaderConfig
      header_name: x-ip-header
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      createHttpConnectionManagerConfig(yaml_string), EnvoyException,
      "Original IP detection extensions and use_remote_address may not be mixed");
}

TEST_F(HttpConnectionManagerConfigTest, OriginalIPDetectionExtensionMixedWithNumTrustedHops) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  xff_num_trusted_hops: 1
  original_ip_detection_extensions:
  - name: envoy.http.original_ip_detection.custom_header
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.original_ip_detection.custom_header.v3.CustomHeaderConfig
      header_name: x-ip-header
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      createHttpConnectionManagerConfig(yaml_string), EnvoyException,
      "Original IP detection extensions and xff_num_trusted_hops may not be mixed");
}

TEST_F(HttpConnectionManagerConfigTest, DynamicFilterWarmingNoDefault) {
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
    apply_default_config_without_warming: true
    type_urls:
    - type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      createHttpConnectionManagerConfig(yaml_string), EnvoyException,
      "Error: filter config foo applied without warming but has no default config.");
}

TEST_F(HttpConnectionManagerConfigTest, DynamicFilterBadDefault) {
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
    default_config:
      "@type": type.googleapis.com/google.protobuf.Value
    type_urls:
    - type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      createHttpConnectionManagerConfig(yaml_string), EnvoyException,
      "Error: cannot find filter factory foo for default filter configuration with type URL "
      "type.googleapis.com/google.protobuf.Value.");
}

TEST_F(HttpConnectionManagerConfigTest, DynamicFilterDefaultNotTerminal) {
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
    default_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.health_check.v3.HealthCheck
      pass_through_mode: false
    type_urls:
    - type.googleapis.com/envoy.extensions.filters.http.health_check.v3.HealthCheck
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      createHttpConnectionManagerConfig(yaml_string), EnvoyException,
      "Error: non-terminal filter named foo of type envoy.filters.http.health_check is the last "
      "filter in a http filter chain.");
}

TEST_F(HttpConnectionManagerConfigTest, DynamicFilterDefaultTerminal) {
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
    default_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
    type_urls:
    - type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
- name: envoy.filters.http.router
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(createHttpConnectionManagerConfig(yaml_string), EnvoyException,
                            "Error: terminal filter named foo of type envoy.filters.http.router "
                            "must be the last filter in a http filter chain.");
}

TEST_F(HttpConnectionManagerConfigTest, DynamicFilterDefaultRequireTypeUrl) {
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
    default_config:
      "@type": type.googleapis.com/udpa.type.v1.TypedStruct
      type_url: type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
    type_urls:
    - type.googleapis.com/envoy.extensions.filters.http.health_check.v3.HealthCheck
- name: envoy.filters.http.router
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      createHttpConnectionManagerConfig(yaml_string), EnvoyException,
      "Error: filter config has type URL envoy.extensions.filters.http.router.v3.Router but "
      "expect envoy.extensions.filters.http.health_check.v3.HealthCheck.");
}

TEST_F(HttpConnectionManagerConfigTest, DynamicFilterRequireTypeUrlMissingFactory) {
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
    - type.googleapis.com/google.protobuf.Value
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      createHttpConnectionManagerConfig(yaml_string), EnvoyException,
      "Error: no factory found for a required type URL google.protobuf.Value.");
}

TEST_F(HttpConnectionManagerConfigTest, DynamicFilterDefaultValid) {
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
    default_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.health_check.v3.HealthCheck
      pass_through_mode: false
    type_urls:
    - type.googleapis.com/envoy.extensions.filters.http.health_check.v3.HealthCheck
    apply_default_config_without_warming: true
- name: envoy.filters.http.router
  )EOF";

  createHttpConnectionManagerConfig(yaml_string);
}

TEST_F(HttpConnectionManagerConfigTest, PathWithEscapedSlashesActionDefault) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_CALL(context_.runtime_loader_.snapshot_,
              featureEnabled(_, An<const envoy::type::v3::FractionalPercent&>()))
      .WillOnce(Return(true));
  EXPECT_CALL(context_.runtime_loader_.snapshot_, getInteger(_, _)).Times(AnyNumber());
  EXPECT_CALL(context_.runtime_loader_.snapshot_,
              getInteger("http_connection_manager.path_with_escaped_slashes_action", 0))
      .WillOnce(Return(0));
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(envoy::extensions::filters::network::http_connection_manager::v3::
                HttpConnectionManager::KEEP_UNCHANGED,
            config.pathWithEscapedSlashesAction());
}

TEST_F(HttpConnectionManagerConfigTest, PathWithEscapedSlashesActionDefaultOverriden) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  path_with_escaped_slashes_action: IMPLEMENTATION_SPECIFIC_DEFAULT
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_CALL(context_.runtime_loader_.snapshot_,
              featureEnabled(_, An<const envoy::type::v3::FractionalPercent&>()))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(context_.runtime_loader_.snapshot_, getInteger(_, _)).Times(AnyNumber());
  EXPECT_CALL(context_.runtime_loader_.snapshot_,
              getInteger("http_connection_manager.path_with_escaped_slashes_action", 0))
      .WillOnce(Return(3));
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(envoy::extensions::filters::network::http_connection_manager::v3::
                HttpConnectionManager::UNESCAPE_AND_REDIRECT,
            config.pathWithEscapedSlashesAction());

  // Check the UNESCAPE_AND_FORWARD override to mollify coverage check
  EXPECT_CALL(context_.runtime_loader_.snapshot_,
              getInteger("http_connection_manager.path_with_escaped_slashes_action", 0))
      .WillOnce(Return(4));
  HttpConnectionManagerConfig config1(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                      date_provider_, route_config_provider_manager_,
                                      scoped_routes_config_provider_manager_, http_tracer_manager_,
                                      filter_config_provider_manager_);
  EXPECT_EQ(envoy::extensions::filters::network::http_connection_manager::v3::
                HttpConnectionManager::UNESCAPE_AND_FORWARD,
            config1.pathWithEscapedSlashesAction());
}

// Verify that runtime override does not affect non default configuration value.
TEST_F(HttpConnectionManagerConfigTest,
       PathWithEscapedSlashesActionRuntimeOverrideDoesNotChangeNonDefaultConfigValue) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  path_with_escaped_slashes_action: REJECT_REQUEST
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_CALL(context_.runtime_loader_.snapshot_,
              featureEnabled(_, An<const envoy::type::v3::FractionalPercent&>()))
      .WillOnce(Return(true));
  EXPECT_CALL(context_.runtime_loader_.snapshot_, getInteger(_, _)).Times(AnyNumber());
  // When configuration value is not the IMPLEMENTATION_SPECIFIC_DEFAULT the runtime override should
  // not even be considered.
  EXPECT_CALL(context_.runtime_loader_.snapshot_,
              getInteger("http_connection_manager.path_with_escaped_slashes_action", 0))
      .Times(0);
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(envoy::extensions::filters::network::http_connection_manager::v3::
                HttpConnectionManager::REJECT_REQUEST,
            config.pathWithEscapedSlashesAction());
}

// Verify that disabling unescaping slashes results in the KEEP_UNCHANGED action when config is
// value is not set.
TEST_F(HttpConnectionManagerConfigTest, PathWithEscapedSlashesActionDefaultOverridenAndDisabled) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_CALL(context_.runtime_loader_.snapshot_, featureEnabled(_, An<uint64_t>()))
      .WillRepeatedly(Invoke(&context_.runtime_loader_.snapshot_,
                             &Runtime::MockSnapshot::featureEnabledDefault));
  EXPECT_CALL(context_.runtime_loader_.snapshot_,
              featureEnabled("http_connection_manager.path_with_escaped_slashes_action_enabled",
                             An<const envoy::type::v3::FractionalPercent&>()))
      .WillOnce(Return(false));
  EXPECT_CALL(context_.runtime_loader_.snapshot_, getInteger(_, _)).Times(AnyNumber());
  EXPECT_CALL(context_.runtime_loader_.snapshot_,
              getInteger("http_connection_manager.path_with_escaped_slashes_action", 0))
      .Times(0);
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(envoy::extensions::filters::network::http_connection_manager::v3::
                HttpConnectionManager::KEEP_UNCHANGED,
            config.pathWithEscapedSlashesAction());
}

// Verify that disabling unescaping slashes results in the KEEP_UNCHANGED action when config is
// value is set.
TEST_F(HttpConnectionManagerConfigTest, PathWithEscapedSlashesActionSetAndDisabled) {
  const std::string yaml_string = R"EOF(
  stat_prefix: ingress_http
  path_with_escaped_slashes_action: UNESCAPE_AND_REDIRECT
  route_config:
    name: local_route
  http_filters:
  - name: envoy.filters.http.router
  )EOF";

  EXPECT_CALL(context_.runtime_loader_.snapshot_, featureEnabled(_, An<uint64_t>()))
      .WillRepeatedly(Invoke(&context_.runtime_loader_.snapshot_,
                             &Runtime::MockSnapshot::featureEnabledDefault));
  EXPECT_CALL(context_.runtime_loader_.snapshot_,
              featureEnabled("http_connection_manager.path_with_escaped_slashes_action_enabled",
                             An<const envoy::type::v3::FractionalPercent&>()))
      .WillOnce(Return(false));
  EXPECT_CALL(context_.runtime_loader_.snapshot_, getInteger(_, _)).Times(AnyNumber());
  EXPECT_CALL(context_.runtime_loader_.snapshot_,
              getInteger("http_connection_manager.path_with_escaped_slashes_action", 0))
      .Times(0);
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml_string), context_,
                                     date_provider_, route_config_provider_manager_,
                                     scoped_routes_config_provider_manager_, http_tracer_manager_,
                                     filter_config_provider_manager_);
  EXPECT_EQ(envoy::extensions::filters::network::http_connection_manager::v3::
                HttpConnectionManager::KEEP_UNCHANGED,
            config.pathWithEscapedSlashesAction());
}

class HcmUtilityTest : public testing::Test {
public:
  HcmUtilityTest() {
    // Although different Listeners will have separate FactoryContexts,
    // those contexts must share the same SingletonManager.
    ON_CALL(context_two_, singletonManager()).WillByDefault([&]() -> Singleton::Manager& {
      return *context_one_.singleton_manager_;
    });
  }
  NiceMock<Server::Configuration::MockFactoryContext> context_one_;
  NiceMock<Server::Configuration::MockFactoryContext> context_two_;
};

TEST_F(HcmUtilityTest, EnsureCreateSingletonsActuallyReturnsTheSameInstances) {
  // Simulate `HttpConnectionManagerFilterConfigFactory::createFilterFactoryFromProtoTyped()`
  // call for filter instance "one".
  auto singletons_one = Utility::createSingletons(context_one_);

  EXPECT_THAT(singletons_one.date_provider_.get(), NotNull());
  EXPECT_THAT(singletons_one.route_config_provider_manager_.get(), NotNull());
  EXPECT_THAT(singletons_one.scoped_routes_config_provider_manager_.get(), NotNull());
  EXPECT_THAT(singletons_one.http_tracer_manager_.get(), NotNull());

  // Simulate `HttpConnectionManagerFilterConfigFactory::createFilterFactoryFromProtoTyped()`
  // call for filter instance "two".
  auto singletons_two = Utility::createSingletons(context_two_);

  // Ensure that returned values are still the same, even though the context has changed.
  EXPECT_EQ(singletons_two.date_provider_, singletons_one.date_provider_);
  EXPECT_EQ(singletons_two.route_config_provider_manager_,
            singletons_one.route_config_provider_manager_);
  EXPECT_EQ(singletons_two.scoped_routes_config_provider_manager_,
            singletons_one.scoped_routes_config_provider_manager_);
  EXPECT_EQ(singletons_two.http_tracer_manager_, singletons_one.http_tracer_manager_);
}

class HttpConnectionManagerMobileConfigTest : public HttpConnectionManagerConfigTest,
                                              public Event::TestUsingSimulatedTime {};

TEST_F(HttpConnectionManagerMobileConfigTest, Mobile) {
  const std::string yaml_string = R"EOF(
  config:
    stat_prefix: ingress_http
    route_config:
      name: local_route
    http_filters:
    - name: envoy.filters.http.router
  )EOF";

  envoy::extensions::filters::network::http_connection_manager::v3::EnvoyMobileHttpConnectionManager
      config;
  TestUtility::loadFromYamlAndValidate(yaml_string, config, false, true);

  MobileHttpConnectionManagerFilterConfigFactory factory;
  Network::FilterFactoryCb create_hcm_cb = factory.createFilterFactoryFromProto(config, context_);

  NiceMock<Network::MockFilterManager> fm;
  NiceMock<Network::MockReadFilterCallbacks> cb;
  Network::ReadFilterSharedPtr hcm_filter;
  Http::ConnectionManagerImpl* hcm = nullptr;
  EXPECT_CALL(fm, addReadFilter(_))
      .WillOnce(Invoke([&](Network::ReadFilterSharedPtr manager) -> void {
        hcm_filter = manager;
        hcm = dynamic_cast<Http::ConnectionManagerImpl*>(manager.get());
      }));
  create_hcm_cb(fm);
  ASSERT(hcm != nullptr);
  hcm->initializeReadFilterCallbacks(cb);
  EXPECT_FALSE(hcm->clearHopByHopResponseHeaders());
}

} // namespace
} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
