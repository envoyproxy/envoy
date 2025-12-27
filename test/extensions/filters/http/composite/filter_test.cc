#include <memory>

#include "envoy/http/metadata_interface.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/composite/action.h"
#include "source/extensions/filters/http/composite/config.h"
#include "source/extensions/filters/http/composite/filter.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/logging.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {
namespace {

using Envoy::Protobuf::util::MessageDifferencer;

class CompositeFilterTest : public ::testing::Test {
public:
  CompositeFilterTest(bool is_upstream)
      : filter_(stats_, decoder_callbacks_.dispatcher(), is_upstream) {
    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    filter_.setEncoderFilterCallbacks(encoder_callbacks_);
  }

  // Templated since MockStreamFilter and MockStreamDecoder filter doesn't share a mock base class.
  template <class T> void expectDelegatedDecoding(T& filter_mock) {
    EXPECT_CALL(filter_mock, decodeHeaders(HeaderMapEqualRef(&default_request_headers_), false));
    EXPECT_CALL(filter_mock, decodeMetadata(_));
    EXPECT_CALL(filter_mock, decodeData(_, false));
    EXPECT_CALL(filter_mock, decodeTrailers(HeaderMapEqualRef(&default_request_trailers_)));
    EXPECT_CALL(filter_mock, decodeComplete());
  }

  // Templated since MockStreamFilter and MockStreamEncoder filter doesn't share a mock base class.
  template <class T> void expectDelegatedEncoding(T& filter_mock) {
    EXPECT_CALL(filter_mock, encode1xxHeaders(HeaderMapEqualRef(&default_response_headers_)));
    EXPECT_CALL(filter_mock, encodeHeaders(HeaderMapEqualRef(&default_response_headers_), false));
    EXPECT_CALL(filter_mock, encodeMetadata(_));
    EXPECT_CALL(filter_mock, encodeData(_, false));
    EXPECT_CALL(filter_mock, encodeTrailers(HeaderMapEqualRef(&default_response_trailers_)));
    EXPECT_CALL(filter_mock, encodeComplete());
  }

  void doAllDecodingCallbacks() {
    filter_.decodeHeaders(default_request_headers_, false);

    Http::MetadataMap metadata;
    filter_.decodeMetadata(metadata);

    Buffer::OwnedImpl buffer("data");
    filter_.decodeData(buffer, false);

    filter_.decodeTrailers(default_request_trailers_);

    filter_.decodeComplete();
  }

  void doAllEncodingCallbacks() {
    filter_.encode1xxHeaders(default_response_headers_);

    filter_.encodeHeaders(default_response_headers_, false);

    Http::MetadataMap metadata;
    filter_.encodeMetadata(metadata);

    Buffer::OwnedImpl buffer("data");
    filter_.encodeData(buffer, false);

    filter_.encodeTrailers(default_response_trailers_);

    filter_.encodeComplete();
  }

  void expectFilterStateInfo(std::shared_ptr<StreamInfo::FilterState> filter_state) {
    auto* info = filter_state->getDataMutable<MatchedActionInfo>(MatchedActionsFilterStateKey);
    EXPECT_NE(nullptr, info);
    Protobuf::Struct expected;
    auto& fields = *expected.mutable_fields();
    fields["rootFilterName"] = ValueUtil::stringValue("actionName");
    EXPECT_TRUE(MessageDifferencer::Equals(expected, *(info->serializeAsProto())));
  }

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;

  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  testing::NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Stats::MockCounter error_counter_;
  Stats::MockCounter success_counter_;
  FilterStats stats_{error_counter_, success_counter_};
  Filter filter_;

  Http::TestRequestHeaderMapImpl default_request_headers_{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  Http::TestRequestTrailerMapImpl default_request_trailers_{{"trailers", "something"}};
  Http::TestResponseHeaderMapImpl default_response_headers_{{":status", "200"}};
  Http::TestResponseTrailerMapImpl default_response_trailers_{
      {"response-trailer", "something-else"}};
};

class FilterTest : public CompositeFilterTest {
public:
  FilterTest() : CompositeFilterTest(false) {}
};

TEST_F(FilterTest, StreamEncoderFilterDelegation) {
  auto stream_filter = std::make_shared<Http::MockStreamEncoderFilter>();
  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(encoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(encoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  Http::FilterFactoryCb factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamEncoderFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setEncoderFilterCallbacks(_));
  ExecuteFilterAction action([&]() -> OptRef<Http::FilterFactoryCb> { return factory_callback; },
                             "actionName", absl::nullopt, context_.runtime_loader_);
  EXPECT_CALL(success_counter_, inc());
  filter_.onMatchCallback(action);

  expectFilterStateInfo(filter_state);

  doAllDecodingCallbacks();
  expectDelegatedEncoding(*stream_filter);
  doAllEncodingCallbacks();

  EXPECT_CALL(*stream_filter, onStreamComplete());
  EXPECT_CALL(*stream_filter, onDestroy());
  filter_.onStreamComplete();
  filter_.onDestroy();
}

TEST_F(FilterTest, StreamDecoderFilterDelegation) {
  auto stream_filter = std::make_shared<Http::MockStreamDecoderFilter>();
  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  Http::FilterFactoryCb factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamDecoderFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setDecoderFilterCallbacks(_));
  ExecuteFilterAction action([&]() -> OptRef<Http::FilterFactoryCb> { return factory_callback; },
                             "actionName", absl::nullopt, context_.runtime_loader_);
  EXPECT_CALL(success_counter_, inc());
  filter_.onMatchCallback(action);

  expectFilterStateInfo(filter_state);

  expectDelegatedDecoding(*stream_filter);
  doAllDecodingCallbacks();
  doAllEncodingCallbacks();
  EXPECT_CALL(*stream_filter, onStreamComplete());
  EXPECT_CALL(*stream_filter, onDestroy());
  filter_.onStreamComplete();
  filter_.onDestroy();
}

TEST_F(FilterTest, StreamFilterDelegation) {
  auto stream_filter = std::make_shared<Http::MockStreamFilter>();
  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  Http::FilterFactoryCb factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*stream_filter, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());
  ExecuteFilterAction action([&]() -> OptRef<Http::FilterFactoryCb> { return factory_callback; },
                             "actionName", absl::nullopt, context_.runtime_loader_);
  filter_.onMatchCallback(action);

  expectFilterStateInfo(filter_state);

  expectDelegatedDecoding(*stream_filter);
  doAllDecodingCallbacks();
  expectDelegatedEncoding(*stream_filter);
  doAllEncodingCallbacks();
  EXPECT_CALL(*stream_filter, onDestroy());
  filter_.onDestroy();
}

// Adding multiple stream filters should cause delegation to be skipped.
TEST_F(FilterTest, StreamFilterDelegationMultipleStreamFilters) {
  auto stream_filter = std::make_shared<Http::MockStreamFilter>();
  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  Http::FilterFactoryCb factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamFilter(stream_filter);
    cb.addStreamFilter(stream_filter);
  };

  ExecuteFilterAction action([&]() -> OptRef<Http::FilterFactoryCb> { return factory_callback; },
                             "actionName", absl::nullopt, context_.runtime_loader_);
  EXPECT_CALL(error_counter_, inc());
  filter_.onMatchCallback(action);

  EXPECT_EQ(nullptr, filter_state->getDataMutable<MatchedActionInfo>(MatchedActionsFilterStateKey));

  doAllDecodingCallbacks();
  doAllEncodingCallbacks();
  filter_.onDestroy();
}

// Adding multiple decoder filters should cause delegation to be skipped.
TEST_F(FilterTest, StreamFilterDelegationMultipleStreamDecoderFilters) {
  auto decoder_filter = std::make_shared<Http::MockStreamDecoderFilter>();
  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  Http::FilterFactoryCb factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamDecoderFilter(decoder_filter);
    cb.addStreamDecoderFilter(decoder_filter);
  };

  ExecuteFilterAction action([&]() -> OptRef<Http::FilterFactoryCb> { return factory_callback; },
                             "actionName", absl::nullopt, context_.runtime_loader_);
  EXPECT_CALL(error_counter_, inc());
  filter_.onMatchCallback(action);

  EXPECT_EQ(nullptr, filter_state->getDataMutable<MatchedActionInfo>(MatchedActionsFilterStateKey));

  doAllDecodingCallbacks();
  doAllEncodingCallbacks();
  filter_.onDestroy();
}

