#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using Envoy::Http::HeaderValueOf;

namespace Envoy {
namespace {

class LuaIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                           public HttpIntegrationTest {
public:
  LuaIntegrationTest() : LuaIntegrationTest(Http::CodecType::HTTP1) {}
  LuaIntegrationTest(Http::CodecType downstream_protocol)
      : HttpIntegrationTest(downstream_protocol, GetParam()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP1);
    addFakeUpstream(Http::CodecType::HTTP1);
    // Create the xDS upstream.
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void initializeFilter(const std::string& filter_config, const std::string& domain = "*") {
    config_helper_.prependFilter(filter_config);

    // Create static clusters.
    createClusters();

    config_helper_.addConfigModifier(
        [domain](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_match()
              ->set_prefix("/test/long/url");

          hcm.mutable_route_config()->mutable_virtual_hosts(0)->set_domains(0, domain);
          auto* new_route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->add_routes();
          new_route->mutable_match()->set_prefix("/alt/route");
          new_route->mutable_route()->set_cluster("alt_cluster");
          auto* response_header =
              new_route->mutable_response_headers_to_add()->Add()->mutable_header();
          response_header->set_key("fake_header");
          response_header->set_value("fake_value");

          const std::string key = "envoy.filters.http.lua";
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

  void initializeWithYaml(const std::string& filter_config, const std::string& route_config) {
    config_helper_.prependFilter(filter_config);

    createClusters();
    config_helper_.addConfigModifier(
        [route_config](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) { TestUtility::loadFromYaml(route_config, *hcm.mutable_route_config()); });
    initialize();
  }

  void createClusters() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* lua_cluster = bootstrap.mutable_static_resources()->add_clusters();
      lua_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      lua_cluster->set_name("lua_cluster");

      auto* alt_cluster = bootstrap.mutable_static_resources()->add_clusters();
      alt_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      alt_cluster->set_name("alt_cluster");

      auto* xds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      xds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      xds_cluster->set_name("xds_cluster");
      ConfigHelper::setHttp2(*xds_cluster);
    });
  }

  void initializeWithRds(const std::string& filter_config, const std::string& route_config_name,
                         const std::string& initial_route_config) {
    config_helper_.prependFilter(filter_config);

    // Create static clusters.
    createClusters();

    // Set RDS config source.
    config_helper_.addConfigModifier(
        [route_config_name](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          hcm.mutable_rds()->set_route_config_name(route_config_name);
          hcm.mutable_rds()->mutable_config_source()->set_resource_api_version(
              envoy::config::core::v3::ApiVersion::V3);
          envoy::config::core::v3::ApiConfigSource* rds_api_config_source =
              hcm.mutable_rds()->mutable_config_source()->mutable_api_config_source();
          rds_api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
          rds_api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
          envoy::config::core::v3::GrpcService* grpc_service =
              rds_api_config_source->add_grpc_services();
          grpc_service->mutable_envoy_grpc()->set_cluster_name("xds_cluster");
        });

    on_server_init_function_ = [&]() {
      AssertionResult result =
          fake_upstreams_[3]->waitForHttpConnection(*dispatcher_, xds_connection_);
      RELEASE_ASSERT(result, result.message());
      result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
      RELEASE_ASSERT(result, result.message());
      xds_stream_->startGrpcStream();

      EXPECT_TRUE(compareSotwDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                              {route_config_name}, true));
      sendSotwDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
          Config::TypeUrl::get().RouteConfiguration,
          {TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(
              initial_route_config)},
          "1");
    };
    initialize();
    registerTestServerPorts({"http"});
  }

  void expectResponseBodyRewrite(const std::string& code, bool empty_body, bool enable_wrap_body) {
    initializeFilter(code);
    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                   {":path", "/test/long/url"},
                                                   {":scheme", "http"},
                                                   {":authority", "host"},
                                                   {"x-forwarded-for", "10.0.0.1"}};

