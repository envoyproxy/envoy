#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/extensions/expr/custom_cel_vocabulary/extended_request/v3/config.pb.h"
#include "envoy/protobuf/message_validator.h"

#include "source/extensions/filters/common/expr/custom_cel/custom_cel_vocabulary.h"
#include "source/extensions/filters/common/expr/custom_cel/extended_request/custom_cel_attributes.h"
#include "source/extensions/filters/common/expr/custom_cel/extended_request/extended_request_cel_vocabulary.h"
#include "source/extensions/filters/common/expr/evaluator.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/custom_cel_test_config.h"
#include "test/test_common/utility.h"

#include "eval/public/activation.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

// #pragma GCC diagnostic ignored "-Wunused-parameter"
// This pragma directive is a temporary solution for the following problem:
// The GitHub pipeline uses a gcc compiler which generates an error about unused parameters
// for FunctionAdapter in cel_function_adapter.h
// The problem of the unused parameters has been fixed in more recent version of the cel-cpp
// library. However, it is not possible to upgrade the cel-cpp in envoy at this time
// as it is waiting on the release of the one of its dependencies.
// TODO(b/219971889): Remove #pragma directives once the cel-cpp dependency has been upgraded.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include "eval/public/cel_function_adapter.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace CustomCel {
namespace ExtendedRequest {

using envoy::extensions::expr::custom_cel_vocabulary::extended_request::v3::
    ExtendedRequestCelVocabularyConfig;
using google::api::expr::runtime::CelFunctionDescriptor;
using google::api::expr::runtime::CelFunctionRegistry;

// tests Extended Request CEL Vocabulary implementation and factory

TEST(ExtendedRequestCelVocabularyFactoryTests, CreateCustomCelVocabularyFromProtoTest) {
  ExtendedRequestCelVocabularyConfig config;
  config.set_return_url_query_string_as_map(true);
  ExtendedRequestCelVocabularyFactory factory;
  CustomCelVocabularyPtr custom_cel_vocabulary;
  // the object should be created from the config without an exception being thrown by
  // validation visitor
  EXPECT_NO_THROW(custom_cel_vocabulary = factory.createCustomCelVocabulary(
                      config, ProtobufMessage::getStrictValidationVisitor()));
  EXPECT_TRUE(custom_cel_vocabulary);
  ExtendedRequestCelVocabulary* extended_request_cel_vocabulary =
      dynamic_cast<ExtendedRequestCelVocabulary*>(custom_cel_vocabulary.get());
  EXPECT_TRUE(extended_request_cel_vocabulary->returnUrlQueryStringAsMap());
}

TEST(ExtendedRequestCelVocabularyFactoryTests, CreateEmptyConfigProtoTest) {
  ExtendedRequestCelVocabularyFactory factory;
  ProtobufTypes::MessagePtr message = factory.createEmptyConfigProto();
  EXPECT_TRUE(message);
  ExtendedRequestCelVocabularyConfig* extended_request_cel_vocab_config =
      dynamic_cast<ExtendedRequestCelVocabularyConfig*>(message.get());
  EXPECT_TRUE(extended_request_cel_vocab_config);
}

TEST(ExtendedRequestCelVocabularyFactoryTests, FactoryCategoryTest) {
  ExtendedRequestCelVocabularyFactory factory;
  auto category = factory.category();
  EXPECT_EQ(category, "envoy.expr.custom_cel_vocabulary_config");
}

TEST(ExtendedRequestCelVocabularyFactoryTests, FactoryNameTest) {
  ExtendedRequestCelVocabularyFactory factory;
  auto name = factory.name();
  EXPECT_EQ(name, "envoy.expr.custom_cel_vocabulary.extended_request");
}

class ExtendedRequestCelVocabularyTests : public testing::Test {
public:
  std::array<absl::string_view, 1> attribute_set_names = {ExtendedRequest};
  std::array<absl::string_view, 2> lazy_function_names = {LazyFuncNameCookie,
                                                          LazyFuncNameCookieValue};
  std::array<absl::string_view, 1> static_function_names = {StaticFuncNameUrl};
};

TEST_F(ExtendedRequestCelVocabularyTests, FillActivationTest) {
  ExtendedRequestCelVocabulary custom_cel_vocabulary(false);
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  Protobuf::Arena arena;
  Activation activation;
  Http::TestRequestHeaderMapImpl request_headers;

  custom_cel_vocabulary.fillActivation(&activation, arena, mock_stream_info, &request_headers,
                                       nullptr, nullptr);

  // verify that the attributes in the activation
  for (int i = 0; static_cast<size_t>(i) < attribute_set_names.size(); ++i) {
    EXPECT_TRUE(activation.FindValue(attribute_set_names[i], &arena).has_value());
  }
  // verify that the functions are in the activation
  for (int i = 0; static_cast<size_t>(i) < lazy_function_names.size(); ++i) {
    EXPECT_EQ(activation.FindFunctionOverloads(lazy_function_names[i]).size(), 1);
  }
}

TEST_F(ExtendedRequestCelVocabularyTests, FillActivationWithNullRequestHeadersTest) {
  ExtendedRequestCelVocabulary custom_cel_vocabulary(false);
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  Protobuf::Arena arena;
  Activation activation;

  custom_cel_vocabulary.fillActivation(&activation, arena, mock_stream_info, nullptr, nullptr,
                                       nullptr);
  // verify that value producers have not been added
  for (int i = 0; static_cast<size_t>(i) < attribute_set_names.size(); ++i) {
    EXPECT_FALSE(activation.FindValue(attribute_set_names[i], &arena).has_value());
  }
  // verify that the functions are NOT in the activation
  for (int i = 0; static_cast<size_t>(i) < lazy_function_names.size(); ++i) {
    EXPECT_EQ(activation.FindFunctionOverloads(lazy_function_names[i]).size(), 0);
  }
}

// evaluateExpressionWithCustomCelVocabulary:
// Given an activation with mappings for vocabulary,
// create a RBAC policy with a condition derived from the given yaml,
// create a CEL expression, and evaluate it.
absl::StatusOr<CelValue> evaluateExpressionWithCustomCelVocabulary(
    Activation& activation, Protobuf::Arena& arena, const absl::string_view& expr_yaml,
    const absl::string_view& param, ExtendedRequestCelVocabulary& custom_cel_vocabulary) {
  envoy::config::rbac::v3::Policy policy;
  policy.mutable_condition()->MergeFrom(TestUtility::parseYaml<google::api::expr::v1alpha1::Expr>(
      fmt::format(std::string(expr_yaml), param)));

  using Envoy::Extensions::Filters::Common::Expr::BuilderPtr;
  using Envoy::Extensions::Filters::Common::Expr::ExpressionPtr;
  using Envoy::Extensions::Filters::Common::Expr::CustomCel::ExtendedRequest::
      ExtendedRequestCelVocabulary;
  BuilderPtr builder =
      Envoy::Extensions::Filters::Common::Expr::createBuilder(nullptr, &custom_cel_vocabulary);
  ExpressionPtr expr = Expr::createExpression(*builder, policy.condition());
  return expr->Evaluate(activation, &arena);
}

TEST_F(ExtendedRequestCelVocabularyTests,
       ReplaceDefaultMappingsWithCustomMappingsInActivationTest) {
  using TestConfig::QueryExpr;
  const std::string path = R"EOF(/query?key1=apple&key2=banana)EOF";

  ExtendedRequestCelVocabulary custom_cel_vocabulary(true);
  Http::TestRequestHeaderMapImpl request_headers{{":path", path}};
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  Protobuf::Arena arena;
  Activation activation;

  activation.InsertValueProducer(
      Request, std::make_unique<RequestWrapper>(arena, &request_headers, mock_stream_info));

  // Evaluate: request[query] as map - request[query][key1].contains(apple)
  // The activation does not contain the mappings for the custom CEL vocabulary yet.
  // Evaluation of the expression with custom vocabulary should not work.
  auto custom_field_evalation_result = evaluateExpressionWithCustomCelVocabulary(
      activation, arena, QueryExpr, "apple", custom_cel_vocabulary);
  EXPECT_FALSE(custom_field_evalation_result.ok());

  custom_cel_vocabulary.fillActivation(&activation, arena, mock_stream_info, &request_headers,
                                       nullptr, nullptr);

  // The activation now contains the mappings for the custom CEL vocabulary.
  // Evaluation of the expression with custom vocabulary should work.
  custom_field_evalation_result = evaluateExpressionWithCustomCelVocabulary(
      activation, arena, QueryExpr, "apple", custom_cel_vocabulary);
  EXPECT_TRUE(custom_field_evalation_result->IsBool() &&
              custom_field_evalation_result->BoolOrDie());
}

TEST_F(ExtendedRequestCelVocabularyTests, AddCustomMappingsToActivationTwiceTest) {
  ExtendedRequestCelVocabulary custom_cel_vocabulary(true);
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  Protobuf::Arena arena;
  absl::Status status;
  Activation activation;

  using google::api::expr::runtime::FunctionAdapter;

  activation.InsertValueProducer(ExtendedRequest, std::make_unique<ExtendedRequestWrapper>(
                                                      arena, nullptr, mock_stream_info, false));

  Http::TestRequestHeaderMapImpl request_headers;
  auto result_or = FunctionAdapter<CelValue>::Create(
      LazyFuncNameCookie, false,
      std::function<CelValue(Protobuf::Arena*)>(
          [request_headers](Protobuf::Arena* arena) { return cookie(arena, request_headers); }));
  if (result_or.ok()) {
    auto cel_function = std::move(result_or.value());
    status = activation.InsertFunction(std::move(cel_function));
  }
  result_or = FunctionAdapter<CelValue, CelValue>::Create(
      LazyFuncNameCookieValue, false,
      std::function<CelValue(Protobuf::Arena*, CelValue)>(
          [request_headers](Protobuf::Arena* arena, CelValue key) {
            return cookieValue(arena, request_headers, key);
          }));
  if (result_or.ok()) {
    auto cel_function = std::move(result_or.value());
    status = activation.InsertFunction(std::move(cel_function));
  }

  custom_cel_vocabulary.fillActivation(&activation, arena, mock_stream_info, &request_headers,
                                       nullptr, nullptr);

  // verify that the attribute sets are in the activation
  for (int i = 0; static_cast<size_t>(i) < attribute_set_names.size(); ++i) {
    EXPECT_TRUE(activation.FindValue(attribute_set_names[i], &arena).has_value());
  }
  // verify that the functions are in the activation (once)
  for (int i = 0; static_cast<size_t>(i) < lazy_function_names.size(); ++i) {
    EXPECT_EQ(activation.FindFunctionOverloads(lazy_function_names[i]).size(), 1);
  }
}

TEST_F(ExtendedRequestCelVocabularyTests, RegisterFunctionsTest) {
  google::api::expr::runtime::CelFunctionRegistry registry;
  ExtendedRequestCelVocabulary custom_cel_vocabulary(true);
  const CelFunctionDescriptor* function_descriptor;

  custom_cel_vocabulary.registerFunctions(&registry);
  auto functions = registry.ListFunctions();

  // verify that functions are in the registry
  for (int i = 0; static_cast<size_t>(i) < lazy_function_names.size(); ++i) {
    EXPECT_EQ(functions.count(lazy_function_names[i]), 1);
    function_descriptor = functions[lazy_function_names[i]].front();
    EXPECT_EQ(registry
                  .FindLazyOverloads(lazy_function_names[i], function_descriptor->receiver_style(),
                                     function_descriptor->types())
                  .size(),
              1);
  }

  for (int i = 0; static_cast<size_t>(i) < static_function_names.size(); ++i) {
    EXPECT_EQ(functions.count(static_function_names[i]), 1);
    function_descriptor = functions[static_function_names[i]].front();
    EXPECT_EQ(registry
                  .FindOverloads(static_function_names[i], function_descriptor->receiver_style(),
                                 function_descriptor->types())
                  .size(),
              1);
  }
}

TEST_F(ExtendedRequestCelVocabularyTests, AddRegistrationsToRegistryTwiceTest) {
  ExtendedRequestCelVocabulary custom_cel_vocabulary(true);
  google::api::expr::runtime::CelFunctionRegistry registry;
  const CelFunctionDescriptor* function_descriptor;
  absl::Status status;

  using google::api::expr::runtime::FunctionAdapter;

  status = registry.Register(std::make_unique<UrlFunction>(StaticFuncNameUrl));

  custom_cel_vocabulary.registerFunctions(&registry);
  auto functions = registry.ListFunctions();

  // verify that functions are in the registry (once)
  for (int i = 0; static_cast<size_t>(i) < lazy_function_names.size(); ++i) {
    EXPECT_EQ(functions.count(lazy_function_names[i]), 1);
    function_descriptor = functions[lazy_function_names[i]].front();
    EXPECT_EQ(registry
                  .FindLazyOverloads(lazy_function_names[i], function_descriptor->receiver_style(),
                                     function_descriptor->types())
                  .size(),
              1);
  }

  for (int i = 0; static_cast<size_t>(i) < static_function_names.size(); ++i) {
    EXPECT_EQ(functions.count(static_function_names[i]), 1);
    function_descriptor = functions[static_function_names[i]].front();
    EXPECT_EQ(registry
                  .FindOverloads(static_function_names[i], function_descriptor->receiver_style(),
                                 function_descriptor->types())
                  .size(),
              1);
  }
}

} // namespace ExtendedRequest
} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