// Adding multiple encoder filters should cause delegation to be skipped.
TEST_F(FilterTest, StreamFilterDelegationMultipleStreamEncoderFilters) {
  auto encode_filter = std::make_shared<Http::MockStreamEncoderFilter>();
  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(encoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(encoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  Http::FilterFactoryCb factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamEncoderFilter(encode_filter);
    cb.addStreamEncoderFilter(encode_filter);
  };

  ExecuteFilterAction action([&]() -> OptRef<Http::FilterFactoryCb> { return factory_callback; },
                             "actionName", absl::nullopt, context_.runtime_loader_);
  EXPECT_CALL(error_counter_, inc());
  filter_.onMatchCallback(action);

  EXPECT_EQ(nullptr, filter_state->getDataMutable<MatchedActionInfo>(MatchedActionsFilterStateKey));

  doAllDecodingCallbacks();
  doAllEncodingCallbacks();
  filter_.onDestroy();
}

// Adding a encoder filter and an access loggers should be permitted and delegate to the access
// logger.
TEST_F(FilterTest, StreamFilterDelegationMultipleAccessLoggers) {
  auto encode_filter = std::make_shared<Http::MockStreamEncoderFilter>();
  auto access_log_1 = std::make_shared<AccessLog::MockInstance>();
  auto access_log_2 = std::make_shared<AccessLog::MockInstance>();
  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(encoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(encoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  Http::FilterFactoryCb factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamEncoderFilter(encode_filter);
    cb.addAccessLogHandler(access_log_1);
    cb.addAccessLogHandler(access_log_2);
  };

  ExecuteFilterAction action([&]() -> OptRef<Http::FilterFactoryCb> { return factory_callback; },
                             "actionName", absl::nullopt, context_.runtime_loader_);
  EXPECT_CALL(*encode_filter, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());
  filter_.onMatchCallback(action);

  expectFilterStateInfo(filter_state);

  doAllDecodingCallbacks();
  expectDelegatedEncoding(*encode_filter);
  doAllEncodingCallbacks();

  EXPECT_CALL(*encode_filter, onDestroy());
  filter_.onDestroy();

  EXPECT_CALL(*access_log_1, log(_, _));
  EXPECT_CALL(*access_log_2, log(_, _));
  filter_.log({}, StreamInfo::MockStreamInfo());
}

TEST(ConfigTest, TestDynamicConfigInDownstream) {
  const std::string yaml_string = R"EOF(
      dynamic_config:
        name: set-response-code
        config_discovery:
          config_source:
              path_config_source:
                path: "{{ test_tmpdir }}/set_response_code.yaml"
          type_urls:
          - type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
  )EOF";

  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = true,
      .stat_prefix_ = "test",
      .factory_context_ = absl::nullopt,
      .upstream_factory_context_ = absl::nullopt,
      .server_factory_context_ = server_factory_context,
  };
  ExecuteFilterActionFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createAction(config, action_context, ProtobufMessage::getStrictValidationVisitor()),
      EnvoyException, "Failed to get downstream factory context or server factory context.");
}

TEST(ConfigTest, TestDynamicConfigInUpstream) {
  const std::string yaml_string = R"EOF(
      dynamic_config:
        name: set-response-code
        config_discovery:
          config_source:
              path_config_source:
                path: "{{ test_tmpdir }}/set_response_code.yaml"
          type_urls:
          - type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfigDual
  )EOF";

  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);

  testing::NiceMock<Server::Configuration::MockUpstreamFactoryContext> upstream_factory_context;
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = false,
      .stat_prefix_ = "test",
      .factory_context_ = absl::nullopt,
      .upstream_factory_context_ = upstream_factory_context,
      .server_factory_context_ = absl::nullopt,
  };
  ExecuteFilterActionFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createAction(config, action_context, ProtobufMessage::getStrictValidationVisitor()),
      EnvoyException, "Failed to get upstream factory context or server factory context.");
}

// For dual filter in downstream, if not overriding
// createFilterFactoryFromProtoWithServerContext(), Envoy exception will be thrown if only
// server_factory_context is provided in action_context.
TEST(ConfigTest, CreateFilterFromServerContextDual) {
  const std::string yaml_string = R"EOF(
      typed_config:
        name: composite-action
        typed_config:
          "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfigDualNoServerContext
          code: 403
  )EOF";

  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = true,
      .stat_prefix_ = "test",
      .factory_context_ = absl::nullopt,
      .upstream_factory_context_ = absl::nullopt,
      .server_factory_context_ = server_factory_context,
  };
  ExecuteFilterActionFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createAction(config, action_context, ProtobufMessage::getStrictValidationVisitor()),
      EnvoyException,
      "DualFactoryBase: creating filter factory from server factory context is not supported");
}

// For dual filter in upstream,  Envoy exception will be thrown if no
// upstream_factory_context provided in the action_context.
TEST(ConfigTest, DualFilterNoUpstreamFactoryContext) {
  const std::string yaml_string = R"EOF(
      typed_config:
        name: composite-action
        typed_config:
          "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfigDualNoServerContext
          code: 403
  )EOF";

  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = false,
      .stat_prefix_ = "test",
      .factory_context_ = absl::nullopt,
      .upstream_factory_context_ = absl::nullopt,
      .server_factory_context_ = server_factory_context,
  };
  ExecuteFilterActionFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createAction(config, action_context, ProtobufMessage::getStrictValidationVisitor()),
      EnvoyException, "Failed to get upstream filter factory creation function");
}

// For downstream filter, Envoy exception will be thrown if no factory_context
// or server_factory_context provided in the action_context.
TEST(ConfigTest, DownstreamFilterNoFactoryContext) {
  const std::string yaml_string = R"EOF(
      typed_config:
        name: composite-action
        typed_config:
          "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfigNoServerContext
          code: 403
  )EOF";

  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = true,
      .stat_prefix_ = "test",
      .factory_context_ = absl::nullopt,
      .upstream_factory_context_ = absl::nullopt,
      .server_factory_context_ = absl::nullopt,
  };
  ExecuteFilterActionFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createAction(config, action_context, ProtobufMessage::getStrictValidationVisitor()),
      EnvoyException, "Failed to get downstream filter factory creation function");
}

// For downstream filter, if not overriding
// createFilterFactoryFromProtoWithServerContext(), Envoy exception will be thrown if only
// server_factory_context is provided in the action_context.
TEST(ConfigTest, TestDownstreamFilterNoOverridingServerContext) {
  const std::string yaml_string = R"EOF(
      typed_config:
        name: composite-action
        typed_config:
          "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfigNoServerContext
          code: 403
  )EOF";

  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = true,
      .stat_prefix_ = "test",
      .factory_context_ = absl::nullopt,
      .upstream_factory_context_ = absl::nullopt,
      .server_factory_context_ = server_factory_context,
  };
  ExecuteFilterActionFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createAction(config, action_context, ProtobufMessage::getStrictValidationVisitor()),
      EnvoyException, "Creating filter factory from server factory context is not supported");
}

// Config test to check if sample_percent config is not specified.
TEST(ConfigTest, TestSamplePercentNotSpecifiedl) {
  const std::string yaml_string = R"EOF(
      typed_config:
        name: set-response-code
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
          abort:
            http_status: 503
   )EOF";

  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  testing::NiceMock<Server::Configuration::MockUpstreamFactoryContext> upstream_factory_context;
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = true,
      .stat_prefix_ = "test",
      .factory_context_ = absl::nullopt,
      .upstream_factory_context_ = absl::nullopt,
      .server_factory_context_ = server_factory_context,
  };
  ExecuteFilterActionFactory factory;
  auto action =
      factory.createAction(config, action_context, ProtobufMessage::getStrictValidationVisitor());

  EXPECT_FALSE(action->getTyped<ExecuteFilterAction>().actionSkip());
}

