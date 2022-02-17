#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/extensions/expr/custom_cel_vocabulary/example/v3/config.pb.h"
#include "envoy/protobuf/message_validator.h"

#include "source/extensions/filters/common/expr/custom_cel/custom_cel_vocabulary.h"
#include "source/extensions/filters/common/expr/custom_cel/example/custom_cel_variables.h"
#include "source/extensions/filters/common/expr/custom_cel/example/example_custom_cel_vocabulary.h"
#include "source/extensions/filters/common/expr/evaluator.h"

#include "test/mocks/stream_info/mocks.h"
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
namespace Custom_CEL {
namespace Example {

using google::api::expr::runtime::CelFunctionDescriptor;
using google::api::expr::runtime::CelFunctionRegistry;

// tests example custom CEL vocabulary implementation and factory

TEST(ExampleCustomCELVocabularyFactoryTests, CreateCustomCELVocabularyFromProtoTest) {
  ExampleCustomCELVocabularyConfig config;
  config.set_return_url_query_string_as_map(true);
  ExampleCustomCELVocabularyFactory factory;
  CustomCELVocabularyPtr custom_cel_vocabulary;
  // the object should be created from the config without an exception being thrown by
  // validation visitor
  EXPECT_NO_THROW(custom_cel_vocabulary = factory.createCustomCELVocabulary(
                      config, ProtobufMessage::getStrictValidationVisitor()));
  ASSERT_TRUE(custom_cel_vocabulary);
  ExampleCustomCELVocabulary* example_custom_cel_vocabulary =
      dynamic_cast<ExampleCustomCELVocabulary*>(custom_cel_vocabulary.get());
  ASSERT_TRUE(example_custom_cel_vocabulary->returnUrlQueryStringAsMap());
}

TEST(ExampleCustomCELVocabularyFactoryTests, CreateEmptyConfigProtoTest) {
  ExampleCustomCELVocabularyFactory factory;
  ProtobufTypes::MessagePtr message = factory.createEmptyConfigProto();
  ASSERT_TRUE(message);
  ExampleCustomCELVocabularyConfig* example_custom_cel_vocab_config =
      dynamic_cast<ExampleCustomCELVocabularyConfig*>(message.get());
  ASSERT_TRUE(example_custom_cel_vocab_config);
}

TEST(ExampleCustomCELVocabularyFactoryTests, FactoryCategoryTest) {
  ExampleCustomCELVocabularyFactory factory;
  auto category = factory.category();
  EXPECT_EQ(category, "envoy.expr.custom_cel_vocabulary_config");
}

TEST(ExampleCustomCELVocabularyFactoryTests, FactoryNameTest) {
  ExampleCustomCELVocabularyFactory factory;
  auto name = factory.name();
  EXPECT_EQ(name, "envoy.expr.custom_cel_vocabulary.example");
}

class ExampleCustomCELVocabularyTests : public testing::Test {
public:
  std::array<absl::string_view, 3> variable_set_names = {CustomVariablesName, SourceVariablesName,
                                                         ExtendedRequestVariablesName};
  std::array<absl::string_view, 3> lazy_function_names = {
      LazyFuncNameGetDouble, LazyFuncNameGetProduct, LazyFuncNameGetNextInt};
  std::array<absl::string_view, 2> static_function_names = {StaticFuncNameGet99,
                                                            StaticFuncNameGetSquareOf};
};

TEST_F(ExampleCustomCELVocabularyTests, FillActivationTest) {
  ExampleCustomCELVocabulary custom_cel_vocabulary(false);
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  Protobuf::Arena arena;
  Activation activation;

  custom_cel_vocabulary.fillActivation(&activation, arena, mock_stream_info, nullptr, nullptr,
                                       nullptr);

  // verify that the variable sets are in the activation
  for (int i = 0; static_cast<size_t>(i) < variable_set_names.size(); ++i) {
    ASSERT_TRUE(activation.FindValue(variable_set_names[i], &arena).has_value());
  }
  // verify that the functions are in the activation
  for (int i = 0; static_cast<size_t>(i) < lazy_function_names.size(); ++i) {
    EXPECT_EQ(activation.FindFunctionOverloads(lazy_function_names[i]).size(), 1);
  }
}

const std::string REQUEST_HAS_QUERY_EXPR = R"EOF(
          call_expr:
            function: _==_
            args:
            - select_expr:
                test_only: true
                operand:
                  ident_expr:
                    name: request
                field: query
            - const_expr:
               bool_value: true
)EOF";

const std::string SOURCE_HAS_DESCRIPTION_EXPR = R"EOF(
          call_expr:
            function: _==_
            args:
            - select_expr:
                test_only: true
                operand:
                  ident_expr:
                    name: source
                field: description
            - const_expr:
               bool_value: true
)EOF";

