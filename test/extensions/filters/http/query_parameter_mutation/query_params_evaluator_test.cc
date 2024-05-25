#include <memory>
#include <string>

#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/http/query_parameter_mutation/query_params_evaluator.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace QueryParameterMutation {

class QueryParamsEvaluatorTest : public testing::Test {
public:
  std::string evaluateWithPath(const std::string& path) {
    auto paramsEvaluator =
        std::make_unique<QueryParamsEvaluator>(query_params_to_add_, query_params_to_remove_);
    Http::TestRequestHeaderMapImpl request_headers{{":path", path}};
    paramsEvaluator->evaluateQueryParams(request_headers, stream_info_);
    return std::string(request_headers.getPathValue());
  }

  void addParamToAdd(absl::string_view key, absl::string_view value) {
    auto* query_param = query_params_to_add_.Add();
    query_param->set_key(key);
    query_param->set_value(value);
  }

  void addParamToRemove(absl::string_view key) {
    auto* remove_param = query_params_to_remove_.Add();
    *remove_param = key;
  }

  void setFilterData(absl::string_view key, absl::string_view value) {
    stream_info_.filter_state_->setData(key, std::make_unique<Router::StringAccessorImpl>(value),
                                        StreamInfo::FilterState::StateType::ReadOnly,
                                        StreamInfo::FilterState::LifeSpan::FilterChain);
  }

  Protobuf::RepeatedPtrField<envoy::config::core::v3::QueryParameter> query_params_to_add_;
  Protobuf::RepeatedPtrField<std::string> query_params_to_remove_;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(QueryParamsEvaluatorTest, EmptyConfigEvaluator) {
  const auto old_path = "/path?this=should&stay=the&exact=same";
  const auto new_path = evaluateWithPath(old_path);
  EXPECT_EQ("/path?this=should&stay=the&exact=same", new_path);
}

TEST_F(QueryParamsEvaluatorTest, AddMultipleParams) {
  addParamToAdd("foo", "value_1");
  addParamToAdd("foo", "value_2");

  const auto old_path = "/path?bar=123";
  const auto new_path = evaluateWithPath(old_path);
  EXPECT_EQ("/path?bar=123&foo=value_1&foo=value_2", new_path);
}

TEST_F(QueryParamsEvaluatorTest, RemoveMultipleParams) {
  addParamToRemove("foo");

  const auto old_path = "/path?foo=value_1&foo=value_2&bar=123";
  const auto new_path = evaluateWithPath(old_path);
  EXPECT_EQ("/path?bar=123", new_path);
}

TEST_F(QueryParamsEvaluatorTest, AddEmptyValue) {
  addParamToAdd("foo", "");

  const auto old_path = "/path?bar=123";
  const auto new_path = evaluateWithPath(old_path);
  EXPECT_EQ("/path?bar=123&foo=", new_path);
}

TEST_F(QueryParamsEvaluatorTest, CommandSubstitution) {
  addParamToAdd("start_time", "%START_TIME(%s)%");
  setFilterData("variable", "substituted-value");

  const SystemTime start_time(std::chrono::seconds(1522796769));
  EXPECT_CALL(stream_info_, startTime()).WillRepeatedly(testing::Return(start_time));

  const auto old_path = "/path";
  const auto new_path = evaluateWithPath(old_path);
  EXPECT_EQ("/path?start_time=1522796769", new_path);
}

TEST_F(QueryParamsEvaluatorTest, CommandSubstitutionFilterState) {
  addParamToAdd("foo", "%FILTER_STATE(variable)%");
  setFilterData("variable", "substituted-value");

  const auto old_path = "/path?bar=123";
  const auto new_path = evaluateWithPath(old_path);
  EXPECT_EQ("/path?bar=123&foo=\"substituted-value\"", new_path);
}

} // namespace QueryParameterMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