// Config test to check if sample_percent config is in place and feature enabled.
TEST(ConfigTest, TestSamplePercentInPlaceFeatureEnabled) {
  const std::string yaml_string = R"EOF(
      typed_config:
        name: set-response-code
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
          abort:
            http_status: 503
      sample_percent:
        default_value:
          numerator: 30
          denominator: HUNDRED
   )EOF";

  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  testing::NiceMock<Server::Configuration::MockUpstreamFactoryContext> upstream_factory_context;
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = true,
      .stat_prefix_ = "test",
      .factory_context_ = absl::nullopt,
      .upstream_factory_context_ = absl::nullopt,
      .server_factory_context_ = server_factory_context,
  };
  ExecuteFilterActionFactory factory;
  auto action =
      factory.createAction(config, action_context, ProtobufMessage::getStrictValidationVisitor());

  EXPECT_CALL(server_factory_context.runtime_loader_.snapshot_,
              featureEnabled(_, testing::A<const envoy::type::v3::FractionalPercent&>()))
      .WillOnce(testing::Return(true));

  EXPECT_FALSE(action->getTyped<ExecuteFilterAction>().actionSkip());
}

// Config test to check if sample_percent config is in place and feature not enabled.
TEST(ConfigTest, TestSamplePercentInPlaceFeatureNotEnabled) {
  const std::string yaml_string = R"EOF(
      typed_config:
        name: set-response-code
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
          abort:
            http_status: 503
      sample_percent:
        default_value:
          numerator: 30
          denominator: HUNDRED
   )EOF";

  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  testing::NiceMock<Server::Configuration::MockUpstreamFactoryContext> upstream_factory_context;
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = true,
      .stat_prefix_ = "test",
      .factory_context_ = absl::nullopt,
      .upstream_factory_context_ = absl::nullopt,
      .server_factory_context_ = server_factory_context,
  };
  ExecuteFilterActionFactory factory;
  auto action =
      factory.createAction(config, action_context, ProtobufMessage::getStrictValidationVisitor());

  EXPECT_CALL(server_factory_context.runtime_loader_.snapshot_,
              featureEnabled(_, testing::A<const envoy::type::v3::FractionalPercent&>()))
      .WillOnce(testing::Return(false));

  EXPECT_TRUE(action->getTyped<ExecuteFilterAction>().actionSkip());
}

TEST_F(FilterTest, FilterStateShouldBeUpdatedWithTheMatchingActionForDynamicConfig) {
  const std::string yaml_string = R"EOF(
      dynamic_config:
        name: actionName
        config_discovery:
          config_source:
              path_config_source:
                path: "{{ test_tmpdir }}/set_response_code.yaml"
          type_urls:
          - type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
)EOF";

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  testing::NiceMock<Server::Configuration::MockUpstreamFactoryContext> upstream_factory_context;
  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = true,
      .stat_prefix_ = "test",
      .factory_context_ = factory_context,
      .upstream_factory_context_ = upstream_factory_context,
      .server_factory_context_ = server_factory_context,
  };
  ExecuteFilterActionFactory factory;
  auto action =
      factory.createAction(config, action_context, ProtobufMessage::getStrictValidationVisitor());

  EXPECT_EQ("actionName", action->getTyped<ExecuteFilterAction>().actionName());
}

TEST_F(FilterTest, FilterStateShouldBeUpdatedWithTheMatchingActionForTypedConfig) {
  const std::string yaml_string = R"EOF(
      typed_config:
        name: actionName
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
          abort:
            http_status: 503
            percentage:
              numerator: 0
              denominator: HUNDRED
)EOF";

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  testing::NiceMock<Server::Configuration::MockUpstreamFactoryContext> upstream_factory_context;
  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = true,
      .stat_prefix_ = "test",
      .factory_context_ = factory_context,
      .upstream_factory_context_ = upstream_factory_context,
      .server_factory_context_ = server_factory_context,
  };
  ExecuteFilterActionFactory factory;
  auto action =
      factory.createAction(config, action_context, ProtobufMessage::getStrictValidationVisitor());

  EXPECT_EQ("actionName", action->getTyped<ExecuteFilterAction>().actionName());
}

TEST_F(FilterTest, FilterStateShouldBeUpdatedWithTheMatchingAction) {
  auto stream_filter = std::make_shared<Http::MockStreamEncoderFilter>();
  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(encoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(encoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  filter_state->setData(MatchedActionsFilterStateKey,
                        std::make_shared<MatchedActionInfo>("rootFilterName", "oldActionName"),
                        StreamInfo::FilterState::StateType::Mutable,
                        StreamInfo::FilterState::LifeSpan::FilterChain);

  Http::FilterFactoryCb factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamEncoderFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setEncoderFilterCallbacks(_));
  ExecuteFilterAction action([&]() -> OptRef<Http::FilterFactoryCb> { return factory_callback; },
                             "actionName", absl::nullopt, context_.runtime_loader_);
  EXPECT_CALL(success_counter_, inc());
  filter_.onMatchCallback(action);

  expectFilterStateInfo(filter_state);

  EXPECT_CALL(*stream_filter, onStreamComplete());
  EXPECT_CALL(*stream_filter, onDestroy());
  filter_.onStreamComplete();
  filter_.onDestroy();
}

TEST_F(FilterTest, MatchingActionShouldNotCollitionWithOtherRootFilter) {
  auto stream_filter = std::make_shared<Http::MockStreamEncoderFilter>();
  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(encoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(encoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  filter_state->setData(MatchedActionsFilterStateKey,
                        std::make_shared<MatchedActionInfo>("otherRootFilterName", "anyActionName"),
                        StreamInfo::FilterState::StateType::Mutable,
                        StreamInfo::FilterState::LifeSpan::FilterChain);

  Http::FilterFactoryCb factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamEncoderFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setEncoderFilterCallbacks(_));
  ExecuteFilterAction action([&]() -> OptRef<Http::FilterFactoryCb> { return factory_callback; },
                             "actionName", absl::nullopt, context_.runtime_loader_);
  EXPECT_CALL(success_counter_, inc());
  filter_.onMatchCallback(action);

  auto* info = filter_state->getDataMutable<MatchedActionInfo>(MatchedActionsFilterStateKey);
  EXPECT_NE(nullptr, info);
  Protobuf::Struct expected;
  auto& fields = *expected.mutable_fields();
  fields["otherRootFilterName"] = ValueUtil::stringValue("anyActionName");
  fields["rootFilterName"] = ValueUtil::stringValue("actionName");
  EXPECT_TRUE(MessageDifferencer::Equals(expected, *(info->serializeAsProto())));

  EXPECT_CALL(*stream_filter, onStreamComplete());
  EXPECT_CALL(*stream_filter, onDestroy());
  filter_.onStreamComplete();
  filter_.onDestroy();
}

class UpstreamFilterTest : public CompositeFilterTest {
public:
  UpstreamFilterTest() : CompositeFilterTest(true) {}
};

TEST_F(UpstreamFilterTest, StreamEncoderFilterDelegationUpstream) {
  auto stream_filter = std::make_shared<Http::MockStreamEncoderFilter>();
  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(encoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(encoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  Http::FilterFactoryCb factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamEncoderFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setEncoderFilterCallbacks(_));
  ExecuteFilterAction action([&]() -> OptRef<Http::FilterFactoryCb> { return factory_callback; },
                             "actionName", absl::nullopt, context_.runtime_loader_);
  EXPECT_CALL(success_counter_, inc());
  filter_.onMatchCallback(action);

  auto* info = filter_state->getDataMutable<MatchedActionInfo>(MatchedActionsFilterStateKey);
  EXPECT_EQ(nullptr, info);

  doAllDecodingCallbacks();
  expectDelegatedEncoding(*stream_filter);
  doAllEncodingCallbacks();

  EXPECT_CALL(*stream_filter, onStreamComplete());
  EXPECT_CALL(*stream_filter, onDestroy());
  filter_.onStreamComplete();
  filter_.onDestroy();
}

// Test that a filter chain with multiple filters executes decode operations in order.
TEST_F(FilterTest, FilterChainDecodeInOrder) {
  auto filter1 = std::make_shared<Http::MockStreamFilter>();
  auto filter2 = std::make_shared<Http::MockStreamFilter>();

  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  FilterFactoryCbList filter_factories;
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter1); });
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter2); });

  EXPECT_CALL(*filter1, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter1, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());

  ExecuteFilterAction action(std::move(filter_factories), "filter_chain", absl::nullopt,
                             context_.runtime_loader_);
  filter_.onMatchCallback(action);

  // Verify filter state is set.
  auto* info = filter_state->getDataMutable<MatchedActionInfo>(MatchedActionsFilterStateKey);
  EXPECT_NE(nullptr, info);

  // Verify decode order where filter1 should be called before filter2.
  testing::InSequence seq;
  EXPECT_CALL(*filter1, decodeHeaders(_, false))
      .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
  EXPECT_CALL(*filter2, decodeHeaders(_, false))
      .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));

  filter_.decodeHeaders(default_request_headers_, false);

  EXPECT_CALL(*filter1, onDestroy());
  EXPECT_CALL(*filter2, onDestroy());
  filter_.onDestroy();
}

