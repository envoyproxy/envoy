#include "source/extensions/filters/http/bandwidth_share/filter_config.h"

#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {

using StatusHelpers::HasStatus;
using StatusHelpers::HasStatusCode;
using testing::HasSubstr;
using testing::NiceMock;
using testing::NotNull;
using testing::ReturnRef;

class ConfigTest : public testing::Test {
public:
  auto factory() {
    auto factory_type_url =
        TypeUtil::typeUrlToDescriptorFullName(config_.typed_config().type_url());
    return Registry::FactoryRegistry<
        Server::Configuration::NamedHttpFilterConfigFactory>::getFactoryByType(factory_type_url);
  }
  void setConfigFromYaml(std::string yaml) { TestUtility::loadFromYaml(std::move(yaml), config_); }

  absl::StatusOr<Http::FilterFactoryCb> filterFactory() {
    if (!factory()) {
      return absl::InvalidArgumentError(
          absl::StrCat("No factory for type ", config_.typed_config().type_url()));
    }
    ProtobufTypes::MessagePtr config = factory()->createEmptyConfigProto();
    config_.typed_config().UnpackTo(config.get());
    return factory()->createFilterFactoryFromProto(*config, "statprefix", mock_factory_context_);
  }

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr> routeConfig() {
    if (!factory()) {
      return absl::InvalidArgumentError(
          absl::StrCat("No factory for type ", config_.typed_config().type_url()));
    }
    ProtobufTypes::MessagePtr config = factory()->createEmptyConfigProto();
    config_.typed_config().UnpackTo(config.get());
    return factory()->createRouteSpecificFilterConfig(
        *config, mock_factory_context_.server_factory_context_, validation_visitor_);
  }

  std::shared_ptr<const FilterConfig> filterConfig() {
    auto route_config = routeConfig();
    EXPECT_OK(route_config);
    auto ret = std::dynamic_pointer_cast<const FilterConfig>(*route_config);
    EXPECT_THAT(ret, NotNull());
    return ret;
  }

  xds::core::v3::TypedExtensionConfig config_;
  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_context_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  // CEL uses the global ServerContext singleton so it must be initialized.
  ScopedThreadLocalServerContextSetter server_context_singleton_setter_{
      mock_factory_context_.server_factory_context_};
};

TEST_F(ConfigTest, ListenerEmptyConfig) {
  setConfigFromYaml(R"(
    name: "empty"
    typed_config:
      "@type": "type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare"
  )");
  EXPECT_OK(filterFactory());
}

TEST_F(ConfigTest, ProtoValidationIsApplied) {
  setConfigFromYaml(R"(
    name: "too_small_fill_interval"
    typed_config:
      "@type": "type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare"
      "fill_interval": 0.005s
  )");
  // Proto validation occurs before it even gets into any of the functions,
  // so only a throw is available, there's no way to catch it for a status.
  EXPECT_THROW_WITH_REGEX(filterFactory().IgnoreError(), EnvoyException,
                          "value must be inside range \\[20ms, 1s\\]");
}

TEST_F(ConfigTest, ValidTenantNameSelectorPlainStringParses) {
  setConfigFromYaml(R"YAML(
    name: "plain_string"
    typed_config:
      "@type": "type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare"
      tenant_name_selector:
        on_no_match:
          action:
            name: use_constant_string_as_tenant_name
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
  )YAML");
  auto filter_cb = filterFactory();
  ASSERT_OK(filter_cb);
  Http::MockFilterChainFactoryCallbacks callbacks;
  EXPECT_CALL(callbacks, addStreamDecoderFilter);
  (*filter_cb)(callbacks);
}

TEST_F(ConfigTest, TenantNameSelectorWithInvalidActionTypeIsAConfigError) {
  setConfigFromYaml(R"YAML(
    name: "invalid_action_type"
    typed_config:
      "@type": "type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare"
      tenant_name_selector:
        on_no_match:
          action:
            name: do_something_unreasonable
            typed_config:
              "@type": "type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare"
  )YAML");
  EXPECT_THAT(filterFactory(), HasStatus(absl::StatusCode::kInvalidArgument,
                                         HasSubstr("Didn't find a registered implementation")));
}

TEST_F(ConfigTest, RouteConfigAlsoDoesValidation) {
  setConfigFromYaml(R"YAML(
    name: "invalid_action_type"
    typed_config:
      "@type": "type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare"
      tenant_name_selector:
        on_no_match:
          action:
            name: do_something_unreasonable
            typed_config:
              "@type": "type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare"
  )YAML");
  EXPECT_THAT(routeConfig(), HasStatus(absl::StatusCode::kInvalidArgument,
                                       HasSubstr("Didn't find a registered implementation")));
}

