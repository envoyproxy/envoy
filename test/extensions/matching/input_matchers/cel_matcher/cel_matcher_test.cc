#include "test/extensions/matching/input_matchers/cel_matcher/cel_matcher_test.h"

#include <exception>
#include <memory>

#include "envoy/config/common/matcher/v3/matcher.pb.validate.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/registry/registry.h"

#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/matching/http/cel_input/cel_input.h"
#include "source/extensions/matching/input_matchers/cel_matcher/config.h"
#include "source/extensions/matching/input_matchers/cel_matcher/matcher.h"

#include "test/common/matcher/test_utility.h"
#include "test/mocks/matcher/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "xds/type/matcher/v3/matcher.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace CelMatcher {

using ::Envoy::Http::LowerCaseString;
using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::TestResponseHeaderMapImpl;
using ::Envoy::Http::TestResponseTrailerMapImpl;

enum class ExpressionType {
  CheckedExpression = 0,
  ParsedExpression = 1,
  NoExpression = 2,
};

constexpr absl::string_view kFilterNamespace = "cel_matcher";
constexpr absl::string_view kMetadataKey = "service_name";
constexpr absl::string_view kMetadataValue = "test_service";

class CelMatcherTest : public ::testing::Test {
public:
  CelMatcherTest()
      : inject_action_(action_factory_),
        cluster_info_(std::make_shared<testing::NiceMock<Upstream::MockClusterInfo>>()),
        route_(std::make_shared<NiceMock<Router::MockRoute>>()),
        data_(Envoy::Http::Matching::HttpMatchingDataImpl(stream_info_)) {}

  void buildCustomHeader(const absl::flat_hash_map<std::string, std::string>& custom_value_pairs,
                         TestRequestHeaderMapImpl& headers) {
    // Add custom_value_pairs to the request header.
    for (auto const& pair : custom_value_pairs) {
      headers.setCopy(LowerCaseString(pair.first), pair.second);
    }
  }

  Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData>
  buildMatcherTree(const std::string& cel_expr_config,
                   ExpressionType expr_type = ExpressionType::CheckedExpression) {
    xds::type::matcher::v3::CelMatcher cel_matcher;
    switch (expr_type) {
    case ExpressionType::CheckedExpression: {
      google::api::expr::v1alpha1::CheckedExpr checked_expr;
      Protobuf::TextFormat::ParseFromString(cel_expr_config, &checked_expr);
      cel_matcher.mutable_expr_match()->mutable_checked_expr()->MergeFrom(checked_expr);
      break;
    }
    case ExpressionType::ParsedExpression: {
      google::api::expr::v1alpha1::ParsedExpr parsed_expr;
      Protobuf::TextFormat::ParseFromString(cel_expr_config, &parsed_expr);
      cel_matcher.mutable_expr_match()->mutable_parsed_expr()->MergeFrom(parsed_expr);
      break;
    }
    case ExpressionType::NoExpression:
      break;
    }

    xds::type::matcher::v3::Matcher matcher;
    auto* inner_matcher = matcher.mutable_matcher_list()->add_matchers();
    auto* single_predicate = inner_matcher->mutable_predicate()->mutable_single_predicate();

    xds::type::matcher::v3::HttpAttributesCelMatchInput cel_match_input;
    single_predicate->mutable_input()->set_name("envoy.matching.inputs.cel_data_input");
    single_predicate->mutable_input()->mutable_typed_config()->PackFrom(cel_match_input);

    auto* custom_matcher = single_predicate->mutable_custom_match();
    custom_matcher->mutable_typed_config()->PackFrom(cel_matcher);

    xds::type::matcher::v3::Matcher::OnMatch on_match;
    std::string on_match_config = R"EOF(
    action:
      name: test_action
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: match!!
  )EOF";
    MessageUtil::loadFromYaml(on_match_config, on_match,
                              ProtobufMessage::getStrictValidationVisitor());

    inner_matcher->mutable_on_match()->MergeFrom(on_match);

    auto string_factory_on_match = Matcher::TestDataInputStringFactory("value");

    Matcher::MockMatchTreeValidationVisitor<Envoy::Http::HttpMatchingData> validation_visitor;
    EXPECT_CALL(validation_visitor,
                performDataInputValidation(
                    _, "type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput"));
    Matcher::MatchTreeFactory<Envoy::Http::HttpMatchingData, absl::string_view> matcher_factory(
        context_, factory_context_, validation_visitor);
    auto match_tree = matcher_factory.create(matcher);

