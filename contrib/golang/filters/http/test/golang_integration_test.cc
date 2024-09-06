#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/config/v2_link_hacks.h"
#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "contrib/golang/filters/http/source/golang_filter.h"
#include "gtest/gtest.h"

namespace Envoy {

using testing::HasSubstr;

// helper function
absl::string_view getHeader(const Http::HeaderMap& headers, absl::string_view key) {
  auto values = headers.get(Http::LowerCaseString(key));
  if (values.empty()) {
    return "";
  }
  return values[0]->value().getStringView();
}

class RetrieveDynamicMetadataFilter : public Http::StreamEncoderFilter {
public:
  // Http::StreamEncoderFilter
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
    return Http::Filter1xxHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    const auto& metadata = decoder_callbacks_->streamInfo().dynamicMetadata().filter_metadata();
    const auto& filter_it = metadata.find("filter.go");
    ASSERT(filter_it != metadata.end());
    const auto& fields = filter_it->second.fields();
    std::string val = fields.at("foo").string_value();
    EXPECT_EQ(val, "bar");
    EXPECT_TRUE(
        decoder_callbacks_->streamInfo().filterState()->hasDataWithName("go_state_test_key"));
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }

  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }

  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  void onDestroy() override{};
  Http::StreamEncoderFilterCallbacks* decoder_callbacks_;
};

class RetrieveDynamicMetadataFilterConfig
    : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  RetrieveDynamicMetadataFilterConfig()
      : Extensions::HttpFilters::Common::EmptyHttpFilterConfig("validate-dynamic-metadata") {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string&, Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamEncoderFilter(std::make_shared<::Envoy::RetrieveDynamicMetadataFilter>());
    };
  }
};

class GolangIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                              public HttpIntegrationTest {
public:
  GolangIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()), registration_(factory_) {}

  RetrieveDynamicMetadataFilterConfig factory_;
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registration_;

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP1);
    addFakeUpstream(Http::CodecType::HTTP1);
  }

  std::string genSoPath(std::string name) {
    return TestEnvironment::substitute(
        "{{ test_rundir }}/contrib/golang/filters/http/test/test_data/" + name + "/filter.so");
  }

  void initializeConfig(const std::string& lib_id, const std::string& lib_path,
                        const std::string& plugin_name) {
    const auto yaml_fmt = R"EOF(
name: golang
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.golang.v3alpha.Config
  library_id: %s
  library_path: %s
  plugin_name: %s
  plugin_config:
    "@type": type.googleapis.com/xds.type.v3.TypedStruct
    value:
     echo_body: "echo from go"
     match_path: "/echo"
)EOF";

    auto yaml_string = absl::StrFormat(yaml_fmt, lib_id, lib_path, plugin_name);
    config_helper_.prependFilter(yaml_string);
    config_helper_.skipPortUsageValidation();
  }

  void initializeBasicFilter(const std::string& so_id, const std::string& domain = "*",
                             bool with_injected_metadata_validator = false) {
    const auto yaml_fmt = R"EOF(
name: golang
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.golang.v3alpha.Config
  library_id: %s
  library_path: %s
  plugin_name: %s
  plugin_config:
    "@type": type.googleapis.com/xds.type.v3.TypedStruct
    value:
      remove: x-test-header-0
      set: foo
)EOF";

    auto yaml_string = absl::StrFormat(yaml_fmt, so_id, genSoPath(so_id), so_id);
    config_helper_.prependFilter(yaml_string);
    if (with_injected_metadata_validator) {
      config_helper_.prependFilter("{ name: validate-dynamic-metadata }");
    }

    config_helper_.skipPortUsageValidation();

    config_helper_.addConfigModifier(
        [domain](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_match()
              ->set_prefix("/test");

          // setting route name for testing
          hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0)->set_name(
              "test-route-name");
          hcm.mutable_route_config()->mutable_virtual_hosts(0)->set_domains(0, domain);

          auto* virtual_cluster =
              hcm.mutable_route_config()->mutable_virtual_hosts(0)->add_virtual_clusters();
          virtual_cluster->set_name("test_vcluster");
          auto* headers = virtual_cluster->add_headers();
          // use test_vcluster if presents
          headers->set_name("existed-header");
          headers->set_present_match(true);

          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_route()
              ->set_cluster("cluster_0");
        });
    config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
    config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
    initialize();
  }

  void initializeRouteConfig(const std::string& so_id) {
    config_helper_.addConfigModifier(
        [so_id](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          // for testing http filter level config, a new virtualhost without per route config
          auto vh = hcm.mutable_route_config()->add_virtual_hosts();
          vh->add_domains("filter-level.com");
          vh->set_name("filter-level.com");
          auto* rt = vh->add_routes();
          rt->mutable_match()->set_prefix("/test");
          rt->mutable_route()->set_cluster("cluster_0");

          // virtualhost level per route config
          const std::string key = "golang";
          const auto yaml_fmt =
              R"EOF(
              "@type": type.googleapis.com/envoy.extensions.filters.http.golang.v3alpha.ConfigsPerRoute
              plugins_config:
                %s:
                  config:
                    "@type": type.googleapis.com/xds.type.v3.TypedStruct
                    type_url: map
                    value:
                      remove: x-test-header-1
                      set: bar
              )EOF";
          auto yaml = absl::StrFormat(yaml_fmt, so_id);
          ProtobufWkt::Any value;
          TestUtility::loadFromYaml(yaml, value);
          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_typed_per_filter_config()
              ->insert(Protobuf::MapPair<std::string, ProtobufWkt::Any>(key, value));

          // route level per route config
          const auto yaml_fmt2 =
              R"EOF(
              "@type": type.googleapis.com/envoy.extensions.filters.http.golang.v3alpha.ConfigsPerRoute
              plugins_config:
                %s:
                  config:
                    "@type": type.googleapis.com/xds.type.v3.TypedStruct
                    type_url: map
                    value:
                      remove: x-test-header-0
                      set: baz
              )EOF";
          auto yaml2 = absl::StrFormat(yaml_fmt2, so_id);
          ProtobufWkt::Any value2;
          TestUtility::loadFromYaml(yaml2, value2);

          auto* new_route2 = hcm.mutable_route_config()->mutable_virtual_hosts(0)->add_routes();
          new_route2->mutable_match()->set_prefix("/route-config-test");
          new_route2->mutable_typed_per_filter_config()->insert(
              Protobuf::MapPair<std::string, ProtobufWkt::Any>(key, value2));
          new_route2->mutable_route()->set_cluster("cluster_0");
        });

    initializeBasicFilter(so_id, "test.com");
  }

  void initializePropertyConfig(const std::string& lib_id, const std::string& lib_path,
                                const std::string& plugin_name) {
    const auto yaml_fmt = R"EOF(
name: golang
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.golang.v3alpha.Config
  library_id: %s
  library_path: %s
  plugin_name: %s
  plugin_config:
    "@type": type.googleapis.com/xds.type.v3.TypedStruct
)EOF";

    auto yaml_string = absl::StrFormat(yaml_fmt, lib_id, lib_path, plugin_name);
    config_helper_.prependFilter(yaml_string);
    config_helper_.skipPortUsageValidation();

    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_match()
              ->set_prefix("/property");

          // setting route name for testing
          hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0)->set_name(
              "test-route-name");
          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_route()
              ->set_cluster("cluster_0");
        });

    config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
    config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  }

  void testBasic(std::string path) {
    initializeBasicFilter(BASIC, "test.com");

    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "POST"},        {":path", path},
        {":scheme", "http"},        {":authority", "test.com"},
        {"x-test-header-0", "foo"}, {"x-test-header-1", "bar"},
        {"existed-header", "foo"},
    };

    auto encoder_decoder = codec_client_->startRequest(request_headers);
    Http::RequestEncoder& request_encoder = encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);
    codec_client_->sendData(request_encoder, "helloworld", false);
    codec_client_->sendData(request_encoder, "", false);

    Http::TestRequestTrailerMapImpl request_trailers{
        {"x-test-trailer-0", "foo"}, {"existed-trailer", "foo"}, {"x-test-trailer-1", "foo"}};
    codec_client_->sendTrailers(request_encoder, request_trailers);

    waitForNextUpstreamRequest();

    EXPECT_EQ("go_state_test_value",
              getHeader(upstream_request_->headers(), "go-state-test-header-key"));

    // original header: x-test-header-0
    EXPECT_EQ("foo", getHeader(upstream_request_->headers(), "x-test-header-0"));

    // check header value which set in golang: test-x-set-header-0
    EXPECT_EQ("foo", getHeader(upstream_request_->headers(), "test-x-set-header-0"));

    // check header exists which removed in golang side: x-test-header-1
    EXPECT_TRUE(upstream_request_->headers().get(Http::LowerCaseString("x-test-header-1")).empty());

    // check header value which set in golang: req-downstream-local-address
    EXPECT_TRUE(
        absl::StrContains(getHeader(upstream_request_->headers(), "req-downstream-local-address"),
                          GetParam() == Network::Address::IpVersion::v4 ? "127.0.0.1:" : "[::1]:"));

    // check header value which set in golang: req-downstream-remote-address
    EXPECT_TRUE(
        absl::StrContains(getHeader(upstream_request_->headers(), "req-downstream-remote-address"),
                          GetParam() == Network::Address::IpVersion::v4 ? "127.0.0.1:" : "[::1]:"));

    // check header value which is appended in golang: existed-header
    auto entries = upstream_request_->headers().get(Http::LowerCaseString("existed-header"));
    EXPECT_EQ(2, entries.size());
    EXPECT_EQ("foo", entries[0]->value().getStringView());
    EXPECT_EQ("bar", entries[1]->value().getStringView());

    // check header value which added in golang: newly-added-header
    entries = upstream_request_->headers().get(Http::LowerCaseString("newly-added-header"));
    EXPECT_EQ(2, entries.size());
    EXPECT_EQ("foo", entries[0]->value().getStringView());
    EXPECT_EQ("bar", entries[1]->value().getStringView());

    // "prepend_" + upper("helloworld") + "_append"
    std::string expected = "prepend_HELLOWORLD_append";
    // only match the prefix since data buffer may be combined into a single.
    EXPECT_EQ(expected, upstream_request_->body().toString());

    // check trailer value which is appended in golang: existed-trailer
    entries = upstream_request_->trailers()->get(Http::LowerCaseString("existed-trailer"));
    EXPECT_EQ(2, entries.size());
    EXPECT_EQ("foo", entries[0]->value().getStringView());
    if (entries.size() == 2) {
      EXPECT_EQ("bar", entries[1]->value().getStringView());
    }

    // check trailer value which set in golang: x-test-trailer-0
    entries = upstream_request_->trailers()->get(Http::LowerCaseString("x-test-trailer-0"));
    EXPECT_EQ("bar", entries[0]->value().getStringView());

    EXPECT_TRUE(
        upstream_request_->trailers()->get(Http::LowerCaseString("x-test-trailer-1")).empty());

    // check trailer value which add in golang: x-test-trailer-2
    entries = upstream_request_->trailers()->get(Http::LowerCaseString("x-test-trailer-2"));

    EXPECT_EQ("bar", entries[0]->value().getStringView());

    Http::TestResponseHeaderMapImpl response_headers{
        {":status", "200"},
        {"x-test-header-0", "foo"},
        {"x-test-header-1", "bar"},
        {"existed-header", "foo"},
    };
    upstream_request_->encodeHeaders(response_headers, false);
    Buffer::OwnedImpl response_data1("good");
    upstream_request_->encodeData(response_data1, false);
    Buffer::OwnedImpl response_data2("bye");
    upstream_request_->encodeData(response_data2, true);

    ASSERT_TRUE(response->waitForEndStream());

    // original resp header: x-test-header-0
    EXPECT_EQ("foo", getHeader(response->headers(), "x-test-header-0"));

    // check resp header value which set in golang: test-x-set-header-0
    EXPECT_EQ("foo", getHeader(response->headers(), "test-x-set-header-0"));

    // check resp header exists which removed in golang side: x-test-header-1
    EXPECT_TRUE(response->headers().get(Http::LowerCaseString("x-test-header-1")).empty());

    // check header value which is appended in golang: existed-header
    entries = response->headers().get(Http::LowerCaseString("existed-header"));
    EXPECT_EQ(2, entries.size());
    EXPECT_EQ("foo", entries[0]->value().getStringView());
    EXPECT_EQ("bar", entries[1]->value().getStringView());

    // check header value which added in golang: newly-added-header
    entries = response->headers().get(Http::LowerCaseString("newly-added-header"));
    EXPECT_EQ(2, entries.size());
    EXPECT_EQ("foo", entries[0]->value().getStringView());
    EXPECT_EQ("bar", entries[1]->value().getStringView());

    // length("helloworld") = 10
    EXPECT_EQ("10", getHeader(response->headers(), "test-req-body-length"));

    // check route name in encode phase
    EXPECT_EQ("test-route-name", getHeader(response->headers(), "rsp-route-name"));

    // check protocol in encode phase
    EXPECT_EQ("HTTP/1.1", getHeader(response->headers(), "rsp-protocol"));

    // check filter chain name in encode phase, exists.
    EXPECT_FALSE(response->headers().get(Http::LowerCaseString("rsp-filter-chain-name")).empty());

    // check response code in encode phase, not exists.
    EXPECT_FALSE(response->headers().get(Http::LowerCaseString("rsp-response-code")).empty());

    // check response code details in encode phase
    EXPECT_EQ("via_upstream", getHeader(response->headers(), "rsp-response-code-details"));

    // check upstream host in encode phase
    EXPECT_TRUE(
        absl::StrContains(getHeader(response->headers(), "rsp-upstream-host"),
                          GetParam() == Network::Address::IpVersion::v4 ? "127.0.0.1:" : "[::1]:"));

    // check upstream cluster in encode phase
    EXPECT_EQ("cluster_0", getHeader(response->headers(), "rsp-upstream-cluster"));

    // check response attempt count in encode phase
    EXPECT_EQ("1", getHeader(response->headers(), "rsp-attempt-count"));

    // check virtual cluster name
    EXPECT_EQ("test_vcluster", getHeader(response->headers(), "rsp-virtual-cluster-name"));

    // verify response status
    EXPECT_EQ("200", getHeader(response->headers(), "rsp-status"));

    // verify protocol
    EXPECT_TRUE(response->headers().get(Http::LowerCaseString("test-protocol")).empty());

    // verify scheme
    EXPECT_EQ("http", getHeader(response->headers(), "test-scheme"));

    // verify method
    EXPECT_EQ("POST", getHeader(response->headers(), "test-method"));

    // verify path
    EXPECT_EQ(path, getHeader(response->headers(), "test-path"));

    // verify host
    EXPECT_EQ("test.com", getHeader(response->headers(), "test-host"));

    // verify log level
    EXPECT_EQ("error", getHeader(response->headers(), "test-log-level"));

    // upper("goodbye")
    EXPECT_EQ("GOODBYE", response->body());

    cleanup();
  }

  void testMetric(std::string path) {
    initializeBasicFilter(METRIC);

    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "POST"}, {":path", path}, {":scheme", "http"}, {":authority", "test.com"}};

    auto encoder_decoder = codec_client_->startRequest(request_headers, true);
    auto response = std::move(encoder_decoder.second);

    waitForNextUpstreamRequest();

    EXPECT_EQ("2", getHeader(upstream_request_->headers(), "go-metric-counter-test-header-key"));

    EXPECT_EQ("3", getHeader(upstream_request_->headers(), "go-metric-gauge-test-header-key"));

    EXPECT_EQ("3",
              getHeader(upstream_request_->headers(), "go-metric-counter-record-test-header-key"));

    EXPECT_EQ("1",
              getHeader(upstream_request_->headers(), "go-metric-gauge-record-test-header-key"));

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    upstream_request_->encodeHeaders(response_headers, true);

    ASSERT_TRUE(response->waitForEndStream());

    cleanup();
  }

  void testRouteConfig(std::string domain, std::string path, bool header_0_existing,
                       std::string set_header) {
    initializeRouteConfig(ROUTECONFIG);

    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", path}, {":scheme", "http"}, {":authority", domain}};

    auto encoder_decoder = codec_client_->startRequest(request_headers, true);
    auto response = std::move(encoder_decoder.second);

    waitForNextUpstreamRequest();

    Http::TestResponseHeaderMapImpl response_headers{
        {":status", "200"}, {"x-test-header-0", "foo"}, {"x-test-header-1", "bar"}};
    upstream_request_->encodeHeaders(response_headers, true);

    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_EQ(header_0_existing,
              !response->headers().get(Http::LowerCaseString("x-test-header-0")).empty());

    if (!set_header.empty()) {
      EXPECT_EQ("test-value", getHeader(response->headers(), set_header));
    }
    cleanup();
  }

  void testRouteCache(std::string path, bool clear) {
    initializeBasicFilter(BASIC);

    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "POST"}, {":path", path}, {":scheme", "http"}, {":authority", "test.com"}};

    auto encoder_decoder = codec_client_->startRequest(request_headers, true);
    auto response = std::move(encoder_decoder.second);

    // no route found after clearing
    if (!clear) {
      waitForNextUpstreamRequest();
      Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
      upstream_request_->encodeHeaders(response_headers, true);
    }

    ASSERT_TRUE(response->waitForEndStream());

    // check resp status
    if (clear) {
      EXPECT_EQ("404", response->headers().getStatusValue());
    } else {
      EXPECT_EQ("200", response->headers().getStatusValue());
    }

    cleanup();
  }

  void testSendLocalReply(std::string path, std::string phase) {
    initializeBasicFilter(BASIC);

    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "POST"}, {":path", path}, {":scheme", "http"}, {":authority", "test.com"}};

    auto encoder_decoder = codec_client_->startRequest(request_headers);
    Http::RequestEncoder& request_encoder = encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);

    // do not sendData when phase is decode-header,
    // since the request may be terminated before sendData.
    if (phase != "decode-header") {
      codec_client_->sendData(request_encoder, "hello", true);
    }

    // need upstream request when send local reply in encode phases.
    if (phase == "encode-header" || phase == "encode-data") {
      waitForNextUpstreamRequest();
      Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
      upstream_request_->encodeHeaders(response_headers, false);

      // do not sendData when phase is encode-header
      if (phase == "encode-data") {
        Buffer::OwnedImpl response_data("bye");
        upstream_request_->encodeData(response_data, true);
      }
    }

    ASSERT_TRUE(response->waitForEndStream());

    // check resp status
    EXPECT_EQ("403", response->headers().getStatusValue());

    // forbidden from go in %s\r\n
    auto body = StringUtil::toUpper(absl::StrFormat("forbidden from go in %s\r\n", phase));
    EXPECT_EQ(body, StringUtil::toUpper(response->body()));

    // verify phase
    EXPECT_EQ(phase, getHeader(response->headers(), "test-phase"));

    // verify content-type
    EXPECT_EQ("text/html", getHeader(response->headers(), "content-type"));

    // verify two values
    auto values = response->headers().get(Http::LowerCaseString("x-two-values"));
    if (values.size() == 2) {
      EXPECT_EQ("foo", values[0]->value().getStringView());
      EXPECT_EQ("bar", values[1]->value().getStringView());
    } else {
      EXPECT_EQ(values.size(), 2);
    }

    cleanup();
  }

  void testBufferExceedLimit(std::string path) {
    config_helper_.setBufferLimits(1024, 150);
    initializeBasicFilter(BASIC);

    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "POST"}, {":path", path}, {":scheme", "http"}, {":authority", "test.com"}};

    auto encoder_decoder = codec_client_->startRequest(request_headers);
    Http::RequestEncoder& request_encoder = encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);
    // 100 + 200 > 150, exceed buffer limit.
    codec_client_->sendData(request_encoder, std::string(100, '-'), false);
    codec_client_->sendData(request_encoder, std::string(200, '-'), true);

    ASSERT_TRUE(response->waitForEndStream());

    // check resp status
    EXPECT_EQ("413", response->headers().getStatusValue());

    auto body = StringUtil::toUpper("payload too large");
    EXPECT_EQ(body, response->body());

    cleanup();
  }

  void testPanicRecover(std::string path, std::string phase) {
    initializeBasicFilter(BASIC);

    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "POST"}, {":path", path}, {":scheme", "http"}, {":authority", "test.com"}};

    auto encoder_decoder = codec_client_->startRequest(request_headers);
    Http::RequestEncoder& request_encoder = encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);

    // do not sendData when phase is decode-header,
    // since the request may be terminated before sendData.
    if (phase != "decode-header") {
      codec_client_->sendData(request_encoder, "hello", true);
    }

    // need upstream request when send local reply in encode phases.
    if (phase == "encode-header" || phase == "encode-data") {
      waitForNextUpstreamRequest();
      Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
      upstream_request_->encodeHeaders(response_headers, false);

      // do not sendData when phase is encode-header
      if (phase == "encode-data") {
        Buffer::OwnedImpl response_data("bye");
        upstream_request_->encodeData(response_data, true);
      }
    }

    ASSERT_TRUE(response->waitForEndStream());

    // check resp status
    EXPECT_EQ("500", response->headers().getStatusValue());

    // error happened in filter\r\n
    auto body = StringUtil::toUpper("error happened in filter\r\n");
    EXPECT_EQ(body, StringUtil::toUpper(response->body()));

    EXPECT_EQ(1, test_server_->counter("http.config_test.golang.panic_error")->value());

    cleanup();
  }

  void cleanup() {
    cleanupUpstreamAndDownstream();

    Dso::DsoManager<Dso::HttpFilterDsoImpl>::cleanUpForTest();
  }

  void testDynamicMetadata(std::string path) {
    initializeBasicFilter(BASIC, "*", true);

    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "POST"},        {":path", path},
        {":scheme", "http"},        {":authority", "test.com"},
        {"x-set-metadata", "true"},
    };

    auto encoder_decoder = codec_client_->startRequest(request_headers);
    Http::RequestEncoder& request_encoder = encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);
    codec_client_->sendData(request_encoder, "helloworld", true);

    waitForNextUpstreamRequest();

    Http::TestResponseHeaderMapImpl response_headers{
        {":status", "200"},
    };
    upstream_request_->encodeHeaders(response_headers, true);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
    cleanup();
  }

  void testActionWithoutData(std::string query) {
    initializeBasicFilter(ACTION, "test.com");
    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test?" + query},
                                                   {":scheme", "http"},
                                                   {":authority", "test.com"}};

    auto encoder_decoder = codec_client_->startRequest(request_headers, true);
    auto response = std::move(encoder_decoder.second);

    if (query.find("encodeHeadersRet") != std::string::npos) {
      waitForNextUpstreamRequest();

      Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
      upstream_request_->encodeHeaders(response_headers, true);
    }

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("500", response->headers().getStatusValue());

    EXPECT_EQ(1, test_server_->counter("http.config_test.golang.panic_error")->value());

    cleanup();
  }

  void testBufferApi(std::string query) {
    initializeBasicFilter(BUFFER, "test.com");

    auto path = std::string("/test?") + query;
    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "POST"},
        {":path", path},
        {":scheme", "http"},
        {":authority", "test.com"},
    };

    auto encoder_decoder = codec_client_->startRequest(request_headers);
    Http::RequestEncoder& request_encoder = encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);
    std::string data = "";
    for (int i = 0; i < 10; i++) {
      data += "12345";
    }
    codec_client_->sendData(request_encoder, data, true);

    waitForNextUpstreamRequest();

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    upstream_request_->encodeHeaders(response_headers, false);
    Buffer::OwnedImpl response_data("goodbye");
    upstream_request_->encodeData(response_data, true);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
    cleanup();
  }

  const std::string ECHO{"echo"};
  const std::string BASIC{"basic"};
  const std::string PASSTHROUGH{"passthrough"};
  const std::string BUFFER{"buffer"};
  const std::string ROUTECONFIG{"routeconfig"};
  const std::string PROPERTY{"property"};
  const std::string ACCESSLOG{"access_log"};
  const std::string METRIC{"metric"};
  const std::string ACTION{"action"};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GolangIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(GolangIntegrationTest, Echo) {
  initializeConfig(ECHO, genSoPath(ECHO), ECHO);
  initialize();
  registerTestServerPorts({"http"});

  auto path = "/echo";
  auto echo_body = "echo from go";
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"}, {":path", path}, {":scheme", "http"}, {":authority", "test.com"}};

  auto encoder_decoder = codec_client_->startRequest(request_headers);
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(response->waitForEndStream());

  // check status for echo
  EXPECT_EQ("403", response->headers().getStatusValue());

  // check body for echo
  auto body = absl::StrFormat("%s, path: %s\r\n", echo_body, path);
  EXPECT_EQ(body, response->body());

  cleanup();
}

