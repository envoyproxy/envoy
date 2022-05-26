#include "envoy/http/filter.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/filter_config.h"

#include "source/common/http/match_delegate/config.h"
#include "source/common/http/matching/inputs.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Common {
namespace Http {
namespace MatchDelegate {
namespace {

struct TestFactory : public Envoy::Server::Configuration::NamedHttpFilterConfigFactory {
  std::string name() const override { return "test"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }
  Envoy::Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::FactoryContext&) override {
    return [](auto& callbacks) {
      callbacks.addStreamDecoderFilter(nullptr);
      callbacks.addStreamEncoderFilter(nullptr);
      callbacks.addStreamFilter(nullptr);

      callbacks.addAccessLogHandler(nullptr);
    };
  }

  Server::Configuration::MatchingRequirementsPtr matchingRequirements() override {
    auto requirements = std::make_unique<
        envoy::extensions::filters::common::dependency::v3::MatchingRequirements>();

    requirements->mutable_data_input_allow_list()->add_type_url(
        "type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput");

    return requirements;
  }
};

TEST(MatchWrapper, WithMatcher) {
  TestFactory test_factory;
  Envoy::Registry::InjectFactory<Envoy::Server::Configuration::NamedHttpFilterConfigFactory>
      inject_factory(test_factory);

  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;

  const auto config =
      TestUtility::parseYaml<envoy::extensions::common::matching::v3::ExtensionWithMatcher>(R"EOF(
extension_config:
  name: test
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
xds_matcher:
  matcher_tree:
    input:
      name: request-headers
      typed_config:
        "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
        header_name: default-matcher-header
    exact_match_map:
        map:
            match:
                action:
                    name: skip
                    typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
)EOF");

  MatchDelegateConfig match_delegate_config;
  auto cb = match_delegate_config.createFilterFactoryFromProto(config, "", factory_context);

  Envoy::Http::MockFilterChainFactoryCallbacks factory_callbacks;
  testing::InSequence s;

  // Delegating filter will be created for this match wrapper.
  EXPECT_CALL(factory_callbacks, addStreamDecoderFilter(_))
      .WillOnce(Invoke([](Envoy::Http::StreamDecoderFilterSharedPtr filter) {
        EXPECT_NE(nullptr, dynamic_cast<DelegatingStreamFilter*>(filter.get()));
      }));
  EXPECT_CALL(factory_callbacks, addStreamEncoderFilter(_))
      .WillOnce(Invoke([](Envoy::Http::StreamEncoderFilterSharedPtr filter) {
        EXPECT_NE(nullptr, dynamic_cast<DelegatingStreamFilter*>(filter.get()));
      }));
  EXPECT_CALL(factory_callbacks, addStreamFilter(_))
      .WillOnce(Invoke([](Envoy::Http::StreamFilterSharedPtr filter) {
        EXPECT_NE(nullptr, dynamic_cast<DelegatingStreamFilter*>(filter.get()));
      }));
  EXPECT_CALL(factory_callbacks, addAccessLogHandler(testing::IsNull()));
  cb(factory_callbacks);
}

TEST(MatchWrapper, DEPRECATED_FEATURE_TEST(WithDeprecatedMatcher)) {
  TestFactory test_factory;
  Envoy::Registry::InjectFactory<Envoy::Server::Configuration::NamedHttpFilterConfigFactory>
      inject_factory(test_factory);

  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;

  const auto config =
      TestUtility::parseYaml<envoy::extensions::common::matching::v3::ExtensionWithMatcher>(R"EOF(
extension_config:
  name: test
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
matcher:
  matcher_tree:
    input:
      name: request-headers
      typed_config:
        "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
        header_name: default-matcher-header
    exact_match_map:
        map:
            match:
                action:
                    name: skip
                    typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
)EOF");

  MatchDelegateConfig match_delegate_config;
  auto cb = match_delegate_config.createFilterFactoryFromProto(config, "", factory_context);

  Envoy::Http::MockFilterChainFactoryCallbacks factory_callbacks;
  testing::InSequence s;

  // Delegating filter will be created for this match wrapper.
  EXPECT_CALL(factory_callbacks, addStreamDecoderFilter(_))
      .WillOnce(Invoke([](Envoy::Http::StreamDecoderFilterSharedPtr filter) {
        EXPECT_NE(nullptr, dynamic_cast<DelegatingStreamFilter*>(filter.get()));
      }));
  EXPECT_CALL(factory_callbacks, addStreamEncoderFilter(_))
      .WillOnce(Invoke([](Envoy::Http::StreamEncoderFilterSharedPtr filter) {
        EXPECT_NE(nullptr, dynamic_cast<DelegatingStreamFilter*>(filter.get()));
      }));
  EXPECT_CALL(factory_callbacks, addStreamFilter(_))
      .WillOnce(Invoke([](Envoy::Http::StreamFilterSharedPtr filter) {
        EXPECT_NE(nullptr, dynamic_cast<DelegatingStreamFilter*>(filter.get()));
      }));
  EXPECT_CALL(factory_callbacks, addAccessLogHandler(testing::IsNull()));
  cb(factory_callbacks);
}

TEST(MatchWrapper, WithNoMatcher) {
  TestFactory test_factory;
  Envoy::Registry::InjectFactory<Envoy::Server::Configuration::NamedHttpFilterConfigFactory>
      inject_factory(test_factory);

  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;

  const auto config =
      TestUtility::parseYaml<envoy::extensions::common::matching::v3::ExtensionWithMatcher>(R"EOF(
extension_config:
  name: test
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
)EOF");

  MatchDelegateConfig match_delegate_config;
  EXPECT_THROW_WITH_REGEX(
      match_delegate_config.createFilterFactoryFromProto(config, "", factory_context),
      EnvoyException, "one of `matcher` and `matcher_tree` must be set.");
}

TEST(MatchWrapper, WithMatcherInvalidDataInput) {
  TestFactory test_factory;
  Envoy::Registry::InjectFactory<Envoy::Server::Configuration::NamedHttpFilterConfigFactory>
      inject_factory(test_factory);

  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;

  const auto config =
      TestUtility::parseYaml<envoy::extensions::common::matching::v3::ExtensionWithMatcher>(R"EOF(
extension_config:
  name: test
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
xds_matcher:
  matcher_tree:
    input:
      name: request-headers
      typed_config:
        "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseHeaderMatchInput
        header_name: default-matcher-header
    exact_match_map:
        map:
            match:
                action:
                    name: skip
                    typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
)EOF");

  MatchDelegateConfig match_delegate_config;
  EXPECT_THROW_WITH_REGEX(
      match_delegate_config.createFilterFactoryFromProto(config, "", factory_context),
      EnvoyException,
      "requirement violation while creating match tree: INVALID_ARGUMENT: data input typeUrl "
      "type.googleapis.com/envoy.type.matcher.v3.HttpResponseHeaderMatchInput not permitted "
      "according to allowlist");
}

struct TestAction : Matcher::ActionBase<ProtobufWkt::StringValue> {};

template <class InputType, class ActionType>
Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData>
createMatchingTree(const std::string& name, const std::string& value) {
  auto tree = std::make_shared<Matcher::ExactMapMatcher<Envoy::Http::HttpMatchingData>>(
      std::make_unique<InputType>(name), absl::nullopt);

  tree->addChild(value, Matcher::OnMatch<Envoy::Http::HttpMatchingData>{
                            []() { return std::make_unique<ActionType>(); }, nullptr});

  return tree;
}

Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> createRequestAndResponseMatchingTree() {
  auto tree = std::make_shared<Matcher::ExactMapMatcher<Envoy::Http::HttpMatchingData>>(
      std::make_unique<Envoy::Http::Matching::HttpResponseHeadersDataInput>("match-header"),
      absl::nullopt);

  tree->addChild(
      "match",
      Matcher::OnMatch<Envoy::Http::HttpMatchingData>{
          []() { return std::make_unique<SkipAction>(); },
          createMatchingTree<Envoy::Http::Matching::HttpRequestHeadersDataInput, SkipAction>(
              "match-header", "match")});

  return tree;
}

TEST(DelegatingFilterTest, MatchTreeSkipActionDecodingHeaders) {
  // The filter is added, but since we match on the request header we skip the filter.
  std::shared_ptr<Envoy::Http::MockStreamDecoderFilter> decoder_filter(
      new Envoy::Http::MockStreamDecoderFilter());
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> callbacks;

  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*decoder_filter, decodeHeaders(_, _)).Times(0);
  EXPECT_CALL(*decoder_filter, decodeData(_, _)).Times(0);
  EXPECT_CALL(*decoder_filter, decodeMetadata(_)).Times(0);
  EXPECT_CALL(*decoder_filter, decodeTrailers(_)).Times(0);
  EXPECT_CALL(*decoder_filter, decodeComplete()).Times(0);