// Test that a filter chain with multiple filters executes encode operations in reverse order.
TEST_F(FilterTest, FilterChainEncodeInReverseOrder) {
  auto filter1 = std::make_shared<Http::MockStreamFilter>();
  auto filter2 = std::make_shared<Http::MockStreamFilter>();

  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  FilterFactoryCbList filter_factories;
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter1); });
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter2); });

  EXPECT_CALL(*filter1, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter1, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());

  ExecuteFilterAction action(std::move(filter_factories), "filter_chain", absl::nullopt,
                             context_.runtime_loader_);
  filter_.onMatchCallback(action);

  // Verify encode order where filter2 should be called before filter1 i.e. in reverse order.
  testing::InSequence seq;
  EXPECT_CALL(*filter2, encodeHeaders(_, false))
      .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
  EXPECT_CALL(*filter1, encodeHeaders(_, false))
      .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));

  filter_.encodeHeaders(default_response_headers_, false);

  EXPECT_CALL(*filter1, onDestroy());
  EXPECT_CALL(*filter2, onDestroy());
  filter_.onDestroy();
}

// Test that filter chain stops iteration when a filter returns StopIteration during decode.
TEST_F(FilterTest, FilterChainStopsIterationOnDecode) {
  auto filter1 = std::make_shared<Http::MockStreamFilter>();
  auto filter2 = std::make_shared<Http::MockStreamFilter>();

  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  FilterFactoryCbList filter_factories;
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter1); });
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter2); });

  EXPECT_CALL(*filter1, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter1, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());

  ExecuteFilterAction action(std::move(filter_factories), "filter_chain", absl::nullopt,
                             context_.runtime_loader_);
  filter_.onMatchCallback(action);

  // filter1 returns StopIteration, so filter2 won't be called.
  EXPECT_CALL(*filter1, decodeHeaders(_, false))
      .WillOnce(testing::Return(Http::FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*filter2, decodeHeaders(_, _)).Times(0);

  auto status = filter_.decodeHeaders(default_request_headers_, false);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, status);

  EXPECT_CALL(*filter1, onDestroy());
  EXPECT_CALL(*filter2, onDestroy());
  filter_.onDestroy();
}

// Test that filter chain properly handles mixed filter types i.e. decoder and encoder filters.
TEST_F(FilterTest, FilterChainWithMixedFilterTypes) {
  auto decoder_filter = std::make_shared<Http::MockStreamDecoderFilter>();
  auto encoder_filter = std::make_shared<Http::MockStreamEncoderFilter>();

  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  FilterFactoryCbList filter_factories;
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamDecoderFilter(decoder_filter); });
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamEncoderFilter(encoder_filter); });

  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*encoder_filter, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());

  ExecuteFilterAction action(std::move(filter_factories), "filter_chain", absl::nullopt,
                             context_.runtime_loader_);
  filter_.onMatchCallback(action);

  // Decoder filter should receive decode calls and encoder filter should receive encode calls.
  EXPECT_CALL(*decoder_filter, decodeHeaders(_, false))
      .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));

  filter_.decodeHeaders(default_request_headers_, false);

  // Encoder filter should receive encode calls.
  EXPECT_CALL(*encoder_filter, encodeHeaders(_, false))
      .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));

  filter_.encodeHeaders(default_response_headers_, false);

  EXPECT_CALL(*decoder_filter, onDestroy());
  EXPECT_CALL(*encoder_filter, onDestroy());
  filter_.onDestroy();
}

// Test that an empty filter chain succeeds but does nothing.
TEST_F(FilterTest, EmptyFilterChainSucceeds) {
  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  FilterFactoryCbList filter_factories; // Empty.

  // Empty filter chain should not call success counter since no filters were injected.
  EXPECT_CALL(success_counter_, inc()).Times(0);

  ExecuteFilterAction action(std::move(filter_factories), "filter_chain", absl::nullopt,
                             context_.runtime_loader_);
  filter_.onMatchCallback(action);

  // No delegated filter, so all calls should return Continue.
  auto status = filter_.decodeHeaders(default_request_headers_, false);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, status);

  filter_.onDestroy();
}

// Test that filter chain with three filters executes in correct order.
TEST_F(FilterTest, FilterChainWithThreeFilters) {
  auto filter1 = std::make_shared<Http::MockStreamFilter>();
  auto filter2 = std::make_shared<Http::MockStreamFilter>();
  auto filter3 = std::make_shared<Http::MockStreamFilter>();

  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  FilterFactoryCbList filter_factories;
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter1); });
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter2); });
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter3); });

  EXPECT_CALL(*filter1, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter1, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter3, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter3, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());

  ExecuteFilterAction action(std::move(filter_factories), "filter_chain", absl::nullopt,
                             context_.runtime_loader_);
  filter_.onMatchCallback(action);

  // Verify decode order where filter1 should be called before filter2 and filter3.
  {
    testing::InSequence seq;
    EXPECT_CALL(*filter1, decodeHeaders(_, false))
        .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
    EXPECT_CALL(*filter2, decodeHeaders(_, false))
        .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
    EXPECT_CALL(*filter3, decodeHeaders(_, false))
        .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
  }
  filter_.decodeHeaders(default_request_headers_, false);

  // Verify encode order where filter3 should be called before filter2 and filter1 i.e. in reverse
  // order.
  {
    testing::InSequence seq;
    EXPECT_CALL(*filter3, encodeHeaders(_, false))
        .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
    EXPECT_CALL(*filter2, encodeHeaders(_, false))
        .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
    EXPECT_CALL(*filter1, encodeHeaders(_, false))
        .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
  }
  filter_.encodeHeaders(default_response_headers_, false);

  EXPECT_CALL(*filter1, onDestroy());
  EXPECT_CALL(*filter2, onDestroy());
  EXPECT_CALL(*filter3, onDestroy());
  filter_.onDestroy();
}

// Test that filter chain with sampling respects the sample_percent configuration.
TEST_F(FilterTest, FilterChainWithSamplingSkipped) {
  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  FilterFactoryCbList filter_factories;
  filter_factories.push_back([](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamFilter(std::make_shared<Http::MockStreamFilter>());
  });

  envoy::config::core::v3::RuntimeFractionalPercent sample_percent;
  sample_percent.mutable_default_value()->set_numerator(0);
  sample_percent.mutable_default_value()->set_denominator(
      envoy::type::v3::FractionalPercent::HUNDRED);

  // Sampling is disabled (0%), so filter chain should be skipped.
  EXPECT_CALL(context_.runtime_loader_.snapshot_,
              featureEnabled(_, testing::A<const envoy::type::v3::FractionalPercent&>()))
      .WillOnce(testing::Return(false));
  EXPECT_CALL(success_counter_, inc()).Times(0);

  ExecuteFilterAction action(std::move(filter_factories), "filter_chain", sample_percent,
                             context_.runtime_loader_);
  filter_.onMatchCallback(action);

  // No filter injected, so decode should return Continue i.e. filter chain should not be triggered.
  auto status = filter_.decodeHeaders(default_request_headers_, false);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, status);

  filter_.onDestroy();
}

// Test that isFilterChain() returns true for filter chain actions.
TEST(FilterChainActionTest, IsFilterChainReturnsTrue) {
  FilterFactoryCbList filter_factories;
  filter_factories.push_back([](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamFilter(std::make_shared<Http::MockStreamFilter>());
  });

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  ExecuteFilterAction action(std::move(filter_factories), "filter_chain", absl::nullopt,
                             context.runtime_loader_);

  EXPECT_TRUE(action.isFilterChain());
}