TEST_P(GolangIntegrationTest, Passthrough) {
  initializeConfig(PASSTHROUGH, genSoPath(PASSTHROUGH), PASSTHROUGH);
  initialize();
  registerTestServerPorts({"http"});

  auto path = "/";
  auto good = "good";
  auto bye = "bye";
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"}, {":path", path}, {":scheme", "http"}, {":authority", "test.com"}};

  auto encoder_decoder = codec_client_->startRequest(request_headers);
  Http::RequestEncoder& request_encoder = encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(request_encoder, "helloworld", true);

  waitForNextUpstreamRequest();
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  Buffer::OwnedImpl response_data1(good);
  upstream_request_->encodeData(response_data1, false);
  Buffer::OwnedImpl response_data2(bye);
  upstream_request_->encodeData(response_data2, true);

  ASSERT_TRUE(response->waitForEndStream());

  // check status for passthrough
  EXPECT_EQ("200", response->headers().getStatusValue());

  // check body for passthrough
  auto body = absl::StrFormat("%s%s", good, bye);
  EXPECT_EQ(body, response->body());

  cleanup();
}

TEST_P(GolangIntegrationTest, PluginNotFound) {
  initializeConfig(ECHO, genSoPath(ECHO), PASSTHROUGH);
  initialize();
  registerTestServerPorts({"http"});

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"}, {":path", "/"}, {":scheme", "http"}, {":authority", "test.com"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ("200", response->headers().getStatusValue());
  cleanup();
}