  auto match_tree =
      createMatchingTree<Envoy::Http::Matching::HttpRequestHeadersDataInput, SkipAction>(
          "match-header", "match");
  auto delegating_filter =
      std::make_shared<DelegatingStreamFilter>(match_tree, decoder_filter, nullptr);

  Envoy::Http::RequestHeaderMapPtr request_headers{
      new Envoy::Http::TestRequestHeaderMapImpl{{":authority", "host"},
                                                {":path", "/"},
                                                {":method", "GET"},
                                                {"match-header", "match"},
                                                {"content-type", "application/grpc"}}};
  Envoy::Http::RequestTrailerMapPtr request_trailers{
      new Envoy::Http::TestRequestTrailerMapImpl{{"test", "test"}}};
  Envoy::Http::MetadataMap metadata_map;
  Buffer::OwnedImpl buffer;

  delegating_filter->setDecoderFilterCallbacks(callbacks);
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            delegating_filter->decodeHeaders(*request_headers, false));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, delegating_filter->decodeData(buffer, false));
  EXPECT_EQ(Envoy::Http::FilterMetadataStatus::Continue,
            delegating_filter->decodeMetadata(metadata_map));
  EXPECT_EQ(Envoy::Http::FilterTrailersStatus::Continue,
            delegating_filter->decodeTrailers(*request_trailers));
  delegating_filter->decodeComplete();
}

