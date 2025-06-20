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
    absl::Status creation_status = absl::OkStatus();
    config_ = std::make_shared<FilterConfig>(proto_config, *stats_.rootScope(), "stats.",
                                             creation_status);
    ASSERT(creation_status.ok());

    if (!route_config_yaml.empty()) {
      ApiKeyAuthPerRouteProto route_config_proto;
      TestUtility::loadFromYaml(route_config_yaml, route_config_proto);
      route_config_ = std::make_shared<RouteConfig>(route_config_proto, creation_status);
      ASSERT(creation_status.ok());
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
  - key: key1
    client: user1
  key_sources:
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
  - key: key1
    client: user1
  key_sources:
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
  - key: key1
    client: user1
  key_sources:
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
  - key: key1
    client: user1
  key_sources:
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
  - key: key1
    client: user1
  key_sources:
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
  - key: key1
    client: user1
  key_sources:
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
  - key: key1
    client: user1
  key_sources:
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
  - key: key1
    client: user1
  key_sources:
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
  - key: key1
    client: user1
  key_sources:
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
  - key: key1
    client: user1
  key_sources:
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
  - key: key1
    client: user1
  key_sources:
  - header: "Authorization"
  )EOF";

  const std::string route_config_yaml = R"EOF(
  credentials:
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
  - key: key1
    client: user1
  key_sources:
  - header: "Authorization"
  )EOF";

  const std::string route_config_yaml = R"EOF(
  key_sources:
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
  - key: key1
    client: user1
  key_sources:
  - header: "Authorization"
  )EOF";
  const std::string route_config_yaml = R"EOF(
  credentials:
  - key: key2
    client: user2
  key_sources:
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
  - key: key1
    client: user1
  - key: key2
    client: user2
  key_sources:
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

TEST_F(FilterTest, HeaderApiKeyWithForwarding) {
  const std::string config_yaml = R"EOF(
  credentials:
  - key: key1
    client: user1
  key_sources:
  - header: "Authorization"
  forwarding:
    header: "x-client-id"
    hide_credentials: false
  )EOF";

  setup(config_yaml, {});

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                 {":method", "GET"},
                                                 {":path", "/path"},
                                                 {"Authorization", "Bearer key1"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);

  // The forwarded client header should be set
  EXPECT_EQ(request_headers.get_("x-client-id"), "user1");
  // The authorization header should still be present, confirming that hide_credentials is
  // effectively false
  EXPECT_EQ(request_headers.get_("authorization"), "Bearer key1");
}

TEST_F(FilterTest, QueryApiKeyWithForwarding) {
  const std::string config_yaml = R"EOF(
  credentials:
  - key: key1
    client: user1
  key_sources:
  - query: "api_key"
  forwarding:
    header: "x-client-id"
    hide_credentials: false
  )EOF";

  setup(config_yaml, {});

  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "host"}, {":method", "GET"}, {":path", "/path?api_key=key1"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);

  // The forwarded client header should be set
  EXPECT_EQ(request_headers.get_("x-client-id"), "user1");
  // The query should still be present, confirming that hide_credentials is effectively false
  const std::string path = std::string(request_headers.getPathValue());
  EXPECT_EQ(path, "/path?api_key=key1");
}

TEST_F(FilterTest, CookieApiKeyWithForwarding) {
  const std::string config_yaml = R"EOF(
  credentials:
  - key: key1
    client: user1
  key_sources:
  - cookie: "api_key"
  forwarding:
    header: "x-client-id"
    hide_credentials: false
  )EOF";

  setup(config_yaml, {});

  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "host"}, {":method", "GET"}, {":path", "/path"}, {"cookie", "api_key=key1"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);

  // The forwarded client header should be set
  EXPECT_EQ(request_headers.get_("x-client-id"), "user1");
  // The cookie should still be present, confirming that hide_credentials is effectively false
  EXPECT_EQ(request_headers.get_("cookie"), "api_key=key1");
}

TEST_F(FilterTest, HideCredentialsHeader) {
  const std::string config_yaml = R"EOF(
  credentials:
  - key: key1
    client: user1
  key_sources:
  - header: "Authorization"
  forwarding:
    hide_credentials: true
  )EOF";

  setup(config_yaml, {});

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                 {":method", "GET"},
                                                 {":path", "/path"},
                                                 {"Authorization", "Bearer key1"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);
  // Authorization header should be removed
  EXPECT_FALSE(request_headers.has("authorization"));
}