TEST_P(GolangIntegrationTest, BufferDrain) { testBufferApi("Drain"); }

TEST_P(GolangIntegrationTest, BufferReset) { testBufferApi("Reset"); }

TEST_P(GolangIntegrationTest, BufferResetAfterDrain) { testBufferApi("ResetAfterDrain"); }

TEST_P(GolangIntegrationTest, BufferLen) { testBufferApi("Len"); }

TEST_P(GolangIntegrationTest, Property) {
  initializePropertyConfig(PROPERTY, genSoPath(PROPERTY), PROPERTY);
  initialize();
  registerTestServerPorts({"http"});

  auto path = "/property?a=1";
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},  {":path", path},  {":scheme", "http"},     {":authority", "test.com"},
      {"User-Agent", "ua"}, {"Referer", "r"}, {"X-Request-Id", "xri"},
  };

  auto encoder_decoder = codec_client_->startRequest(request_headers);
  Http::RequestEncoder& request_encoder = encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(request_encoder, "helloworld", true);

  waitForNextUpstreamRequest();

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  Buffer::OwnedImpl response_data("goodbye");
  upstream_request_->encodeData(response_data, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  cleanup();
}

TEST_P(GolangIntegrationTest, AccessLog) {
  useAccessLog("%DYNAMIC_METADATA(golang:access_log_var)%");
  initializeBasicFilter(ACCESSLOG, "test.com");

  auto path = "/test";
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},        {":path", path},  {":scheme", "http"},
      {":authority", "test.com"}, {"Referer", "r"},
  };

  auto encoder_decoder = codec_client_->startRequest(request_headers);
  Http::RequestEncoder& request_encoder = encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(request_encoder, "helloworld", false);

  Http::TestRequestTrailerMapImpl request_trailers{
      {"x-trailer", "foo"},
  };
  codec_client_->sendTrailers(request_encoder, request_trailers);

  waitForNextUpstreamRequest();

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "206"},
  };
  upstream_request_->encodeHeaders(response_headers, false);
  Buffer::OwnedImpl response_data1("good");
  upstream_request_->encodeData(response_data1, false);
  Buffer::OwnedImpl response_data2("bye");
  upstream_request_->encodeData(response_data2, false);

  Http::TestResponseTrailerMapImpl response_trailers{{"x-trailer", "bar"}};
  upstream_request_->encodeTrailers(response_trailers);

  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();

  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("access_log_var written by Golang filter"));

  // use the second request to get the logged data
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("206", getHeader(upstream_request_->headers(), "respCode"));
  EXPECT_EQ("7", getHeader(upstream_request_->headers(), "respSize"));
  EXPECT_EQ("true", getHeader(upstream_request_->headers(), "canRunAsyncly"));
  EXPECT_EQ("foo", getHeader(upstream_request_->headers(), "x-req-trailer"));
  EXPECT_EQ("bar", getHeader(upstream_request_->headers(), "x-resp-trailer"));

  cleanup();
}

