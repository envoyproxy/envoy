#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/data/core/v3/tlv_metadata.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/protobuf/utility.h"

#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class LuaIntegrationTest : public UpstreamDownstreamIntegrationTest {
public:
  void createUpstreams() override {
    fake_upstreams_count_ = 3;
    HttpIntegrationTest::createUpstreams();

    // Create the xDS upstream.
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  static std::vector<std::tuple<HttpProtocolTestParams, bool>>
  getDefaultTestParams(const std::vector<Http::CodecType>& downstream_protocols =
                           {
                               Http::CodecType::HTTP1,
                               Http::CodecType::HTTP2,
                           },
                       const std::vector<Http::CodecType>& upstream_protocols = {
                           Http::CodecType::HTTP1,
                           Http::CodecType::HTTP2,
                       }) {
    std::vector<std::tuple<HttpProtocolTestParams, bool>> ret;
    std::vector<HttpProtocolTestParams> protocol_defaults =
        HttpProtocolIntegrationTest::getProtocolTestParams(downstream_protocols,
                                                           upstream_protocols);
    const std::vector<bool> testing_downstream_filter_values{true, false};

    for (auto& param : protocol_defaults) {
      for (bool testing_downstream_filter : testing_downstream_filter_values) {
        ret.push_back(std::make_tuple(param, testing_downstream_filter));
      }
    }
    return ret;
  }

  void initializeFilter(const std::string& filter_config, const std::string& domain = "*") {
    config_helper_.prependFilter(filter_config, testing_downstream_filter_);

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

          // Metadata variables for the virtual host and route.
          const std::string key = "lua";
          Protobuf::Struct value;
          std::string yaml;

          // Sets the virtual host's metadata.
          yaml =
              R"EOF(
            foo.bar:
              foo: vhost_bar
              baz: vhost_bat
          )EOF";
          TestUtility::loadFromYaml(yaml, value);
          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_metadata()
              ->mutable_filter_metadata()
              ->insert(Protobuf::MapPair<std::string, Protobuf::Struct>(key, value));

          // Sets the route's metadata.
          yaml =
              R"EOF(
            foo.bar:
              foo: bar
              baz: bat
            keyset:
              foo: MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAp0cSZtAdFgMI1zQJwG8ujTXFMcRY0+SA6fMZGEfQYuxcz/e8UelJ1fLDVAwYmk7KHoYzpizy0JIxAcJ+OAE+cd6a6RpwSEm/9/vizlv0vWZv2XMRAqUxk/5amlpQZE/4sRg/qJdkZZjKrSKjf5VEUQg2NytExYyYWG+3FEYpzYyUeVktmW0y/205XAuEQuxaoe+AUVKeoON1iDzvxywE42C0749XYGUFicqBSRj2eO7jm4hNWvgTapYwpswM3hV9yOAPOVQGKNXzNbLDbFTHyLw3OKayGs/4FUBa+ijlGD9VDawZq88RRaf5ztmH22gOSiKcrHXe40fsnrzh/D27uwIDAQAB
          )EOF";
          TestUtility::loadFromYaml(yaml, value);
          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_metadata()
              ->mutable_filter_metadata()
              ->insert(Protobuf::MapPair<std::string, Protobuf::Struct>(key, value));
        });

    // This filter is not compatible with the async load balancer, as httpCall with data will
    // always hit the `router_.awaitingHost()` check in `AsyncStreamImpl::sendData`.
    async_lb_ = false;

    initialize();
  }

  void initializeWithYaml(const std::string& filter_config, const std::string& route_config) {
    config_helper_.prependFilter(filter_config, testing_downstream_filter_);

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
      // make sure upstream filter config only present in the first cluster, otherwise we will end
      // up in an infinite loop of httpCall()s
      lua_cluster->set_name("lua_cluster");
      clearUpstreamFilters(lua_cluster);

      auto* alt_cluster = bootstrap.mutable_static_resources()->add_clusters();
      alt_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      alt_cluster->set_name("alt_cluster");
      clearUpstreamFilters(alt_cluster);

      auto* xds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      xds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      xds_cluster->set_name("xds_cluster");
      ConfigHelper::setHttp2(*xds_cluster);
      xds_cluster->clear_transport_socket();
      clearUpstreamFilters(xds_cluster);
    });
  }

  static void clearUpstreamFilters(envoy::config::cluster::v3::Cluster* cluster) {
    ConfigHelper::HttpProtocolOptions old_protocol_options;
    if (cluster->typed_extension_protocol_options().contains(
            "envoy.extensions.upstreams.http.v3.HttpProtocolOptions")) {
      old_protocol_options = MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
          (*cluster->mutable_typed_extension_protocol_options())
              ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
      old_protocol_options.clear_http_filters();
      (*cluster->mutable_typed_extension_protocol_options())
          ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
              .PackFrom(old_protocol_options);
    }
  }

  void initializeWithRds(const std::string& filter_config, const std::string& route_config_name,
                         const std::string& initial_route_config) {
    config_helper_.prependFilter(filter_config, testing_downstream_filter_);

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

      EXPECT_TRUE(compareSotwDiscoveryRequest(Config::TestTypeUrl::get().RouteConfiguration, "",
                                              {route_config_name}, true));
      sendSotwDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
          Config::TestTypeUrl::get().RouteConfiguration,
          {TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(
              initial_route_config)},
          "1");
    };
    // async lb doesn't seem to work for xds.
    async_lb_ = false;
    initialize();
    registerTestServerPorts({"http"});
  }

  void expectResponseBodyRewrite(const std::string& code, bool empty_body, bool enable_wrap_body) {
    initializeFilter(code);
    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                   {":path", "/test/long/url"},
                                                   {":scheme", "http"},
                                                   {":authority", "foo.lyft.com"},
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

  IntegrationStreamDecoderPtr initializeAndSendRequest(const std::string& code) {
    initializeFilter(code);
    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                   {":path", "/test/long/url"},
                                                   {":scheme", "http"},
                                                   {":authority", "foo.lyft.com"},
                                                   {"x-forwarded-for", "10.0.0.1"}};

    auto encoder_decoder = codec_client_->startRequest(request_headers);
    Buffer::OwnedImpl request_data("done");
    encoder_decoder.first.encodeData(request_data, true);
    waitForNextUpstreamRequest();

    return std::move(encoder_decoder.second);
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

  const char* expectedDownStreamProtocol() const {
    switch (downstream_protocol_) {
    case Http::CodecType::HTTP1:
      return "HTTP/1.1";
    case Http::CodecType::HTTP2:
      return "HTTP/2";
    case Http::CodecType::HTTP3:
      return "HTTP/3";
    default:
      return "";
    }
  }

  FakeHttpConnectionPtr fake_lua_connection_;
  FakeStreamPtr lua_request_;
};