// Test that isFilterChain() returns false for single filter actions.
TEST(FilterChainActionTest, IsFilterChainReturnsFalse) {
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Http::FilterFactoryCb callback = [](Http::FilterChainFactoryCallbacks&) {};

  ExecuteFilterAction action(
      [cb = std::move(callback)]() mutable -> OptRef<Http::FilterFactoryCb> { return cb; },
      "single_filter", absl::nullopt, context.runtime_loader_);

  EXPECT_FALSE(action.isFilterChain());
}

// Test filter chain with all decode and encode operations.
TEST_F(FilterTest, FilterChainComprehensiveDecodeEncodeCoverage) {
  auto filter1 = std::make_shared<Http::MockStreamFilter>();
  auto filter2 = std::make_shared<Http::MockStreamFilter>();

  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  FilterFactoryCbList filter_factories;
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter1); });
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter2); });

  EXPECT_CALL(*filter1, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter1, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());

  ExecuteFilterAction action(std::move(filter_factories), "filter_chain", absl::nullopt,
                             context_.runtime_loader_);
  filter_.onMatchCallback(action);

  // Test decodeHeaders.
  {
    testing::InSequence seq;
    EXPECT_CALL(*filter1, decodeHeaders(_, false))
        .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
    EXPECT_CALL(*filter2, decodeHeaders(_, false))
        .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
  }
  filter_.decodeHeaders(default_request_headers_, false);

  // Test decodeData.
  {
    testing::InSequence seq;
    EXPECT_CALL(*filter1, decodeData(_, false))
        .WillOnce(testing::Return(Http::FilterDataStatus::Continue));
    EXPECT_CALL(*filter2, decodeData(_, false))
        .WillOnce(testing::Return(Http::FilterDataStatus::Continue));
  }
  Buffer::OwnedImpl decode_data("decode_data");
  filter_.decodeData(decode_data, false);

  // Test decodeTrailers. It covers DelegatedFilterChain::decodeTrailers.
  {
    testing::InSequence seq;
    EXPECT_CALL(*filter1, decodeTrailers(_))
        .WillOnce(testing::Return(Http::FilterTrailersStatus::Continue));
    EXPECT_CALL(*filter2, decodeTrailers(_))
        .WillOnce(testing::Return(Http::FilterTrailersStatus::Continue));
  }
  filter_.decodeTrailers(default_request_trailers_);

  // Test decodeMetadata. It covers DelegatedFilterChain::decodeMetadata.
  {
    testing::InSequence seq;
    EXPECT_CALL(*filter1, decodeMetadata(_))
        .WillOnce(testing::Return(Http::FilterMetadataStatus::Continue));
    EXPECT_CALL(*filter2, decodeMetadata(_))
        .WillOnce(testing::Return(Http::FilterMetadataStatus::Continue));
  }
  Http::MetadataMap metadata;
  filter_.decodeMetadata(metadata);

  // Test decodeComplete. It covers DelegatedFilterChain::decodeComplete.
  EXPECT_CALL(*filter1, decodeComplete());
  EXPECT_CALL(*filter2, decodeComplete());
  filter_.decodeComplete();

  // Test encode1xxHeaders. It covers DelegatedFilterChain::encode1xxHeaders (reverse order).
  {
    testing::InSequence seq;
    EXPECT_CALL(*filter2, encode1xxHeaders(_))
        .WillOnce(testing::Return(Http::Filter1xxHeadersStatus::Continue));
    EXPECT_CALL(*filter1, encode1xxHeaders(_))
        .WillOnce(testing::Return(Http::Filter1xxHeadersStatus::Continue));
  }
  filter_.encode1xxHeaders(default_response_headers_);

  // Test encodeHeaders in the reverse order.
  {
    testing::InSequence seq;
    EXPECT_CALL(*filter2, encodeHeaders(_, false))
        .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
    EXPECT_CALL(*filter1, encodeHeaders(_, false))
        .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
  }
  filter_.encodeHeaders(default_response_headers_, false);

  // Test encodeData. It covers DelegatedFilterChain::encodeData in the reverse order.
  {
    testing::InSequence seq;
    EXPECT_CALL(*filter2, encodeData(_, false))
        .WillOnce(testing::Return(Http::FilterDataStatus::Continue));
    EXPECT_CALL(*filter1, encodeData(_, false))
        .WillOnce(testing::Return(Http::FilterDataStatus::Continue));
  }
  Buffer::OwnedImpl encode_data("encode_data");
  filter_.encodeData(encode_data, false);

  // Test encodeTrailers. It covers DelegatedFilterChain::encodeTrailers in the reverse order.
  {
    testing::InSequence seq;
    EXPECT_CALL(*filter2, encodeTrailers(_))
        .WillOnce(testing::Return(Http::FilterTrailersStatus::Continue));
    EXPECT_CALL(*filter1, encodeTrailers(_))
        .WillOnce(testing::Return(Http::FilterTrailersStatus::Continue));
  }
  filter_.encodeTrailers(default_response_trailers_);

  // Test encodeMetadata. It covers DelegatedFilterChain::encodeMetadata in the reverse order.
  {
    testing::InSequence seq;
    EXPECT_CALL(*filter2, encodeMetadata(_))
        .WillOnce(testing::Return(Http::FilterMetadataStatus::Continue));
    EXPECT_CALL(*filter1, encodeMetadata(_))
        .WillOnce(testing::Return(Http::FilterMetadataStatus::Continue));
  }
  Http::MetadataMap encode_metadata;
  filter_.encodeMetadata(encode_metadata);

  // Test encodeComplete. It covers DelegatedFilterChain::encodeComplete in the reverse order.
  {
    testing::InSequence seq;
    EXPECT_CALL(*filter2, encodeComplete());
    EXPECT_CALL(*filter1, encodeComplete());
  }
  filter_.encodeComplete();

  // Test onStreamComplete. It covers DelegatedFilterChain::onStreamComplete.
  EXPECT_CALL(*filter1, onStreamComplete());
  EXPECT_CALL(*filter2, onStreamComplete());
  filter_.onStreamComplete();

  EXPECT_CALL(*filter1, onDestroy());
  EXPECT_CALL(*filter2, onDestroy());
  filter_.onDestroy();
}

// Test that DelegatedFilterChain stops iteration on non-Continue status for trailers.
TEST_F(FilterTest, FilterChainStopsIterationOnDecodeTrailers) {
  auto filter1 = std::make_shared<Http::MockStreamFilter>();
  auto filter2 = std::make_shared<Http::MockStreamFilter>();

  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  FilterFactoryCbList filter_factories;
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter1); });
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter2); });

  EXPECT_CALL(*filter1, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter1, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());

  ExecuteFilterAction action(std::move(filter_factories), "filter_chain", absl::nullopt,
                             context_.runtime_loader_);
  filter_.onMatchCallback(action);

  // filter1 returns StopIteration on decodeTrailers, so filter2 should not be called.
  EXPECT_CALL(*filter1, decodeTrailers(_))
      .WillOnce(testing::Return(Http::FilterTrailersStatus::StopIteration));
  EXPECT_CALL(*filter2, decodeTrailers(_)).Times(0);

  auto status = filter_.decodeTrailers(default_request_trailers_);
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, status);

  EXPECT_CALL(*filter1, onDestroy());
  EXPECT_CALL(*filter2, onDestroy());
  filter_.onDestroy();
}

// Test that DelegatedFilterChain stops iteration on non-Continue status for decodeMetadata.
TEST_F(FilterTest, FilterChainStopsIterationOnDecodeMetadata) {
  auto filter1 = std::make_shared<Http::MockStreamFilter>();
  auto filter2 = std::make_shared<Http::MockStreamFilter>();

  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  FilterFactoryCbList filter_factories;
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter1); });
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter2); });

  EXPECT_CALL(*filter1, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter1, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());

  ExecuteFilterAction action(std::move(filter_factories), "filter_chain", absl::nullopt,
                             context_.runtime_loader_);
  filter_.onMatchCallback(action);

  // filter1 returns StopIterationNoBuffer on decodeMetadata, so filter2 should not be called.
  EXPECT_CALL(*filter1, decodeMetadata(_))
      .WillOnce(testing::Return(Http::FilterMetadataStatus::StopIterationForLocalReply));
  EXPECT_CALL(*filter2, decodeMetadata(_)).Times(0);

  Http::MetadataMap metadata;
  auto status = filter_.decodeMetadata(metadata);
  EXPECT_EQ(Http::FilterMetadataStatus::StopIterationForLocalReply, status);

  EXPECT_CALL(*filter1, onDestroy());
  EXPECT_CALL(*filter2, onDestroy());
  filter_.onDestroy();
}