    auto encoder_decoder = codec_client_->startRequest(request_headers);
    Http::StreamEncoder& encoder = encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);
    Buffer::OwnedImpl request_data("done");
    encoder.encodeData(request_data, true);

    waitForNextUpstreamRequest();

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"foo", "bar"}};

    if (empty_body) {
      upstream_request_->encodeHeaders(response_headers, true);
    } else {
      upstream_request_->encodeHeaders(response_headers, false);
      Buffer::OwnedImpl response_data1("good");
      upstream_request_->encodeData(response_data1, false);
      Buffer::OwnedImpl response_data2("bye");
      upstream_request_->encodeData(response_data2, true);
    }

    ASSERT_TRUE(response->waitForEndStream());

    if (enable_wrap_body) {
      EXPECT_EQ("2", response->headers()
                         .get(Http::LowerCaseString("content-length"))[0]
                         ->value()
                         .getStringView());
      EXPECT_EQ("ok", response->body());
    } else {
      EXPECT_EQ("", response->body());
    }

    cleanup();
  }

  void testRewriteResponse(const std::string& code) {
    expectResponseBodyRewrite(code, false, true);
  }

  void testRewriteResponseWithoutUpstreamBody(const std::string& code, bool enable_wrap_body) {
    expectResponseBodyRewrite(code, true, enable_wrap_body);
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
    if (xds_connection_ != nullptr) {
      AssertionResult result = xds_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = xds_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      xds_connection_ = nullptr;
    }
  }

  FakeHttpConnectionPtr fake_lua_connection_;
  FakeStreamPtr lua_request_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, LuaIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Regression test for pulling route info during early local replies using the Lua filter
// metadata() API. Covers both the upgrade required and no authority cases.
TEST_P(LuaIntegrationTest, CallMetadataDuringLocalReply) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_response(response_handle)
        local metadata = response_handle:metadata():get("foo.bar")
        if metadata == nil then
        end
      end
)EOF";

  initializeFilter(FILTER_AND_CODE, "foo");
  std::string response;

  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response, true);
  EXPECT_TRUE(response.find("HTTP/1.1 426 Upgrade Required\r\n") == 0);

  response = "";
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.1\r\n\r\n", &response, true);
  EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request\r\n") == 0);
}

