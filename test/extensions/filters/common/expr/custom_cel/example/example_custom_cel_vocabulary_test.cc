#include "envoy/protobuf/message_validator.h"

#include "source/extensions/filters/common/expr/custom_cel//custom_cel_vocabulary.h"
#include "source/extensions/filters/common/expr/custom_cel/example/example_custom_cel_vocabulary.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "eval/public/activation.h"
#include "eval/public/cel_function_adapter.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_Cel {
namespace Example {

using google::api::expr::runtime::CelFunctionDescriptor;
using google::api::expr::runtime::CelFunctionRegistry;

TEST(ExampleCustomCelVocabularyFactoryTests, CreateEmptyConfigProtoTest) {
  ExampleCustomCelVocabularyFactory factory;
  ProtobufTypes::MessagePtr message_ptr = factory.createEmptyConfigProto();
  ASSERT_TRUE(message_ptr);
}

TEST(ExampleCustomCelVocabularyFactoryTests, CreateCustomCelVocabularyTest) {
  ExampleCustomCelVocabularyConfig config;
  ExampleCustomCelVocabularyFactory factory;
  CustomCelVocabularyPtr custom_cel_vocabulary;
  // the object should be created from the config without an exception being thrown by validation
  // visitor
  EXPECT_NO_THROW(custom_cel_vocabulary = factory.createCustomCelVocabulary(
                      config, ProtobufMessage::getStrictValidationVisitor()));
}

class ExampleCustomCelVocabularyTests : public testing::Test {
public:
  std::array<absl::string_view, 3> lazy_eval_function_names = {
      LazyEvalFuncNameGetDouble, LazyEvalFuncNameGetProduct, LazyEvalFuncNameGet99};
  std::array<absl::string_view, 2> eager_eval_function_names = {EagerEvalFuncNameGetNextInt,
                                                                EagerEvalFuncNameGetSquareOf};
};

TEST_F(ExampleCustomCelVocabularyTests, FillActivationTest) {
  ExampleCustomCelVocabulary custom_cel_vocabulary;
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo mock_stream_info;
  Protobuf::Arena arena;

  Activation activation;

  // calling FillActivation for the first time; lazy functions should be registered
  EXPECT_NO_THROW(custom_cel_vocabulary.FillActivation(&activation, arena, mock_stream_info,
                                                       &request_headers, &response_headers,
                                                       &response_trailers));

  // verify that functions are in the activation
  for (int i = 0; static_cast<size_t>(i) < lazy_eval_function_names.size(); ++i) {
    EXPECT_EQ(activation.FindFunctionOverloads(lazy_eval_function_names[i]).size(), 1);
  }
  // calling FillActivation for the second time
  // an exception should be thrown as the functions are already registered
  EXPECT_THROW_WITH_REGEX(custom_cel_vocabulary.FillActivation(&activation, arena, mock_stream_info,
                                                               &request_headers, &response_headers,
                                                               &response_trailers),
                          EnvoyException, "failed to register function*");
}

TEST_F(ExampleCustomCelVocabularyTests, RegisterFunctionsTest) {
  google::api::expr::runtime::CelFunctionRegistry registry;
  ExampleCustomCelVocabulary custom_cel_vocabulary;
  const CelFunctionDescriptor* function_descriptor;

  EXPECT_NO_THROW(custom_cel_vocabulary.RegisterFunctions(&registry));
  auto functions = registry.ListFunctions();

  // verify that functions are in the registry
  for (int i = 0; static_cast<size_t>(i) < lazy_eval_function_names.size(); ++i) {
    EXPECT_EQ(functions.count(lazy_eval_function_names[i]), 1);
    function_descriptor = functions[lazy_eval_function_names[i]].front();
    EXPECT_EQ(registry
                  .FindLazyOverloads(lazy_eval_function_names[i],
                                     function_descriptor->receiver_style(),
                                     function_descriptor->types())
                  .size(),
              1);
  }

  for (int i = 0; static_cast<size_t>(i) < eager_eval_function_names.size(); ++i) {
    EXPECT_EQ(functions.count(eager_eval_function_names[i]), 1);
    function_descriptor = functions[eager_eval_function_names[i]].front();
    EXPECT_EQ(registry
                  .FindOverloads(eager_eval_function_names[i],
                                 function_descriptor->receiver_style(),
                                 function_descriptor->types())
                  .size(),
              1);
  }

  // an attempt to call RegisterFunctions a second time should fail
  EXPECT_THROW_WITH_REGEX(custom_cel_vocabulary.RegisterFunctions(&registry), EnvoyException,
                          "failed to register function*");
}

} // namespace Example
} // namespace Custom_Cel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