TEST_F(ConfigTest, CelFormatterCanExtractCertificateNameAsTenant) {
  setConfigFromYaml(R"YAML(
    name: "cel_config"
    typed_config:
      "@type": "type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare"
      tenant_name_selector:
        on_no_match:
          action:
            name: use_client_certificate_name_as_tenant_name
            typed_config:
              "@type": type.googleapis.com/envoy.config.core.v3.SubstitutionFormatString
              omit_empty_values: true
              text_format_source:
                inline_string: '%CEL(re.extract(connection.subject_peer_certificate, R"CN=([^,]*)", "\\1"))%'
              formatters:
              - name: envoy.formatter.cel
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.formatter.cel.v3.Cel
  )YAML");
  auto filter_config = filterConfig();
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
  Http::TestRequestHeaderMapImpl request_headers{};
  std::string cert{"CN=example_cn,something_else"};
  EXPECT_CALL(*ssl, subjectPeerCertificate).WillRepeatedly(ReturnRef(cert));
  stream_info.downstream_connection_info_provider_->setSslConnection(ssl);
  EXPECT_THAT(filter_config->getTenantName(stream_info, request_headers), "example_cn");
  // Also try with no certificate to check that's formatted reasonably and not an error.
  stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
  EXPECT_THAT(filter_config->getTenantName(stream_info, request_headers), "");
}

TEST_F(ConfigTest, StatsGetTenantTagOnlyIfExplicitlyConfiguredTo) {
  setConfigFromYaml(R"YAML(
    name: "optional_tenant_tagging"
    typed_config:
      "@type": "type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare"
      request_limit: {bucket_id: "bucket1", kbps: {default_value: 1000, runtime_key: "a"}}
      response_limit: {bucket_id: "bucket2", kbps: {default_value: 1000, runtime_key: "a"}}
      tenant_configs:
        "foo": {include_stats_tag: true}
  )YAML");
  auto filter_config = filterConfig();
  BandwidthShareStats& foo_stats = filter_config->requestStatsForTenant("foo");
  BandwidthShareStats& bar_stats = filter_config->responseStatsForTenant("bar");
  EXPECT_THAT(
      foo_stats.bytes_limited_.name(),
      "bandwidth_share.bytes.bucket_id.bucket1.tenant.foo.direction.request.handling.limited");
  EXPECT_THAT(
      foo_stats.bytes_not_limited_.name(),
      "bandwidth_share.bytes.bucket_id.bucket1.tenant.foo.direction.request.handling.not_limited");
  EXPECT_THAT(
      foo_stats.streams_currently_limited_.name(),
      "bandwidth_share.streams_currently_limited.bucket_id.bucket1.tenant.foo.direction.request");
  EXPECT_THAT(foo_stats.bytes_pending_.name(),
              "bandwidth_share.bytes_pending.bucket_id.bucket1.tenant.foo.direction.request");
  // bar_stats should have omitted tenant, and response stats should have direction=response.
  EXPECT_THAT(
      bar_stats.bytes_limited_.name(),
      "bandwidth_share.bytes.bucket_id.bucket2.tenant..direction.response.handling.limited");
}

TEST_F(ConfigTest, ResponseTrailersPrefixWhenNotEnabledIsAConfigError) {
  setConfigFromYaml(R"YAML(
    name: "optional_tenant_tagging"
    typed_config:
      "@type": "type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare"
      response_trailer_prefix: foo-
  )YAML");
  EXPECT_THAT(routeConfig(), HasStatus(absl::StatusCode::kInvalidArgument,
                                       HasSubstr("enable_response_trailers must be true")));
}

TEST_F(ConfigTest, ResponseTrailersDisabledIsReflectedInConfig) {
  setConfigFromYaml(R"YAML(
    name: "optional_tenant_tagging"
    typed_config:
      "@type": "type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare"
      enable_response_trailers: false
  )YAML");
  auto filter_config = filterConfig();
  EXPECT_FALSE(filter_config->enableResponseTrailers());
}

TEST_F(ConfigTest, ResponseTrailersWithNoPrefixSetsTrailers) {
  setConfigFromYaml(R"YAML(
    name: "optional_tenant_tagging"
    typed_config:
      "@type": "type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare"
      enable_response_trailers: true
  )YAML");
  auto filter_config = filterConfig();
  EXPECT_TRUE(filter_config->enableResponseTrailers());
  EXPECT_EQ(filter_config->responseTrailers().request_duration_.get(),
            "bandwidth-request-duration-ms");
  EXPECT_EQ(filter_config->responseTrailers().response_duration_.get(),
            "bandwidth-response-duration-ms");
  EXPECT_EQ(filter_config->responseTrailers().request_delay_.get(), "bandwidth-request-delay-ms");
  EXPECT_EQ(filter_config->responseTrailers().response_delay_.get(), "bandwidth-response-delay-ms");
}

TEST_F(ConfigTest, ResponseTrailersWithPrefixSetsTrailersWithPrefix) {
  setConfigFromYaml(R"YAML(
    name: "optional_tenant_tagging"
    typed_config:
      "@type": "type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare"
      enable_response_trailers: true
      response_trailer_prefix: "foo-"
  )YAML");
  auto filter_config = filterConfig();
  EXPECT_TRUE(filter_config->enableResponseTrailers());
  EXPECT_EQ(filter_config->responseTrailers().request_duration_.get(),
            "foo-bandwidth-request-duration-ms");
  EXPECT_EQ(filter_config->responseTrailers().response_duration_.get(),
            "foo-bandwidth-response-duration-ms");
  EXPECT_EQ(filter_config->responseTrailers().request_delay_.get(),
            "foo-bandwidth-request-delay-ms");
  EXPECT_EQ(filter_config->responseTrailers().response_delay_.get(),
            "foo-bandwidth-response-delay-ms");
}

} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