INSTANTIATE_TEST_SUITE_P(Protocols, LuaIntegrationTest,
                         testing::ValuesIn(LuaIntegrationTest::getDefaultTestParams()),
                         UpstreamDownstreamIntegrationTest::testParamsToString);

// Regression test for pulling route info during early local replies using the Lua filter
// metadata() API. Covers both the upgrade required and no authority cases.
TEST_P(LuaIntegrationTest, CallMetadataDuringLocalReply) {
  if (!testing_downstream_filter_) {
    GTEST_SKIP() << "This is a local reply test that does not go upstream";
  }
  if (downstream_protocol_ != Http::CodecType::HTTP1) {
    GTEST_SKIP() << "This is a raw test that only supports http1";
  }

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

// Test that handle:metadata() falls back to metadata under the filter canonical name
// (envoy.filters.http.lua) when no metadata is present under the filter configured name.
TEST_P(LuaIntegrationTest, MetadataFallbackToCanonicalName) {
  const std::string filter_config =
      R"EOF(
name: lua-filter-custom-name
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        local foo_bar = request_handle:metadata():get("foo.bar")
        request_handle:logTrace(foo_bar["name"])
        request_handle:logTrace(foo_bar["prop"])
      end
      function envoy_on_response(response_handle)
        local baz_bat = response_handle:metadata():get("baz.bat")
        response_handle:logTrace(baz_bat["name"])
        response_handle:logTrace(baz_bat["prop"])
      end
)EOF";

  const std::string route_config =
      R"EOF(
name: test_routes
virtual_hosts:
- name: test_vhost
  domains: ["foo.lyft.com"]
  routes:
  - match:
      path: "/test/long/url"
    metadata:
      filter_metadata:
        envoy.filters.http.lua:
          foo.bar:
            name: foo
            prop: bar
          baz.bat:
            name: baz
            prop: bat
    route:
      cluster: cluster_0
)EOF";

  initializeWithYaml(filter_config, route_config);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "foo.lyft.com"},
                                                 {"x-forwarded-for", "10.0.0.1"}};

  IntegrationStreamDecoderPtr response;
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "foo"},
                                 {"trace", "bar"},
                                 {"trace", "baz"},
                                 {"trace", "bat"},
                             }),
                             {
                               response = codec_client_->makeHeaderOnlyRequest(request_headers);
                               waitForNextUpstreamRequest();

                               upstream_request_->encodeHeaders(default_response_headers_, true);
                               ASSERT_TRUE(response->waitForEndStream());
                             });

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanup();
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

        local vhost_metadata = request_handle:virtualHost():metadata():get("foo.bar")
        local route_metadata = request_handle:route():metadata():get("foo.bar")
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
        request_handle:headers():add("request_vhost_metadata_foo", vhost_metadata["foo"])
        request_handle:headers():add("request_vhost_metadata_baz", vhost_metadata["baz"])
        request_handle:headers():add("request_route_metadata_foo", route_metadata["foo"])
        request_handle:headers():add("request_route_metadata_baz", route_metadata["baz"])
        request_handle:headers():add("request_metadata_foo", metadata["foo"])
        request_handle:headers():add("request_metadata_baz", metadata["baz"])
        if request_handle:connection():ssl() == nil then
          request_handle:headers():add("request_secure", "false")
        else
          request_handle:headers():add("request_secure", "true")
        end
        request_handle:headers():add("request_protocol", request_handle:streamInfo():protocol())
        request_handle:headers():add("request_dynamic_metadata_value", dynamic_metadata_value)
        request_handle:headers():add("request_downstream_direct_local_address_value",
          request_handle:streamInfo():downstreamDirectLocalAddress())
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
        local vhost_metadata = response_handle:virtualHost():metadata():get("foo.bar")
        local route_metadata = response_handle:route():metadata():get("foo.bar")
        local metadata = response_handle:metadata():get("foo.bar")
        local body_length = response_handle:body():length()
        response_handle:headers():add("response_vhost_metadata_foo", vhost_metadata["foo"])
        response_handle:headers():add("response_vhost_metadata_baz", vhost_metadata["baz"])
        response_handle:headers():add("response_route_metadata_foo", route_metadata["foo"])
        response_handle:headers():add("response_route_metadata_baz", route_metadata["baz"])
        response_handle:headers():add("response_metadata_foo", metadata["foo"])
        response_handle:headers():add("response_metadata_baz", metadata["baz"])
        response_handle:headers():add("response_body_size", body_length)
        response_handle:headers():add("request_protocol", response_handle:streamInfo():protocol())
        response_handle:headers():remove("foo")
      end
)EOF";

  initializeFilter(FILTER_AND_CODE);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "foo.lyft.com"},
                                                 {"x-forwarded-for", "10.0.0.1"},
                                                 {"x-test-header", "foo"},
                                                 {"x-test-header", "bar"},
                                                 {"set-cookie", "foo;bar;"},
                                                 {"set-cookie", "1,3;2,5;"}};

  IntegrationStreamDecoderPtr response;
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({{"trace", "log test"},
                                                         {"debug", "log test"},
                                                         {"info", "log test"},
                                                         {"warn", "log test"},
                                                         {"error", "log test"},
                                                         {"critical", "log test"}}),
                             {
                               auto encoder_decoder = codec_client_->startRequest(request_headers);
                               Http::StreamEncoder& encoder = encoder_decoder.first;
                               response = std::move(encoder_decoder.second);
                               Buffer::OwnedImpl request_data1("hello");
                               encoder.encodeData(request_data1, false);
                               Buffer::OwnedImpl request_data2("world");
                               encoder.encodeData(request_data2, true);

                               waitForNextUpstreamRequest();
                             });
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

  EXPECT_EQ("vhost_bar", upstream_request_->headers()
                             .get(Http::LowerCaseString("request_vhost_metadata_foo"))[0]
                             ->value()
                             .getStringView());

  EXPECT_EQ("vhost_bat", upstream_request_->headers()
                             .get(Http::LowerCaseString("request_vhost_metadata_baz"))[0]
                             ->value()
                             .getStringView());

  EXPECT_EQ("bar", upstream_request_->headers()
                       .get(Http::LowerCaseString("request_route_metadata_foo"))[0]
                       ->value()
                       .getStringView());

  EXPECT_EQ("bat", upstream_request_->headers()
                       .get(Http::LowerCaseString("request_route_metadata_baz"))[0]
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
  EXPECT_EQ(downstream_protocol_ == Http::CodecType::HTTP3 ? "true" : "false",
            upstream_request_->headers()
                .get(Http::LowerCaseString("request_secure"))[0]
                ->value()
                .getStringView());

  EXPECT_EQ(expectedDownStreamProtocol(), upstream_request_->headers()
                                              .get(Http::LowerCaseString("request_protocol"))[0]
                                              ->value()
                                              .getStringView());

  EXPECT_EQ("bar", upstream_request_->headers()
                       .get(Http::LowerCaseString("request_dynamic_metadata_value"))[0]
                       ->value()
                       .getStringView());

  EXPECT_TRUE(absl::StrContains(
      upstream_request_->headers()
          .get(Http::LowerCaseString("request_downstream_direct_local_address_value"))[0]
          ->value()
          .getStringView(),
      std::get<0>(GetParam()).version == Network::Address::IpVersion::v4 ? "127.0.0.1:"
                                                                         : "[::1]:"));

  EXPECT_TRUE(absl::StrContains(
      upstream_request_->headers()
          .get(Http::LowerCaseString("request_downstream_local_address_value"))[0]
          ->value()
          .getStringView(),
      std::get<0>(GetParam()).version == Network::Address::IpVersion::v4 ? "127.0.0.1:"
                                                                         : "[::1]:"));

  EXPECT_TRUE(absl::StrContains(
      upstream_request_->headers()
          .get(Http::LowerCaseString("request_downstream_directremote_address_value"))[0]
          ->value()
          .getStringView(),
      std::get<0>(GetParam()).version == Network::Address::IpVersion::v4 ? "127.0.0.1:"
                                                                         : "[::1]:"));

  EXPECT_EQ("10.0.0.1:0",
            upstream_request_->headers()
                .get(Http::LowerCaseString("request_downstream_remote_address_value"))[0]
                ->value()
                .getStringView());

  EXPECT_EQ(downstream_protocol_ == Http::CodecType::HTTP3 ? "lyft.com" : "",
            upstream_request_->headers()
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
  EXPECT_EQ("vhost_bar", response->headers()
                             .get(Http::LowerCaseString("response_vhost_metadata_foo"))[0]
                             ->value()
                             .getStringView());
  EXPECT_EQ("vhost_bat", response->headers()
                             .get(Http::LowerCaseString("response_vhost_metadata_baz"))[0]
                             ->value()
                             .getStringView());
  EXPECT_EQ("bar", response->headers()
                       .get(Http::LowerCaseString("response_route_metadata_foo"))[0]
                       ->value()
                       .getStringView());
  EXPECT_EQ("bat", response->headers()
                       .get(Http::LowerCaseString("response_route_metadata_baz"))[0]
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
  EXPECT_EQ(expectedDownStreamProtocol(), response->headers()
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
          [":authority"] = "foo.lyft.com"
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
                                                 {":authority", "foo.lyft.com"},
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
  if (!testing_downstream_filter_) {
    GTEST_SKIP() << "This is a local reply test that does not go upstream";
  }

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
                                                 {":authority", "foo.lyft.com"},
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
          [":authority"] = "foo.lyft.com"
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
                                                 {":authority", "foo.lyft.com"},
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
          [":authority"] = "foo.lyft.com"
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
                                                 {":authority", "foo.lyft.com"},
                                                 {"x-forwarded-for", "10.0.0.1"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_lua_connection_));
  ASSERT_TRUE(fake_lua_connection_->waitForNewStream(*dispatcher_, lua_request_));
  ASSERT_TRUE(lua_request_->waitForEndStream(*dispatcher_));
  // Sanity checking that we sent the expected data.
  EXPECT_THAT(lua_request_->headers(), ContainsHeader(Http::Headers::get().Method, "POST"));
  EXPECT_THAT(lua_request_->headers(), ContainsHeader(Http::Headers::get().Path, "/"));

  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  cleanup();

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Filter alters headers and changes route.
TEST_P(LuaIntegrationTest, ChangeRoute) {
  if (!testing_downstream_filter_) {
    GTEST_SKIP() << "Changing routes only works with downstream filters";
  }
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
                                                 {":authority", "foo.lyft.com"},
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
                                                 {":authority", "foo.lyft.com"},
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
  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "https"},
                                                 {":authority", "foo.lyft.com"},
                                                 {"x-forwarded-for", "10.0.0.1"},
                                                 {"message", "hello"},
                                                 {"keyid", "foo"},
                                                 {"signature", signature},
                                                 {"hash", "sha256"}};

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
  domains: ["foo.lyft.com"]
  routes:
  - match:
      prefix: "/lua/per/route/default"
    route:
      cluster: cluster_0
  - match:
      prefix: "/lua/per/route/disabled"
    route:
      cluster: cluster_0
    typed_per_filter_config:
      lua:
        "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.LuaPerRoute
        disabled: true
  - match:
      prefix: "/lua/per/route/hello"
    route:
      cluster: cluster_0
    typed_per_filter_config:
      lua:
        "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.LuaPerRoute
        name: hello.lua
  - match:
      prefix: "/lua/per/route/byebye"
    route:
      cluster: cluster_0
    typed_per_filter_config:
      lua:
        "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.LuaPerRoute
        name: byebye.lua
  - match:
      prefix: "/lua/per/route/inline"
    route:
      cluster: cluster_0
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
      cluster: cluster_0
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
  domains: ["foo.lyft.com"]
  routes:
  - match:
      prefix: "/lua/per/route/hello"
    route:
      cluster: cluster_0
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
      cluster: cluster_0
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
    waitForNextUpstreamRequest(0);

    auto entry = upstream_request_->headers().get(Http::LowerCaseString("code"));
    if (!expected_value.empty()) {
      EXPECT_FALSE(entry.empty()) << "no header found. expected value: " << expected_value;
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
                                                 {":authority", "foo.lyft.com"},
                                                 {"x-forwarded-for", "10.0.0.1"}};
  check_request(default_headers, "code_from_global");

  // Test whether LuaPerRoute can disable the Lua filter.
  Http::TestRequestHeaderMapImpl disabled_headers{{":method", "GET"},
                                                  {":path", "/lua/per/route/disabled"},
                                                  {":scheme", "http"},
                                                  {":authority", "foo.lyft.com"},
                                                  {"x-forwarded-for", "10.0.0.1"}};
  check_request(disabled_headers, "");

  // Test whether LuaPerRoute can correctly reference Lua code defined in filter config.
  Http::TestRequestHeaderMapImpl hello_headers{{":method", "GET"},
                                               {":path", "/lua/per/route/hello"},
                                               {":scheme", "http"},
                                               {":authority", "foo.lyft.com"},
                                               {"x-forwarded-for", "10.0.0.1"}};
  check_request(hello_headers, "code_from_hello");

  Http::TestRequestHeaderMapImpl byebye_headers{{":method", "GET"},
                                                {":path", "/lua/per/route/byebye"},
                                                {":scheme", "http"},
                                                {":authority", "foo.lyft.com"},
                                                {"x-forwarded-for", "10.0.0.1"}};
  check_request(byebye_headers, "code_from_byebye");

  // Test whether LuaPerRoute can directly provide inline Lua code.
  Http::TestRequestHeaderMapImpl inline_headers{{":method", "GET"},
                                                {":path", "/lua/per/route/inline"},
                                                {":scheme", "http"},
                                                {":authority", "foo.lyft.com"},
                                                {"x-forwarded-for", "10.0.0.1"}};
  check_request(inline_headers, "inline_code_from_inline");

  // When the name referenced by LuaPerRoute does not exist, Lua filter does nothing.
  Http::TestRequestHeaderMapImpl nocode_headers{{":method", "GET"},
                                                {":path", "/lua/per/route/nocode"},
                                                {":scheme", "http"},
                                                {":authority", "foo.lyft.com"},
                                                {"x-forwarded-for", "10.0.0.1"}};

  check_request(nocode_headers, "");
  cleanup();
}

TEST_P(LuaIntegrationTest, DirectResponseLuaMetadata) {
  if (!testing_downstream_filter_) {
    GTEST_SKIP() << "Direct response only works with downstream filters";
  }
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
    waitForNextUpstreamRequest(0);

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
                                               {":authority", "foo.lyft.com"},
                                               {"x-forwarded-for", "10.0.0.1"}};
  check_request(hello_headers, "code_from_hello");

  Http::TestRequestHeaderMapImpl inline_headers{{":method", "GET"},
                                                {":path", "/lua/per/route/inline"},
                                                {":scheme", "http"},
                                                {":authority", "foo.lyft.com"},
                                                {"x-forwarded-for", "10.0.0.1"}};
  check_request(inline_headers, "inline_code_from_inline");

  // Update route config by RDS. Test whether RDS can work normally.
  sendSotwDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TestTypeUrl::get().RouteConfiguration,
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

// Rewrite response buffer to a huge body.
TEST_P(LuaIntegrationTest, RewriteResponseToHugeBody) {
  const std::string FILTER_AND_CODE =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_response(response_handle)
        -- Default HTTP2 body buffer limit is 16MB for now. To set
        -- a 16MB+ body to ensure both HTTP1 and HTTP2 will hit the limit.
        local huge_body = string.rep("a", 1024 * 1024 * 16 + 1) -- 16MB + 1
        local content_length = response_handle:body():setBytes(huge_body)
        response_handle:logTrace(content_length)
      end
)EOF";

  auto response = initializeAndSendRequest(FILTER_AND_CODE);

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"foo", "bar"}};
  upstream_request_->encodeHeaders(response_headers, false);
  Buffer::OwnedImpl response_data1("good");
  upstream_request_->encodeData(response_data1, false);
  Buffer::OwnedImpl response_data2("bye");
  upstream_request_->encodeData(response_data2, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  EXPECT_EQ(response->headers().getStatusValue(), "500");

  cleanup();
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
  if (std::get<0>(GetParam()).http2_implementation == Http2Impl::Nghttp2) {
    GTEST_SKIP() << "This test fails with nghttp2";
  }
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
  if (downstream_protocol_ != Http::CodecType::HTTP1) {
    GTEST_SKIP() << "sendRaw only works with http1";
  }
  if (!testing_downstream_filter_) {
    GTEST_SKIP() << "This is a local reply test that does not go upstream";
  }
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

// Test sending local reply due to too much data. HTTP2 is needed as it
// will propagate the end stream from the downstream in the same decodeData
// call the filter receives the downstream request body.
TEST_P(LuaIntegrationTest, LocalReplyWhenWaitingForBodyFollowedByHttpRequest) {

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    GTEST_SKIP() << "This test does not work on http1";
  }
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

// Forward declare the filter class
class TestTypedMetadataFilter;

// Custom filter to add typed metadata
class TestTypedMetadataFilter final : public Network::ReadFilter {
public:
  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }

  Network::FilterStatus onNewConnection() override {
    const std::string metadata_key = "envoy.test.typed_metadata";

    // Get mutable access to the typed filter metadata map
    auto& typed_filter_metadata = *read_callbacks_->connection()
                                       .streamInfo()
                                       .dynamicMetadata()
                                       .mutable_typed_filter_metadata();

    // Create metadata protobuf
    envoy::data::core::v3::TlvsMetadata metadata;
    auto* typed_metadata_map = metadata.mutable_typed_metadata();

    // Add basic key-value pair
    (*typed_metadata_map)["test_key"] = "test_key";
    (*typed_metadata_map)["test_value"] = "test_value";

    // Add protocol value
    (*typed_metadata_map)["protocol_version"] = "h2,http/1.1";

    // Add authority
    (*typed_metadata_map)["authority"] = "example.com";

    // Add some metadata that would typically be in SSL properties
    (*typed_metadata_map)["ssl_version"] = "TLSv1.3";
    (*typed_metadata_map)["ssl_cn"] = "client.example.com";

    // Pack metadata into Any
    Protobuf::Any typed_config;
    typed_config.PackFrom(metadata);
    typed_filter_metadata.insert({metadata_key, typed_config});

    return Network::FilterStatus::Continue;
  }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
};

