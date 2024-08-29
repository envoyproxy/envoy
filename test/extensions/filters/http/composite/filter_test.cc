#include <memory>

#include "envoy/http/metadata_interface.h"

#include "source/extensions/filters/http/composite/action.h"
#include "source/extensions/filters/http/composite/filter.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"

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
    ProtobufWkt::Struct expected;
    auto& fields = *expected.mutable_fields();
    fields["rootFilterName"] = ValueUtil::stringValue("actionName");
    EXPECT_TRUE(MessageDifferencer::Equals(expected, *(info->serializeAsProto())));
  }

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

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamEncoderFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setEncoderFilterCallbacks(_));
  ExecuteFilterAction action(factory_callback, "actionName");
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

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamDecoderFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setDecoderFilterCallbacks(_));
  ExecuteFilterAction action(factory_callback, "actionName");
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

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*stream_filter, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());
  ExecuteFilterAction action(factory_callback, "actionName");
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

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamFilter(stream_filter);
    cb.addStreamFilter(stream_filter);
  };

  ExecuteFilterAction action(factory_callback, "actionName");
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

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamDecoderFilter(decoder_filter);
    cb.addStreamDecoderFilter(decoder_filter);
  };

  ExecuteFilterAction action(factory_callback, "actionName");
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

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamEncoderFilter(encode_filter);
    cb.addStreamEncoderFilter(encode_filter);
  };

  ExecuteFilterAction action(factory_callback, "actionName");
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

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamEncoderFilter(encode_filter);
    cb.addAccessLogHandler(access_log_1);
    cb.addAccessLogHandler(access_log_2);
  };

  ExecuteFilterAction action(factory_callback, "actionName");
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

// Validate that when dynamic_config and typed_config are both set, an exception is thrown.
TEST(ConfigTest, TestConfig) {
  const std::string yaml_string = R"EOF(
      typed_config:
        name: set-response-code
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
          abort:
            http_status: 503
            percentage:
              numerator: 0
              denominator: HUNDRED
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
  testing::NiceMock<Server::Configuration::MockUpstreamFactoryContext> upstream_factory_context;
  for (bool is_downstream : {false, true}) {
    Envoy::Http::Matching::HttpFilterActionContext action_context{
        .is_downstream_ = is_downstream,
        .stat_prefix_ = "test",
        .factory_context_ = factory_context,
        .upstream_factory_context_ = upstream_factory_context,
        .server_factory_context_ = server_factory_context};
    ExecuteFilterActionFactory factory;
    EXPECT_THROW_WITH_MESSAGE(
        factory.createActionFactoryCb(config, action_context,
                                      ProtobufMessage::getStrictValidationVisitor()),
        EnvoyException, "Error: Only one of `dynamic_config` or `typed_config` can be set.");
  }
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
      .server_factory_context_ = server_factory_context};
  ExecuteFilterActionFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createActionFactoryCb(config, action_context,
                                    ProtobufMessage::getStrictValidationVisitor()),
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
      .server_factory_context_ = absl::nullopt};
  ExecuteFilterActionFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createActionFactoryCb(config, action_context,
                                    ProtobufMessage::getStrictValidationVisitor()),
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
      .server_factory_context_ = server_factory_context};
  ExecuteFilterActionFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createActionFactoryCb(config, action_context,
                                    ProtobufMessage::getStrictValidationVisitor()),
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
      .server_factory_context_ = server_factory_context};
  ExecuteFilterActionFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createActionFactoryCb(config, action_context,
                                    ProtobufMessage::getStrictValidationVisitor()),
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
      .server_factory_context_ = absl::nullopt};
  ExecuteFilterActionFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createActionFactoryCb(config, action_context,
                                    ProtobufMessage::getStrictValidationVisitor()),
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
      .server_factory_context_ = server_factory_context};
  ExecuteFilterActionFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createActionFactoryCb(config, action_context,
                                    ProtobufMessage::getStrictValidationVisitor()),
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

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  NiceMock<Runtime::MockLoader>& runtime = server_factory_context.runtime_loader_;
  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);
  ExecuteFilterActionFactory factory;
  EXPECT_TRUE(factory.isSampled(config, runtime));
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

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  NiceMock<Runtime::MockLoader>& runtime = server_factory_context.runtime_loader_;
  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);
  ExecuteFilterActionFactory factory;
  EXPECT_CALL(runtime.snapshot_,
              featureEnabled(_, testing::A<const envoy::type::v3::FractionalPercent&>()))
      .WillOnce(testing::Return(true));
  EXPECT_TRUE(factory.isSampled(config, runtime));
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
        runtime_key:
   )EOF";

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  NiceMock<Runtime::MockLoader>& runtime = server_factory_context.runtime_loader_;
  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction config;
  TestUtility::loadFromYaml(yaml_string, config);
  ExecuteFilterActionFactory factory;
  EXPECT_CALL(runtime.snapshot_,
              featureEnabled(_, testing::A<const envoy::type::v3::FractionalPercent&>()))
      .WillOnce(testing::Return(false));
  EXPECT_FALSE(factory.isSampled(config, runtime));
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
      .server_factory_context_ = server_factory_context};
  ExecuteFilterActionFactory factory;
  auto action = factory.createActionFactoryCb(config, action_context,
                                              ProtobufMessage::getStrictValidationVisitor())();

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
      .server_factory_context_ = server_factory_context};
  ExecuteFilterActionFactory factory;
  auto action = factory.createActionFactoryCb(config, action_context,
                                              ProtobufMessage::getStrictValidationVisitor())();

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

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamEncoderFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setEncoderFilterCallbacks(_));
  ExecuteFilterAction action(factory_callback, "actionName");
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

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamEncoderFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setEncoderFilterCallbacks(_));
  ExecuteFilterAction action(factory_callback, "actionName");
  EXPECT_CALL(success_counter_, inc());
  filter_.onMatchCallback(action);

  auto* info = filter_state->getDataMutable<MatchedActionInfo>(MatchedActionsFilterStateKey);
  EXPECT_NE(nullptr, info);
  ProtobufWkt::Struct expected;
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

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamEncoderFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setEncoderFilterCallbacks(_));
  ExecuteFilterAction action(factory_callback, "actionName");
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

} // namespace
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