// Test that DelegatedFilterChain stops iteration on non-Continue status for decodeData.
TEST_F(FilterTest, FilterChainStopsIterationOnDecodeData) {
  auto filter1 = std::make_shared<Http::MockStreamFilter>();
  auto filter2 = std::make_shared<Http::MockStreamFilter>();

  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  FilterFactoryCbList filter_factories;
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter1); });
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter2); });

  EXPECT_CALL(*filter1, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter1, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());

  ExecuteFilterAction action(std::move(filter_factories), "filter_chain", absl::nullopt,
                             context_.runtime_loader_);
  filter_.onMatchCallback(action);

  // filter1 returns StopIterationAndBuffer on decodeData, so filter2 should not be called.
  EXPECT_CALL(*filter1, decodeData(_, false))
      .WillOnce(testing::Return(Http::FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*filter2, decodeData(_, _)).Times(0);

  Buffer::OwnedImpl data("test_data");
  auto status = filter_.decodeData(data, false);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, status);

  EXPECT_CALL(*filter1, onDestroy());
  EXPECT_CALL(*filter2, onDestroy());
  filter_.onDestroy();
}

// Test that DelegatedFilterChain stops iteration on non-Continue status for encode1xxHeaders.
TEST_F(FilterTest, FilterChainStopsIterationOnEncode1xxHeaders) {
  auto filter1 = std::make_shared<Http::MockStreamFilter>();
  auto filter2 = std::make_shared<Http::MockStreamFilter>();

  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  FilterFactoryCbList filter_factories;
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter1); });
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter2); });

  EXPECT_CALL(*filter1, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter1, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());

  ExecuteFilterAction action(std::move(filter_factories), "filter_chain", absl::nullopt,
                             context_.runtime_loader_);
  filter_.onMatchCallback(action);

  // Encode operations are in reverse order. filter2 returns StopIteration, so filter1 should not
  // be called.
  EXPECT_CALL(*filter2, encode1xxHeaders(_))
      .WillOnce(testing::Return(Http::Filter1xxHeadersStatus::StopIteration));
  EXPECT_CALL(*filter1, encode1xxHeaders(_)).Times(0);

  auto status = filter_.encode1xxHeaders(default_response_headers_);
  EXPECT_EQ(Http::Filter1xxHeadersStatus::StopIteration, status);

  EXPECT_CALL(*filter1, onDestroy());
  EXPECT_CALL(*filter2, onDestroy());
  filter_.onDestroy();
}

// Test that DelegatedFilterChain stops iteration on non-Continue status for encodeData.
TEST_F(FilterTest, FilterChainStopsIterationOnEncodeData) {
  auto filter1 = std::make_shared<Http::MockStreamFilter>();
  auto filter2 = std::make_shared<Http::MockStreamFilter>();

  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  FilterFactoryCbList filter_factories;
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter1); });
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter2); });

  EXPECT_CALL(*filter1, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter1, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());

  ExecuteFilterAction action(std::move(filter_factories), "filter_chain", absl::nullopt,
                             context_.runtime_loader_);
  filter_.onMatchCallback(action);

  // Encode operations are in reverse order. filter2 returns StopIterationAndBuffer, so filter1
  // should not be called.
  EXPECT_CALL(*filter2, encodeData(_, false))
      .WillOnce(testing::Return(Http::FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*filter1, encodeData(_, _)).Times(0);

  Buffer::OwnedImpl data("test_data");
  auto status = filter_.encodeData(data, false);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, status);

  EXPECT_CALL(*filter1, onDestroy());
  EXPECT_CALL(*filter2, onDestroy());
  filter_.onDestroy();
}

// Test that DelegatedFilterChain stops iteration on non-Continue status for encodeTrailers.
TEST_F(FilterTest, FilterChainStopsIterationOnEncodeTrailers) {
  auto filter1 = std::make_shared<Http::MockStreamFilter>();
  auto filter2 = std::make_shared<Http::MockStreamFilter>();

  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  FilterFactoryCbList filter_factories;
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter1); });
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter2); });

  EXPECT_CALL(*filter1, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter1, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());

  ExecuteFilterAction action(std::move(filter_factories), "filter_chain", absl::nullopt,
                             context_.runtime_loader_);
  filter_.onMatchCallback(action);

  // Encode operations are in reverse order. filter2 returns StopIteration, so filter1 should not
  // be called.
  EXPECT_CALL(*filter2, encodeTrailers(_))
      .WillOnce(testing::Return(Http::FilterTrailersStatus::StopIteration));
  EXPECT_CALL(*filter1, encodeTrailers(_)).Times(0);

  auto status = filter_.encodeTrailers(default_response_trailers_);
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, status);

  EXPECT_CALL(*filter1, onDestroy());
  EXPECT_CALL(*filter2, onDestroy());
  filter_.onDestroy();
}

// Test that DelegatedFilterChain stops iteration on non-Continue status for encodeMetadata.
TEST_F(FilterTest, FilterChainStopsIterationOnEncodeMetadata) {
  auto filter1 = std::make_shared<Http::MockStreamFilter>();
  auto filter2 = std::make_shared<Http::MockStreamFilter>();

  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  FilterFactoryCbList filter_factories;
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter1); });
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter2); });

  EXPECT_CALL(*filter1, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter1, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());

  ExecuteFilterAction action(std::move(filter_factories), "filter_chain", absl::nullopt,
                             context_.runtime_loader_);
  filter_.onMatchCallback(action);

  // Encode operations are in reverse order. filter2 returns StopIterationForLocalReply, so filter1
  // should not be called.
  EXPECT_CALL(*filter2, encodeMetadata(_))
      .WillOnce(testing::Return(Http::FilterMetadataStatus::StopIterationForLocalReply));
  EXPECT_CALL(*filter1, encodeMetadata(_)).Times(0);

  Http::MetadataMap metadata;
  auto status = filter_.encodeMetadata(metadata);
  EXPECT_EQ(Http::FilterMetadataStatus::StopIterationForLocalReply, status);

  EXPECT_CALL(*filter1, onDestroy());
  EXPECT_CALL(*filter2, onDestroy());
  filter_.onDestroy();
}

// Test empty filter chain configuration throws exception.
TEST(ConfigTest, TestEmptyFilterChainThrowsException) {
  const std::string yaml_string = R"EOF(
      filter_chain:
        typed_config: []
  )EOF";

  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = true,
      .stat_prefix_ = "test",
      .factory_context_ = factory_context,
      .upstream_factory_context_ = absl::nullopt,
      .server_factory_context_ = server_factory_context,
  };
  ExecuteFilterActionFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createAction(config, action_context, ProtobufMessage::getStrictValidationVisitor()),
      EnvoyException, "filter_chain must contain at least one filter.");
}

// Test filter chain configuration for upstream filters.
TEST(ConfigTest, TestUpstreamFilterChainConfiguration) {
  const std::string yaml_string = R"EOF(
      filter_chain:
        typed_config:
          - name: set-response-code
            typed_config:
              "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfigDual
              code: 403
  )EOF";

  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  testing::NiceMock<Server::Configuration::MockUpstreamFactoryContext> upstream_factory_context;
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = false,
      .stat_prefix_ = "test",
      .factory_context_ = absl::nullopt,
      .upstream_factory_context_ = upstream_factory_context,
      .server_factory_context_ = server_factory_context,
  };
  ExecuteFilterActionFactory factory;
  auto action =
      factory.createAction(config, action_context, ProtobufMessage::getStrictValidationVisitor());

  EXPECT_TRUE(action->getTyped<ExecuteFilterAction>().isFilterChain());
  EXPECT_EQ("filter_chain", action->getTyped<ExecuteFilterAction>().actionName());
}