    return match_tree();
  }

  void setUpstreamClusterMetadata(const std::string& namespace_str, const std::string& metadata_key,
                                  const std::string& metadata_value) {
    Envoy::Config::Metadata::mutableMetadataValue(metadata_, namespace_str, metadata_key)
        .set_string_value(metadata_value);
    EXPECT_CALL(*cluster_info_, metadata()).WillRepeatedly(testing::ReturnPointee(&metadata_));
    stream_info_.setUpstreamClusterInfo(cluster_info_);
  }

  void setUpstreamRouteMetadata(const std::string& namespace_str, const std::string& metadata_key,
                                const std::string& metadata_value) {
    Envoy::Config::Metadata::mutableMetadataValue(metadata_, namespace_str, metadata_key)
        .set_string_value(metadata_value);
    EXPECT_CALL(*route_, metadata()).WillRepeatedly(testing::ReturnPointee(&metadata_));
    EXPECT_CALL(stream_info_, route()).WillRepeatedly(testing::ReturnPointee(&route_));
  }

  void setDynamicMetadata(const std::string& namespace_str, const std::string& metadata_key,
                          const std::string& metadata_value) {
    Envoy::Config::Metadata::mutableMetadataValue(stream_info_.metadata_, namespace_str,
                                                  metadata_key)
        .set_string_value(metadata_value);
  }

  Matcher::StringActionFactory action_factory_;
  Registry::InjectFactory<Matcher::ActionFactory<absl::string_view>> inject_action_;
  std::shared_ptr<testing::NiceMock<Upstream::MockClusterInfo>> cluster_info_;
  std::shared_ptr<NiceMock<Router::MockRoute>> route_;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  absl::string_view context_ = "";
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  envoy::config::core::v3::Metadata metadata_;

  TestRequestHeaderMapImpl default_headers_{
      {":method", "GET"}, {":scheme", "http"}, {":authority", "host"}};

  Envoy::Http::Matching::HttpMatchingDataImpl data_;
};

