#include "envoy/extensions/filters/http/api_key_auth/v3/api_key_auth.pb.h"

#include "source/extensions/filters/http/api_key_auth/api_key_auth.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ApiKeyAuth {

class FilterTest : public testing::Test {
public:
  void setup(const std::string& config_yaml, const std::string& scope_config_yaml) {
    ApiKeyAuthProto proto_config;
    TestUtility::loadFromYaml(config_yaml, proto_config);
    config_ = std::make_shared<FilterConfig>(proto_config, *stats_.rootScope(), "stats.");

    if (!scope_config_yaml.empty()) {
      ApiKeyAuthPerScopeProto scope_config_proto;
      TestUtility::loadFromYaml(scope_config_yaml, scope_config_proto);
      scope_config_ = std::make_shared<ScopeConfig>(scope_config_proto);
    }

    filter_ = std::make_shared<ApiKeyAuthFilter>(config_);
    ON_CALL(decoder_filter_callbacks_, mostSpecificPerFilterConfig())
        .WillByDefault(Invoke([this]() { return scope_config_.get(); }));
  }

  Stats::IsolatedStoreImpl stats_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks_;
  FilterConfigSharedPtr config_;
  std::shared_ptr<ScopeConfig> scope_config_;
  std::shared_ptr<ApiKeyAuthFilter> filter_;
};

TEST_F(FilterTest, NoHeaderApiKey) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - api_key: key1
        client_id: user1
  authentication_header: "Authorization"
  )EOF";

  setup(config_yaml, {});

  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "host"}, {":method", "GET"}, {":path", "/"}};

  EXPECT_CALL(decoder_filter_callbacks_,
              sendLocalReply(Http::Code::Unauthorized, _, _, _, "missing_api_key"));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));

  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.unauthorized").value(), 1);
}

TEST_F(FilterTest, MultipleHeaderApiKey) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - api_key: key1
        client_id: user1
  authentication_header: "Authorization"
  )EOF";

  setup(config_yaml, {});

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                 {":method", "GET"},
                                                 {":path", "/"},
                                                 {"Authorization", "Bearer key1"},
                                                 {"Authorization", "Bearer key2"}};

  EXPECT_CALL(decoder_filter_callbacks_,
              sendLocalReply(Http::Code::Unauthorized, _, _, _, "multiple_api_key"));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));

  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.unauthorized").value(), 1);
}

TEST_F(FilterTest, HeaderApiKey) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - api_key: key1
        client_id: user1
  authentication_header: "Authorization"
  )EOF";

  setup(config_yaml, {});

  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "host"}, {":method", "GET"}, {":path", "/"}, {"Authorization", "Bearer key1"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);
}

TEST_F(FilterTest, NoQueryApiKey) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - api_key: key1
        client_id: user1
  authentication_query: "api_key"
  )EOF";
  setup(config_yaml, {});
  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "host"}, {":method", "GET"}, {":path", "/path"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);
}

} // namespace ApiKeyAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