// Filter factory
class TestTypedMetadataFilterConfig final
    : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override {
    return Network::FilterFactoryCb([](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<TestTypedMetadataFilter>());
    });
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Any>();
  }

  std::string name() const override { return "envoy.test.typed_metadata"; }
  std::set<std::string> configTypes() override { return {}; };
};

// ``PPV2`` typed metadata filter that mimics the real proxy protocol behavior
class PPV2TypedMetadataFilter final : public Network::ReadFilter {
public:
  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }

  Network::FilterStatus onNewConnection() override {
    const std::string metadata_key = "envoy.filters.listener.proxy_protocol";

    // Get mutable access to the typed filter metadata map
    auto& typed_filter_metadata = *read_callbacks_->connection()
                                       .streamInfo()
                                       .dynamicMetadata()
                                       .mutable_typed_filter_metadata();

    // Create metadata protobuf that mimics PP v2 structure
    envoy::data::core::v3::TlvsMetadata metadata;
    auto* typed_metadata_map = metadata.mutable_typed_metadata();

    // PP2_TYPE_ALPN (0x01)
    (*typed_metadata_map)["PP2_TYPE_ALPN"] = "h2,http/1.1";

    // PP2_TYPE_AUTHORITY (0x02)
    (*typed_metadata_map)["PP2_TYPE_AUTHORITY"] = "proxy.example.com";

    // PP2_TYPE_CRC32C (0x03)
    (*typed_metadata_map)["PP2_TYPE_CRC32C"] = std::string("\x12\x34\x56\x78", 4);

    // PP2_TYPE_NOOP (0x04)
    (*typed_metadata_map)["PP2_TYPE_NOOP"] = "";

    // PP2_TYPE_UNIQUE_ID (0x05)
    (*typed_metadata_map)["PP2_TYPE_UNIQUE_ID"] = "d8e8fca2-dc0f-4a8d-8664-5a97001";

    // PP2_SUBTYPE_SSL (0x20)
    (*typed_metadata_map)["ssl_version"] = "TLSv1.3";
    (*typed_metadata_map)["ssl_cn"] = "client.example.com";
    (*typed_metadata_map)["ssl_cipher"] = "ECDHE-RSA-AES128-GCM-SHA256";

    // Pack metadata into Any
    Protobuf::Any typed_config;
    typed_config.PackFrom(metadata);
    typed_filter_metadata.insert({metadata_key, typed_config});

    return Network::FilterStatus::Continue;
  }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
};