TEST_P(GolangIntegrationTest, AccessLogDownstreamStart) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        hcm.mutable_access_log_options()->set_flush_access_log_on_new_request(true);
      });
  initializeBasicFilter(ACCESSLOG, "test.com");

  auto path = "/test";
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},        {":path", path},  {":scheme", "http"},
      {":authority", "test.com"}, {"Referer", "r"},
  };

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  EXPECT_TRUE(response->complete());
  codec_client_->close();

  // use the second request to get the logged data
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers2{
      {":method", "POST"},        {":path", path},   {":scheme", "http"},
      {":authority", "test.com"}, {"Referer", "r2"},
  };

  response = sendRequestAndWaitForResponse(request_headers2, 0, default_response_headers_, 0);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("r;r2", getHeader(upstream_request_->headers(), "referers"));
  EXPECT_EQ("true", getHeader(upstream_request_->headers(), "canRunAsynclyForDownstreamStart"));

  cleanup();
}

TEST_P(GolangIntegrationTest, AccessLogDownstreamPeriodic) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        hcm.mutable_access_log_options()->mutable_access_log_flush_interval()->set_nanos(
            100000000); // 0.1 seconds
      });
  initializeBasicFilter(ACCESSLOG, "test.com");

  auto path = "/test?periodic=1";
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},        {":path", path},  {":scheme", "http"},
      {":authority", "test.com"}, {"Referer", "r"},
  };

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  EXPECT_TRUE(response->complete());
  codec_client_->close();

  // use the second request to get the logged data
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("r", getHeader(upstream_request_->headers(), "referers"));
  EXPECT_EQ("true", getHeader(upstream_request_->headers(), "canRunAsynclyForDownstreamPeriodic"));

  cleanup();
}