// Basic request and response.
TEST_P(LuaIntegrationTest, RequestAndResponse) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
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

        local test_header_value_0 = request_handle:headers():getAtIndex("X-Test-Header", 0)
        request_handle:headers():add("test_header_value_0", test_header_value_0)
        local test_header_value_1 = request_handle:headers():getAtIndex("X-TEST-Header", 1)
        request_handle:headers():add("test_header_value_1", test_header_value_1)
        local test_header_value_2 = request_handle:headers():getAtIndex("x-test-header", 2)
        if test_header_value_2 == nil then
          request_handle:headers():add("test_header_value_2", "nil_value")
        end
        local test_header_value_size = request_handle:headers():getNumValues("x-test-header")
        request_handle:headers():add("test_header_value_size", test_header_value_size)
        request_handle:headers():add("cookie_0", request_handle:headers():getAtIndex("set-cookie", 0))
        request_handle:headers():add("cookie_1", request_handle:headers():getAtIndex("set-cookie", 1))
        request_handle:headers():add("cookie_size", request_handle:headers():getNumValues("set-cookie"))

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
        request_handle:headers():add("request_downstream_local_address_value",
          request_handle:streamInfo():downstreamLocalAddress())
        request_handle:headers():add("request_downstream_directremote_address_value",
          request_handle:streamInfo():downstreamDirectRemoteAddress())
        request_handle:headers():add("request_downstream_remote_address_value",
          request_handle:streamInfo():downstreamRemoteAddress())
        request_handle:headers():add("request_requested_server_name",
          request_handle:streamInfo():requestedServerName())
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
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},      {":path", "/test/long/url"},     {":scheme", "http"},
      {":authority", "host"},   {"x-forwarded-for", "10.0.0.1"}, {"x-test-header", "foo"},
      {"x-test-header", "bar"}, {"set-cookie", "foo;bar;"},      {"set-cookie", "1,3;2,5;"}};

  auto encoder_decoder = codec_client_->startRequest(request_headers);
  Http::StreamEncoder& encoder = encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  Buffer::OwnedImpl request_data1("hello");
  encoder.encodeData(request_data1, false);
  Buffer::OwnedImpl request_data2("world");
  encoder.encodeData(request_data2, true);

  waitForNextUpstreamRequest();
  EXPECT_EQ("foo", upstream_request_->headers()
                       .get(Http::LowerCaseString("test_header_value_0"))[0]
                       ->value()
                       .getStringView());

  EXPECT_EQ("bar", upstream_request_->headers()
                       .get(Http::LowerCaseString("test_header_value_1"))[0]
                       ->value()
                       .getStringView());

  EXPECT_EQ("nil_value", upstream_request_->headers()
                             .get(Http::LowerCaseString("test_header_value_2"))[0]
                             ->value()
                             .getStringView());

  EXPECT_EQ("2", upstream_request_->headers()
                     .get(Http::LowerCaseString("test_header_value_size"))[0]
                     ->value()
                     .getStringView());

  EXPECT_EQ("foo;bar;", upstream_request_->headers()
                            .get(Http::LowerCaseString("cookie_0"))[0]
                            ->value()
                            .getStringView());

  EXPECT_EQ("1,3;2,5;", upstream_request_->headers()
                            .get(Http::LowerCaseString("cookie_1"))[0]
                            ->value()
                            .getStringView());

  EXPECT_EQ("2", upstream_request_->headers()
                     .get(Http::LowerCaseString("cookie_size"))[0]
                     ->value()
                     .getStringView());

  EXPECT_EQ("10", upstream_request_->headers()
                      .get(Http::LowerCaseString("request_body_size"))[0]
                      ->value()
                      .getStringView());

  EXPECT_EQ("bar", upstream_request_->headers()
                       .get(Http::LowerCaseString("request_metadata_foo"))[0]
                       ->value()
                       .getStringView());

  EXPECT_EQ("bat", upstream_request_->headers()
                       .get(Http::LowerCaseString("request_metadata_baz"))[0]
                       ->value()
                       .getStringView());
  EXPECT_EQ("false", upstream_request_->headers()
                         .get(Http::LowerCaseString("request_secure"))[0]
                         ->value()
                         .getStringView());

  EXPECT_EQ("HTTP/1.1", upstream_request_->headers()
                            .get(Http::LowerCaseString("request_protocol"))[0]
                            ->value()
                            .getStringView());

  EXPECT_EQ("bar", upstream_request_->headers()
                       .get(Http::LowerCaseString("request_dynamic_metadata_value"))[0]
                       ->value()
                       .getStringView());

  EXPECT_TRUE(
      absl::StrContains(upstream_request_->headers()
                            .get(Http::LowerCaseString("request_downstream_local_address_value"))[0]
                            ->value()
                            .getStringView(),
                        GetParam() == Network::Address::IpVersion::v4 ? "127.0.0.1:" : "[::1]:"));

  EXPECT_TRUE(absl::StrContains(
      upstream_request_->headers()
          .get(Http::LowerCaseString("request_downstream_directremote_address_value"))[0]
          ->value()
          .getStringView(),
      GetParam() == Network::Address::IpVersion::v4 ? "127.0.0.1:" : "[::1]:"));

  EXPECT_EQ("10.0.0.1:0",
            upstream_request_->headers()
                .get(Http::LowerCaseString("request_downstream_remote_address_value"))[0]
                ->value()
                .getStringView());

  EXPECT_EQ("", upstream_request_->headers()
                    .get(Http::LowerCaseString("request_requested_server_name"))[0]
                    ->value()
                    .getStringView());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"foo", "bar"}};
  upstream_request_->encodeHeaders(response_headers, false);
  Buffer::OwnedImpl response_data1("good");
  upstream_request_->encodeData(response_data1, false);
  Buffer::OwnedImpl response_data2("bye");
  upstream_request_->encodeData(response_data2, true);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ("7", response->headers()
                     .get(Http::LowerCaseString("response_body_size"))[0]
                     ->value()
                     .getStringView());
  EXPECT_EQ("bar", response->headers()
                       .get(Http::LowerCaseString("response_metadata_foo"))[0]
                       ->value()
                       .getStringView());
  EXPECT_EQ("bat", response->headers()
                       .get(Http::LowerCaseString("response_metadata_baz"))[0]
                       ->value()
                       .getStringView());
  EXPECT_EQ("HTTP/1.1", response->headers()
                            .get(Http::LowerCaseString("request_protocol"))[0]
                            ->value()
                            .getStringView());
  EXPECT_TRUE(response->headers().get(Http::LowerCaseString("foo")).empty());

  cleanup();
}