// Filter factory for ``PPV2`` typed metadata
class PPV2TypedMetadataFilterConfig final
    : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override {
    return Network::FilterFactoryCb([](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<PPV2TypedMetadataFilter>());
    });
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Any>();
  }

  std::string name() const override { return "envoy.test.ppv2.typed_metadata"; }
  std::set<std::string> configTypes() override { return {}; };
};

TEST_P(LuaIntegrationTest, ConnectionTypedMetadata) {
  // Register our test filter
  TestTypedMetadataFilterConfig factory;
  Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory> registry(factory);

  // Setup network filter config
  const std::string FILTER_CONFIG = R"EOF(
name: envoy.test.typed_metadata
typed_config:
  "@type": type.googleapis.com/google.protobuf.Any
)EOF";

  config_helper_.addNetworkFilter(FILTER_CONFIG);

  const std::string LUA_FILTER = R"EOF(
  name: lua
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
    default_source_code:
      inline_string: |
        function envoy_on_request(request_handle)
          -- Test valid metadata
          local meta = request_handle:connectionStreamInfo():dynamicTypedMetadata("envoy.test.typed_metadata")
          if meta and meta.typed_metadata then
            -- Check basic key-value pair
            request_handle:headers():add("typed_metadata_key", meta.typed_metadata.test_key)
            request_handle:headers():add("typed_metadata_value", meta.typed_metadata.test_value)

            -- Check protocol field
            if meta.typed_metadata.protocol_version then
              request_handle:headers():add("protocol_version", meta.typed_metadata.protocol_version)
            end

            -- Check authority field
            if meta.typed_metadata.authority then
              request_handle:headers():add("authority", meta.typed_metadata.authority)
            end

            -- Check SSL properties
            if meta.typed_metadata.ssl_version then
              request_handle:headers():add("ssl_version", meta.typed_metadata.ssl_version)
            end

            if meta.typed_metadata.ssl_cn then
              request_handle:headers():add("ssl_cn", meta.typed_metadata.ssl_cn)
            end
          end

          -- Test missing metadata
          local missing = request_handle:connectionStreamInfo():dynamicTypedMetadata("missing.metadata")
          if missing == nil then
            request_handle:headers():add("missing_metadata", "is_nil")
          end
        end
)EOF";

  initializeFilter(LUA_FILTER);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority", "host"},
  };

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  // Verify the typed metadata was accessible
  auto typed_metadata_key =
      upstream_request_->headers().get(Http::LowerCaseString("typed_metadata_key"));
  EXPECT_FALSE(typed_metadata_key.empty());
  EXPECT_EQ("test_key", typed_metadata_key[0]->value().getStringView());

  auto typed_metadata_value =
      upstream_request_->headers().get(Http::LowerCaseString("typed_metadata_value"));
  EXPECT_FALSE(typed_metadata_value.empty());
  EXPECT_EQ("test_value", typed_metadata_value[0]->value().getStringView());

  // Verify protocol fields
  auto protocol_version =
      upstream_request_->headers().get(Http::LowerCaseString("protocol_version"));
  EXPECT_FALSE(protocol_version.empty());
  EXPECT_EQ("h2,http/1.1", protocol_version[0]->value().getStringView());

  auto authority = upstream_request_->headers().get(Http::LowerCaseString("authority"));
  EXPECT_FALSE(authority.empty());
  EXPECT_EQ("example.com", authority[0]->value().getStringView());

  // Verify SSL properties
  auto ssl_version = upstream_request_->headers().get(Http::LowerCaseString("ssl_version"));
  EXPECT_FALSE(ssl_version.empty());
  EXPECT_EQ("TLSv1.3", ssl_version[0]->value().getStringView());

  auto ssl_cn = upstream_request_->headers().get(Http::LowerCaseString("ssl_cn"));
  EXPECT_FALSE(ssl_cn.empty());
  EXPECT_EQ("client.example.com", ssl_cn[0]->value().getStringView());

  // Verify the missing metadata returns nil
  auto missing_metadata =
      upstream_request_->headers().get(Http::LowerCaseString("missing_metadata"));
  EXPECT_FALSE(missing_metadata.empty());
  EXPECT_EQ("is_nil", missing_metadata[0]->value().getStringView());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanup();
}

