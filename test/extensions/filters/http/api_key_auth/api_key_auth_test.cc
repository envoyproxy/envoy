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
  void setup(const std::string& config_yaml, const std::string& route_config_yaml) {
    ApiKeyAuthProto proto_config;
    TestUtility::loadFromYaml(config_yaml, proto_config);
    config_ = std::make_shared<FilterConfig>(proto_config, *stats_.rootScope(), "stats.");

    if (!route_config_yaml.empty()) {
      ApiKeyAuthPerRouteProto route_config_proto;
      TestUtility::loadFromYaml(route_config_yaml, route_config_proto);
      route_config_ = std::make_shared<RouteConfig>(route_config_proto);
    }

    filter_ = std::make_shared<ApiKeyAuthFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_filter_callbacks_);
    ON_CALL(decoder_filter_callbacks_, mostSpecificPerFilterConfig())
        .WillByDefault(Invoke([this]() { return route_config_.get(); }));
  }

  Stats::IsolatedStoreImpl stats_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks_;
  FilterConfigSharedPtr config_;
  std::shared_ptr<RouteConfig> route_config_;
  std::shared_ptr<ApiKeyAuthFilter> filter_;
};

TEST_F(FilterTest, NoHeaderApiKey) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - key: key1
        client: user1
  key_sources:
    entries:
      - header: "Authorization"
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

TEST_F(FilterTest, HeaderApiKey) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - key: key1
        client: user1
  key_sources:
    entries:
      - header: "Authorization"
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
      - key: key1
        client: user1
  key_sources:
    entries:
      - query: "api_key"
  )EOF";
  setup(config_yaml, {});
  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "host"}, {":method", "GET"}, {":path", "/path"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              sendLocalReply(Http::Code::Unauthorized, _, _, _, "missing_api_key"));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));

  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.unauthorized").value(), 1);
}

TEST_F(FilterTest, QueryApiKey) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - key: key1
        client: user1
  key_sources:
    entries:
      - query: "api_key"
  )EOF";
  setup(config_yaml, {});
  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "host"}, {":method", "GET"}, {":path", "/path?api_key=key1"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);
}

TEST_F(FilterTest, NoCookieApiKey) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - key: key1
        client: user1
  key_sources:
    entries:
      - cookie: "api_key"
  )EOF";
  setup(config_yaml, {});
  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "host"}, {":method", "GET"}, {":path", "/path"}};

  EXPECT_CALL(decoder_filter_callbacks_,
              sendLocalReply(Http::Code::Unauthorized, _, _, _, "missing_api_key"));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));

  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.unauthorized").value(), 1);
}

TEST_F(FilterTest, CookieApiKey) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - key: key1
        client: user1
  key_sources:
    entries:
      - cookie: "api_key"
  )EOF";

  setup(config_yaml, {});

  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "host"}, {":method", "GET"}, {":path", "/path"}, {"cookie", "api_key=key1"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);
}

TEST_F(FilterTest, FallbackToQueryApiKey) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - key: key1
        client: user1
  key_sources:
    entries:
      - header: "Authorization"
      - query: "api_key"
  )EOF";

  setup(config_yaml, {});

  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "host"}, {":method", "GET"}, {":path", "/path?api_key=key1"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);
}

TEST_F(FilterTest, FallbackToCookieApiKey) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - key: key1
        client: user1
  key_sources:
    entries:
      - header: "Authorization"
      - query: "api_key"
      - cookie: "api_key"
  )EOF";

  setup(config_yaml, {});

  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "host"}, {":method", "GET"}, {":path", "/path"}, {"cookie", "api_key=key1"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);
}

TEST_F(FilterTest, OrderOfKeySources) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - key: key1
        client: user1
  key_sources:
    entries:
      - header: "Authorization"
      - query: "api_key"
      - cookie: "api_key"
  )EOF";

  setup(config_yaml, {});

  {
    // Header, query, and cookie all have the key. The filter should use the header.
    // But the header contains the wrong key.
    Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                   {":method", "GET"},
                                                   {":path", "/path?api_key=key1"},
                                                   {"cookie", "api_key=key1"},
                                                   {"Authorization", "Bearer key2"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers, true));
    EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.unauthorized").value(), 1);
  }

  {
    // Header, query, and cookie all have the key. The filter should use the header.
    Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                   {":method", "GET"},
                                                   {":path", "/path?api_key=key1"},
                                                   {"cookie", "api_key=key1"},
                                                   {"Authorization", "Bearer key1"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
    EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);
  }

  {
    // Query and cookie have the key. The filter should use the query.
    // But the query contains the wrong key.
    Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                   {":method", "GET"},
                                                   {":path", "/path?api_key=key2"},
                                                   {"cookie", "api_key=key1"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers, true));
    EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.unauthorized").value(), 2);
  }

  {
    // Query and cookie have the key. The filter should use the query.
    Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                   {":method", "GET"},
                                                   {":path", "/path?api_key=key1"},
                                                   {"cookie", "api_key=key1"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
    EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 2);
  }
}

TEST_F(FilterTest, UnkonwnApiKey) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - key: key1
        client: user1
  key_sources:
    entries:
      - header: "Authorization"
  )EOF";
  setup(config_yaml, {});

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                 {":method", "GET"},
                                                 {":path", "/path"},
                                                 {"Authorization", "Bearer key2"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              sendLocalReply(Http::Code::Unauthorized, _, _, _, "unkonwn_api_key"));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));

  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.unauthorized").value(), 1);
}