TEST(DelegatingFilterTest, MatchTreeSkipActionEncodingHeaders) {
  // The filter is added, but since we match on the request header we skip the filter.
  std::shared_ptr<Envoy::Http::MockStreamEncoderFilter> encoder_filter(
      new Envoy::Http::MockStreamEncoderFilter());
  NiceMock<Envoy::Http::MockStreamEncoderFilterCallbacks> callbacks;

  EXPECT_CALL(*encoder_filter, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*encoder_filter, encodeHeaders(_, _)).Times(0);
  EXPECT_CALL(*encoder_filter, encode1xxHeaders(_)).Times(0);
  EXPECT_CALL(*encoder_filter, encodeData(_, _)).Times(0);
  EXPECT_CALL(*encoder_filter, encodeMetadata(_)).Times(0);
  EXPECT_CALL(*encoder_filter, encodeTrailers(_)).Times(0);
  EXPECT_CALL(*encoder_filter, encodeComplete()).Times(0);

  auto match_tree =
      createMatchingTree<Envoy::Http::Matching::HttpResponseHeadersDataInput, SkipAction>(
          "match-header", "match");
  auto delegating_filter =
      std::make_shared<DelegatingStreamFilter>(match_tree, nullptr, encoder_filter);

  Envoy::Http::ResponseHeaderMapPtr response_headers{new Envoy::Http::TestResponseHeaderMapImpl{
      {":status", "200"}, {"match-header", "match"}, {"content-type", "application/grpc"}}};
  Envoy::Http::ResponseTrailerMapPtr response_trailers{
      new Envoy::Http::TestResponseTrailerMapImpl{{"test", "test"}}};
  Envoy::Http::MetadataMap metadata_map;
  Buffer::OwnedImpl buffer;

  delegating_filter->setEncoderFilterCallbacks(callbacks);
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            delegating_filter->encodeHeaders(*response_headers, false));
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            delegating_filter->encode1xxHeaders(*response_headers));

  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, delegating_filter->encodeData(buffer, false));
  EXPECT_EQ(Envoy::Http::FilterMetadataStatus::Continue,
            delegating_filter->encodeMetadata(metadata_map));
  EXPECT_EQ(Envoy::Http::FilterTrailersStatus::Continue,
            delegating_filter->encodeTrailers(*response_trailers));
  delegating_filter->encodeComplete();
}