TEST_F(FilterTest, HideCredentialsHeaderWithForwarding) {
  const std::string config_yaml = R"EOF(
  credentials:
  - key: key1
    client: user1
  key_sources:
  - header: "Authorization"
  forwarding:
    header: "x-client-id"
    hide_credentials: true
  )EOF";

  setup(config_yaml, {});

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                 {":method", "GET"},
                                                 {":path", "/path"},
                                                 {"Authorization", "Bearer key1"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);
  EXPECT_EQ(request_headers.get_("x-client-id"), "user1");
  // Authorization header should be removed
  EXPECT_FALSE(request_headers.has("authorization"));
}

TEST_F(FilterTest, HideCredentialsQueryWithForwarding) {
  const std::string config_yaml = R"EOF(
  credentials:
  - key: key1
    client: user1
  key_sources:
  - query: "api_key"
  forwarding:
    header: "x-client-id"
    hide_credentials: true
  )EOF";

  setup(config_yaml, {});

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {":authority", "host"}, {":method", "GET"}, {":path", "/path?api_key=key1"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
    EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);
    EXPECT_EQ(request_headers.get_("x-client-id"), "user1");

    // Query parameter should be stripped from path
    const std::string path = std::string(request_headers.getPathValue());
    EXPECT_EQ(path, "/path");
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {":authority", "host"}, {":method", "GET"}, {":path", "/path?api_key=key1&foo=bar"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
    EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 2);
    EXPECT_EQ(request_headers.get_("x-client-id"), "user1");

    const std::string path = std::string(request_headers.getPathValue());
    // Only the API key query parameter should be removed
    EXPECT_EQ(path, "/path?foo=bar");
  }
}

TEST_F(FilterTest, HideCredentialsCookieWithForwarding) {
  const std::string config_yaml = R"EOF(
  credentials:
  - key: key1
    client: user1
  key_sources:
  - cookie: "api_key"
  forwarding:
    header: "x-client-id"
    hide_credentials: true
  )EOF";

  setup(config_yaml, {});

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {":authority", "host"}, {":method", "GET"}, {":path", "/path"}, {"cookie", "api_key=key1"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
    EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);
    EXPECT_EQ(request_headers.get_("x-client-id"), "user1");
    // Cookie should be removed
    EXPECT_FALSE(request_headers.has("cookie"));
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                   {":method", "GET"},
                                                   {":path", "/path"},
                                                   {"cookie", "api_key=key1; foo=bar"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
    EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 2);
    EXPECT_EQ(request_headers.get_("x-client-id"), "user1");
    // Only the API key cookie should be removed
    EXPECT_EQ(request_headers.get_("cookie"), "foo=bar");
  }
}

TEST_F(FilterTest, HideCredentialsMultipleKeySourcesWithForwarding) {
  const std::string config_yaml = R"EOF(
  credentials:
  - key: key1
    client: user1
  key_sources:
  - header: "Authorization"
  - query: "api_key"
  - cookie: "api_key"
  forwarding:
    header: "x-client-id"
    hide_credentials: true
  )EOF";

  setup(config_yaml, {});

  // Header, query, and cookie all have the key.
  Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                 {":method", "GET"},
                                                 {":path", "/path?api_key=key1"},
                                                 {"cookie", "api_key=key1"},
                                                 {"Authorization", "Bearer key1"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);
  EXPECT_EQ(request_headers.get_("x-client-id"), "user1");

  // Verify that the API key has been removed from all key sources
  EXPECT_FALSE(request_headers.has("authorization"));
  const std::string path = std::string(request_headers.getPathValue());
  EXPECT_EQ(path, "/path");
  EXPECT_FALSE(request_headers.has("cookie"));
}

TEST_F(FilterTest, RouteConfigOverrideForwarding) {
  const std::string config_yaml = R"EOF(
  credentials:
  - key: key1
    client: user1
  key_sources:
  - header: "Authorization"
  forwarding:
    header: "x-client-id"
    hide_credentials: false
  )EOF";

  const std::string route_config_yaml = R"EOF(
  forwarding:
    header: "x-client-header"
    hide_credentials: true
  )EOF";

  setup(config_yaml, route_config_yaml);

  // Forwarding is overridden and the API key will be hidden.

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "host"},
                                                 {":method", "GET"},
                                                 {":path", "/path"},
                                                 {"Authorization", "Bearer key1"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(stats_.counterFromString("stats.api_key_auth.allowed").value(), 1);
  EXPECT_FALSE(request_headers.has("x-client-id"));
  EXPECT_EQ(request_headers.get_("x-client-header"), "user1");

  // Authorization header should be removed
  EXPECT_FALSE(request_headers.has("authorization"));
}

} // namespace ApiKeyAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