// Test filter chain configuration for upstream filters when upstream factory context is missing.
TEST(ConfigTest, TestUpstreamFilterChainNoUpstreamFactoryContext) {
  const std::string yaml_string = R"EOF(
      filter_chain:
        typed_config:
          - name: set-response-code
            typed_config:
              "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfigDual
              code: 403
  )EOF";

  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = false,
      .stat_prefix_ = "test",
      .factory_context_ = absl::nullopt,
      .upstream_factory_context_ = absl::nullopt,
      .server_factory_context_ = server_factory_context,
  };
  ExecuteFilterActionFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createAction(config, action_context, ProtobufMessage::getStrictValidationVisitor()),
      EnvoyException, "Failed to create upstream filter factory for filter 'set-response-code'");
}

// Test filter_chain_name creates a named filter chain lookup action.
TEST(ConfigTest, TestFilterChainNameCreatesLookupAction) {
  const std::string yaml_string = R"EOF(
      filter_chain_name: "my-chain"
  )EOF";

  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = true,
      .stat_prefix_ = "test",
      .factory_context_ = factory_context,
      .upstream_factory_context_ = absl::nullopt,
      .server_factory_context_ = server_factory_context};
  ExecuteFilterActionFactory factory;
  auto action =
      factory.createAction(config, action_context, ProtobufMessage::getStrictValidationVisitor());

  // Action should be a named filter chain lookup, not a filter chain.
  EXPECT_TRUE(action->getTyped<ExecuteFilterAction>().isNamedFilterChainLookup());
  EXPECT_FALSE(action->getTyped<ExecuteFilterAction>().isFilterChain());
  EXPECT_EQ("my-chain", action->getTyped<ExecuteFilterAction>().filterChainName());
}

// Test filter_chain_name with sample_percent configuration.
TEST(ConfigTest, TestFilterChainNameWithSamplePercent) {
  const std::string yaml_string = R"EOF(
      filter_chain_name: "my-chain"
      sample_percent:
        default_value:
          numerator: 50
          denominator: HUNDRED
  )EOF";

  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = true,
      .stat_prefix_ = "test",
      .factory_context_ = factory_context,
      .upstream_factory_context_ = absl::nullopt,
      .server_factory_context_ = server_factory_context};
  ExecuteFilterActionFactory factory;
  auto action =
      factory.createAction(config, action_context, ProtobufMessage::getStrictValidationVisitor());

  EXPECT_TRUE(action->getTyped<ExecuteFilterAction>().isNamedFilterChainLookup());

  // Test sampling enabled.
  EXPECT_CALL(server_factory_context.runtime_loader_.snapshot_,
              featureEnabled(_, testing::A<const envoy::type::v3::FractionalPercent&>()))
      .WillOnce(testing::Return(false));
  EXPECT_TRUE(action->getTyped<ExecuteFilterAction>().actionSkip());
}

// Test filter chain (inline) with multiple filters is still working.
TEST_F(FilterTest, FilterChainWithMultipleFilters) {
  auto filter1 = std::make_shared<Http::MockStreamFilter>();
  auto filter2 = std::make_shared<Http::MockStreamFilter>();

  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  // Simulate filter factories from inline filter chain.
  FilterFactoryCbList filter_factories;
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter1); });
  filter_factories.push_back(
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter2); });

  EXPECT_CALL(*filter1, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter1, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());

  ExecuteFilterAction action(std::move(filter_factories), "filter_chain", absl::nullopt,
                             context_.runtime_loader_);
  filter_.onMatchCallback(action);

  EXPECT_EQ("filter_chain", action.actionName());

  // Verify decode order where filter1 should be called before filter2.
  testing::InSequence seq;
  EXPECT_CALL(*filter1, decodeHeaders(_, false))
      .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
  EXPECT_CALL(*filter2, decodeHeaders(_, false))
      .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));

  filter_.decodeHeaders(default_request_headers_, false);

  EXPECT_CALL(*filter1, onDestroy());
  EXPECT_CALL(*filter2, onDestroy());
  filter_.onDestroy();
}

// A failing test filter factory used to exercise error paths in CompositeFilterFactory.
class FailingNamedFilterFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  std::string name() const override { return "envoy.filters.http.test.fail_factory"; }
  std::string configType() { return "type.googleapis.com/google.protobuf.Struct"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::FactoryContext&) override {
    return absl::InvalidArgumentError("boom");
  }
  Http::FilterFactoryCb createFilterFactoryFromProtoWithServerContext(
      const Protobuf::Message&, const std::string&,
      Server::Configuration::ServerFactoryContext&) override {
    return nullptr;
  }
};

TEST(ConfigTest, CompileNamedFilterChainsFailsOnEmptyChain) {
  envoy::extensions::filters::http::composite::v3::Composite composite_config;
  (*composite_config.mutable_named_filter_chains())["empty-chain"];
  // Leave typed_config empty to trigger the validation error.

  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  CompositeFilterFactory factory;
  auto status_or_named =
      CompositeFilterFactory::compileNamedFilterChains(composite_config, "test.", factory_context);
  EXPECT_FALSE(status_or_named.ok());
  EXPECT_THAT(status_or_named.status().message(),
              testing::HasSubstr("must contain at least one filter"));
}

TEST(ConfigTest, CompileNamedFilterChainsFailsOnFactoryError) {
  envoy::extensions::filters::http::composite::v3::Composite composite_config;
  auto& chain = (*composite_config.mutable_named_filter_chains())["fail-chain"];
  auto* typed = chain.add_typed_config();
  typed->set_name("envoy.filters.http.test.fail_factory");
  Protobuf::Struct struct_config;
  typed->mutable_typed_config()->PackFrom(struct_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  CompositeFilterFactory factory;
  FailingNamedFilterFactory failing_factory;
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registration(
      failing_factory);
  auto status_or_named =
      CompositeFilterFactory::compileNamedFilterChains(composite_config, "test.", factory_context);
  EXPECT_FALSE(status_or_named.ok());
  EXPECT_THAT(status_or_named.status().message(),
              testing::HasSubstr("Failed to create filter factory"));
}

TEST(FilterCallbacksWrapperTest, SingleModeRejectsMultipleFiltersAndExposesDispatcher) {
  Stats::MockCounter error_counter;
  Stats::MockCounter success_counter;
  FilterStats stats{error_counter, success_counter};
  testing::NiceMock<Event::MockDispatcher> dispatcher;
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  testing::NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;

  Filter filter(stats, dispatcher, false);
  filter.setDecoderFilterCallbacks(decoder_callbacks);
  filter.setEncoderFilterCallbacks(encoder_callbacks);

  FactoryCallbacksWrapper wrapper(filter, dispatcher);
  auto filter1 = std::make_shared<Http::MockStreamFilter>();
  auto filter2 = std::make_shared<Http::MockStreamFilter>();

  wrapper.addStreamFilter(filter1);
  wrapper.addStreamFilter(filter2); // should record an error in single-filter mode

  EXPECT_EQ(&dispatcher, &wrapper.dispatcher());
  EXPECT_TRUE(wrapper.filter_to_inject_.has_value());
  EXPECT_EQ(1, wrapper.errors_.size());
}

TEST(FilterCallbacksWrapperTest, ChainModeAcceptsMultipleFilters) {
  Stats::MockCounter error_counter;
  Stats::MockCounter success_counter;
  FilterStats stats{error_counter, success_counter};
  testing::NiceMock<Event::MockDispatcher> dispatcher;
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  testing::NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;

  Filter filter(stats, dispatcher, false);
  filter.setDecoderFilterCallbacks(decoder_callbacks);
  filter.setEncoderFilterCallbacks(encoder_callbacks);

  FactoryCallbacksWrapper wrapper(filter, dispatcher, true);
  auto filter1 = std::make_shared<Http::MockStreamFilter>();
  auto filter2 = std::make_shared<Http::MockStreamFilter>();

  wrapper.addStreamFilter(filter1);
  wrapper.addStreamFilter(filter2);

  EXPECT_TRUE(wrapper.errors_.empty());
  EXPECT_EQ(2, wrapper.filters_to_inject_.size());
}

// Test named filter chain lookup with sampling that skips execution.
TEST_F(FilterTest, NamedFilterChainLookupWithSamplingSkipped) {
  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  // Create named filter chains for the filter.
  auto named_chains = std::make_shared<NamedFilterChainFactoryMap>();
  (*named_chains)["my-chain"] = {[](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamFilter(std::make_shared<Http::MockStreamFilter>());
  }};

  // Create filter with named chains.
  Filter filter_with_chains(stats_, decoder_callbacks_.dispatcher(), false, named_chains);
  filter_with_chains.setDecoderFilterCallbacks(decoder_callbacks_);
  filter_with_chains.setEncoderFilterCallbacks(encoder_callbacks_);

  // Configure sampling to return false and skip the action.
  envoy::config::core::v3::RuntimeFractionalPercent sample_percent;
  sample_percent.mutable_default_value()->set_numerator(0);
  sample_percent.mutable_default_value()->set_denominator(
      envoy::type::v3::FractionalPercent::HUNDRED);

  EXPECT_CALL(context_.runtime_loader_.snapshot_,
              featureEnabled(_, testing::A<const envoy::type::v3::FractionalPercent&>()))
      .WillOnce(testing::Return(false));

  // Success counter should not be incremented since action is skipped.
  EXPECT_CALL(success_counter_, inc()).Times(0);

  ExecuteFilterAction action("my-chain", sample_percent, context_.runtime_loader_);
  filter_with_chains.onMatchCallback(action);

  // No filter injected since sampling skipped.
  auto status = filter_with_chains.decodeHeaders(default_request_headers_, false);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, status);

  filter_with_chains.onDestroy();
}