TEST_F(FilterTest, RouteConfigOverrideCredentials) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - key: key1
        client: user1
  key_sources:
    entries:
      - header: "Authorization"
  )EOF";

  const std::string route_config_yaml = R"EOF(
  override_config:
    credentials:
      entries:
        - key: key2
          client: user2
  )EOF";

  setup(config_yaml, route_config_yaml);

  {
    // Credentials is overridden and key2 is allowed.

    Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                   {":method", "GET"},
                                                   {":path", "/path"},
                                                   {"Authorization", "Bearer key2"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
    EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);
  }

  {
    // Credentials is overridden and key1 is not allowed.

    Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                   {":method", "GET"},
                                                   {":path", "/path"},
                                                   {"Authorization", "Bearer key1"}};
    EXPECT_CALL(decoder_filter_callbacks_,
                sendLocalReply(Http::Code::Unauthorized, _, _, _, "unkonwn_api_key"));
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers, true));
    EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.unauthorized").value(), 1);
  }
}

TEST_F(FilterTest, RouteConfigOverrideKeySource) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - key: key1
        client: user1
  key_sources:
    entries:
      - header: "Authorization"
  )EOF";

  const std::string route_config_yaml = R"EOF(
  override_config:
    key_sources:
      entries:
        - query: "api_key"
  )EOF";

  setup(config_yaml, route_config_yaml);

  {

    // Key source is overridden and the filter will use query.

    Http::TestRequestHeaderMapImpl request_headers{
        {":authority", "host"}, {":method", "GET"}, {":path", "/path?api_key=key1"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
    EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);
  }

  {
    // Key source is overridden so the filter cannot find the key.

    Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                   {":method", "GET"},
                                                   {":path", "/path"},
                                                   {"Authorization", "Bearer key1"}};
    EXPECT_CALL(decoder_filter_callbacks_,
                sendLocalReply(Http::Code::Unauthorized, _, _, _, "missing_api_key"));
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers, true));
  }
}

TEST_F(FilterTest, RouteConfigOverrideKeySourceAndCredentials) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - key: key1
        client: user1
  key_sources:
    entries:
      - header: "Authorization"
  )EOF";
  const std::string route_config_yaml = R"EOF(
  override_config:
    credentials:
      entries:
        - key: key2
          client: user2
    key_sources:
      entries:
        - query: "api_key"
  )EOF";

  setup(config_yaml, route_config_yaml);

  {
    // Both key source and credentials are overridden.
    Http::TestRequestHeaderMapImpl request_headers{
        {":authority", "host"}, {":method", "GET"}, {":path", "/path?api_key=key2"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
    EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);
  }

  {
    // Both key source and credentials are overridden.
    Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                   {":method", "GET"},
                                                   {":path", "/path"},
                                                   {"Authorization", "Bearer key1"}};
    EXPECT_CALL(decoder_filter_callbacks_,
                sendLocalReply(Http::Code::Unauthorized, _, _, _, "missing_api_key"));
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers, true));
    EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.unauthorized").value(), 1);
  }

  {
    // Both key source and credentials are overridden.
    Http::TestRequestHeaderMapImpl request_headers{
        {":authority", "host"}, {":method", "GET"}, {":path", "/path?api_key=key1"}};
    EXPECT_CALL(decoder_filter_callbacks_,
                sendLocalReply(Http::Code::Unauthorized, _, _, _, "unkonwn_api_key"));
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers, true));
    EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.unauthorized").value(), 2);
  }
}

TEST_F(FilterTest, NoCredentials) {
  const std::string config_yaml = R"EOF(
  key_sources:
    entries:
      - header: "Authorization"
  )EOF";

  setup(config_yaml, {});

  // No credentials is provided.
  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "host"}, {":method", "GET"}, {":path", "/path"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              sendLocalReply(Http::Code::Unauthorized, _, _, _, "missing_credentials"));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.unauthorized").value(), 1);
}

TEST_F(FilterTest, NoKeySource) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - key: key1
        client: user1
  )EOF";
  setup(config_yaml, {});
  // No key source is provided.
  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "host"}, {":method", "GET"}, {":path", "/path"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              sendLocalReply(Http::Code::Unauthorized, _, _, _, "missing_key_sources"));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.unauthorized").value(), 1);
}

TEST_F(FilterTest, KnownApiKeyButNotAllowed) {
  const std::string config_yaml = R"EOF(
  credentials:
    entries:
      - key: key1
        client: user1
      - key: key2
        client: user2
  key_sources:
    entries:
      - header: "Authorization"
  )EOF";
  const std::string route_config_yaml = R"EOF(
  allowed_clients:
    - user2
  )EOF";

  setup(config_yaml, route_config_yaml);

  {
    // Known api key but not allowed.
    Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                   {":method", "GET"},
                                                   {":path", "/path"},
                                                   {"Authorization", "Bearer key1"}};
    EXPECT_CALL(decoder_filter_callbacks_,
                sendLocalReply(Http::Code::Forbidden, _, _, _, "client_not_allowed"));
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers, true));
    EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.forbidden").value(), 1);
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                   {":method", "GET"},
                                                   {":path", "/path"},
                                                   {"Authorization", "Bearer key2"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
    EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);
  }
}

} // namespace ApiKeyAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
