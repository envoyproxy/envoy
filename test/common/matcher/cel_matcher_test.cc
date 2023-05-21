#include "test/common/matcher/cel_matcher_test.h"

#include <exception>
#include <memory>

#include "envoy/config/common/matcher/v3/matcher.pb.validate.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/registry/registry.h"

// #include "source/common/http/matching/inputs.h"
#include "source/common/http/matching/cel_input.h"
#include "source/common/matcher/cel_matcher.h"
#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"

#include "test/common/matcher/test_utility.h"
#include "test/mocks/matcher/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "xds/type/matcher/v3/matcher.pb.validate.h"

namespace Envoy {
namespace Matcher {

class CelMatcherTest : public ::testing::Test {
public:
  CelMatcherTest()
      : inject_action_(action_factory_),
        data_(Envoy::Http::Matching::HttpMatchingDataImpl(stream_info_)) {}

  void buildCustomHeader(const absl::flat_hash_map<std::string, std::string>& custom_value_pairs,
                         Http::TestRequestHeaderMapImpl& headers) {
    // Add custom_value_pairs to the request header.
    for (auto const& pair : custom_value_pairs) {
      headers.setCopy(Http::LowerCaseString(pair.first), pair.second);
    }
  }

  Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData>
  buildMatcherTree(const std::string& cel_expr_config) {
    google::api::expr::v1alpha1::CheckedExpr checked_expr;
    Protobuf::TextFormat::ParseFromString(cel_expr_config, &checked_expr);
    xds::type::matcher::v3::CelMatcher cel_matcher;
    cel_matcher.mutable_expr_match()->mutable_checked_expr()->MergeFrom(checked_expr);

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

    // TODO(tyxia) build remove
    // std::cout << on_match.DebugString() << std::endl;
    inner_matcher->mutable_on_match()->MergeFrom(on_match);

    auto string_factory_on_match = TestDataInputStringFactory("value");
    // TODO(tyxia) build remove
    // std::cout << matcher.DebugString() << std::endl;

    MockMatchTreeValidationVisitor<Envoy::Http::HttpMatchingData> validation_visitor;
    EXPECT_CALL(validation_visitor,
                performDataInputValidation(
                    _, "type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput"));
    Matcher::MatchTreeFactory<Envoy::Http::HttpMatchingData, absl::string_view> matcher_factory(
        context_, factory_context_, validation_visitor);
    auto match_tree = matcher_factory.create(matcher);

    return match_tree();
  }

  StringActionFactory action_factory_;
  Registry::InjectFactory<ActionFactory<absl::string_view>> inject_action_;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  absl::string_view context_ = "";
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;

  // Http::TestRequestHeaderMapImpl default_headers_{
  //     {":method", "GET"}, {":path", "/foo"}, {":scheme", "http"}, {":authority", "host"}};

  Http::TestRequestHeaderMapImpl default_headers_{
      {":method", "GET"}, {":scheme", "http"}, {":authority", "host"}};

