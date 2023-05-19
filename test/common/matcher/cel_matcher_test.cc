#include <exception>
#include <memory>

#include "envoy/config/common/matcher/v3/matcher.pb.validate.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/registry/registry.h"

#include "source/common/matcher/list_matcher.h"
#include "source/common/matcher/cel_matcher.h"
#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"
#include "source/common/http/matching/inputs.h"
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
  CelMatcherTest() : inject_action_(action_factory_) {}

  StringActionFactory action_factory_;
  Registry::InjectFactory<ActionFactory<absl::string_view>> inject_action_;

  absl::string_view context_ = "";
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
};

// google3/third_party/envoy/src/test/extensions/common/matcher/trie_matcher_test.cc

TEST_F(CelMatcherTest, TestCelMatcher) {
  //HttpCelDataInputTestFactory factory;
  // Http::Matching::HttpCelDataInputFactory factory;
  // Registry::InjectFactory<Http::Matching::HttpCelDataInputFactory> register_factory(factory);
  // TODO(tyxia) Refer to envoy.matching.inputs.destination_ip!!!
  std::string checked_expr_config = R"pb(
    expr {
      id: 8
      call_expr {
        function: "_==_"
        args {
          id: 6
          call_expr {
            function: "_[_]"
            args {
              id: 5
              select_expr {
                operand {
                  id: 4
                  ident_expr {name: "request"}
                }
                field: "headers"
              }
            }
            args {
              id: 7
              const_expr {
                string_value: "authenticated_user"
              }
            }
          }
        }
        args {
          id: 9
          const_expr { string_value: "staging" }
        }
      }
    }
  )pb";

  google::api::expr::v1alpha1::CheckedExpr checked_expr;
  Protobuf::TextFormat::ParseFromString(checked_expr_config, &checked_expr);
  xds::type::matcher::v3::CelMatcher cel_matcher;
  cel_matcher.mutable_expr_match()->mutable_checked_expr()->MergeFrom(checked_expr);

  xds::type::matcher::v3::Matcher matcher;
  auto* inner_matcher = matcher.mutable_matcher_list()->add_matchers();
  auto* single_predicate = inner_matcher->mutable_predicate()
                               ->mutable_single_predicate();

  xds::type::matcher::v3::HttpAttributesCelMatchInput cel_match_input;
  single_predicate->mutable_input()->set_name("envoy.matching.inputs.cel_data_input");
  single_predicate->mutable_input()->mutable_typed_config()->PackFrom(
      cel_match_input);

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
  MessageUtil::loadFromYaml(on_match_config, on_match, ProtobufMessage::getStrictValidationVisitor());

  std::cout << on_match.DebugString() << std::endl;
  inner_matcher->mutable_on_match()->MergeFrom(on_match);


  auto string_factory_on_match = TestDataInputStringFactory("value");

  // EXPECT_CALL(validation_visitor_,
  //             performDataInputValidation(_, "type.googleapis.com/google.protobuf.StringValue"));

  std::cout << matcher.DebugString() << std::endl;
  //auto match_tree = factory_.create(matcher);
  // const auto result = match_tree()->match(TestData());

  MockMatchTreeValidationVisitor<Envoy::Http::HttpMatchingData> validation_visitor;
  EXPECT_CALL(validation_visitor,
              performDataInputValidation(_, "type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput"));
  Matcher::MatchTreeFactory<Envoy::Http::HttpMatchingData, absl::string_view> matcher_factory(
      context_, factory_context_, validation_visitor);
  auto match_tree = matcher_factory.create(matcher);
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Envoy::Http::Matching::HttpMatchingDataImpl data = Envoy::Http::Matching::HttpMatchingDataImpl(stream_info);
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}, {"authenticated_user", "staging"}};
  data.onRequestHeaders(request_headers);
  const auto result = match_tree()->match(data);
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}


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
//   createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
//     return [] { return std::make_unique<HttpCelDataTestInput>(); };
//   }

//   virtual ProtobufTypes::MessagePtr createEmptyConfigProto() override {
//     return std::make_unique<xds::type::matcher::v3::HttpAttributesCelMatchInput>();
//   }

//   Registry::InjectFactory<Matcher::DataInputFactory<TestData>> inject_factory_;
// };

} // namespace Matcher
} // namespace Envoy