TEST(DelegatingFilterTest, MatchTreeSkipActionRequestAndResponseHeaders) {
  std::shared_ptr<Envoy::Http::MockStreamFilter> filter(new Envoy::Http::MockStreamFilter());
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  NiceMock<Envoy::Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;

  auto match_tree = createRequestAndResponseMatchingTree();

  auto delegating_filter = std::make_shared<DelegatingStreamFilter>(match_tree, filter, filter);

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter, setEncoderFilterCallbacks(_));

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([&](auto&, bool) -> Envoy::Http::FilterHeadersStatus {
        Envoy::Http::ResponseHeaderMapPtr headers{new Envoy::Http::TestResponseHeaderMapImpl{
            {":status", "200"}, {"match-header", "match"}, {"content-type", "application/grpc"}}};
        filter->decoder_callbacks_->encodeHeaders(std::move(headers), false, "details");
        Buffer::OwnedImpl empty_buffer;
        filter->decoder_callbacks_->encodeData(empty_buffer, false);

        Envoy::Http::ResponseTrailerMapPtr trailers{
            new Envoy::Http::TestResponseTrailerMapImpl{{"test", "test"}}};
        filter->decoder_callbacks_->encodeTrailers(std::move(trailers));

        return Envoy::Http::FilterHeadersStatus::StopIteration;
      }));
  EXPECT_CALL(decoder_callbacks, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](Envoy::Http::ResponseHeaderMap& headers, bool end_stream) {
        // All the following encoding callbacks will be skipped.
        EXPECT_CALL(*filter, encodeHeaders(_, _)).Times(0);
        delegating_filter->encodeHeaders(headers, end_stream);
      }));
  EXPECT_CALL(decoder_callbacks, encodeData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool end_stream) {
        EXPECT_CALL(*filter, encodeData(_, _)).Times(0);
        delegating_filter->encodeData(data, end_stream);
      }));
  EXPECT_CALL(decoder_callbacks, encodeTrailers_(_))
      .WillOnce(Invoke([&](Envoy::Http::ResponseTrailerMap& trailers) {
        EXPECT_CALL(*filter, encodeTrailers(_)).Times(0);
        delegating_filter->encodeTrailers(trailers);
      }));
  // The decodeComplete will be skipped because the match tree is evaluated.
  EXPECT_CALL(*filter, decodeComplete()).Times(0);

  Envoy::Http::RequestHeaderMapPtr request_headers{
      new Envoy::Http::TestRequestHeaderMapImpl{{":authority", "host"},
                                                {":path", "/"},
                                                {":method", "GET"},
                                                {"match-header", "match"},
                                                {"content-type", "application/grpc"}}};

  delegating_filter->setDecoderFilterCallbacks(decoder_callbacks);
  delegating_filter->setEncoderFilterCallbacks(encoder_callbacks);

  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            delegating_filter->decodeHeaders(*request_headers, true));
  delegating_filter->decodeComplete();
}

// Verify that we propagate custom match actions to a decoding filter.
TEST(DelegatingFilterTest, MatchTreeFilterActionDecodingHeaders) {

  std::shared_ptr<Envoy::Http::MockStreamDecoderFilter> decoder_filter(
      new Envoy::Http::MockStreamDecoderFilter());
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> callbacks;

  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*decoder_filter, onMatchCallback(_));
  EXPECT_CALL(*decoder_filter, decodeHeaders(_, _));
  EXPECT_CALL(*decoder_filter, decodeComplete());

  auto match_tree =
      createMatchingTree<Envoy::Http::Matching::HttpRequestHeadersDataInput, TestAction>(
          "match-header", "match");
  auto delegating_filter =
      std::make_shared<DelegatingStreamFilter>(match_tree, decoder_filter, nullptr);

  Envoy::Http::RequestHeaderMapPtr request_headers{
      new Envoy::Http::TestRequestHeaderMapImpl{{":authority", "host"},
                                                {":path", "/"},
                                                {":method", "GET"},
                                                {"match-header", "match"},
                                                {"content-type", "application/grpc"}}};

  delegating_filter->setDecoderFilterCallbacks(callbacks);
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            delegating_filter->decodeHeaders(*request_headers, true));
  delegating_filter->decodeComplete();
}

