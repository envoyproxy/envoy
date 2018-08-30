#include "extensions/filters/http/well_known_names.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class LuaIntegrationTest : public HttpIntegrationTest,
                           public testing::TestWithParam<Network::Address::IpVersion> {
public:
  LuaIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_));
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_));
  }

  void initializeFilter(const std::string& filter_config) {
    config_helper_.addFilter(filter_config);

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* lua_cluster = bootstrap.mutable_static_resources()->add_clusters();
      lua_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      lua_cluster->set_name("lua_cluster");

      auto* alt_cluster = bootstrap.mutable_static_resources()->add_clusters();
      alt_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      alt_cluster->set_name("alt_cluster");
    });

    config_helper_.addConfigModifier(
        [](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
               hcm) {
          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_match()
              ->set_prefix("/test/long/url");

          auto* new_route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->add_routes();
          new_route->mutable_match()->set_prefix("/alt/route");
          new_route->mutable_route()->set_cluster("alt_cluster");

          const std::string key = Extensions::HttpFilters::HttpFilterNames::get().Lua;
          const std::string yaml =
              R"EOF(
            foo.bar:
              foo: bar
              baz: bat
          )EOF";

          ProtobufWkt::Struct value;
          MessageUtil::loadFromYaml(yaml, value);

          // Sets the route's metadata.
          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_metadata()
              ->mutable_filter_metadata()
              ->insert(
                  Protobuf::MapPair<Envoy::ProtobufTypes::String, ProtobufWkt::Struct>(key, value));
        });

    initialize();
  }

  void cleanup() {
    codec_client_->close();
    if (fake_lua_connection_ != nullptr) {
      AssertionResult result = fake_lua_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_lua_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
    if (fake_upstream_connection_ != nullptr) {
      AssertionResult result = fake_upstream_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_upstream_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
  }

  FakeHttpConnectionPtr fake_lua_connection_;
  FakeStreamPtr lua_request_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, LuaIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

// Basic request and response.
TEST_P(LuaIntegrationTest, RequestAndResponse) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: envoy.lua
config:
  inline_code: |
    function envoy_on_request(request_handle)
      request_handle:logTrace("log test")
      request_handle:logDebug("log test")
      request_handle:logInfo("log test")
      request_handle:logWarn("log test")
      request_handle:logErr("log test")
      request_handle:logCritical("log test")

      local metadata = request_handle:metadata():get("foo.bar")
      local body_length = request_handle:body():length()

      request_handle:requestInfo():dynamicMetadata():set("envoy.lb", "foo", "bar")
      local dynamic_metadata_value = request_handle:requestInfo():dynamicMetadata():get("envoy.lb")["foo"]

      request_handle:headers():add("request_body_size", body_length)
      request_handle:headers():add("request_metadata_foo", metadata["foo"])
      request_handle:headers():add("request_metadata_baz", metadata["baz"])
      if request_handle:connection():ssl() == nil then
        request_handle:headers():add("request_secure", "false")
      else
        request_handle:headers():add("request_secure", "true")
      end
      request_handle:headers():add("request_protocol", request_handle:requestInfo():protocol())
      request_handle:headers():add("request_dynamic_metadata_value", dynamic_metadata_value)
    end

    function envoy_on_response(response_handle)
      local metadata = response_handle:metadata():get("foo.bar")
      local body_length = response_handle:body():length()
      response_handle:headers():add("response_metadata_foo", metadata["foo"])
      response_handle:headers():add("response_metadata_baz", metadata["baz"])
      response_handle:headers():add("response_body_size", body_length)
      response_handle:headers():add("request_protocol", response_handle:requestInfo():protocol())
      response_handle:headers():remove("foo")
    end
)EOF";

  initializeFilter(FILTER_AND_CODE);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestHeaderMapImpl request_headers{{":method", "POST"},
                                          {":path", "/test/long/url"},
                                          {":scheme", "http"},
                                          {":authority", "host"},
                                          {"x-forwarded-for", "10.0.0.1"}};

  auto encoder_decoder = codec_client_->startRequest(request_headers);
  Http::StreamEncoder& encoder = encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  Buffer::OwnedImpl request_data1("hello");
  encoder.encodeData(request_data1, false);
  Buffer::OwnedImpl request_data2("world");
  encoder.encodeData(request_data2, true);

  waitForNextUpstreamRequest();
  EXPECT_STREQ("10", upstream_request_->headers()
                         .get(Http::LowerCaseString("request_body_size"))
                         ->value()
                         .c_str());

  EXPECT_STREQ("bar", upstream_request_->headers()
                          .get(Http::LowerCaseString("request_metadata_foo"))
                          ->value()
                          .c_str());

  EXPECT_STREQ("bat", upstream_request_->headers()
                          .get(Http::LowerCaseString("request_metadata_baz"))
                          ->value()
                          .c_str());
  EXPECT_STREQ(
      "false",
      upstream_request_->headers().get(Http::LowerCaseString("request_secure"))->value().c_str());

  EXPECT_STREQ(
      "HTTP/1.1",
      upstream_request_->headers().get(Http::LowerCaseString("request_protocol"))->value().c_str());

  EXPECT_STREQ("bar", upstream_request_->headers()
                          .get(Http::LowerCaseString("request_dynamic_metadata_value"))
                          ->value()
                          .c_str());

  Http::TestHeaderMapImpl response_headers{{":status", "200"}, {"foo", "bar"}};
  upstream_request_->encodeHeaders(response_headers, false);
  Buffer::OwnedImpl response_data1("good");
  upstream_request_->encodeData(response_data1, false);
  Buffer::OwnedImpl response_data2("bye");
  upstream_request_->encodeData(response_data2, true);

  response->waitForEndStream();

  EXPECT_STREQ(
      "7", response->headers().get(Http::LowerCaseString("response_body_size"))->value().c_str());
  EXPECT_STREQ(
      "bar",
      response->headers().get(Http::LowerCaseString("response_metadata_foo"))->value().c_str());
  EXPECT_STREQ(
      "bat",
      response->headers().get(Http::LowerCaseString("response_metadata_baz"))->value().c_str());
  EXPECT_STREQ("HTTP/1.1",
               response->headers().get(Http::LowerCaseString("request_protocol"))->value().c_str());
  EXPECT_EQ(nullptr, response->headers().get(Http::LowerCaseString("foo")));

  cleanup();
}