// Upstream call followed by continuation.
TEST_P(LuaIntegrationTest, UpstreamHttpCall) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
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
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-forwarded-for", "10.0.0.1"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_lua_connection_));
  ASSERT_TRUE(fake_lua_connection_->waitForNewStream(*dispatcher_, lua_request_));
  ASSERT_TRUE(lua_request_->waitForEndStream(*dispatcher_));
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"foo", "bar"}};
  lua_request_->encodeHeaders(response_headers, false);
  Buffer::OwnedImpl response_data1("good");
  lua_request_->encodeData(response_data1, true);

  waitForNextUpstreamRequest();
  EXPECT_EQ("bar", upstream_request_->headers()
                       .get(Http::LowerCaseString("upstream_foo"))[0]
                       ->value()
                       .getStringView());
  EXPECT_EQ("4", upstream_request_->headers()
                     .get(Http::LowerCaseString("upstream_body_size"))[0]
                     ->value()
                     .getStringView());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  cleanup();
}

// Test whether the 'response_headers_to_add' is valid for the Lua 'respond' method.
TEST_P(LuaIntegrationTest, Respond) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        request_handle:respond(
          {[":status"] = "403"},
          "nope")
      end
)EOF";

  initializeFilter(FILTER_AND_CODE);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/alt/route"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-forwarded-for", "10.0.0.1"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  ASSERT_TRUE(response->waitForEndStream());
  cleanup();

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
  EXPECT_EQ("nope", response->body());
  EXPECT_EQ(
      "fake_value",
      response->headers().get(Http::LowerCaseString("fake_header"))[0]->value().getStringView());
}

// Upstream call followed by immediate response.
TEST_P(LuaIntegrationTest, UpstreamCallAndRespond) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
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
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-forwarded-for", "10.0.0.1"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_lua_connection_));
  ASSERT_TRUE(fake_lua_connection_->waitForNewStream(*dispatcher_, lua_request_));
  ASSERT_TRUE(lua_request_->waitForEndStream(*dispatcher_));
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"foo", "bar"}};
  lua_request_->encodeHeaders(response_headers, true);

  ASSERT_TRUE(response->waitForEndStream());
  cleanup();

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
  EXPECT_EQ("nope", response->body());
}

