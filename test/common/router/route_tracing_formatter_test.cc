#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/router/config_impl.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

using testing::NiceMock;
using testing::StrEq;

class TestConfigImpl : public ConfigImpl {
public:
  using ConfigImpl::route;

  TestConfigImpl(const envoy::config::route::v3::RouteConfiguration& config,
                 Server::Configuration::ServerFactoryContext& factory_context,
                 bool validate_clusters_default, absl::Status& creation_status)
      : ConfigImpl(config, factory_context, ProtobufMessage::getNullValidationVisitor(),
                   validate_clusters_default, creation_status) {}

  VirtualHostRoute route(const Http::RequestHeaderMap& headers, uint64_t random_value) const {
    return ConfigImpl::route(headers, NiceMock<Envoy::StreamInfo::MockStreamInfo>(), random_value);
  }
};

TEST(RouteTracingFormatterTest, FormattersEnableCelStringFunctions) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains: ["*"]
  routes:
  - match:
      prefix: "/"
    tracing:
      operation: "%CEL(request.headers['x-operation'].replace('original', 'mutated'))%"
      upstream_operation: "%CEL(request.headers['x-upstream-operation'].replace('original', 'mutated'))%"
      custom_tags:
      - tag: trace_tag
        value: "%CEL(request.headers['x-trace-tag'].replace('original', 'mutated'))%"
      formatters:
      - name: envoy.formatter.cel
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.formatter.cel.v3.Cel
          cel_config:
            enable_string_functions: true
    route:
      cluster: cluster
  )EOF";

  envoy::config::route::v3::RouteConfiguration proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  factory_context.cluster_manager_.initializeClusters({"cluster"}, {});
  absl::Status creation_status = absl::OkStatus();
  TestConfigImpl config(proto_config, factory_context, true, creation_status);
  ASSERT_TRUE(creation_status.ok()) << creation_status;

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl headers{
      {":authority", "example.com"},
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "https"},
      {"x-forwarded-proto", "https"},
      {"x-operation", "original-operation"},
      {"x-upstream-operation", "original-upstream-operation"},
      {"x-trace-tag", "original-tag"},
  };
  const RouteConstSharedPtr route = config.route(headers, 0).route;
  ASSERT_NE(nullptr, route);
  ASSERT_NE(nullptr, route->tracingConfig());

  Formatter::Context formatter_context{&headers};
  EXPECT_EQ("mutated-operation",
            route->tracingConfig()->operation()->format(formatter_context, stream_info));
  EXPECT_EQ("mutated-upstream-operation",
            route->tracingConfig()->upstreamOperation()->format(formatter_context, stream_info));

  const Tracing::CustomTagMap& custom_tags = route->tracingConfig()->getCustomTags();
  const auto custom_tag = custom_tags.find("trace_tag");
  ASSERT_NE(custom_tags.end(), custom_tag);

  NiceMock<Tracing::MockSpan> span;
  Tracing::TestTraceContextImpl trace_context;
  const Tracing::CustomTagContext tag_context{trace_context, stream_info, formatter_context};
  EXPECT_CALL(span, setTag(StrEq("trace_tag"), StrEq("mutated-tag")));
  custom_tag->second->applySpan(span, tag_context);
}

TEST(RouteTracingFormatterTest, FormatterConfigDisablesCelStringFunctions) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains: ["*"]
  routes:
  - match:
      prefix: "/"
    tracing:
      operation: "%CEL(request.headers['x-operation'].replace('original', 'mutated'))%"
      formatters:
      - name: envoy.formatter.cel
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.formatter.cel.v3.Cel
          cel_config:
            enable_string_functions: false
    route:
      cluster: cluster
  )EOF";

  envoy::config::route::v3::RouteConfiguration proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  factory_context.cluster_manager_.initializeClusters({"cluster"}, {});
  absl::Status creation_status = absl::OkStatus();
  EXPECT_THROW(TestConfigImpl(proto_config, factory_context, true, creation_status),
               EnvoyException);
}

TEST(RouteTracingFormatterTest, DefaultFormattersSupportCel) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains: ["*"]
  routes:
  - match:
      prefix: "/"
    tracing:
      operation: "%CEL(request.headers['x-operation'])%"
      upstream_operation: "%CEL(request.headers['x-upstream-operation'])%"
      custom_tags:
      - tag: trace_tag
        value: "%CEL(request.headers['x-trace-tag'])%"
    route:
      cluster: cluster
  )EOF";

  envoy::config::route::v3::RouteConfiguration proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  ScopedThreadLocalServerContextSetter server_context_singleton_setter(factory_context);
  factory_context.cluster_manager_.initializeClusters({"cluster"}, {});
  absl::Status creation_status = absl::OkStatus();
  TestConfigImpl config(proto_config, factory_context, true, creation_status);
  ASSERT_TRUE(creation_status.ok()) << creation_status;

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl headers{
      {":authority", "example.com"},
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "https"},
      {"x-forwarded-proto", "https"},
      {"x-operation", "operation"},
      {"x-upstream-operation", "upstream-operation"},
      {"x-trace-tag", "tag-value"},
  };
  const RouteConstSharedPtr route = config.route(headers, 0).route;
  ASSERT_NE(nullptr, route);
  ASSERT_NE(nullptr, route->tracingConfig());

  Formatter::Context formatter_context{&headers};
  EXPECT_EQ("operation",
            route->tracingConfig()->operation()->format(formatter_context, stream_info));
  EXPECT_EQ("upstream-operation",
            route->tracingConfig()->upstreamOperation()->format(formatter_context, stream_info));

  const Tracing::CustomTagMap& custom_tags = route->tracingConfig()->getCustomTags();
  const auto custom_tag = custom_tags.find("trace_tag");
  ASSERT_NE(custom_tags.end(), custom_tag);

  NiceMock<Tracing::MockSpan> span;
  Tracing::TestTraceContextImpl trace_context;
  const Tracing::CustomTagContext tag_context{trace_context, stream_info, formatter_context};
  EXPECT_CALL(span, setTag(StrEq("trace_tag"), StrEq("tag-value")));
  custom_tag->second->applySpan(span, tag_context);
}

} // namespace
} // namespace Router
} // namespace Envoy