// Upstream call followed by continuation.
TEST_P(LuaIntegrationTest, UpstreamHttpCall) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: envoy.lua
config:
  inline_code: |
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
      "lua_cluster",
      {
        [":method"] = "POST",
        [":path"] = "/",
        [":authority"] = "lua_cluster"
      },
      "hello world",
      5000)

      request_handle:headers():add("upstream_foo", headers["foo"])
      request_handle:headers():add("upstream_body_size", #body)
    end
)EOF";

  initializeFilter(FILTER_AND_CODE);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestHeaderMapImpl request_headers{{":method", "GET"},
                                          {":path", "/test/long/url"},
                                          {":scheme", "http"},
                                          {":authority", "host"},
                                          {"x-forwarded-for", "10.0.0.1"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_lua_connection_));
  ASSERT_TRUE(fake_lua_connection_->waitForNewStream(*dispatcher_, lua_request_));
  ASSERT_TRUE(lua_request_->waitForEndStream(*dispatcher_));
  Http::TestHeaderMapImpl response_headers{{":status", "200"}, {"foo", "bar"}};
  lua_request_->encodeHeaders(response_headers, false);
  Buffer::OwnedImpl response_data1("good");
  lua_request_->encodeData(response_data1, true);

  waitForNextUpstreamRequest();
  EXPECT_STREQ(
      "bar",
      upstream_request_->headers().get(Http::LowerCaseString("upstream_foo"))->value().c_str());
  EXPECT_STREQ("4", upstream_request_->headers()
                        .get(Http::LowerCaseString("upstream_body_size"))
                        ->value()
                        .c_str());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();

  cleanup();
}

// Upstream call followed by immediate response.
TEST_P(LuaIntegrationTest, UpstreamCallAndRespond) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: envoy.lua
config:
  inline_code: |
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
      "lua_cluster",
      {
        [":method"] = "POST",
        [":path"] = "/",
        [":authority"] = "lua_cluster"
      },
      "hello world",
      5000)

      request_handle:respond(
        {[":status"] = "403",
         ["upstream_foo"] = headers["foo"]},
        "nope")
    end
)EOF";

  initializeFilter(FILTER_AND_CODE);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestHeaderMapImpl request_headers{{":method", "GET"},
                                          {":path", "/test/long/url"},
                                          {":scheme", "http"},
                                          {":authority", "host"},
                                          {"x-forwarded-for", "10.0.0.1"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_lua_connection_));
  ASSERT_TRUE(fake_lua_connection_->waitForNewStream(*dispatcher_, lua_request_));
  ASSERT_TRUE(lua_request_->waitForEndStream(*dispatcher_));
  Http::TestHeaderMapImpl response_headers{{":status", "200"}, {"foo", "bar"}};
  lua_request_->encodeHeaders(response_headers, true);

  response->waitForEndStream();
  cleanup();

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("403", response->headers().Status()->value().c_str());
  EXPECT_EQ("nope", response->body());
}

// Filter alters headers and changes route.
TEST_P(LuaIntegrationTest, ChangeRoute) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: envoy.lua
config:
  inline_code: |
    function envoy_on_request(request_handle)
      request_handle:headers():remove(":path")
      request_handle:headers():add(":path", "/alt/route")
    end
)EOF";

  initializeFilter(FILTER_AND_CODE);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestHeaderMapImpl request_headers{{":method", "GET"},
                                          {":path", "/test/long/url"},
                                          {":scheme", "http"},
                                          {":authority", "host"},
                                          {"x-forwarded-for", "10.0.0.1"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  waitForNextUpstreamRequest(2);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  cleanup();

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

// Should survive from 30 calls when calling requestInfo():dynamicMetadata(). This is a regression
// test for #4305.
TEST_P(LuaIntegrationTest, SurviveMultipleCalls) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: envoy.lua
config:
  inline_code: |
    function envoy_on_request(request_handle)
      request_handle:requestInfo():dynamicMetadata()
    end
)EOF";

  initializeFilter(FILTER_AND_CODE);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestHeaderMapImpl request_headers{{":method", "GET"},
                                          {":path", "/test/long/url"},
                                          {":scheme", "http"},
                                          {":authority", "host"},
                                          {"x-forwarded-for", "10.0.0.1"}};

  for (uint32_t i = 0; i < 30; ++i) {
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(default_response_headers_, true);
    response->waitForEndStream();

    EXPECT_TRUE(response->complete());
    EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  }

  cleanup();
}

} // namespace
} // namespace Envoy