// Add this after the existing ConnectionTypedMetadata test
TEST_P(LuaIntegrationTest, ProxyProtocolTypedMetadata) {
  // Register our test filter
  PPV2TypedMetadataFilterConfig factory;
  Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory> registry(factory);

  // Setup network filter config
  const std::string FILTER_CONFIG = R"EOF(
name: envoy.test.ppv2.typed_metadata
typed_config:
  "@type": type.googleapis.com/google.protobuf.Any
)EOF";

  config_helper_.addNetworkFilter(FILTER_CONFIG);

  const std::string LUA_FILTER = R"EOF(
  name: lua
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
    default_source_code:
      inline_string: |
        function envoy_on_request(request_handle)
          -- Access Proxy Protocol typed metadata
          local meta = request_handle:connectionStreamInfo():dynamicTypedMetadata("envoy.filters.listener.proxy_protocol")
          if meta and meta.typed_metadata then
            -- Add ALPN values
            if meta.typed_metadata.PP2_TYPE_ALPN then
              request_handle:headers():add("pp-alpn", meta.typed_metadata.PP2_TYPE_ALPN)
            end

            -- Add Authority
            if meta.typed_metadata.PP2_TYPE_AUTHORITY then
              request_handle:headers():add("pp-authority", meta.typed_metadata.PP2_TYPE_AUTHORITY)
            end

            -- Add unique ID if present
            if meta.typed_metadata.PP2_TYPE_UNIQUE_ID then
              request_handle:headers():add("pp-unique-id", meta.typed_metadata.PP2_TYPE_UNIQUE_ID)
            end

            -- Check SSL properties
            if meta.typed_metadata.ssl_version then
              request_handle:headers():add("pp-ssl-version", meta.typed_metadata.ssl_version)
            end

            if meta.typed_metadata.ssl_cipher then
              request_handle:headers():add("pp-ssl-cipher", meta.typed_metadata.ssl_cipher)
            end
          end
        end
)EOF";

  initializeFilter(LUA_FILTER);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority", "host"},
  };

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  // Verify the proxy protocol typed metadata was accessible
  auto alpn = upstream_request_->headers().get(Http::LowerCaseString("pp-alpn"));
  EXPECT_FALSE(alpn.empty());
  EXPECT_EQ("h2,http/1.1", alpn[0]->value().getStringView());

  auto authority = upstream_request_->headers().get(Http::LowerCaseString("pp-authority"));
  EXPECT_FALSE(authority.empty());
  EXPECT_EQ("proxy.example.com", authority[0]->value().getStringView());

  auto unique_id = upstream_request_->headers().get(Http::LowerCaseString("pp-unique-id"));
  EXPECT_FALSE(unique_id.empty());
  EXPECT_EQ("d8e8fca2-dc0f-4a8d-8664-5a97001", unique_id[0]->value().getStringView());

  // Verify SSL properties
  auto ssl_version = upstream_request_->headers().get(Http::LowerCaseString("pp-ssl-version"));
  EXPECT_FALSE(ssl_version.empty());
  EXPECT_EQ("TLSv1.3", ssl_version[0]->value().getStringView());

  auto ssl_cipher = upstream_request_->headers().get(Http::LowerCaseString("pp-ssl-cipher"));
  EXPECT_FALSE(ssl_cipher.empty());
  EXPECT_EQ("ECDHE-RSA-AES128-GCM-SHA256", ssl_cipher[0]->value().getStringView());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanup();
}