// Metric API testing
TEST_P(GolangIntegrationTest, Metric) { testMetric("/test"); }

// Metric API testing in async mode.
TEST_P(GolangIntegrationTest, AsyncMetric) { testMetric("/test?async=1"); }

// Basic API testing, i.e. add/remove/set Headers & data rewrite.
TEST_P(GolangIntegrationTest, Basic) { testBasic("/test"); }

// Basic API testing in async mode.
TEST_P(GolangIntegrationTest, Async) { testBasic("/test?async=1"); }

// buffer all data in decode header phase.
TEST_P(GolangIntegrationTest, DataBuffer_DecodeHeader) {
  testBasic("/test?databuffer=decode-header");
}

// Go sleep in sync mode.
TEST_P(GolangIntegrationTest, Sleep) { testBasic("/test?sleep=1"); }

// Go sleep in decode/encode data phase.
TEST_P(GolangIntegrationTest, DataSleep) { testBasic("/test?data_sleep=1"); }

// Go sleep in async mode.
TEST_P(GolangIntegrationTest, Async_Sleep) { testBasic("/test?async=1&sleep=1"); }

// Go sleep in decode/encode data phase with async mode.
TEST_P(GolangIntegrationTest, Async_DataSleep) { testBasic("/test?async=1&data_sleep=1"); }