// evaluateExpressionWithCustomCELVocabulary:
// Given an activation with mappings for vocabulary,
// create a RBAC policy with a condition derived from the given yaml,
// create a CEL expression, and evaluate it.
absl::StatusOr<CelValue>
evaluateExpressionWithCustomCELVocabulary(Activation& activation, Protobuf::Arena& arena,
                                          const std::string& expr_yaml,
                                          ExampleCustomCELVocabulary& custom_cel_vocabulary) {
  envoy::config::rbac::v3::Policy policy;
  policy.mutable_condition()->MergeFrom(
      TestUtility::parseYaml<google::api::expr::v1alpha1::Expr>(expr_yaml));

  using Envoy::Extensions::Filters::Common::Expr::BuilderPtr;
  using Envoy::Extensions::Filters::Common::Expr::ExpressionPtr;
  using Envoy::Extensions::Filters::Common::Expr::Custom_CEL::Example::ExampleCustomCELVocabulary;
  BuilderPtr builder =
      Envoy::Extensions::Filters::Common::Expr::createBuilder(nullptr, &custom_cel_vocabulary);
  ExpressionPtr expr = Expr::createExpression(*builder, policy.condition());
  return expr->Evaluate(activation, &arena);
}

TEST_F(ExampleCustomCELVocabularyTests, ReplaceDefaultMappingsWithCustomMappingsInActivationTest) {
  ExampleCustomCELVocabulary custom_cel_vocabulary(true);
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/query?a=apple&a=apricot&b=banana&=&c=cranberry"}};
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  Protobuf::Arena arena;
  Activation activation;

  activation.InsertValueProducer(Source, std::make_unique<PeerWrapper>(mock_stream_info, false));
  activation.InsertValueProducer(
      Request, std::make_unique<RequestWrapper>(arena, &request_headers, mock_stream_info));

  // The activation does not contain the mappings for the custom CEL vocabulary yet.
  // The check for custom CEL fields should evaluate to false.
  auto has_custom_field_status = evaluateExpressionWithCustomCELVocabulary(
      activation, arena, SOURCE_HAS_DESCRIPTION_EXPR, custom_cel_vocabulary);
  ASSERT_TRUE(has_custom_field_status.ok() && has_custom_field_status.value().IsBool() &&
              !has_custom_field_status.value().BoolOrDie());
  has_custom_field_status = evaluateExpressionWithCustomCELVocabulary(
      activation, arena, REQUEST_HAS_QUERY_EXPR, custom_cel_vocabulary);
  ASSERT_TRUE(has_custom_field_status.ok() && has_custom_field_status.value().IsBool() &&
              !has_custom_field_status.value().BoolOrDie());

  custom_cel_vocabulary.fillActivation(&activation, arena, mock_stream_info, &request_headers,
                                       nullptr, nullptr);

  // The activation now contains the mappings for the custom CEL vocabulary.
  // The check for custom CEL fields should evaluate to true.
  has_custom_field_status = evaluateExpressionWithCustomCELVocabulary(
      activation, arena, SOURCE_HAS_DESCRIPTION_EXPR, custom_cel_vocabulary);
  ASSERT_TRUE(has_custom_field_status.ok() && has_custom_field_status.value().IsBool() &&
              has_custom_field_status.value().BoolOrDie());
  has_custom_field_status = evaluateExpressionWithCustomCELVocabulary(
      activation, arena, REQUEST_HAS_QUERY_EXPR, custom_cel_vocabulary);
  ASSERT_TRUE(has_custom_field_status.ok() && has_custom_field_status.value().IsBool() &&
              has_custom_field_status.value().BoolOrDie());
}