// Upstream fire and forget asynchronous call.
TEST_P(LuaIntegrationTest, UpstreamAsyncHttpCall) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: envoy.filters.http.lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        local headers, body = request_handle:httpCall(
        "lua_cluster",
        {
          [":method"] = "POST",
          [":path"] = "/",
          [":authority"] = "lua_cluster"
        },
        "hello world",
        5000,
        true)
      end
)EOF";

  initializeFilter(FILTER_AND_CODE);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-forwarded-for", "10.0.0.1"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_lua_connection_));
  ASSERT_TRUE(fake_lua_connection_->waitForNewStream(*dispatcher_, lua_request_));
  ASSERT_TRUE(lua_request_->waitForEndStream(*dispatcher_));
  // Sanity checking that we sent the expected data.
  EXPECT_THAT(lua_request_->headers(), HeaderValueOf(Http::Headers::get().Method, "POST"));
  EXPECT_THAT(lua_request_->headers(), HeaderValueOf(Http::Headers::get().Path, "/"));

  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  cleanup();

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Filter alters headers and changes route.
TEST_P(LuaIntegrationTest, ChangeRoute) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        request_handle:headers():remove(":path")
        request_handle:headers():add(":path", "/alt/route")
      end
)EOF";

  initializeFilter(FILTER_AND_CODE);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-forwarded-for", "10.0.0.1"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  waitForNextUpstreamRequest(2);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  cleanup();

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Should survive from 30 calls when calling streamInfo():dynamicMetadata(). This is a regression
// test for #4305.
TEST_P(LuaIntegrationTest, SurviveMultipleCalls) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        request_handle:streamInfo():dynamicMetadata()
      end
)EOF";

  initializeFilter(FILTER_AND_CODE);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-forwarded-for", "10.0.0.1"}};

  for (uint32_t i = 0; i < 30; ++i) {
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  cleanup();
}

// Basic test for verifying signature.
TEST_P(LuaIntegrationTest, SignatureVerification) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
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
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},    {":path", "/test/long/url"},     {":scheme", "https"},
      {":authority", "host"}, {"x-forwarded-for", "10.0.0.1"}, {"message", "hello"},
      {"keyid", "foo"},       {"signature", signature},        {"hash", "sha256"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  EXPECT_EQ("approved", upstream_request_->headers()
                            .get(Http::LowerCaseString("signature_verification"))[0]
                            ->value()
                            .getStringView());

  EXPECT_EQ("done", upstream_request_->headers()
                        .get(Http::LowerCaseString("verification"))[0]
                        ->value()
                        .getStringView());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanup();
}

const std::string FILTER_AND_CODE =
    R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        request_handle:headers():add("code", "code_from_global")
      end
  source_codes:
    hello.lua:
      inline_string: |
        function envoy_on_request(request_handle)
          request_handle:headers():add("code", "code_from_hello")
        end
    byebye.lua:
      inline_string: |
        function envoy_on_request(request_handle)
          request_handle:headers():add("code", "code_from_byebye")
        end
)EOF";

const std::string INITIAL_ROUTE_CONFIG =
    R"EOF(
name: basic_lua_routes
virtual_hosts:
- name: rds_vhost_1
  domains: ["lua.per.route"]
  routes:
  - match:
      prefix: "/lua/per/route/default"
    route:
      cluster: lua_cluster
  - match:
      prefix: "/lua/per/route/disabled"
    route:
      cluster: lua_cluster
    typed_per_filter_config:
      lua:
        "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.LuaPerRoute
        disabled: true
  - match:
      prefix: "/lua/per/route/hello"
    route:
      cluster: lua_cluster
    typed_per_filter_config:
      lua:
        "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.LuaPerRoute
        name: hello.lua
  - match:
      prefix: "/lua/per/route/byebye"
    route:
      cluster: lua_cluster
    typed_per_filter_config:
      lua:
        "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.LuaPerRoute
        name: byebye.lua
  - match:
      prefix: "/lua/per/route/inline"
    route:
      cluster: lua_cluster
    typed_per_filter_config:
      lua:
        "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.LuaPerRoute
        source_code:
          inline_string: |
            function envoy_on_request(request_handle)
              request_handle:headers():add("code", "inline_code_from_inline")
            end
  - match:
      prefix: "/lua/per/route/nocode"
    route:
      cluster: lua_cluster
    typed_per_filter_config:
      lua:
        "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.LuaPerRoute
        name: nocode.lua
)EOF";

