#include "extensions/filters/http/well_known_names.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class LuaIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                           public HttpIntegrationTest {
public:
  LuaIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_, timeSystem()));
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_, timeSystem()));
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
            keyset:
              foo: MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAp0cSZtAdFgMI1zQJwG8ujTXFMcRY0+SA6fMZGEfQYuxcz/e8UelJ1fLDVAwYmk7KHoYzpizy0JIxAcJ+OAE+cd6a6RpwSEm/9/vizlv0vWZv2XMRAqUxk/5amlpQZE/4sRg/qJdkZZjKrSKjf5VEUQg2NytExYyYWG+3FEYpzYyUeVktmW0y/205XAuEQuxaoe+AUVKeoON1iDzvxywE42C0749XYGUFicqBSRj2eO7jm4hNWvgTapYwpswM3hV9yOAPOVQGKNXzNbLDbFTHyLw3OKayGs/4FUBa+ijlGD9VDawZq88RRaf5ztmH22gOSiKcrHXe40fsnrzh/D27uwIDAQAB
          )EOF";

          ProtobufWkt::Struct value;
          TestUtility::loadFromYaml(yaml, value);

          // Sets the route's metadata.
          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_metadata()
              ->mutable_filter_metadata()
              ->insert(Protobuf::MapPair<std::string, ProtobufWkt::Struct>(key, value));
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

INSTANTIATE_TEST_SUITE_P(IpVersions, LuaIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Basic request and response.
TEST_P(LuaIntegrationTest, RequestAndResponse) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: envoy.lua
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.lua.v2.Lua
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

      request_handle:streamInfo():dynamicMetadata():set("envoy.lb", "foo", "bar")
      local dynamic_metadata_value = request_handle:streamInfo():dynamicMetadata():get("envoy.lb")["foo"]

      request_handle:headers():add("request_body_size", body_length)
      request_handle:headers():add("request_metadata_foo", metadata["foo"])
      request_handle:headers():add("request_metadata_baz", metadata["baz"])
      if request_handle:connection():ssl() == nil then
        request_handle:headers():add("request_secure", "false")
      else
        request_handle:headers():add("request_secure", "true")
      end
      request_handle:headers():add("request_protocol", request_handle:streamInfo():protocol())
      request_handle:headers():add("request_dynamic_metadata_value", dynamic_metadata_value)
    end

    function envoy_on_response(response_handle)
      local metadata = response_handle:metadata():get("foo.bar")
      local body_length = response_handle:body():length()
      response_handle:headers():add("response_metadata_foo", metadata["foo"])
      response_handle:headers():add("response_metadata_baz", metadata["baz"])
      response_handle:headers():add("response_body_size", body_length)
      response_handle:headers():add("request_protocol", response_handle:streamInfo():protocol())
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
  EXPECT_EQ("10", upstream_request_->headers()
                      .get(Http::LowerCaseString("request_body_size"))
                      ->value()
                      .getStringView());

  EXPECT_EQ("bar", upstream_request_->headers()
                       .get(Http::LowerCaseString("request_metadata_foo"))
                       ->value()
                       .getStringView());

  EXPECT_EQ("bat", upstream_request_->headers()
                       .get(Http::LowerCaseString("request_metadata_baz"))
                       ->value()
                       .getStringView());
  EXPECT_EQ("false", upstream_request_->headers()
                         .get(Http::LowerCaseString("request_secure"))
                         ->value()
                         .getStringView());

  EXPECT_EQ("HTTP/1.1", upstream_request_->headers()
                            .get(Http::LowerCaseString("request_protocol"))
                            ->value()
                            .getStringView());

  EXPECT_EQ("bar", upstream_request_->headers()
                       .get(Http::LowerCaseString("request_dynamic_metadata_value"))
                       ->value()
                       .getStringView());

  Http::TestHeaderMapImpl response_headers{{":status", "200"}, {"foo", "bar"}};
  upstream_request_->encodeHeaders(response_headers, false);
  Buffer::OwnedImpl response_data1("good");
  upstream_request_->encodeData(response_data1, false);
  Buffer::OwnedImpl response_data2("bye");
  upstream_request_->encodeData(response_data2, true);

  response->waitForEndStream();

  EXPECT_EQ("7", response->headers()
                     .get(Http::LowerCaseString("response_body_size"))
                     ->value()
                     .getStringView());
  EXPECT_EQ("bar", response->headers()
                       .get(Http::LowerCaseString("response_metadata_foo"))
                       ->value()
                       .getStringView());
  EXPECT_EQ("bat", response->headers()
                       .get(Http::LowerCaseString("response_metadata_baz"))
                       ->value()
                       .getStringView());
  EXPECT_EQ(
      "HTTP/1.1",
      response->headers().get(Http::LowerCaseString("request_protocol"))->value().getStringView());
  EXPECT_EQ(nullptr, response->headers().get(Http::LowerCaseString("foo")));

  cleanup();
}