  Envoy::Http::Matching::HttpMatchingDataImpl data_;
};

// google3/third_party/envoy/src/test/extensions/common/matcher/trie_matcher_test.cc
TEST_F(CelMatcherTest, CelMatcherRequestHeaderMatched) {
  // HttpCelDataInputTestFactory factory;
  //  Http::Matching::HttpCelDataInputFactory factory;
  //  Registry::InjectFactory<Http::Matching::HttpCelDataInputFactory> register_factory(factory);
  //  TODO(tyxia) Refer to envoy.matching.inputs.destination_ip!!!

  auto matcher_tree = buildMatcherTree(RequestHeadeCelExprString);

  Http::TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"authenticated_user", "staging"}}, request_headers);
  data_.onRequestHeaders(request_headers);
  // data.onRequestHeaders(request_headers);

  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherRequestHeaderNotMatched) {
  auto matcher_tree = buildMatcherTree(RequestHeadeCelExprString);

  // Build header with request header value field mismatched case.
  Http::TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"authenticated_user", "NOT_MATCHED"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);

  // Build header with request header key field mismatched case.
  Http::TestRequestHeaderMapImpl request_headers_2 = default_headers_;
  buildCustomHeader({{"NOT_MATCHED", "staging"}}, request_headers_2);
  Envoy::Http::Matching::HttpMatchingDataImpl data_2 =
      Envoy::Http::Matching::HttpMatchingDataImpl(stream_info_);
  data_2.onRequestHeaders(request_headers_2);
  const auto result_2 = matcher_tree->match(data_2);
  // The match was completed, no match found.
  EXPECT_EQ(result_2.match_state_, MatchState::MatchComplete);
  EXPECT_EQ(result_2.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, CelMatcherNoRequestAttributes) {
  auto matcher_tree = buildMatcherTree(RequestHeadeCelExprString);

  // No request attributes added to matching data.
  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, MatchState::UnableToMatch);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, CelMatcherRequestHeaderPathMatched) {
  auto matcher_tree = buildMatcherTree(RequestHeadePathCelExprString);

  Http::TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{":path", "/foo"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherRequestHeaderPathNotMatched) {
  auto matcher_tree = buildMatcherTree(RequestHeadePathCelExprString);

  Http::TestRequestHeaderMapImpl request_headers = default_headers_;
  // The matching condition is: request.path == '/foo'.
  buildCustomHeader({{":path", "/bar"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, CelMatcherResponseHeaderMatched) {
  auto matcher_tree = buildMatcherTree(ReponseHeadeCelExprString);

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.addCopy(Http::LowerCaseString(":status"), "200");
  response_headers.addCopy(Http::LowerCaseString("content-type"), "text/plain");
  response_headers.addCopy(Http::LowerCaseString("content-length"), "3");
  data_.onResponseHeaders(response_headers);

  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherResponseHeaderNotMatched) {
  auto matcher_tree = buildMatcherTree(ReponseHeadeCelExprString);

  Http::TestResponseHeaderMapImpl response_headers = {{"content-type", "text/html"}};
  data_.onResponseHeaders(response_headers);

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, CelMatcherResponseTrailerMatched) {
  auto matcher_tree = buildMatcherTree(ReponseTrailerCelExprString);

  Http::TestResponseTrailerMapImpl response_trailers = {{"transfer-encoding", "chunked"}};
  data_.onResponseTrailers(response_trailers);

  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherResponseTrailerNotMatched) {
  auto matcher_tree = buildMatcherTree(ReponseTrailerCelExprString);

  Http::TestResponseTrailerMapImpl response_trailers = {
      {"transfer-encoding", "chunked_not_matched"}};
  data_.onResponseTrailers(response_trailers);

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, CelMatcherRequestHeaderAndPathMatched) {
  auto matcher_tree = buildMatcherTree(RequestHeaderAndPathCelString);

  Http::TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"user", "staging"}, {":path", "/foo"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherRequestHeaderAndPathNotMatched) {
  auto matcher_tree = buildMatcherTree(RequestHeaderAndPathCelString);

  Http::TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"user", "prod"}, {":path", "/foo"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, CelMatcherRequestHeaderOrPathMatched) {
  auto matcher_tree = buildMatcherTree(RequestHeaderOrPathCelString);

  Http::TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"user", "prod"}, {":path", "/foo"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherRequestHeaderOrPathNotMatched) {
  auto matcher_tree = buildMatcherTree(RequestHeaderOrPathCelString);

  Http::TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"user", "prod"}, {":path", "/bar"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

TEST_F(CelMatcherTest, CelMatcherRequestResponseMatched) {
  auto matcher_tree = buildMatcherTree(RequestAndResponseCelString);

  Http::TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"user", "staging"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  Http::TestResponseHeaderMapImpl response_headers = {{"content-type", "text/plain"}};
  data_.onResponseHeaders(response_headers);

  Http::TestResponseTrailerMapImpl response_trailers = {{"transfer-encoding", "chunked"}};
  data_.onResponseTrailers(response_trailers);

  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(CelMatcherTest, CelMatcherRequestResponseNotMatched) {
  auto matcher_tree = buildMatcherTree(RequestAndResponseCelString);

  Http::TestRequestHeaderMapImpl request_headers = default_headers_;
  buildCustomHeader({{"user", "staging"}}, request_headers);
  data_.onRequestHeaders(request_headers);

  Http::TestResponseHeaderMapImpl response_headers = {{"content-type", "text/html"}};
  data_.onResponseHeaders(response_headers);

  Http::TestResponseTrailerMapImpl response_trailers = {{"transfer-encoding", "chunked"}};
  data_.onResponseTrailers(response_trailers);

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// class HttpCelDataTestInput : public Matcher::DataInput<TestData> {
//  public:
//   HttpCelDataTestInput() = default;
//   Matcher::DataInputGetResult get(const TestData&) const override {
//     return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
//             // TODO(tyxia) make unique
//             absl::monostate()};
//   }

//   virtual absl::string_view dataInputType() const override {
//      return "cel_data_input";
//   }

//  private:
// };

// // Envoy::Http::HttpMatchingData
// class HttpCelDataInputTestFactory
//     : public DataInputFactory<TestData> {
// public:
//   HttpCelDataInputTestFactory() : inject_factory_(*this) {}
//   std::string name() const override { return "envoy.matching.inputs.cel_data_input"; }

//   virtual DataInputFactoryCb<TestData>
//   createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&)
//   override {
//     return [] { return std::make_unique<HttpCelDataTestInput>(); };
//   }

//   virtual ProtobufTypes::MessagePtr createEmptyConfigProto() override {
//     return std::make_unique<xds::type::matcher::v3::HttpAttributesCelMatchInput>();
//   }

//   Registry::InjectFactory<Matcher::DataInputFactory<TestData>> inject_factory_;
// };

} // namespace Matcher
} // namespace Envoy