TEST_F(CelMatcherTest, CelMatcherRequestHeaderMatched) {
  auto matcher_tree = buildMatcherTree(RequestHeaderCelExprString);

  TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"authenticated_user", "staging"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherRequestHeaderNotMatched) {
  auto matcher_tree = buildMatcherTree(RequestHeaderCelExprString);

  // Build header with request header value field mismatched case.
  TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"authenticated_user", "NOT_MATCHED"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);

  // Build header with request header key field mismatched case.
  TestRequestHeaderMapImpl request_headers_2 = default_headers_;
  buildCustomHeader({{"NOT_MATCHED", "staging"}}, request_headers_2);
  Envoy::Http::Matching::HttpMatchingDataImpl data_2 =
      Envoy::Http::Matching::HttpMatchingDataImpl(stream_info_);
  data_2.onRequestHeaders(request_headers_2);
  const auto result_2 = matcher_tree->match(data_2);
  // The match was completed, no match found.
  EXPECT_EQ(result_2.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_EQ(result_2.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, CelMatcherClusterMetadataMatched) {
  setUpstreamClusterMetadata(std::string(kFilterNamespace), std::string(kMetadataKey),
                             std::string(kMetadataValue));
  Envoy::Http::Matching::HttpMatchingDataImpl data =
      Envoy::Http::Matching::HttpMatchingDataImpl(stream_info_);
  auto matcher_tree = buildMatcherTree(absl::StrFormat(
      UpstreamClusterMetadataCelString, kFilterNamespace, kMetadataKey, kMetadataValue));
  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherClusterMetadataNotMatched) {
  setUpstreamClusterMetadata(std::string(kFilterNamespace), std::string(kMetadataKey),
                             "wrong_service");
  Envoy::Http::Matching::HttpMatchingDataImpl data =
      Envoy::Http::Matching::HttpMatchingDataImpl(stream_info_);
  auto matcher_tree = buildMatcherTree(absl::StrFormat(
      UpstreamClusterMetadataCelString, kFilterNamespace, kMetadataKey, kMetadataValue));

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, CelMatcherRouteMetadataMatched) {
  setUpstreamRouteMetadata(std::string(kFilterNamespace), std::string(kMetadataKey),
                           std::string(kMetadataValue));
  Envoy::Http::Matching::HttpMatchingDataImpl data =
      Envoy::Http::Matching::HttpMatchingDataImpl(stream_info_);
  auto matcher_tree = buildMatcherTree(absl::StrFormat(
      UpstreamRouteMetadataCelString, kFilterNamespace, kMetadataKey, kMetadataValue));
  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherRouteMetadataNotMatched) {
  setUpstreamRouteMetadata(std::string(kFilterNamespace), std::string(kMetadataKey),
                           "wrong_service");
  Envoy::Http::Matching::HttpMatchingDataImpl data =
      Envoy::Http::Matching::HttpMatchingDataImpl(stream_info_);
  auto matcher_tree = buildMatcherTree(absl::StrFormat(
      UpstreamClusterMetadataCelString, kFilterNamespace, kMetadataKey, kMetadataValue));

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, CelMatcherDynamicMetadataMatched) {
  setDynamicMetadata(std::string(kFilterNamespace), std::string(kMetadataKey),
                     std::string(kMetadataValue));
  Envoy::Http::Matching::HttpMatchingDataImpl data =
      Envoy::Http::Matching::HttpMatchingDataImpl(stream_info_);
  auto matcher_tree = buildMatcherTree(
      absl::StrFormat(DynamicMetadataCelString, kFilterNamespace, kMetadataKey, kMetadataValue));
  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherDynamicMetadataNotMatched) {
  setDynamicMetadata(std::string(kFilterNamespace), std::string(kMetadataKey), "wrong_service");
  Envoy::Http::Matching::HttpMatchingDataImpl data =
      Envoy::Http::Matching::HttpMatchingDataImpl(stream_info_);
  auto matcher_tree = buildMatcherTree(
      absl::StrFormat(DynamicMetadataCelString, kFilterNamespace, kMetadataKey, kMetadataValue));

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, CelMatcherRequestHeaderPathMatched) {
  auto matcher_tree = buildMatcherTree(RequestPathCelExprString);

  TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{":path", "/foo"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherRequestHeaderPathNotMatched) {
  auto matcher_tree = buildMatcherTree(RequestPathCelExprString);

  TestRequestHeaderMapImpl request_headers = default_headers_;
  // The matching condition is: request.path == '/foo'.
  buildCustomHeader({{":path", "/bar"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, CelMatcherResponseHeaderMatched) {
  auto matcher_tree = buildMatcherTree(ReponseHeaderCelExprString);

  TestResponseHeaderMapImpl response_headers;
  response_headers.addCopy(LowerCaseString(":status"), "200");
  response_headers.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers.addCopy(LowerCaseString("content-length"), "3");
  data_.onResponseHeaders(response_headers);

  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherResponseHeaderNotMatched) {
  auto matcher_tree = buildMatcherTree(ReponseHeaderCelExprString);

  TestResponseHeaderMapImpl response_headers = {{"content-type", "text/html"}};
  data_.onResponseHeaders(response_headers);

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, CelMatcherResponseTrailerMatched) {
  auto matcher_tree = buildMatcherTree(ReponseTrailerCelExprString);

  TestResponseTrailerMapImpl response_trailers = {{"transfer-encoding", "chunked"}};
  data_.onResponseTrailers(response_trailers);

  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherResponseTrailerNotMatched) {
  auto matcher_tree = buildMatcherTree(ReponseTrailerCelExprString);

  TestResponseTrailerMapImpl response_trailers = {{"transfer-encoding", "chunked_not_matched"}};
  data_.onResponseTrailers(response_trailers);

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, CelMatcherRequestHeaderAndPathMatched) {
  auto matcher_tree = buildMatcherTree(RequestHeaderAndPathCelString);

  TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"user", "staging"}, {":path", "/foo"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherRequestHeaderAndPathNotMatched) {
  auto matcher_tree = buildMatcherTree(RequestHeaderAndPathCelString);

  TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"user", "prod"}, {":path", "/foo"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, CelMatcherRequestHeaderOrPathMatched) {
  auto matcher_tree = buildMatcherTree(RequestHeaderOrPathCelString);

  TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"user", "prod"}, {":path", "/foo"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherRequestHeaderOrPathNotMatched) {
  auto matcher_tree = buildMatcherTree(RequestHeaderOrPathCelString);

  TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"user", "prod"}, {":path", "/bar"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, CelMatcherRequestResponseMatched) {
  auto matcher_tree = buildMatcherTree(RequestAndResponseCelString);

  TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"user", "staging"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  TestResponseHeaderMapImpl response_headers = {{"content-type", "text/plain"}};
  data_.onResponseHeaders(response_headers);

  TestResponseTrailerMapImpl response_trailers = {{"transfer-encoding", "chunked"}};
  data_.onResponseTrailers(response_trailers);

  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherRequestResponseNotMatched) {
  auto matcher_tree = buildMatcherTree(RequestAndResponseCelString);

  TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"user", "staging"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  TestResponseHeaderMapImpl response_headers = {{"content-type", "text/html"}};
  data_.onResponseHeaders(response_headers);

  TestResponseTrailerMapImpl response_trailers = {{"transfer-encoding", "chunked"}};
  data_.onResponseTrailers(response_trailers);

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, CelMatcherRequestResponseMatchedWithParsedExpr) {
  auto matcher_tree =
      buildMatcherTree(RequestAndResponseCelString, ExpressionType::ParsedExpression);

  TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"user", "staging"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  TestResponseHeaderMapImpl response_headers = {{"content-type", "text/plain"}};
  data_.onResponseHeaders(response_headers);

  TestResponseTrailerMapImpl response_trailers = {{"transfer-encoding", "chunked"}};
  data_.onResponseTrailers(response_trailers);

  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherRequestResponseNotMatchedWithParsedExpr) {
  auto matcher_tree =
      buildMatcherTree(RequestAndResponseCelString, ExpressionType::ParsedExpression);

  TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"user", "staging"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  TestResponseHeaderMapImpl response_headers = {{"content-type", "text/html"}};
  data_.onResponseHeaders(response_headers);

  TestResponseTrailerMapImpl response_trailers = {{"transfer-encoding", "chunked"}};
  data_.onResponseTrailers(response_trailers);

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, NoCelExpression) {
  EXPECT_DEATH(buildMatcherTree(RequestHeaderCelExprString, ExpressionType::NoExpression),
               ".*panic: unset oneof.*");
}

} // namespace CelMatcher
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