// Test named filter chain lookup when no named filter chains are configured.
TEST_F(FilterTest, NamedFilterChainLookupNoNamedChainsConfigured) {
  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  // Create filter without the named chains.
  Filter filter_without_chains(stats_, decoder_callbacks_.dispatcher(), false, nullptr);
  filter_without_chains.setDecoderFilterCallbacks(decoder_callbacks_);
  filter_without_chains.setEncoderFilterCallbacks(encoder_callbacks_);

  // Success and error counters should not be incremented.
  EXPECT_CALL(success_counter_, inc()).Times(0);
  EXPECT_CALL(error_counter_, inc()).Times(0);

  ExecuteFilterAction action("non-existent-chain", absl::nullopt, context_.runtime_loader_);
  EXPECT_LOG_CONTAINS("debug",
                      "filter_chain_name 'non-existent-chain' specified but no named filter chains",
                      filter_without_chains.onMatchCallback(action));

  // No filter injected due to failure.
  auto status = filter_without_chains.decodeHeaders(default_request_headers_, false);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, status);

  filter_without_chains.onDestroy();
}

// Test named filter chain lookup when the chain name is not found.
TEST_F(FilterTest, NamedFilterChainLookupChainNotFound) {
  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  // Create named filter chains but not the one we're looking for.
  auto named_chains = std::make_shared<NamedFilterChainFactoryMap>();
  (*named_chains)["existing-chain"] = {[](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamFilter(std::make_shared<Http::MockStreamFilter>());
  }};

  Filter filter_with_chains(stats_, decoder_callbacks_.dispatcher(), false, named_chains);
  filter_with_chains.setDecoderFilterCallbacks(decoder_callbacks_);
  filter_with_chains.setEncoderFilterCallbacks(encoder_callbacks_);

  // Success and error counters should not be incremented.
  EXPECT_CALL(success_counter_, inc()).Times(0);
  EXPECT_CALL(error_counter_, inc()).Times(0);

  ExecuteFilterAction action("missing-chain", absl::nullopt, context_.runtime_loader_);
  EXPECT_LOG_CONTAINS("debug", "filter_chain_name 'missing-chain' not found in named filter chains",
                      filter_with_chains.onMatchCallback(action));

  // No filter injected due to failure.
  auto status = filter_with_chains.decodeHeaders(default_request_headers_, false);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, status);

  filter_with_chains.onDestroy();
}

// Test named filter chain lookup that succeeds and creates filters.
TEST_F(FilterTest, NamedFilterChainLookupSuccess) {
  auto filter1 = std::make_shared<Http::MockStreamFilter>();
  auto filter2 = std::make_shared<Http::MockStreamFilter>();

  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  // Create named filter chains with the target chain.
  auto named_chains = std::make_shared<NamedFilterChainFactoryMap>();
  (*named_chains)["my-chain"] = {
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter1); },
      [&](Http::FilterChainFactoryCallbacks& cb) { cb.addStreamFilter(filter2); }};

  Filter filter_with_chains(stats_, decoder_callbacks_.dispatcher(), false, named_chains);
  filter_with_chains.setDecoderFilterCallbacks(decoder_callbacks_);
  filter_with_chains.setEncoderFilterCallbacks(encoder_callbacks_);

  EXPECT_CALL(*filter1, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter1, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter2, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());

  ExecuteFilterAction action("my-chain", absl::nullopt, context_.runtime_loader_);
  filter_with_chains.onMatchCallback(action);

  // Verify filter state is updated with the chain name.
  auto* info = filter_state->getDataMutable<MatchedActionInfo>(MatchedActionsFilterStateKey);
  EXPECT_NE(nullptr, info);

  // Verify filters execute in order during decode.
  {
    testing::InSequence seq;
    EXPECT_CALL(*filter1, decodeHeaders(_, false))
        .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
    EXPECT_CALL(*filter2, decodeHeaders(_, false))
        .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
  }
  filter_with_chains.decodeHeaders(default_request_headers_, false);

  // Verify filters execute in reverse order during encode.
  {
    testing::InSequence seq;
    EXPECT_CALL(*filter2, encodeHeaders(_, false))
        .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
    EXPECT_CALL(*filter1, encodeHeaders(_, false))
        .WillOnce(testing::Return(Http::FilterHeadersStatus::Continue));
  }
  filter_with_chains.encodeHeaders(default_response_headers_, false);

  EXPECT_CALL(*filter1, onDestroy());
  EXPECT_CALL(*filter2, onDestroy());
  filter_with_chains.onDestroy();
}

// Test that createFilters() returns early for named filter chain lookup actions.
TEST(ExecuteFilterActionTest, CreateFiltersReturnsEarlyForNamedFilterChainLookup) {
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;

  // Create a named filter chain lookup action.
  ExecuteFilterAction action("my-chain", absl::nullopt, context.runtime_loader_);

  EXPECT_TRUE(action.isNamedFilterChainLookup());
  EXPECT_FALSE(action.isFilterChain());

  // createFilters should return early for named filter chain lookup.
  testing::NiceMock<Http::MockFilterChainFactoryCallbacks> callbacks;
  EXPECT_CALL(callbacks, addStreamFilter(_)).Times(0);
  EXPECT_CALL(callbacks, addStreamDecoderFilter(_)).Times(0);
  EXPECT_CALL(callbacks, addStreamEncoderFilter(_)).Times(0);

  action.createFilters(callbacks);
}

// Test named filter chain lookup with access loggers.
TEST_F(FilterTest, NamedFilterChainLookupWithAccessLoggers) {
  auto filter1 = std::make_shared<Http::MockStreamFilter>();
  auto access_log = std::make_shared<AccessLog::MockInstance>();

  StreamInfo::FilterStateSharedPtr filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(testing::Return("rootFilterName"));
  ON_CALL(decoder_callbacks_.stream_info_, filterState())
      .WillByDefault(testing::ReturnRef(filter_state));

  // Create named filter chains with a filter and access logger.
  auto named_chains = std::make_shared<NamedFilterChainFactoryMap>();
  (*named_chains)["chain-with-logger"] = {
      [&](Http::FilterChainFactoryCallbacks& cb) {
        cb.addStreamFilter(filter1);
        cb.addAccessLogHandler(access_log);
      },
  };

  Filter filter_with_chains(stats_, decoder_callbacks_.dispatcher(), false, named_chains);
  filter_with_chains.setDecoderFilterCallbacks(decoder_callbacks_);
  filter_with_chains.setEncoderFilterCallbacks(encoder_callbacks_);

  EXPECT_CALL(*filter1, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter1, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());

  ExecuteFilterAction action("chain-with-logger", absl::nullopt, context_.runtime_loader_);
  filter_with_chains.onMatchCallback(action);

  // Verify access loggers are called.
  EXPECT_CALL(*access_log, log(_, _));
  filter_with_chains.log({}, StreamInfo::MockStreamInfo());

  EXPECT_CALL(*filter1, onDestroy());
  filter_with_chains.onDestroy();
}

} // namespace
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