// buffer all data in decode header phase with async mode.
TEST_P(GolangIntegrationTest, Async_DataBuffer_DecodeHeader) {
  testBasic("/test?async=1&databuffer=decode-header");
}

// buffer all data in decode data phase with sync mode.
TEST_P(GolangIntegrationTest, DataBuffer_DecodeData) { testBasic("/test?databuffer=decode-data"); }

// buffer all data in decode data phase with async mode.
TEST_P(GolangIntegrationTest, Async_DataBuffer_DecodeData) {
  testBasic("/test?async=1&databuffer=decode-data");
}

// Go send local reply in decode header phase.
TEST_P(GolangIntegrationTest, LocalReply_DecodeHeader) {
  testSendLocalReply("/test?localreply=decode-header", "decode-header");
}

// Go send local reply in decode header phase with async mode.
TEST_P(GolangIntegrationTest, LocalReply_DecodeHeader_Async) {
  testSendLocalReply("/test?async=1&localreply=decode-header", "decode-header");
}

// Go send local reply in decode data phase.
TEST_P(GolangIntegrationTest, LocalReply_DecodeData) {
  testSendLocalReply("/test?localreply=decode-data", "decode-data");
}

// Go send local reply in decode data phase with async mode.
TEST_P(GolangIntegrationTest, LocalReply_DecodeData_Async) {
  testSendLocalReply("/test?async=1&sleep=1&localreply=decode-data", "decode-data");
}

// The next filter(lua filter) send local reply after Go filter continue in decode header phase.
// Go filter will terminate when lua filter send local reply.
TEST_P(GolangIntegrationTest, LuaRespondAfterGoHeaderContinue) {
  // put lua filter after golang filter
  // golang filter => lua filter.

  const std::string LUA_RESPOND =
      R"EOF(
name: envoy.filters.http.lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
        function envoy_on_request(handle)
        local orig_header = handle:headers():get('x-test-header-0')
        local go_header = handle:headers():get('test-x-set-header-0')
        handle:respond({[":status"] = "403"}, "forbidden from lua, orig header: "
            .. (orig_header or "nil")
            .. ", go header: "
            .. (go_header or "nil"))
        end
)EOF";
  config_helper_.prependFilter(LUA_RESPOND);

  initializeBasicFilter(BASIC);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/test"},
                                                 {":scheme", "http"},
                                                 {":authority", "test.com"},
                                                 {"x-test-header-0", "foo"}};

  auto encoder_decoder = codec_client_->startRequest(request_headers);
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(response->waitForEndStream());

  // check resp status
  EXPECT_EQ("403", response->headers().getStatusValue());

  // forbidden from lua, orig header: foo, go header: foo
  auto body = StringUtil::toUpper("forbidden from lua, orig header: foo, go header: foo");
  EXPECT_EQ(body, response->body());

  cleanup();
}

