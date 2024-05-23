#include <memory>

#include "envoy/extensions/filters/http/query_parameter_mutation/v3/config.pb.h"

#include "source/extensions/filters/http/query_parameter_mutation/filter.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace QueryParameterMutation {
using ::testing::NiceMock;

class FilterTest : public testing::Test {
public:
  Http::TestRequestHeaderMapImpl requestHeaders(const std::string& path) {
    return {{Http::Headers::get().Path.get(), path}};
  }

  envoy::extensions::filters::http::query_parameter_mutation::v3::Config proto_config_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
};

TEST_F(FilterTest, EmptyConfig) {
  auto config = std::make_shared<Config>(proto_config_);
  auto filter = std::make_unique<Filter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  const auto path = "/some?random=path";
  auto request_headers = requestHeaders(path);

  auto mock_config = std::make_shared<NiceMock<Envoy::Router::MockConfig>>();
  ON_CALL(decoder_callbacks_.route_->route_entry_.virtual_host_, routeConfig())
      .WillByDefault(testing::ReturnRef(*mock_config));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, false));
  // Path should be unchanged after running the filter.
  EXPECT_EQ(path, request_headers.Path()->value().getStringView());
}

TEST_F(FilterTest, RemoveQueryParameter) {
  auto remove_list = proto_config_.mutable_query_parameters_to_remove();
  remove_list->Add("random");
  auto config = std::make_shared<Config>(proto_config_);
  auto filter = std::make_unique<Filter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  const auto path = "/some?random=path";
  auto request_headers = requestHeaders(path);

  auto mock_config = std::make_shared<NiceMock<Envoy::Router::MockConfig>>();
  ON_CALL(decoder_callbacks_.route_->route_entry_.virtual_host_, routeConfig())
      .WillByDefault(testing::ReturnRef(*mock_config));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, false));
  EXPECT_EQ("/some", request_headers.Path()->value().getStringView());
}

} // namespace QueryParameterMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