const std::string UPDATE_ROUTE_CONFIG =
    R"EOF(
name: basic_lua_routes
virtual_hosts:
- name: rds_vhost_1
  domains: ["lua.per.route"]
  routes:
  - match:
      prefix: "/lua/per/route/hello"
    route:
      cluster: lua_cluster
    typed_per_filter_config:
      lua:
        "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.LuaPerRoute
        source_code:
          inline_string: |
            function envoy_on_request(request_handle)
              request_handle:headers():add("code", "inline_code_from_hello")
            end
  - match:
      prefix: "/lua/per/route/inline"
    route:
      cluster: lua_cluster
    typed_per_filter_config:
      lua:
        "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.LuaPerRoute
        source_code:
          inline_string: |
            function envoy_on_request(request_handle)
              request_handle:headers():add("code", "new_inline_code_from_inline")
            end
)EOF";

// Test whether LuaPerRoute works properly. Since this test is mainly for configuration, the Lua
// script can be very simple.
TEST_P(LuaIntegrationTest, BasicTestOfLuaPerRoute) {
  initializeWithYaml(FILTER_AND_CODE, INITIAL_ROUTE_CONFIG);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto check_request = [this](Http::TestRequestHeaderMapImpl request_headers,
                              std::string expected_value) {
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    waitForNextUpstreamRequest(1);

    auto entry = upstream_request_->headers().get(Http::LowerCaseString("code"));
    if (!expected_value.empty()) {
      EXPECT_EQ(expected_value, entry[0]->value().getStringView());
    } else {
      EXPECT_TRUE(entry.empty());
    }

    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  };

  // Lua code defined in 'default_source_code' will be executed by default.
  Http::TestRequestHeaderMapImpl default_headers{{":method", "GET"},
                                                 {":path", "/lua/per/route/default"},
                                                 {":scheme", "http"},
                                                 {":authority", "lua.per.route"},
                                                 {"x-forwarded-for", "10.0.0.1"}};
  check_request(default_headers, "code_from_global");

  // Test whether LuaPerRoute can disable the Lua filter.
  Http::TestRequestHeaderMapImpl disabled_headers{{":method", "GET"},
                                                  {":path", "/lua/per/route/disabled"},
                                                  {":scheme", "http"},
                                                  {":authority", "lua.per.route"},
                                                  {"x-forwarded-for", "10.0.0.1"}};
  check_request(disabled_headers, "");

  // Test whether LuaPerRoute can correctly reference Lua code defined in filter config.
  Http::TestRequestHeaderMapImpl hello_headers{{":method", "GET"},
                                               {":path", "/lua/per/route/hello"},
                                               {":scheme", "http"},
                                               {":authority", "lua.per.route"},
                                               {"x-forwarded-for", "10.0.0.1"}};
  check_request(hello_headers, "code_from_hello");

  Http::TestRequestHeaderMapImpl byebye_headers{{":method", "GET"},
                                                {":path", "/lua/per/route/byebye"},
                                                {":scheme", "http"},
                                                {":authority", "lua.per.route"},
                                                {"x-forwarded-for", "10.0.0.1"}};
  check_request(byebye_headers, "code_from_byebye");

  // Test whether LuaPerRoute can directly provide inline Lua code.
  Http::TestRequestHeaderMapImpl inline_headers{{":method", "GET"},
                                                {":path", "/lua/per/route/inline"},
                                                {":scheme", "http"},
                                                {":authority", "lua.per.route"},
                                                {"x-forwarded-for", "10.0.0.1"}};
  check_request(inline_headers, "inline_code_from_inline");

  // When the name referenced by LuaPerRoute does not exist, Lua filter does nothing.
  Http::TestRequestHeaderMapImpl nocode_headers{{":method", "GET"},
                                                {":path", "/lua/per/route/nocode"},
                                                {":scheme", "http"},
                                                {":authority", "lua.per.route"},
                                                {"x-forwarded-for", "10.0.0.1"}};

  check_request(nocode_headers, "");
  cleanup();
}