// Buffer exceed limit in decode header phase.
TEST_P(GolangIntegrationTest, BufferExceedLimit_DecodeHeader) {
  testBufferExceedLimit("/test?databuffer=decode-header");
}

// Using the original config in http filter: (no per route config)
// remove: x-test-header-0
// set: foo
TEST_P(GolangIntegrationTest, RouteConfig_Filter) {
  testRouteConfig("filter-level.com", "/test", false, "foo");
}

// Using the merged config from http filter & virtualhost level per route config:
// remove: x-test-header-1
// set: bar
TEST_P(GolangIntegrationTest, RouteConfig_VirtualHost) {
  testRouteConfig("test.com", "/test", true, "bar");
}

// Using the merged config from route level & virtualhost level & http filter:
// remove: x-test-header-0
// set: baz
TEST_P(GolangIntegrationTest, RouteConfig_Route) {
  testRouteConfig("test.com", "/route-config-test", false, "baz");
}

// Set new path without clear route cache, will get 200 response status
TEST_P(GolangIntegrationTest, RouteCache_noClear) {
  testRouteCache("/test?newPath=/not-found-path", false);
}

// Set new path with clear route cache, will get 404 response status
TEST_P(GolangIntegrationTest, RouteCache_Clear) {
  testRouteCache("/test?newPath=/not-found-path&clearRoute=1", true);
}

// Out of range in decode header phase
TEST_P(GolangIntegrationTest, PanicRecover_DecodeHeader) {
  testPanicRecover("/test?panic=decode-header", "decode-header");
}

// Out of range in decode header phase with async mode
TEST_P(GolangIntegrationTest, PanicRecover_DecodeHeader_Async) {
  testPanicRecover("/test?async=1&panic=decode-header", "decode-header");
}

// Out of range in decode data phase
TEST_P(GolangIntegrationTest, PanicRecover_DecodeData) {
  testPanicRecover("/test?panic=decode-data", "decode-data");
}

// Out of range in decode data phase with async mode & sleep
TEST_P(GolangIntegrationTest, PanicRecover_DecodeData_Async) {
  testPanicRecover("/test?async=1&sleep=1&panic=decode-data", "decode-data");
}

// Out of range in encode data phase with async mode & sleep
TEST_P(GolangIntegrationTest, PanicRecover_EncodeData_Async) {
  testPanicRecover("/test?async=1&sleep=1&panic=encode-data", "encode-data");
}

// Panic ErrInvalidPhase
TEST_P(GolangIntegrationTest, PanicRecover_BadAPI) {
  testPanicRecover("/test?badapi=decode-data", "decode-data");
}

TEST_P(GolangIntegrationTest, DynamicMetadata) { testDynamicMetadata("/test?dymeta=1"); }

TEST_P(GolangIntegrationTest, DynamicMetadata_Async) {
  testDynamicMetadata("/test?dymeta=1&async=1");
}

TEST_P(GolangIntegrationTest, DynamicMetadata_Async_Sleep) {
  testDynamicMetadata("/test?dymeta=1&async=1&sleep=1");
}

TEST_P(GolangIntegrationTest, DecodeHeadersWithoutData_StopAndBuffer) {
  testActionWithoutData("decodeHeadersRet=StopAndBuffer");
}

TEST_P(GolangIntegrationTest, DecodeHeadersWithoutData_StopAndBufferWatermark) {
  testActionWithoutData("decodeHeadersRet=StopAndBufferWatermark");
}

TEST_P(GolangIntegrationTest, DecodeHeadersWithoutData_StopAndBuffer_Async) {
  testActionWithoutData("decodeHeadersRet=StopAndBuffer&aysnc=1");
}

TEST_P(GolangIntegrationTest, DecodeHeadersWithoutData_StopAndBufferWatermark_Async) {
  testActionWithoutData("decodeHeadersRet=StopAndBufferWatermark&aysnc=1");
}

TEST_P(GolangIntegrationTest, EncodeHeadersWithoutData_StopAndBuffer) {
  testActionWithoutData("encodeHeadersRet=StopAndBuffer");
}

TEST_P(GolangIntegrationTest, EncodeHeadersWithoutData_StopAndBufferWatermark) {
  testActionWithoutData("encodeHeadersRet=StopAndBufferWatermark");
}

TEST_P(GolangIntegrationTest, EncodeHeadersWithoutData_StopAndBuffer_Async) {
  testActionWithoutData("encodeHeadersRet=StopAndBuffer&aysnc=1");
}

TEST_P(GolangIntegrationTest, EncodeHeadersWithoutData_StopAndBufferWatermark_Async) {
  testActionWithoutData("encodeHeadersRet=StopAndBufferWatermark&aysnc=1");
}

} // namespace Envoy
