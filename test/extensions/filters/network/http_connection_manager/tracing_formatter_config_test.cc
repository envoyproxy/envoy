#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/network/http_connection_manager/config.h"

#include "test/extensions/filters/network/http_connection_manager/config_test_base.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {
namespace {

using testing::NiceMock;
using testing::Return;
using testing::StrEq;

TEST_F(HttpConnectionManagerConfigTest, TracingFormattersEnableCelStringFunctions) {
  const std::string yaml = R"EOF(
stat_prefix: router
route_config:
  name: local_route
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
  )EOF";

  EXPECT_CALL(tracer_manager_, getOrCreateTracer(testing::_)).WillOnce(Return(tracer_));
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml), context_,
                                     date_provider_, route_config_provider_manager_,
                                     &scoped_routes_config_provider_manager_, tracer_manager_,
                                     filter_config_provider_manager_, creation_status_);
  ASSERT_TRUE(creation_status_.ok()) << creation_status_;
  ASSERT_NE(nullptr, config.tracingConfig());

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl headers{
      {"x-operation", "original-operation"},
      {"x-upstream-operation", "original-upstream-operation"},
      {"x-trace-tag", "original-tag"},
  };
  Formatter::Context formatter_context{&headers};
  EXPECT_EQ("mutated-operation",
            config.tracingConfig()->operation_->format(formatter_context, stream_info));
  EXPECT_EQ("mutated-upstream-operation",
            config.tracingConfig()->upstream_operation_->format(formatter_context, stream_info));

  const Tracing::CustomTagMap& custom_tags = config.tracingConfig()->custom_tags_;
  const auto custom_tag = custom_tags.find("trace_tag");
  ASSERT_NE(custom_tags.end(), custom_tag);

  NiceMock<Tracing::MockSpan> span;
  Tracing::TestTraceContextImpl trace_context;
  const Tracing::CustomTagContext tag_context{trace_context, stream_info, formatter_context};
  EXPECT_CALL(span, setTag(StrEq("trace_tag"), StrEq("mutated-tag")));
  custom_tag->second->applySpan(span, tag_context);
}

TEST_F(HttpConnectionManagerConfigTest, TracingFormattersDisableCelStringFunctions) {
  const std::string yaml = R"EOF(
stat_prefix: router
route_config:
  name: local_route
tracing:
  operation: "%CEL(request.headers['x-operation'].replace('original', 'mutated'))%"
  formatters:
  - name: envoy.formatter.cel
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.formatter.cel.v3.Cel
      cel_config:
        enable_string_functions: false
  )EOF";

  EXPECT_CALL(tracer_manager_, getOrCreateTracer(testing::_)).WillOnce(Return(tracer_));
  EXPECT_THROW(HttpConnectionManagerConfig(parseHttpConnectionManagerFromYaml(yaml), context_,
                                           date_provider_, route_config_provider_manager_,
                                           &scoped_routes_config_provider_manager_, tracer_manager_,
                                           filter_config_provider_manager_, creation_status_),
               EnvoyException);
}

TEST_F(HttpConnectionManagerConfigTest, DefaultFormattersSupportCel) {
  const std::string yaml = R"EOF(
stat_prefix: router
route_config:
  name: local_route
tracing:
  operation: "%CEL(request.headers['x-operation'])%"
  upstream_operation: "%CEL(request.headers['x-upstream-operation'])%"
  custom_tags:
  - tag: trace_tag
    value: "%CEL(request.headers['x-trace-tag'])%"
  )EOF";

  ScopedThreadLocalServerContextSetter server_context_singleton_setter(
      context_.server_factory_context_);
  EXPECT_CALL(tracer_manager_, getOrCreateTracer(testing::_)).WillOnce(Return(tracer_));
  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromYaml(yaml), context_,
                                     date_provider_, route_config_provider_manager_,
                                     &scoped_routes_config_provider_manager_, tracer_manager_,
                                     filter_config_provider_manager_, creation_status_);
  ASSERT_TRUE(creation_status_.ok()) << creation_status_;
  ASSERT_NE(nullptr, config.tracingConfig());

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl headers{
      {"x-operation", "operation"},
      {"x-upstream-operation", "upstream-operation"},
      {"x-trace-tag", "tag-value"},
  };
  Formatter::Context formatter_context{&headers};
  EXPECT_EQ("operation",
            config.tracingConfig()->operation_->format(formatter_context, stream_info));
  EXPECT_EQ("upstream-operation",
            config.tracingConfig()->upstream_operation_->format(formatter_context, stream_info));

  const Tracing::CustomTagMap& custom_tags = config.tracingConfig()->custom_tags_;
  const auto custom_tag = custom_tags.find("trace_tag");
  ASSERT_NE(custom_tags.end(), custom_tag);

  NiceMock<Tracing::MockSpan> span;
  Tracing::TestTraceContextImpl trace_context;
  const Tracing::CustomTagContext tag_context{trace_context, stream_info, formatter_context};
  EXPECT_CALL(span, setTag(StrEq("trace_tag"), StrEq("tag-value")));
  custom_tag->second->applySpan(span, tag_context);
}

} // namespace
} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