TEST_P(LuaIntegrationTest, DirectResponseLuaMetadata) {
  const std::string filter_config =
      R"EOF(
  name: lua
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
    default_source_code:
      inline_string: |
        function envoy_on_response(response_handle)
          response_handle:headers():add('foo', response_handle:metadata():get('foo') or 'nil')
        end
)EOF";

  std::string route_config =
      R"EOF(
name: basic_lua_routes
virtual_hosts:
- name: rds_vhost_1
  domains: ["lua.per.route"]
  routes:
  - match:
      prefix: "/lua/direct_response"
    direct_response:
      status: 200
      body:
        inline_string: "hello"
    metadata:
      filter_metadata:
        envoy.filters.http.lua:
          foo: bar
)EOF";

  initializeWithYaml(filter_config, route_config);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Lua code defined in 'default_source_code' will be executed by default.
  Http::TestRequestHeaderMapImpl default_headers{{":method", "GET"},
                                                 {":path", "/lua/direct_response"},
                                                 {":scheme", "http"},
                                                 {":authority", "lua.per.route"},
                                                 {"x-forwarded-for", "10.0.0.1"}};

  auto encoder_decoder = codec_client_->startRequest(default_headers);
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  EXPECT_EQ("bar",
            response->headers().get(Http::LowerCaseString("foo"))[0]->value().getStringView());

  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("hello", response->body());

  cleanup();
}

// Test whether Rds can correctly deliver LuaPerRoute configuration.
TEST_P(LuaIntegrationTest, RdsTestOfLuaPerRoute) {
// When the route configuration is updated dynamically via RDS and the configuration contains an
// inline Lua code, Envoy may call `luaL_newstate`
// (https://www.lua.org/manual/5.1/manual.html#luaL_newstate) in multiple threads to create new
// lua_State objects.
// During lua_State creation, 'LuaJIT' uses some static local variables shared by multiple threads
// to aid memory allocation. Although 'LuaJIT' itself guarantees that there is no thread safety
// issue here, the use of these static local variables by multiple threads will cause a TSAN alarm.
#if defined(__has_feature) && __has_feature(thread_sanitizer)
  ENVOY_LOG_MISC(critical, "LuaIntegrationTest::RdsTestOfLuaPerRoute not supported by this "
                           "compiler configuration");
#else
  initializeWithRds(FILTER_AND_CODE, "basic_lua_routes", INITIAL_ROUTE_CONFIG);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto check_request = [this](Http::TestRequestHeaderMapImpl request_headers,
                              std::string expected_value) {
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    waitForNextUpstreamRequest(1);

    auto entry = upstream_request_->headers().get(Http::LowerCaseString("code"));
    if (!expected_value.empty()) {
      EXPECT_EQ(expected_value, entry[0]->value().getStringView());
    } else {
      EXPECT_TRUE(entry.empty());
    }

    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  };

  Http::TestRequestHeaderMapImpl hello_headers{{":method", "GET"},
                                               {":path", "/lua/per/route/hello"},
                                               {":scheme", "http"},
                                               {":authority", "lua.per.route"},
                                               {"x-forwarded-for", "10.0.0.1"}};
  check_request(hello_headers, "code_from_hello");

  Http::TestRequestHeaderMapImpl inline_headers{{":method", "GET"},
                                                {":path", "/lua/per/route/inline"},
                                                {":scheme", "http"},
                                                {":authority", "lua.per.route"},
                                                {"x-forwarded-for", "10.0.0.1"}};
  check_request(inline_headers, "inline_code_from_inline");

  // Update route config by RDS. Test whether RDS can work normally.
  sendSotwDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration,
      {TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(UPDATE_ROUTE_CONFIG)},
      "2");
  test_server_->waitForCounterGe("http.config_test.rds.basic_lua_routes.update_success", 2);

  check_request(hello_headers, "inline_code_from_hello");
  check_request(inline_headers, "new_inline_code_from_inline");

  cleanup();