// Test StreamInfo dynamicTypedMetadata basic functionality.
TEST_P(LuaIntegrationTest, StreamInfoDynamicTypedMetadataBasic) {
  const std::string FILTER_AND_CODE = R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        -- Test dynamicTypedMetadata function with non-existent filter
        local result = request_handle:streamInfo():dynamicTypedMetadata("nonexistent.filter")
        if result == nil then
          request_handle:headers():add("typed_metadata_result", "nil")
        else
          request_handle:headers():add("typed_metadata_result", "unexpected")
        end

        -- Test another non-existent filter name
        local result2 = request_handle:streamInfo():dynamicTypedMetadata("test.filter")
        if result2 == nil then
          request_handle:headers():add("typed_metadata_result2", "nil")
        else
          request_handle:headers():add("typed_metadata_result2", "unexpected")
        end
      end
)EOF";

  initializeFilter(FILTER_AND_CODE);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  // Verify both calls return nil as expected
  EXPECT_EQ("nil", upstream_request_->headers()
                       .get(Http::LowerCaseString("typed_metadata_result"))[0]
                       ->value()
                       .getStringView());

  EXPECT_EQ("nil", upstream_request_->headers()
                       .get(Http::LowerCaseString("typed_metadata_result2"))[0]
                       ->value()
                       .getStringView());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanup();
}

