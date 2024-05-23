#include <memory>
#include <string>

#include "source/extensions/filters/http/query_parameter_mutation/query_params_evaluator.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace QueryParameterMutation {

TEST(QueryParamsEvaluatorTest, EmptyConfigEvaluator) {
  const Protobuf::RepeatedPtrField<envoy::config::core::v3::QueryParameter> query_params_to_add;
  const Protobuf::RepeatedPtrField<std::string> query_params_to_remove;

  auto paramsEvaluator =
      std::make_unique<QueryParamsEvaluator>(query_params_to_add, query_params_to_remove);
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path?this=should&stay=the&exact=same"}};
  paramsEvaluator->evaluateQueryParams(request_headers);
  EXPECT_EQ("/path?this=should&stay=the&exact=same", request_headers.getPathValue());
}

TEST(QueryParamsEvaluatorTest, AddMultipleParams) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::QueryParameter> query_params_to_add;
  auto* query_param = query_params_to_add.Add();
  query_param->set_key("foo");
  query_param->set_value("value_1");
  query_param = query_params_to_add.Add();
  query_param->set_key("foo");
  query_param->set_value("value_2");

  const Protobuf::RepeatedPtrField<std::string> query_params_to_remove;
  auto paramsEvaluator =
      std::make_unique<QueryParamsEvaluator>(query_params_to_add, query_params_to_remove);
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/path?bar=123"}};
  paramsEvaluator->evaluateQueryParams(request_headers);
  EXPECT_EQ("/path?bar=123&foo=value_1&foo=value_2", request_headers.getPathValue());
}

TEST(QueryParamsEvaluatorTest, RemoveMultipleParams) {
  const Protobuf::RepeatedPtrField<envoy::config::core::v3::QueryParameter> query_params_to_add;

  const std::vector<std::string> remove{"foo"};
  const Protobuf::RepeatedPtrField<std::string> query_params_to_remove(remove.begin(),
                                                                       remove.end());

  auto paramsEvaluator =
      std::make_unique<QueryParamsEvaluator>(query_params_to_add, query_params_to_remove);
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path?foo=value_1&foo=value_2&bar=123"}};
  paramsEvaluator->evaluateQueryParams(request_headers);
  EXPECT_EQ("/path?bar=123", request_headers.getPathValue());
}

TEST(QueryParamsEvaluatorTest, AddEmptyValue) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::QueryParameter> query_params_to_add;
  auto* query_param = query_params_to_add.Add();
  query_param->set_key("foo");
  query_param->set_value("");

  const Protobuf::RepeatedPtrField<std::string> query_params_to_remove;
  auto paramsEvaluator =
      std::make_unique<QueryParamsEvaluator>(query_params_to_add, query_params_to_remove);
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/path?bar=123"}};
  paramsEvaluator->evaluateQueryParams(request_headers);
  EXPECT_EQ("/path?bar=123&foo=", request_headers.getPathValue());
}

} // namespace QueryParameterMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