// Verify that we propagate custom match actions to a decoding filter when matching on request
// trailers.
TEST(DelegatingFilterTest, MatchTreeFilterActionDecodingTrailers) {
  std::shared_ptr<Envoy::Http::MockStreamDecoderFilter> decoder_filter(
      new Envoy::Http::MockStreamDecoderFilter());
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> callbacks;

  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*decoder_filter, decodeHeaders(_, _));
  EXPECT_CALL(*decoder_filter, decodeData(_, _));
  EXPECT_CALL(*decoder_filter, decodeTrailers(_));
  EXPECT_CALL(*decoder_filter, decodeComplete());

  auto match_tree =
      createMatchingTree<Envoy::Http::Matching::HttpRequestTrailersDataInput, TestAction>(
          "match-trailer", "match");
  auto delegating_filter =
      std::make_shared<DelegatingStreamFilter>(match_tree, decoder_filter, nullptr);

  Envoy::Http::RequestHeaderMapPtr request_headers{
      new Envoy::Http::TestRequestHeaderMapImpl{{":authority", "host"},
                                                {":path", "/"},
                                                {":method", "GET"},
                                                {"content-type", "application/grpc"}}};
  Envoy::Http::RequestTrailerMapPtr request_trailers{
      new Envoy::Http::TestRequestTrailerMapImpl{{"match-trailer", "match"}}};
  Buffer::OwnedImpl buffer;

  delegating_filter->setDecoderFilterCallbacks(callbacks);
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            delegating_filter->decodeHeaders(*request_headers, false));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, delegating_filter->decodeData(buffer, false));

  EXPECT_CALL(*decoder_filter, onMatchCallback(_));
  EXPECT_EQ(Envoy::Http::FilterTrailersStatus::Continue,
            delegating_filter->decodeTrailers(*request_trailers));
  delegating_filter->decodeComplete();
}

// Verify that we propagate custom match actions to an encoding filter when matching on response
// trailers.
TEST(DelegatingFilterTest, MatchTreeFilterActionEncodingTrailers) {
  std::shared_ptr<Envoy::Http::MockStreamFilter> filter(new Envoy::Http::MockStreamFilter());
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  NiceMock<Envoy::Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;

  auto match_tree =
      createMatchingTree<Envoy::Http::Matching::HttpResponseTrailersDataInput, TestAction>(
          "match-trailer", "match");

  auto delegating_filter = std::make_shared<DelegatingStreamFilter>(match_tree, filter, filter);

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter, setEncoderFilterCallbacks(_));

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([&](auto&, bool) -> Envoy::Http::FilterHeadersStatus {
        Envoy::Http::ResponseHeaderMapPtr headers{new Envoy::Http::TestResponseHeaderMapImpl{
            {":status", "200"}, {"content-type", "application/grpc"}}};
        filter->decoder_callbacks_->encodeHeaders(std::move(headers), false, "details");
        Buffer::OwnedImpl empty_buffer;
        filter->decoder_callbacks_->encodeData(empty_buffer, false);

        Envoy::Http::ResponseTrailerMapPtr trailers{
            new Envoy::Http::TestResponseTrailerMapImpl{{"match-trailer", "match"}}};
        filter->decoder_callbacks_->encodeTrailers(std::move(trailers));

        return Envoy::Http::FilterHeadersStatus::StopIteration;
      }));
  EXPECT_CALL(decoder_callbacks, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](Envoy::Http::ResponseHeaderMap& headers, bool end_stream) {
        EXPECT_CALL(*filter, encodeHeaders(_, _));
        delegating_filter->encodeHeaders(headers, end_stream);
      }));
  EXPECT_CALL(decoder_callbacks, encodeData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool end_stream) {
        EXPECT_CALL(*filter, encodeData(_, _));
        delegating_filter->encodeData(data, end_stream);
      }));
  EXPECT_CALL(decoder_callbacks, encodeTrailers_(_))
      .WillOnce(Invoke([&](Envoy::Http::ResponseTrailerMap& trailers) {
        EXPECT_CALL(*filter, onMatchCallback(_));
        EXPECT_CALL(*filter, encodeTrailers(_));
        delegating_filter->encodeTrailers(trailers);
      }));
  EXPECT_CALL(*filter, decodeComplete());

  Envoy::Http::RequestHeaderMapPtr request_headers{
      new Envoy::Http::TestRequestHeaderMapImpl{{":authority", "host"},
                                                {":path", "/"},
                                                {":method", "GET"},
                                                {"content-type", "application/grpc"}}};

  delegating_filter->setDecoderFilterCallbacks(decoder_callbacks);
  delegating_filter->setEncoderFilterCallbacks(encoder_callbacks);

  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
            delegating_filter->decodeHeaders(*request_headers, true));
  delegating_filter->decodeComplete();
}

} // namespace
} // namespace MatchDelegate
} // namespace Http
} // namespace Common
} // namespace Envoy