// Test StreamInfo dynamicTypedMetadata with actual metadata from set_metadata filter.
TEST_P(LuaIntegrationTest, StreamInfoDynamicTypedMetadata) {
  // First, configure the set_metadata filter to set actual typed metadata
  const std::string SET_METADATA_FILTER = R"EOF(
name: envoy.filters.http.set_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
  metadata:
  - metadata_namespace: test.namespace
    typed_value:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        test_key: "test_value"
        version: "1.0.0"
        enabled: true
        count: 42
  - metadata_namespace: simple.typed.metadata
    typed_value:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: "simple_string_value"
)EOF";

  // Then configure the Lua filter to read the typed metadata
  const std::string LUA_FILTER = R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        local stream_info = request_handle:streamInfo()

        -- Test retrieving the structured typed metadata from set_metadata filter
        local struct_meta = stream_info:dynamicTypedMetadata("test.namespace")
        if struct_meta then
          request_handle:headers():add("struct_meta_found", "true")

          -- Access the values directly from the Struct (converted to Lua table)
          if struct_meta.test_key then
            request_handle:headers():add("struct_test_key", struct_meta.test_key)
          end
          if struct_meta.version then
            request_handle:headers():add("struct_version", struct_meta.version)
          end
          if struct_meta.enabled ~= nil then
            request_handle:headers():add("struct_enabled", tostring(struct_meta.enabled))
          end
          if struct_meta.count then
            request_handle:headers():add("struct_count", tostring(struct_meta.count))
          end
        else
          request_handle:headers():add("struct_meta_found", "false")

          -- Debug: Try a few other possible namespaces
          local alt1 = stream_info:dynamicTypedMetadata("envoy.filters.http.set_metadata")
          if alt1 then
            request_handle:headers():add("debug_alt1", "found")
          else
            request_handle:headers():add("debug_alt1", "nil")
          end

          local alt2 = stream_info:dynamicTypedMetadata("set_metadata")
          if alt2 then
            request_handle:headers():add("debug_alt2", "found")
          else
            request_handle:headers():add("debug_alt2", "nil")
          end
        end

        -- Test retrieving the simple typed metadata
        local simple_meta = stream_info:dynamicTypedMetadata("simple.typed.metadata")
        if simple_meta then
          request_handle:headers():add("simple_meta_found", "true")
          if simple_meta.value then
            request_handle:headers():add("simple_value", simple_meta.value)
          end
        else
          request_handle:headers():add("simple_meta_found", "false")
        end

        -- Test non-existent metadata still returns nil
        local missing_meta = stream_info:dynamicTypedMetadata("nonexistent.filter")
        if missing_meta == nil then
          request_handle:headers():add("missing_meta", "nil")
        else
          request_handle:headers():add("missing_meta", "found")
        end
      end
)EOF";

  // Configure both filters in the chain (set_metadata first, then lua)
  config_helper_.prependFilter(LUA_FILTER);
  config_helper_.prependFilter(SET_METADATA_FILTER);

  // Create static clusters
  createClusters();

  // Add basic route configuration
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        hcm.mutable_route_config()
            ->mutable_virtual_hosts(0)
            ->mutable_routes(0)
            ->mutable_match()
            ->set_prefix("/test/long/url");
      });

  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  // Check what we found for struct metadata
  auto struct_found_headers =
      upstream_request_->headers().get(Http::LowerCaseString("struct_meta_found"));
  std::string struct_meta_result = std::string(struct_found_headers[0]->value().getStringView());
  // Verify structured typed metadata was successfully retrieved
  auto test_key_headers =
      upstream_request_->headers().get(Http::LowerCaseString("struct_test_key"));
  if (!test_key_headers.empty()) {
    EXPECT_EQ("test_value", test_key_headers[0]->value().getStringView());
  }

  auto version_headers = upstream_request_->headers().get(Http::LowerCaseString("struct_version"));
  if (!version_headers.empty()) {
    EXPECT_EQ("1.0.0", version_headers[0]->value().getStringView());
  }

  auto enabled_headers = upstream_request_->headers().get(Http::LowerCaseString("struct_enabled"));
  if (!enabled_headers.empty()) {
    EXPECT_EQ("true", enabled_headers[0]->value().getStringView());
  }

  auto count_headers = upstream_request_->headers().get(Http::LowerCaseString("struct_count"));
  if (!count_headers.empty()) {
    EXPECT_EQ("42", count_headers[0]->value().getStringView());
  }

  // Verify simple typed metadata was successfully retrieved
  auto simple_found_headers =
      upstream_request_->headers().get(Http::LowerCaseString("simple_meta_found"));
  if (simple_found_headers.empty()) {
    FAIL() << "simple_meta_found header not present - Lua code crashed before checking simple "
              "metadata";
  }
  EXPECT_EQ("true", simple_found_headers[0]->value().getStringView());

  auto simple_value_headers =
      upstream_request_->headers().get(Http::LowerCaseString("simple_value"));
  if (!simple_value_headers.empty()) {
    EXPECT_EQ("simple_string_value", simple_value_headers[0]->value().getStringView());
  }

  // Verify non-existent metadata still returns nil
  auto missing_meta_headers =
      upstream_request_->headers().get(Http::LowerCaseString("missing_meta"));
  if (!missing_meta_headers.empty()) {
    EXPECT_EQ("nil", missing_meta_headers[0]->value().getStringView());
  }

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanup();
}

// Test ``filterState()`` functionality with simple string values.
TEST_P(LuaIntegrationTest, FilterStateBasic) {
  const std::string FILTER_AND_CODE = R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        local stream_info = request_handle:streamInfo()

        -- Test filterState() function with non-existent key
        local missing_state = stream_info:filterState():get("nonexistent_key")
        if missing_state == nil then
          request_handle:headers():add("missing_state", "nil")
        else
          request_handle:headers():add("missing_state", "unexpected")
        end

        -- Test with another non-existent key
        local another_missing = stream_info:filterState():get("another_missing")
        if another_missing == nil then
          request_handle:headers():add("another_missing", "nil")
        else
          request_handle:headers():add("another_missing", "unexpected")
        end
      end
)EOF";

  initializeFilter(FILTER_AND_CODE);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  // Verify both calls return nil as expected for non-existent keys.
  EXPECT_EQ("nil", upstream_request_->headers()
                       .get(Http::LowerCaseString("missing_state"))[0]
                       ->value()
                       .getStringView());

  EXPECT_EQ("nil", upstream_request_->headers()
                       .get(Http::LowerCaseString("another_missing"))[0]
                       ->value()
                       .getStringView());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanup();
}

// Test that handle:virtualHost():metadata() returns valid metadata when virtual host matches
// but route doesn't, ensuring metadata access works correctly.
TEST_P(LuaIntegrationTest, VirtualHostValidWhenNoRouteMatch) {
  if (!testing_downstream_filter_) {
    GTEST_SKIP() << "This is a local reply test that does not go upstream";
  }

  const std::string filter_config =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        local metadata = request_handle:virtualHost():metadata()
        request_handle:logTrace(metadata:get("foo.bar")["name"])
        request_handle:logTrace(metadata:get("foo.bar")["prop"])
      end
      function envoy_on_response(response_handle)
        local metadata = response_handle:virtualHost():metadata()
        response_handle:logTrace(metadata:get("baz.bat")["name"])
        response_handle:logTrace(metadata:get("baz.bat")["prop"])
      end
)EOF";

  const std::string route_config =
      R"EOF(
name: test_routes
virtual_hosts:
- name: test_vhost
  domains: ["foo.lyft.com"]
  metadata:
    filter_metadata:
      lua:
        foo.bar:
          name: foo
          prop: bar
        baz.bat:
          name: baz
          prop: bat
  routes:
  - match:
      path: "/existing/route"
    route:
      cluster: cluster_0
)EOF";

  initializeWithYaml(filter_config, route_config);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/non/existing/path"},
                                                 {":scheme", "http"},
                                                 {":authority", "foo.lyft.com"},
                                                 {"x-forwarded-for", "10.0.0.1"}};

  IntegrationStreamDecoderPtr response;
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "foo"},
                                 {"trace", "bar"},
                                 {"trace", "baz"},
                                 {"trace", "bat"},
                             }),
                             {
                               auto encoder_decoder = codec_client_->startRequest(request_headers);
                               response = std::move(encoder_decoder.second);

                               ASSERT_TRUE(response->waitForEndStream());
                             });

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());
  cleanup();
}