TEST_F(ExampleCustomCELVocabularyTests, AddCustomMappingsToActivationTwiceTest) {
  ExampleCustomCELVocabulary custom_cel_vocabulary(true);
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  Protobuf::Arena arena;
  absl::Status status;
  Activation activation;

  using google::api::expr::runtime::FunctionAdapter;

  activation.InsertValueProducer(CustomVariablesName,
                                 std::make_unique<CustomWrapper>(arena, mock_stream_info));
  activation.InsertValueProducer(SourceVariablesName,
                                 std::make_unique<SourceWrapper>(arena, mock_stream_info));
  activation.InsertValueProducer(
      ExtendedRequestVariablesName,
      std::make_unique<ExtendedRequestWrapper>(arena, nullptr, mock_stream_info, false));

  status = activation.InsertFunction(std::make_unique<GetDouble>(LazyFuncNameGetDouble));
  status = activation.InsertFunction(std::make_unique<GetProduct>(LazyFuncNameGetProduct));

  auto result_or =
      FunctionAdapter<CelValue, int64_t>::Create(LazyFuncNameGetNextInt, false, getNextInt);
  if (result_or.ok()) {
    auto cel_function = std::move(result_or.value());
    status = activation.InsertFunction(std::move(cel_function));
  }

  custom_cel_vocabulary.fillActivation(&activation, arena, mock_stream_info, nullptr, nullptr,
                                       nullptr);

  // verify that the variable sets are in the activation
  for (int i = 0; static_cast<size_t>(i) < variable_set_names.size(); ++i) {
    ASSERT_TRUE(activation.FindValue(variable_set_names[i], &arena).has_value());
  }
  // verify that the functions are in the activation
  for (int i = 0; static_cast<size_t>(i) < lazy_function_names.size(); ++i) {
    EXPECT_EQ(activation.FindFunctionOverloads(lazy_function_names[i]).size(), 1);
  }
}

TEST_F(ExampleCustomCELVocabularyTests, RegisterFunctionsTest) {
  google::api::expr::runtime::CelFunctionRegistry registry;
  ExampleCustomCELVocabulary custom_cel_vocabulary(true);
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

TEST_F(ExampleCustomCELVocabularyTests, AddRegistrationsToRegistryTwiceTest) {
  ExampleCustomCELVocabulary custom_cel_vocabulary(true);
  google::api::expr::runtime::CelFunctionRegistry registry;
  const CelFunctionDescriptor* function_descriptor;
  absl::Status status;

  using google::api::expr::runtime::FunctionAdapter;

  status = registry.RegisterLazyFunction(GetDouble::createDescriptor(LazyFuncNameGetDouble));
  status = registry.RegisterLazyFunction(GetProduct::createDescriptor(LazyFuncNameGetProduct));
  auto result_or =
      FunctionAdapter<CelValue, int64_t>::Create(LazyFuncNameGetNextInt, false, getNextInt);
  if (result_or.ok()) {
    auto cel_function = std::move(result_or.value());
    status = registry.RegisterLazyFunction(cel_function->descriptor());
  }

  status = registry.Register(std::make_unique<Get99>(StaticFuncNameGet99));
  status = FunctionAdapter<CelValue, int64_t>::CreateAndRegister(StaticFuncNameGetSquareOf, true,
                                                                 getSquareOf, &registry);

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

} // namespace Example
} // namespace Custom_CEL
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