// Upstream call followed by continuation.
TEST_P(LuaIntegrationTest, UpstreamHttpCall) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: envoy.lua
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.lua.v2.Lua
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
  EXPECT_EQ("bar", upstream_request_->headers()
                       .get(Http::LowerCaseString("upstream_foo"))
                       ->value()
                       .getStringView());
  EXPECT_EQ("4", upstream_request_->headers()
                     .get(Http::LowerCaseString("upstream_body_size"))
                     ->value()
                     .getStringView());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();

  cleanup();
}

// Upstream call followed by immediate response.
TEST_P(LuaIntegrationTest, UpstreamCallAndRespond) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: envoy.lua
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.lua.v2.Lua
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
  EXPECT_EQ("403", response->headers().Status()->value().getStringView());
  EXPECT_EQ("nope", response->body());
}

// Filter alters headers and changes route.
TEST_P(LuaIntegrationTest, ChangeRoute) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: envoy.lua
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.lua.v2.Lua
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
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

// Should survive from 30 calls when calling streamInfo():dynamicMetadata(). This is a regression
// test for #4305.
TEST_P(LuaIntegrationTest, SurviveMultipleCalls) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: envoy.lua
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.lua.v2.Lua
  inline_code: |
    function envoy_on_request(request_handle)
      request_handle:streamInfo():dynamicMetadata()
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
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  }

  cleanup();
}

// Basic test for verifying signature.
TEST_P(LuaIntegrationTest, SignatureVerification) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: envoy.lua
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.lua.v2.Lua
  inline_code: |
    function string.fromhex(str)
      return (str:gsub('..', function (cc)
        return string.char(tonumber(cc, 16))
      end))
    end

    -- decoding
    function dec(data)
      local b='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
      data = string.gsub(data, '[^'..b..'=]', '')
      return (data:gsub('.', function(x)
        if (x == '=') then return '' end
        local r,f='',(b:find(x)-1)
        for i=6,1,-1 do r=r..(f%2^i-f%2^(i-1)>0 and '1' or '0') end
        return r;
      end):gsub('%d%d%d?%d?%d?%d?%d?%d?', function(x)
        if (#x ~= 8) then return '' end
        local c=0
        for i=1,8 do c=c+(x:sub(i,i)=='1' and 2^(8-i) or 0) end
        return string.char(c)
      end))
    end

    function envoy_on_request(request_handle)
      local metadata = request_handle:metadata():get("keyset")
      local keyder = metadata[request_handle:headers():get("keyid")]

      local rawkeyder = dec(keyder)
      local pubkey = request_handle:importPublicKey(rawkeyder, string.len(rawkeyder)):get()

      if pubkey == nil then
        request_handle:logErr("log test")
        request_handle:headers():add("signature_verification", "rejected")
        return
      end

      local hash = request_handle:headers():get("hash")
      local sig = request_handle:headers():get("signature")
      local rawsig = sig:fromhex()
      local data = request_handle:headers():get("message")
      local ok, error = request_handle:verifySignature(hash, pubkey, rawsig, string.len(rawsig), data, string.len(data)) 

      if ok then
        request_handle:headers():add("signature_verification", "approved")
      else
        request_handle:logErr(error)
        request_handle:headers():add("signature_verification", "rejected")
      end

      request_handle:headers():add("verification", "done")
    end
)EOF";

  initializeFilter(FILTER_AND_CODE);

  auto signature =
      "345ac3a167558f4f387a81c2d64234d901a7ceaa544db779d2f797b0ea4ef851b740905a63e2f4d5af42cee093a2"
      "9c7155db9a63d3d483e0ef948f5ac51ce4e10a3a6606fd93ef68ee47b30c37491103039459122f78e1c7ea71a1a5"
      "ea24bb6519bca02c8c9915fe8be24927c91812a13db72dbcb500103a79e8f67ff8cb9e2a631974e0668ab3977bf5"
      "70a91b67d1b6bcd5dce84055f21427d64f4256a042ab1dc8e925d53a769f6681a873f5859693a7728fcbe95beace"
      "1563b5ffbcd7c93b898aeba31421dafbfadeea50229c49fd6c445449314460f3d19150bd29a91333beaced557ed6"
      "295234f7c14fa46303b7e977d2c89ba8a39a46a35f33eb07a332";

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestHeaderMapImpl request_headers{
      {":method", "POST"},    {":path", "/test/long/url"},     {":scheme", "https"},
      {":authority", "host"}, {"x-forwarded-for", "10.0.0.1"}, {"message", "hello"},
      {"keyid", "foo"},       {"signature", signature},        {"hash", "sha256"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  EXPECT_EQ("approved", upstream_request_->headers()
                            .get(Http::LowerCaseString("signature_verification"))
                            ->value()
                            .getStringView());

  EXPECT_EQ("done", upstream_request_->headers()
                        .get(Http::LowerCaseString("verification"))
                        ->value()
                        .getStringView());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());

  cleanup();
}

} // namespace
} // namespace Envoy