#endif
}

// Rewrite response buffer.
TEST_P(LuaIntegrationTest, RewriteResponseBuffer) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_response(response_handle)
        local content_length = response_handle:body():setBytes("ok")
        response_handle:logTrace(content_length)

        response_handle:headers():replace("content-length", content_length)
      end
)EOF";

  testRewriteResponse(FILTER_AND_CODE);
}

// Rewrite response buffer, without original upstream response body
// and always wrap body.
TEST_P(LuaIntegrationTest, RewriteResponseBufferWithoutUpstreamBody) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_response(response_handle)
        local content_length = response_handle:body(true):setBytes("ok")
        response_handle:logTrace(content_length)

        response_handle:headers():replace("content-length", content_length)
      end
)EOF";

  testRewriteResponseWithoutUpstreamBody(FILTER_AND_CODE, true);
}

// Rewrite response buffer, without original upstream response body
// and don't always wrap body.
TEST_P(LuaIntegrationTest, RewriteResponseBufferWithoutUpstreamBodyAndDisableWrapBody) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_response(response_handle)
        if response_handle:body(false) then
          local content_length = response_handle:body():setBytes("ok")
          response_handle:logTrace(content_length)
          response_handle:headers():replace("content-length", content_length)
        end
      end
)EOF";

  testRewriteResponseWithoutUpstreamBody(FILTER_AND_CODE, false);
}

// Rewrite chunked response body.
TEST_P(LuaIntegrationTest, RewriteChunkedBody) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_response(response_handle)
        response_handle:headers():replace("content-length", 2)
        local last
        for chunk in response_handle:bodyChunks() do
          chunk:setBytes("")
          last = chunk
        end
        last:setBytes("ok")
      end
)EOF";

  testRewriteResponse(FILTER_AND_CODE);
}

TEST_P(LuaIntegrationTest, RewriteResponseBufferWithoutHeaderReplaceContentLength) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_response(response_handle)
        local content_length = response_handle:body():setBytes("ok")
        response_handle:logTrace(content_length)
      end
)EOF";

  testRewriteResponse(FILTER_AND_CODE);
}

// Test whether setting the HTTP1 reason phrase
TEST_P(LuaIntegrationTest, Http1ReasonPhrase) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_response(response_handle)
        response_handle:headers():setHttp1ReasonPhrase("Slow Down")
      end
)EOF";

  initializeFilter(FILTER_AND_CODE);

  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.1\r\n\r\n", &response, true);
  EXPECT_TRUE(response.find("HTTP/1.1 400 Slow Down\r\n") == 0);
}

// Lua tests that need HTTP2.
class Http2LuaIntegrationTest : public LuaIntegrationTest {
protected:
  Http2LuaIntegrationTest() : LuaIntegrationTest(Http::CodecType::HTTP2) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, Http2LuaIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test sending local reply due to too much data. HTTP2 is needed as it
// will propagate the end stream from the downstream in the same decodeData
// call the filter receives the downstream request body.
TEST_P(Http2LuaIntegrationTest, LocalReplyWhenWaitingForBodyFollowedByHttpRequest) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        local initial_req_body = request_handle:body()
        local headers, body = request_handle:httpCall(
        "lua_cluster",
        {
          [":method"] = "POST",
          [":path"] = "/",
          [":authority"] = "lua_cluster"
        },
        "hello world",
        1000)
        request_handle:headers():replace("x-code", headers["code"] or "")
      end
)EOF";

  // Set low buffer limits to allow us to trigger local reply easy.
  const int buffer_limit = 65535;
  config_helper_.setBufferLimits(buffer_limit, buffer_limit);

  initializeFilter(FILTER_AND_CODE);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":scheme", "http"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "host"}});
  auto request_encoder = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder, buffer_limit + 1, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
}

} // namespace
} // namespace Envoy