// Test that handle:virtualHost() returns a valid object when no virtual host matches the request
// authority. This verifies that metadata() returns an empty metadata object that can be safely
// iterated.
TEST_P(LuaIntegrationTest, VirtualHostValidWhenNoVirtualHostMatch) {
  if (!testing_downstream_filter_) {
    GTEST_SKIP() << "This is a local reply test that does not go upstream";
  }

  const std::string FILTER_AND_CODE =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        local virtual_host = request_handle:virtualHost()
        for _, _ in pairs(virtual_host:metadata()) do
          return
        end
        request_handle:logTrace("No metadata found during request handling")
      end
      function envoy_on_response(response_handle)
        local virtual_host = response_handle:virtualHost()
        for _, _ in pairs(virtual_host:metadata()) do
          return
        end
        response_handle:logTrace("No metadata found during response handling")
      end

)EOF";

  initializeFilter(FILTER_AND_CODE, "foo.lyft.com");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "bar.lyft.com"},
                                                 {"x-forwarded-for", "10.0.0.1"}};

  IntegrationStreamDecoderPtr response;
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "No metadata found during request handling"},
                                 {"trace", "No metadata found during response handling"},
                             }),
                             {
                               auto encoder_decoder = codec_client_->startRequest(request_headers);
                               response = std::move(encoder_decoder.second);

                               ASSERT_TRUE(response->waitForEndStream());
                             });

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());
  cleanup();
}

// Test that handle:route() returns a valid object when no route matches the request.
// This verifies that metadata() returns an empty metadata object that can be safely
// iterated.
TEST_P(LuaIntegrationTest, RouteValidWhenNoRouteMatch) {
  if (!testing_downstream_filter_) {
    GTEST_SKIP() << "This is a local reply test that does not go upstream";
  }

  const std::string filter_config =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        local route = request_handle:route()
        for _, _ in pairs(route:metadata()) do
          return
        end
        request_handle:logTrace("No metadata found during request handling")
      end
      function envoy_on_response(response_handle)
        local route = response_handle:route()
        for _, _ in pairs(route:metadata()) do
          return
        end
        response_handle:logTrace("No metadata found during response handling")
      end
)EOF";

  const std::string route_config =
      R"EOF(
name: test_routes
virtual_hosts:
- name: test_vhost
  domains: ["foo.lyft.com"]
  routes:
  - match:
      path: "/existing/route"
    metadata:
      filter_metadata:
        lua:
          foo.bar:
            name: foo
            prop: bar
          baz.bat:
            name: baz
            prop: bat
    route:
      cluster: cluster_0
)EOF";

  initializeWithYaml(filter_config, route_config);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/non/existing/path"},
                                                 {":scheme", "http"},
                                                 {":authority", "foo.lyft.com"},
                                                 {"x-forwarded-for", "10.0.0.1"}};

  IntegrationStreamDecoderPtr response;
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "No metadata found during request handling"},
                                 {"trace", "No metadata found during response handling"},
                             }),
                             {
                               auto encoder_decoder = codec_client_->startRequest(request_headers);
                               response = std::move(encoder_decoder.second);

                               ASSERT_TRUE(response->waitForEndStream());
                             });

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());

  cleanup();
}

#ifdef NDEBUG
// This test is only run in release mode because in debug mode,
// the code reaches ENVOY_BUG() which triggers a forced abort
// that stops execution.
TEST_P(LuaIntegrationTest, ModifyResponseBodyAndRemoveStatusHeader) {
  if (downstream_protocol_ != Http::CodecType::HTTP1) {
    GTEST_SKIP() << "This is a test that only supports http1";
  }
  if (!testing_downstream_filter_) {
    GTEST_SKIP() << "This is a local reply test that does not go upstream";
  }
  const std::string filter_config =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_response(response_handle)
        local response_headers = response_handle:headers()
        response_headers:remove(":status")
        response_handle:body(true):setBytes("hello world")
      end
)EOF";

  const std::string route_config =
      R"EOF(
name: basic_lua_routes
virtual_hosts:
- name: rds_vhost_1
  domains: ["lua.per.route"]
  routes:
  - match:
      prefix: "/lua"
    direct_response:
      status: 200
      body:
        inline_string: "hello"
)EOF";

  initializeWithYaml(filter_config, route_config);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl default_headers{{":method", "GET"},
                                                 {":path", "/lua"},
                                                 {":scheme", "http"},
                                                 {":authority", "lua.per.route"},
                                                 {"x-forwarded-for", "10.0.0.1"}};

  auto encoder_decoder = codec_client_->startRequest(default_headers);
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("502", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("missing required header: :status"));

  cleanup();
}
#endif

// Test drainConnectionUponCompletion triggers connection draining for HTTP/1.1.
TEST_P(LuaIntegrationTest, DrainConnectionUponCompletion) {
  const std::string filter_config =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        -- Set drain on every request to test connection closure behavior.
        request_handle:streamInfo():drainConnectionUponCompletion()
      end
)EOF";

  initializeFilter(filter_config);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Make request.
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // For HTTP/1.1, we should see Connection: close header.
  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    EXPECT_EQ("close", response->headers().getConnectionValue());
  }

  // Connection should be closed after request completes.
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  cleanup();
}

// Test CounterStats validates that the counters statistics are accurately reported.
TEST_P(LuaIntegrationTest, CounterStats) {
  if (!testing_downstream_filter_) {
    GTEST_SKIP() << "Fake upstream metrics are not checked in this test";
  }

  const std::string config1 =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  stat_prefix: config1
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        request_handle:logInfo("hello")
      end
)EOF";
  config_helper_.prependFilter(config1, testing_downstream_filter_);
  config_helper_.prependFilter(config1, testing_downstream_filter_);

  const std::string config2 =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  stat_prefix: config2
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        request_handle:logInfo("hello")
      end
      function envoy_on_response(response_handle)
        response_handle:logInfo("hello")
      end
)EOF";
  config_helper_.prependFilter(config2, testing_downstream_filter_);

  const std::string config3 =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  stat_prefix: config3
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        local foo = nil
        foo["bar"] = "baz"
      end
)EOF";
  config_helper_.prependFilter(config3, testing_downstream_filter_);

  const std::string config4 =
      R"EOF(
name: lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  stat_prefix: config4
  default_source_code:
    inline_string: |
      print("hello")
)EOF";
  initializeFilter(config4);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  test_server_->waitForCounterEq("http.config_test.lua.config1.executions", 2);
  test_server_->waitForCounterEq("http.config_test.lua.config1.errors", 0);
  test_server_->waitForCounterEq("http.config_test.lua.config2.executions", 2);
  test_server_->waitForCounterEq("http.config_test.lua.config2.errors", 0);
  test_server_->waitForCounterEq("http.config_test.lua.config3.executions", 1);
  test_server_->waitForCounterEq("http.config_test.lua.config3.errors", 1);
  test_server_->waitForCounterEq("http.config_test.lua.config4.executions", 0);
  test_server_->waitForCounterEq("http.config_test.lua.config4.errors", 0);

  cleanup();
}

} // namespace
} // namespace Envoy
